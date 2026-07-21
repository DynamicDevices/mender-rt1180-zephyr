#!/usr/bin/env python3
"""Translate live e-tabelone JPEG/PNG assets to ES6F for native_sim.

The production firmware remains ES6F-only. This development bridge fetches the
real config/schedule, rewrites image URLs to a local endpoint, and lazily
converts each source asset into the exact packed frame consumed by Zephyr.
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
import urllib.error
import urllib.parse
import urllib.request
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
    ) -> None:
        self.upstream = upstream.rstrip("/")
        self.device_id = device_id
        self.public_base = public_base.rstrip("/")
        self.cache_dir = cache_dir
        self.token = token
        self.image_urls: dict[str, str] = {}
        self.lock = threading.Lock()
        self.converter = Path(__file__).with_name("gen-eink-frame.py")
        cache_dir.mkdir(parents=True, exist_ok=True)

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
        url = f"{self.upstream}/node/v0/device/{self.device_id}/config"
        status, _, body = self.request(url)
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
            image["url"] = f"{self.public_base}/images/{image_id}.es6f"

        with self.lock:
            self.image_urls = next_urls
        return json.dumps(config, separators=(",", ":")).encode()

    def image(self, image_id: str) -> bytes:
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
        output = self.cache_dir / f"{image_id}-{key}.es6f"
        if output.exists():
            return output.read_bytes()

        status, content_type, source = self.request(source_url)
        if status != 200:
            raise RuntimeError(f"upstream image returned HTTP {status}")
        suffix = ".png" if "png" in content_type.lower() else ".jpg"
        with tempfile.TemporaryDirectory(prefix="etabelone-image-") as tmp:
            source_path = Path(tmp) / f"source{suffix}"
            temp_output = Path(tmp) / "converted.es6f"
            source_path.write_bytes(source)
            subprocess.run(
                [
                    sys.executable,
                    str(self.converter),
                    "--image",
                    str(source_path),
                    "--output",
                    str(temp_output),
                ],
                check=True,
            )
            os.replace(temp_output, output)
        return output.read_bytes()

    def telemetry(self, body: bytes) -> tuple[int, str, bytes]:
        url = f"{self.upstream}/node/v0/device/{self.device_id}/telemetry"
        return self.request(url, method="POST", body=body)


class Handler(BaseHTTPRequestHandler):
    bridge: Bridge
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"bridge: {fmt % args}", flush=True)

    def send_body(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        try:
            parsed = urllib.parse.urlsplit(self.path)
            config_path = f"/node/v0/device/{self.bridge.device_id}/config"
            if parsed.path == config_path:
                self.send_body(200, "application/json", self.bridge.config())
                return
            prefix = "/images/"
            if parsed.path.startswith(prefix) and parsed.path.endswith(".es6f"):
                image_id = parsed.path[len(prefix) : -len(".es6f")]
                self.send_body(
                    200,
                    "application/octet-stream",
                    self.bridge.image(image_id),
                )
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
    args = parser.parse_args()

    Handler.bridge = Bridge(
        args.upstream,
        args.device_id,
        args.public_base,
        args.cache_dir,
        os.environ.get(args.token_env, ""),
    )
    server = ThreadingHTTPServer((args.listen, args.port), Handler)
    print(
        f"bridge: {args.upstream} -> {args.public_base} "
        f"device={args.device_id} (production remains ES6F-only)",
        flush=True,
    )
    server.serve_forever()


if __name__ == "__main__":
    main()
