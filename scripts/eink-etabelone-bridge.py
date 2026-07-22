#!/usr/bin/env python3
"""Translate live e-tabelone JPEG/PNG assets to ES6F (optionally LZ4) for native_sim.

Production firmware accepts raw ES6F or an LZ4 *frame* whose payload is a full
ES6F v1 file (magic 04 22 4D 18). This development bridge fetches the real
config/schedule, rewrites image URLs to a local endpoint, and lazily converts
each source asset into that packed frame (default: LZ4-framed for smaller
radio transfers).

After each config fetch, remaining images are prefetched in parallel so gallery
downloads usually hit the host cache.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Bridge:
    def __init__(
        self,
        upstream: str,
        device_id: str,
        public_base: str,
        cache_dir: Path,
        token: str,
        prefetch_workers: int = 4,
        lz4: bool = True,
    ) -> None:
        self.upstream = upstream.rstrip("/")
        self.device_id = device_id
        self.public_base = public_base.rstrip("/")
        self.cache_dir = cache_dir
        self.token = token
        self.prefetch_workers = max(1, prefetch_workers)
        self.prefetch_enabled = True
        self.lz4 = lz4
        self.image_urls: dict[str, str] = {}
        self.lock = threading.Lock()
        self._id_locks: dict[str, threading.Lock] = {}
        self._config_etag: str | None = None
        self.converter = Path(__file__).with_name("gen-eink-frame.py")
        self.lz4_wrap = Path(__file__).with_name("eink-lz4-wrap.py")
        cache_dir.mkdir(parents=True, exist_ok=True)

    @property
    def image_suffix(self) -> str:
        return ".es6f.lz4" if self.lz4 else ".es6f"

    def _id_lock(self, image_id: str) -> threading.Lock:
        with self.lock:
            lock = self._id_locks.get(image_id)
            if lock is None:
                lock = threading.Lock()
                self._id_locks[image_id] = lock
            return lock

    def request(
        self, url: str, method: str = "GET", body: bytes | None = None
    ) -> tuple[int, str, bytes]:
        headers = {"User-Agent": "active-esl-native-sim-bridge/1"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if body is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            url, data=body, method=method, headers=headers
        )
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return (
                    response.status,
                    response.headers.get("Content-Type", "application/octet-stream"),
                    response.read(),
                )
        except urllib.error.HTTPError as error:
            return (
                error.code,
                error.headers.get("Content-Type", "application/json"),
                error.read(),
            )

    def config(self) -> bytes:
        t0 = time.perf_counter()
        url = f"{self.upstream}/node/v0/device/{self.device_id}/config"
        status, _, body = self.request(url)
        fetch_ms = (time.perf_counter() - t0) * 1000
        if status != 200:
            raise RuntimeError(f"upstream config returned HTTP {status}")
        config = json.loads(body)
        images = config.get("images")
        schedule = config.get("schedule")
        if not isinstance(images, list) or not isinstance(schedule, list):
            raise RuntimeError("upstream config lacks images/schedule arrays")

        next_urls: dict[str, str] = {}
        for image in images:
            image_id = image.get("image_id")
            image_url = image.get("url")
            if not isinstance(image_id, str) or not isinstance(image_url, str):
                raise RuntimeError("invalid image record in upstream config")
            parsed = urllib.parse.urlsplit(image_url)
            if parsed.scheme != "https":
                raise RuntimeError("upstream image URL must use HTTPS")
            next_urls[image_id] = image_url
            image["url"] = f"{self.public_base}/images/{image_id}{self.image_suffix}"

        with self.lock:
            self.image_urls = next_urls
        out = json.dumps(config, separators=(",", ":")).encode()
        etag = f'"{hashlib.sha256(out).hexdigest()[:16]}"'
        with self.lock:
            self._config_etag = etag
        print(
            f"bridge: config images={len(next_urls)} bytes={len(out)} "
            f"etag={etag} upstream={fetch_ms:.0f} ms "
            f"total={(time.perf_counter() - t0) * 1000:.0f} ms",
            flush=True,
        )
        # Warm ES6F cache in the background so gallery GETs are usually hits.
        if next_urls and self.prefetch_enabled:
            threading.Thread(
                target=self._prefetch_all,
                args=(list(next_urls.keys()),),
                name="es6f-prefetch",
                daemon=True,
            ).start()
        return out

    @property
    def config_etag(self) -> str | None:
        with self.lock:
            return self._config_etag

    def _prefetch_all(self, image_ids: list[str]) -> None:
        t0 = time.perf_counter()
        workers = min(self.prefetch_workers, len(image_ids))
        ok = 0
        fail = 0
        print(
            f"bridge: prefetch start n={len(image_ids)} workers={workers}",
            flush=True,
        )
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {
                pool.submit(self.image, image_id): image_id for image_id in image_ids
            }
            for fut in as_completed(futures):
                image_id = futures[fut]
                try:
                    fut.result()
                    ok += 1
                except Exception as error:
                    fail += 1
                    print(
                        f"bridge: prefetch {image_id} failed: {error}",
                        file=sys.stderr,
                        flush=True,
                    )
        print(
            f"bridge: prefetch done ok={ok} fail={fail} "
            f"in {(time.perf_counter() - t0) * 1000:.0f} ms",
            flush=True,
        )

    def image(self, image_id: str) -> bytes:
        t0 = time.perf_counter()
        with self.lock:
            source_url = self.image_urls.get(image_id)
        if source_url is None:
            # Refresh mapping if the simulator asks after a bridge restart.
            self.config()
            with self.lock:
                source_url = self.image_urls.get(image_id)
        if source_url is None:
            raise KeyError(image_id)

        # URL hash changes whenever e-tabelone points this ID at another asset.
        key = hashlib.sha256(source_url.split("?", 1)[0].encode()).hexdigest()[:16]
        raw_output = self.cache_dir / f"{image_id}-{key}.es6f"
        output = (
            self.cache_dir / f"{image_id}-{key}.es6f.lz4" if self.lz4 else raw_output
        )
        if output.exists():
            data = output.read_bytes()
            print(
                f"bridge: cache hit {image_id} {len(data)} B in "
                f"{(time.perf_counter() - t0) * 1000:.0f} ms",
                flush=True,
            )
            return data

        with self._id_lock(image_id):
            if output.exists():
                data = output.read_bytes()
                print(
                    f"bridge: cache hit {image_id} {len(data)} B in "
                    f"{(time.perf_counter() - t0) * 1000:.0f} ms",
                    flush=True,
                )
                return data

            t_fetch = time.perf_counter()
            status, content_type, source = self.request(source_url)
            fetch_ms = (time.perf_counter() - t_fetch) * 1000
            if status != 200:
                raise RuntimeError(f"upstream image returned HTTP {status}")
            suffix = ".png" if "png" in content_type.lower() else ".jpg"
            with tempfile.TemporaryDirectory(prefix="etabelone-image-") as tmp:
                source_path = Path(tmp) / f"source{suffix}"
                temp_output = Path(tmp) / "converted.es6f"
                source_path.write_bytes(source)
                t_conv = time.perf_counter()
                subprocess.run(
                    [
                        sys.executable,
                        str(self.converter),
                        "--image",
                        str(source_path),
                        "--output",
                        str(temp_output),
                        # Pack landscape sources into portrait ES6F the way Spectra6
                        # does; SDL present rotates CCW back to a full landscape window.
                        "--rotate-to-panel",
                    ],
                    check=True,
                )
                conv_ms = (time.perf_counter() - t_conv) * 1000
                os.replace(temp_output, raw_output)
                lz4_ms = 0.0
                if self.lz4:
                    t_lz4 = time.perf_counter()
                    subprocess.run(
                        [
                            sys.executable,
                            str(self.lz4_wrap),
                            str(raw_output),
                            "-o",
                            str(output),
                        ],
                        check=True,
                    )
                    lz4_ms = (time.perf_counter() - t_lz4) * 1000
            data = output.read_bytes()
            print(
                f"bridge: convert {image_id} src={len(source)} B "
                f"out={len(data)} B lz4={self.lz4} "
                f"fetch={fetch_ms:.0f} ms convert={conv_ms:.0f} ms "
                f"lz4={lz4_ms:.0f} ms "
                f"total={(time.perf_counter() - t0) * 1000:.0f} ms",
                flush=True,
            )
            return data

    def telemetry(self, body: bytes) -> tuple[int, str, bytes]:
        url = f"{self.upstream}/node/v0/device/{self.device_id}/telemetry"
        return self.request(url, method="POST", body=body)


class Handler(BaseHTTPRequestHandler):
    bridge: Bridge
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"bridge: {fmt % args}", flush=True)

    def send_body(
        self,
        status: int,
        content_type: str,
        body: bytes,
        *,
        etag: str | None = None,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        if etag:
            self.send_header("ETag", etag)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        try:
            parsed = urllib.parse.urlsplit(self.path)
            config_path = f"/node/v0/device/{self.bridge.device_id}/config"
            if parsed.path == config_path:
                body = self.bridge.config()
                etag = self.bridge.config_etag
                inm = self.headers.get("If-None-Match")
                if etag and inm and inm.strip() == etag:
                    print("bridge: config 304 Not Modified", flush=True)
                    self.send_response(304)
                    self.send_header("ETag", etag)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_body(200, "application/json", body, etag=etag)
                return
            prefix = "/images/"
            if parsed.path.startswith(prefix):
                name = parsed.path[len(prefix) :]
                if name.endswith(".es6f.lz4"):
                    image_id = name[: -len(".es6f.lz4")]
                elif name.endswith(".es6f"):
                    image_id = name[: -len(".es6f")]
                else:
                    image_id = ""
                if image_id:
                    body = self.bridge.image(image_id)
                    ctype = (
                        "application/vnd.etablone.es6f+lz4"
                        if body[:4] == b"\x04\x22\x4d\x18"
                        else "application/octet-stream"
                    )
                    self.send_body(200, ctype, body)
                    return
            self.send_body(404, "text/plain", b"not found\n")
        except KeyError:
            self.send_body(404, "text/plain", b"unknown image\n")
        except Exception as error:
            print(f"bridge: GET failed: {error}", file=sys.stderr, flush=True)
            self.send_body(502, "text/plain", b"upstream/conversion failed\n")

    def do_POST(self) -> None:  # noqa: N802
        telemetry_path = f"/node/v0/device/{self.bridge.device_id}/telemetry"
        if urllib.parse.urlsplit(self.path).path != telemetry_path:
            self.send_body(404, "text/plain", b"not found\n")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            status, content_type, body = self.bridge.telemetry(
                self.rfile.read(length)
            )
            self.send_body(status, content_type, body)
        except Exception as error:
            print(f"bridge: POST failed: {error}", file=sys.stderr, flush=True)
            self.send_body(502, "text/plain", b"upstream telemetry failed\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id", required=True)
    parser.add_argument(
        "--upstream", default="https://api.dev.e-tabelone.com"
    )
    parser.add_argument("--listen", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument(
        "--public-base", default="http://192.0.2.2:8765"
    )
    parser.add_argument(
        "--cache-dir", type=Path, default=Path("/tmp/eink-etabelone-cache")
    )
    parser.add_argument(
        "--token-env",
        default="ETABELONE_TOKEN",
        help="optional environment variable holding upstream bearer token",
    )
    parser.add_argument(
        "--prefetch-workers",
        type=int,
        default=int(os.environ.get("EINK_BRIDGE_PREFETCH_WORKERS", "4")),
        help="parallel convert workers after each config fetch",
    )
    parser.add_argument(
        "--lz4",
        action=argparse.BooleanOptionalAction,
        default=os.environ.get("EINK_BRIDGE_LZ4", "1") != "0",
        help="serve LZ4-framed ES6F (default on; use --no-lz4 for raw ES6F)",
    )
    args = parser.parse_args()

    bridge = Bridge(
        args.upstream,
        args.device_id,
        args.public_base,
        args.cache_dir,
        os.environ.get(args.token_env, ""),
        prefetch_workers=max(1, args.prefetch_workers),
        lz4=bool(args.lz4),
    )
    if os.environ.get("EINK_BRIDGE_PREFETCH", "1") == "0":
        bridge.prefetch_enabled = False

    Handler.bridge = bridge
    server = ThreadingHTTPServer((args.listen, args.port), Handler)
    print(
        f"bridge: {args.upstream} -> {args.public_base} "
        f"device={args.device_id} format="
        f"{'es6f.lz4' if bridge.lz4 else 'es6f'}",
        flush=True,
    )
    server.serve_forever()


if __name__ == "__main__":
    main()
