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
    args = ap.parse_args()

    image = Path(args.image)
    if not image.is_file():
        print(f"missing image: {image}", file=sys.stderr)
        return 2

    fixture_dir = Path("/tmp/eink-fixture")
    fixture_dir.mkdir(exist_ok=True)
    # Cron is minute+hour numeric only (no '*'); 00:00 UTC is always overdue.
    cfg = {
        "images": [
            {
                "image_id": "white",
                "url": f"file://{image.resolve()}",
            }
        ],
        "schedule": [
            {
                "job_id": "j1",
                "image_id": "white",
                "cron": "0 0 * * *",
            }
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
                # Zephyr deferred logging can flush after shell_print("sync ok").
                drain_until = time.time() + 8.0
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
                    if "telemetry posted" in text or "scheduler tick result=" in text:
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
                "failed",
            )
        ):
            print(line)

    ok = (
        "sync ok" in text
        and "parsed" in text
        and ("accepted image white" in text or "importing fixture image white" in text)
        and ("show job=j1" in text or "refresh done result=0" in text)
    )
    print("RESULT", "OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
