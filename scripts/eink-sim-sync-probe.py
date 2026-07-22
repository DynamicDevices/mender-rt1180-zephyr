#!/usr/bin/env python3
"""Drive native_sim through file:// e-tabelone fixture sync + scheduled show."""
from __future__ import annotations

import argparse
import json
import os
import pty
import re
import select
import subprocess
import sys
import time
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*[mKJ]")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--bin",
        default=str(repo_root / "build-native_sim-eink/zephyr/zephyr.exe"),
    )
    ap.add_argument("--device", default="sim-local")
    ap.add_argument("--image", default="/tmp/eink-zephyr/images/white.es6f")
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument(
        "--expect-lz4",
        choices=("auto", "expand", "keep", "no"),
        default="auto",
        help="auto: expand if image ends with .lz4; expand/keep/no override",
    )
    args = ap.parse_args()

    image = Path(args.image)
    if not image.is_file():
        print(f"missing image: {image}", file=sys.stderr)
        return 2

    expect_lz4 = args.expect_lz4
    if expect_lz4 == "auto":
        expect_lz4 = "expand" if image.name.endswith(".lz4") else "no"

    fixture_dir = Path("/tmp/eink-fixture")
    fixture_dir.mkdir(exist_ok=True)
    # Cron is minute+hour numeric only (no '*'); 00:00 UTC is always overdue.
    bars = Path("/tmp/eink-zephyr/images/bars.es6f")
    if not bars.is_file():
        # verify gate generates bars; fall back to white-only if absent
        bars = image
    cfg = {
        "images": [
            {
                "image_id": "white",
                "url": f"file://{image.resolve()}",
            },
            {
                "image_id": "bars",
                "url": f"file://{bars.resolve()}",
            },
        ],
        "schedule": [
            {
                "job_id": "j1",
                "image_id": "white",
                "cron": "0 0 * * *",
            },
            {
                "job_id": "j2",
                "image_id": "bars",
                "cron": "0 18 * * *",
            },
        ],
        "orientation": 0,
    }
    (fixture_dir / f"{args.device}.config.json").write_text(json.dumps(cfg))

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        [args.bin],
        stdin=slave,
        stdout=slave,
        stderr=subprocess.STDOUT,
        close_fds=True,
    )
    os.close(slave)

    buf = b""
    phase = 0
    start = time.time()

    def feed(cmd: str) -> None:
        os.write(master, (cmd + "\n").encode())
        print(">>", cmd, flush=True)

    try:
        while time.time() - start < args.timeout and proc.poll() is None:
            r, _, _ = select.select([master], [], [], 0.25)
            if master not in r:
                continue
            try:
                chunk = os.read(master, 8192)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            text = ANSI.sub("", buf.decode("utf-8", "replace"))

            if phase == 0 and "eink selftest OK" in text:
                feed(f"eink creds file://{fixture_dir} {args.device} none")
                phase = 1
            elif phase == 1 and "credentials updated" in text:
                feed("eink sync")
                phase = 2
            elif phase == 2 and ("sync ok" in text or "sync failed" in text):
                # Critical path ends at telemetry; gallery may still be queued.
                drain_until = time.time() + 60.0
                while time.time() < drain_until:
                    r, _, _ = select.select([master], [], [], 0.25)
                    if master not in r:
                        continue
                    try:
                        more = os.read(master, 8192)
                    except OSError:
                        break
                    if not more:
                        break
                    buf += more
                    text = ANSI.sub("", buf.decode("utf-8", "replace"))
                    if "sync failed" in text:
                        break
                    has_telem = (
                        "telemetry posted" in text
                        or "schedule next_wake unix=" in text
                    )
                    if not has_telem:
                        continue
                    if "gallery deferred (" in text:
                        if (
                            "gallery deferred done" in text
                            or "accepted image bars" in text
                            or "importing fixture image bars" in text
                            or "gallery image bars already cached" in text
                        ):
                            break
                        continue
                    if "prof: sync total=" in text:
                        break
                break
    finally:
        proc.kill()
        try:
            proc.wait(timeout=3)
        except Exception:
            pass

    text = ANSI.sub("", buf.decode("utf-8", "replace"))
    for line in text.splitlines():
        if any(
            k in line
            for k in (
                "eink_",
                "sync",
                "cred",
                "parsed",
                "import",
                "accepted",
                "reject",
                "show job",
                "refresh",
                "scheduler tick",
                "next_wake",
                "gallery",
                "failed",
                "prof:",
                "LZ4",
            )
        ):
            print(line)

    white_ok = (
        "accepted image white" in text
        or "accepted LZ4 image white" in text
        or "importing fixture image white" in text
        or "display image white already cached" in text
        or "due image white already cached" in text
        or "will show current scheduled image white" in text
    )
    if expect_lz4 == "expand":
        white_ok = white_ok and (
            "LZ4 frame detected for white" in text
            or "lz4_expand=" in text
            or "LZ4 expand " in text
        )
        white_ok = white_ok and "accepted image white" in text
    elif expect_lz4 == "keep":
        white_ok = white_ok and (
            "LZ4 frame kept compressed for white" in text
            or "accepted LZ4 image white" in text
        )

    ok_full = (
        "parsed" in text
        and white_ok
        and (
            "accepted image bars" in text
            or "importing fixture image bars" in text
            or "gallery image bars already cached" in text
            or "gallery image bars" in text
            or "gallery deferred done" in text
            # Warm store: nothing left to cache (defer queued 0).
            or "gallery=0 (0 dl)" in text
        )
        and ("show job=j1" in text or "refresh done result=0" in text)
    )
    ok_fast = (
        "sync fast path" in text
        and ("telemetry posted" in text or "telemetry deferred" in text)
        and "schedule next_wake unix=" in text
    )
    ok = (
        "sync ok" in text
        and (ok_full or ok_fast)
        and "schedule next_wake unix=" in text
    )
    print("RESULT", "OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
