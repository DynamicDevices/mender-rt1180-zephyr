#!/usr/bin/env python3
"""Run one native_sim sync against the local e-tabelone ES6F bridge."""

from __future__ import annotations

import argparse
import os
import pty
import re
import select
import subprocess
import time
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*[mKJHD]")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id", required=True)
    parser.add_argument(
        "--binary", default="build-native_sim-eink/zephyr/zephyr.exe"
    )
    parser.add_argument("--base", default="http://192.0.2.2:8765")
    parser.add_argument(
        "--token",
        default="none",
        help="Bearer token for eink creds (none/- omit Authorization)",
    )
    parser.add_argument("--timeout", type=float, default=700.0)
    parser.add_argument(
        "--hold",
        type=float,
        default=0.0,
        help="keep simulator/SDL window alive after a successful refresh",
    )
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="remove flash.bin so the current scheduled job displays again",
    )
    args = parser.parse_args()

    if args.fresh:
        Path("flash.bin").unlink(missing_ok=True)

    master, slave = pty.openpty()
    process = subprocess.Popen(
        [args.binary],
        stdin=slave,
        stdout=slave,
        stderr=subprocess.STDOUT,
        close_fds=True,
    )
    os.close(slave)
    buffer = b""
    phase = 0
    started = time.time()

    def send(command: str) -> None:
        os.write(master, (command + "\n").encode())
        print(f">> {command}", flush=True)

    try:
        while time.time() - started < args.timeout and process.poll() is None:
            readable, _, _ = select.select([master], [], [], 0.25)
            if master not in readable:
                continue
            try:
                chunk = os.read(master, 16384)
            except OSError:
                break
            if not chunk:
                break
            buffer += chunk
            text = buffer.decode("utf-8", "replace")
            if phase == 0 and (
                "Received: 192.0.2." in text
                or "Network already has a preferred IPv4 address" in text
                or "Address[1]: 192.0.2." in text
            ):
                send(f"eink creds {args.base} {args.device_id} {args.token}")
                phase = 1
            elif phase == 1 and "credentials updated" in text:
                send("eink sync")
                phase = 2
            elif phase == 2 and ("sync ok" in text or "sync failed" in text):
                # Deferred logs + gallery can trail shell "sync ok"; wait for paint.
                drain_until = time.time() + 120.0
                while time.time() < drain_until:
                    readable, _, _ = select.select([master], [], [], 0.25)
                    if master in readable:
                        try:
                            buffer += os.read(master, 16384)
                        except OSError:
                            break
                    text = ANSI.sub("", buffer.decode("utf-8", "replace"))
                    if "refresh done result=0" in text and "telemetry posted" in text:
                        break
                    if "fast path" in text and "telemetry posted" in text:
                        break
                    if "prof: sync total=" in text and "telemetry posted" in text:
                        break
                    if "lz4" in text.lower() and "telemetry posted" in text and (
                        "refresh done result=0" in text or "show job=" in text
                    ):
                        break
                    if "sync failed" in text:
                        break
                # One short drain so trailing prof: lines land after telemetry.
                drain_until = time.time() + 2.0
                while time.time() < drain_until:
                    readable, _, _ = select.select([master], [], [], 0.25)
                    if master in readable:
                        try:
                            buffer += os.read(master, 16384)
                        except OSError:
                            break
                    text = ANSI.sub("", buffer.decode("utf-8", "replace"))
                    if "prof: sync total=" in text:
                        break
                break
    finally:
        text = ANSI.sub("", buffer.decode("utf-8", "replace"))
        for line in text.splitlines():
            if any(
                marker in line
                for marker in (
                    "Mender client disabled",
                    "parsed ",
                    "downloading image",
                    "accepted image",
                    "no new scheduled image",
                    "show job=",
                    "refresh done",
                    "scheduler tick result",
                    "telemetry posted",
                    "prof:",
                    "lz4",
                    "LZ4",
                    "gallery",
                    "fast path",
                    "sync ok",
                    "sync failed",
                    "heap corruption",
                    "FATAL ERROR",
                )
            ):
                print(line)

        painted = (
            ("parsed " in text and "show job=" in text and "refresh done result=0" in text)
            or "fast path" in text
        )
        lz4_ok = any(
            marker in text
            for marker in (
                "lz4_materialize",
                "lz4 expand",
                "lz4_decompress",
                "delivery_format",
                ".es6f.lz4",
                "EXPAND",
            )
        ) or ("lz4" in text.lower() and painted)
        succeeded = (
            "Mender client disabled" in text
            and "sync ok" in text
            and "heap corruption" not in text
            and "FATAL ERROR" not in text
            and "telemetry posted" in text
            and painted
            and (lz4_ok or "es6f.lz4" in text or "prof: lz4" in text)
        )
        print("RESULT", "OK" if succeeded else "FAIL")
        if succeeded and args.hold > 0:
            print(f"Holding simulator for {args.hold:g}s", flush=True)
            time.sleep(args.hold)
        process.kill()
        try:
            process.wait(timeout=3)
        except Exception:
            pass

    return 0 if succeeded else 1


if __name__ == "__main__":
    raise SystemExit(main())
