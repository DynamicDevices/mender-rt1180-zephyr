#!/usr/bin/env python3
"""Cold vs warm e-tabelone sync timing profile (bridge + Zephyr native_sim).

Runs two syncs against the live bridge:
  cold — empty host ES6F cache + fresh flash.bin (convert + download + paint)
  warm — host cache kept, flash.bin cleared (download + paint only)

Parses firmware `prof:` lines and bridge stage timings into a summary table.
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICE_DEFAULT = "imx93-jaguar-eink-ae67616ce3445ce2c3981b8cf7680167"
CACHE_DIR = Path("/tmp/eink-etabelone-cache")
BRIDGE_LOG = Path("/tmp/eink-profile-bridge.log")
PROF_SYNC = re.compile(
    r"prof: sync total=(\d+) ms config=(\d+) primary=(\d+) paint=(\d+) "
    r"gallery=(\d+) \((\d+) dl\) telem=(\d+)"
)
PROF_IMAGE = re.compile(
    r"prof: image (\S+) http=(\d+) ms validate=(\d+) ms total=(\d+) ms ret=(-?\d+)"
)
BRIDGE_CONVERT = re.compile(
    r"bridge: convert (\S+) .* fetch=([\d.]+) ms convert=([\d.]+) ms total=([\d.]+) ms"
)
BRIDGE_HIT = re.compile(r"bridge: cache hit (\S+) .* in ([\d.]+) ms")
BRIDGE_CONFIG = re.compile(
    r"bridge: config images=(\d+) .* upstream=([\d.]+) ms total=([\d.]+) ms"
)


def wait_bridge(device_id: str, timeout: float = 30.0) -> None:
    url = f"http://127.0.0.1:8765/node/v0/device/{device_id}/config"
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                if response.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise SystemExit("bridge failed to become ready")


def port_in_use(port: int = 8765) -> bool:
    out = subprocess.run(
        ["ss", "-H", "-ltn", f"( sport = :{port} )"],
        capture_output=True,
        text=True,
        check=False,
    )
    return bool(out.stdout.strip())


def clear_cache() -> None:
    if CACHE_DIR.exists():
        for path in CACHE_DIR.iterdir():
            if path.is_file():
                path.unlink()
    else:
        CACHE_DIR.mkdir(parents=True, exist_ok=True)


def run_probe(
    device_id: str, binary: Path, timeout: float, *, fresh: bool = True
) -> tuple[str, float]:
    if fresh:
        (ROOT / "flash.bin").unlink(missing_ok=True)
    t0 = time.perf_counter()
    cmd = [
        sys.executable,
        str(ROOT / "scripts/eink-sim-etabelone-probe.py"),
        "--device-id",
        device_id,
        "--binary",
        str(binary),
        "--timeout",
        str(timeout),
    ]
    if fresh:
        cmd.append("--fresh")
    proc = subprocess.run(
        cmd,
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        check=False,
    )
    wall = (time.perf_counter() - t0) * 1000
    text = proc.stdout + "\n" + proc.stderr
    if proc.returncode != 0 or "RESULT OK" not in text:
        print(text[-8000:], file=sys.stderr)
        raise SystemExit(f"probe failed (exit {proc.returncode})")
    return text, wall


def parse_run(label: str, probe_text: str, bridge_text: str, wall_ms: float) -> dict:
    sync = None
    for match in PROF_SYNC.finditer(probe_text):
        sync = {
            "total": int(match.group(1)),
            "config": int(match.group(2)),
            "primary": int(match.group(3)),
            "paint": int(match.group(4)),
            "gallery": int(match.group(5)),
            "gallery_dl": int(match.group(6)),
            "telem": int(match.group(7)),
        }
    images = [
        {
            "id": m.group(1)[:8],
            "http": int(m.group(2)),
            "validate": int(m.group(3)),
            "total": int(m.group(4)),
        }
        for m in PROF_IMAGE.finditer(probe_text)
    ]
    converts = [
        {
            "id": m.group(1)[:8],
            "fetch": float(m.group(2)),
            "convert": float(m.group(3)),
            "total": float(m.group(4)),
        }
        for m in BRIDGE_CONVERT.finditer(bridge_text)
    ]
    hits = [
        {"id": m.group(1)[:8], "ms": float(m.group(2))}
        for m in BRIDGE_HIT.finditer(bridge_text)
    ]
    # Ignore stale convert lines if this slice is cache-hit dominated (slice race).
    if hits and len(hits) >= len(converts):
        converts = []
    cfg = None
    for match in BRIDGE_CONFIG.finditer(bridge_text):
        cfg = {
            "images": int(match.group(1)),
            "upstream": float(match.group(2)),
            "total": float(match.group(3)),
        }
    return {
        "label": label,
        "wall_ms": wall_ms,
        "sync": sync,
        "images": images,
        "converts": converts,
        "hits": hits,
        "bridge_config": cfg,
    }


def print_report(runs: list[dict]) -> None:
    print("\n=== Sync profile summary ===")
    print(
        f"{'run':<6} {'wall':>8} {'fw_tot':>8} {'config':>8} {'primary':>8} "
        f"{'paint':>8} {'gallery':>8} {'telem':>8} {'fw_imgs':>8} "
        f"{'br_conv':>8} {'br_hit':>6}"
    )
    for run in runs:
        s = run["sync"] or {}
        print(
            f"{run['label']:<6} {run['wall_ms']:8.0f} "
            f"{s.get('total', -1):8} {s.get('config', -1):8} "
            f"{s.get('primary', -1):8} {s.get('paint', -1):8} "
            f"{s.get('gallery', -1):8} {s.get('telem', -1):8} "
            f"{len(run['images']):8} {len(run['converts']):8} {len(run['hits']):6}"
        )

    for run in runs:
        print(f"\n--- {run['label']} detail ---")
        if run["bridge_config"]:
            c = run["bridge_config"]
            print(
                f"  bridge config: {c['images']} images, "
                f"upstream {c['upstream']:.0f} ms, total {c['total']:.0f} ms"
            )
        if run["converts"]:
            fetch = sum(x["fetch"] for x in run["converts"])
            conv = sum(x["convert"] for x in run["converts"])
            tot = sum(x["total"] for x in run["converts"])
            print(
                f"  bridge convert Σ: fetch={fetch:.0f} ms convert={conv:.0f} ms "
                f"total={tot:.0f} ms ({len(run['converts'])} images)"
            )
            for item in run["converts"]:
                print(
                    f"    {item['id']} fetch={item['fetch']:.0f} "
                    f"convert={item['convert']:.0f} total={item['total']:.0f}"
                )
        if run["hits"]:
            print(
                f"  bridge cache hits: {len(run['hits'])} "
                f"(Σ {sum(x['ms'] for x in run['hits']):.0f} ms)"
            )
        if run["images"]:
            http = sum(x["http"] for x in run["images"])
            val = sum(x["validate"] for x in run["images"])
            print(
                f"  firmware downloads: http={http} ms validate={val} ms "
                f"({len(run['images'])} images)"
            )
            for item in run["images"]:
                print(
                    f"    {item['id']} http={item['http']} "
                    f"validate={item['validate']} total={item['total']}"
                )
        if run["sync"]:
            s = run["sync"]
            print(
                f"  firmware sync: config={s['config']} primary={s['primary']} "
                f"paint={s['paint']} gallery={s['gallery']} "
                f"({s['gallery_dl']} dl) telem={s['telem']} total={s['total']}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id", default=DEVICE_DEFAULT)
    parser.add_argument(
        "--binary",
        default=str(ROOT / "build-native_sim-eink-sdl/zephyr/zephyr.exe"),
    )
    parser.add_argument("--timeout", type=float, default=700.0)
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="do not rebuild before profiling",
    )
    parser.add_argument(
        "--warm-only",
        action="store_true",
        help="skip cold run (keep existing host cache)",
    )
    args = parser.parse_args()
    binary = Path(args.binary)
    os.chdir(ROOT)

    if not args.skip_build:
        print("Building native_sim SDL firmware…", flush=True)
        subprocess.run(
            ["./scripts/build-native-sim-eink-sdl.sh"],
            check=True,
        )
    if not binary.exists():
        raise SystemExit(f"missing binary: {binary}")

    if port_in_use():
        raise SystemExit("port 8765 already in use; stop the existing bridge first")

    BRIDGE_LOG.unlink(missing_ok=True)
    bridge = subprocess.Popen(
        [
            sys.executable,
            str(ROOT / "scripts/eink-etabelone-bridge.py"),
            "--device-id",
            args.device_id,
        ],
        stdout=BRIDGE_LOG.open("w"),
        stderr=subprocess.STDOUT,
        cwd=str(ROOT),
    )

    def cleanup(*_args: object) -> None:
        if bridge.poll() is None:
            bridge.send_signal(signal.SIGTERM)
            try:
                bridge.wait(timeout=5)
            except subprocess.TimeoutExpired:
                bridge.kill()

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    runs: list[dict] = []
    try:
        wait_bridge(args.device_id)
        print("bridge ready", flush=True)

        def bridge_slice(start: int) -> str:
            data = BRIDGE_LOG.read_bytes()
            return data[start:].decode("utf-8", "replace")

        if not args.warm_only:
            print("\n=== COLD sync (empty host cache + fresh flash) ===", flush=True)
            clear_cache()
            cold_start = BRIDGE_LOG.stat().st_size if BRIDGE_LOG.exists() else 0
            probe_text, wall = run_probe(args.device_id, binary, args.timeout)
            runs.append(
                parse_run("cold", probe_text, bridge_slice(cold_start), wall)
            )
            print(f"cold wall {wall:.0f} ms", flush=True)

        print("\n=== WARM sync (host cache warm, fresh flash) ===", flush=True)
        warm_start = BRIDGE_LOG.stat().st_size if BRIDGE_LOG.exists() else 0
        probe_text, wall = run_probe(args.device_id, binary, args.timeout, fresh=True)
        runs.append(parse_run("warm", probe_text, bridge_slice(warm_start), wall))
        print(f"warm wall {wall:.0f} ms", flush=True)

        # Keep LittleFS: exercises telem-only / skip-radio wake path.
        print("\n=== DEVICE-WARM sync (host + flash kept) ===", flush=True)
        time.sleep(1.0)  # let deferred gallery finish writing before reboot
        dwarm_start = BRIDGE_LOG.stat().st_size if BRIDGE_LOG.exists() else 0
        probe_text, wall = run_probe(
            args.device_id, binary, args.timeout, fresh=False
        )
        runs.append(parse_run("dwarm", probe_text, bridge_slice(dwarm_start), wall))
        print(f"dwarm wall {wall:.0f} ms", flush=True)

        print_report(runs)

        # Optimization hints from measured data
        print("\n=== Optimization candidates ===")
        cold = next((r for r in runs if r["label"] == "cold"), None)
        warm = next((r for r in runs if r["label"] == "warm"), None)
        dwarm = next((r for r in runs if r["label"] == "dwarm"), None)
        if cold and cold["converts"]:
            conv = sum(x["convert"] for x in cold["converts"])
            fetch = sum(x["fetch"] for x in cold["converts"])
            print(
                f"- Cold host convert Σ {conv:.0f} ms / upstream fetch Σ {fetch:.0f} ms "
                f"— pre-warm cache or parallelize convert workers."
            )
        if warm and warm["images"]:
            http = sum(x["http"] for x in warm["images"])
            val = sum(x["validate"] for x in warm["images"])
            avg = http / max(len(warm["images"]), 1)
            print(
                f"- Warm Zephyr HTTP Σ {http} ms ({avg:.0f} ms/image) — "
                f"TCP window / recv buf / fewer round-trips."
            )
            print(
                f"- Validate/accept Σ {val} ms — consider CRC skip when size+magic match "
                f"or defer gallery validate."
            )
        if warm and warm["sync"]:
            s = warm["sync"]
            if s["gallery"] > s["primary"] + s["paint"]:
                print(
                    f"- Gallery phase {s['gallery']} ms dominates after primary paint "
                    f"({s['primary']}+{s['paint']} ms) — defer gallery until idle / "
                    f"skip if battery-critical."
                )
            if s["paint"] > 500:
                print(
                    f"- Paint/tick {s['paint']} ms — profile display blit / SDL present."
                )
            if s["telem"] > 200:
                print(f"- Telemetry {s['telem']} ms — fire-and-forget or batch.")
        if warm and warm["sync"] and warm["sync"]["config"] > 200:
            print(
                f"- Config fetch {warm['sync']['config']} ms on warm path — "
                f"cache ETag / shrink JSON."
            )
        if dwarm and dwarm["sync"]:
            s = dwarm["sync"]
            print(
                f"- Device-warm firmware sync total={s['total']} ms "
                f"(config={s['config']} primary={s['primary']} telem={s['telem']})."
            )
            if s["config"] == 0 and s["primary"] == 0:
                print("  Fast path active (telem-only).")
    finally:
        cleanup()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
