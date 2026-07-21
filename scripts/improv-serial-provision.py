#!/usr/bin/env python3
"""Drive Improv Wi-Fi provisioning over a serial port / native_sim PTY.

Chrome's Web Serial installer (https://www.improv-wifi.com/serial/) does not
enumerate Linux pseudo-terminals, so this script is the practical way to exercise
the improv-zephyr serial handshake against the native_sim emulator target.

It speaks the Improv serial wire format (see modules/improv-zephyr/src/improv.c):

    b"IMPROV" + version(1) + type(1) + len(1) + payload(len) + checksum(1)

Usage:
    python3 scripts/improv-serial-provision.py /dev/pts/N --ssid MyNet --psk secret

The device (native_sim + CONFIG_APP_WIFI_SIM) reports association success and
starts DHCPv4 on the native TAP interface, so Mender connects end-to-end. The
firmware's log output is printed inline as it arrives.
"""
import argparse
import os
import sys
import termios
import time

IMPROV_HEADER = b"IMPROV"
IMPROV_VERSION = 1

# Serial frame types (device <-> host)
TYPE_CURRENT_STATE = 0x01
TYPE_ERROR_STATE = 0x02
TYPE_RPC = 0x03
TYPE_RPC_RESPONSE = 0x04

# RPC command bytes (host -> device)
CMD_WIFI_SETTINGS = 0x01
CMD_GET_CURRENT_STATE = 0x02
CMD_GET_DEVICE_INFO = 0x03
CMD_GET_WIFI_NETWORKS = 0x04

STATE_NAMES = {
    0x00: "STOPPED",
    0x01: "AWAITING_AUTHORIZATION",
    0x02: "AUTHORIZED",
    0x03: "PROVISIONING",
    0x04: "PROVISIONED",
}
ERROR_NAMES = {
    0x00: "NONE",
    0x01: "INVALID_RPC",
    0x02: "UNKNOWN_RPC",
    0x03: "UNABLE_TO_CONNECT",
    0x04: "NOT_AUTHORIZED",
    0xFF: "UNKNOWN",
}


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def wrap(frame_type: int, payload: bytes) -> bytes:
    body = IMPROV_HEADER + bytes([IMPROV_VERSION, frame_type, len(payload)]) + payload
    return body + bytes([checksum(body)])


def rpc_body(command: int, payload: bytes = b"") -> bytes:
    return bytes([command, len(payload)]) + payload


def wifi_settings_body(ssid: str, psk: str) -> bytes:
    s = ssid.encode()
    p = psk.encode()
    return rpc_body(CMD_WIFI_SETTINGS, bytes([len(s)]) + s + bytes([len(p)]) + p)


def open_port(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    try:
        attrs = termios.tcgetattr(fd)
        # Raw mode: no translation, no echo.
        for i in (0, 1, 2, 3):
            attrs[i] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except termios.error:
        # Not a tty (e.g. a plain fifo) - that's fine.
        pass
    return fd


class FrameReader:
    """Incrementally extract Improv frames from a byte stream, passing through
    any non-frame bytes as device log text."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf += data
        frames = []
        while True:
            idx = self.buf.find(IMPROV_HEADER)
            if idx == -1:
                # Keep a small tail in case a header is split across reads.
                if len(self.buf) > len(IMPROV_HEADER):
                    text = self.buf[: -len(IMPROV_HEADER)]
                    if text:
                        _emit_text(text)
                    del self.buf[: -len(IMPROV_HEADER)]
                break
            if idx > 0:
                _emit_text(self.buf[:idx])
                del self.buf[:idx]
            # Need header(6) + ver + type + len before we know the size.
            if len(self.buf) < 9:
                break
            length = self.buf[8]
            total = 9 + length + 1
            if len(self.buf) < total:
                break
            frame = bytes(self.buf[:total])
            del self.buf[:total]
            if checksum(frame[:-1]) == frame[-1] and frame[6] == IMPROV_VERSION:
                frames.append((frame[7], frame[9 : 9 + length]))
            else:
                _emit_text(frame)  # not a valid frame; treat as noise
        return frames


def _emit_text(data: bytes):
    sys.stdout.write(data.decode(errors="replace"))
    sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", help="serial port / PTY, e.g. /dev/pts/5")
    ap.add_argument("--ssid", required=True)
    ap.add_argument("--psk", default="")
    ap.add_argument("--timeout", type=float, default=60.0,
                    help="seconds to wait for PROVISIONED")
    args = ap.parse_args()

    fd = open_port(args.port)
    reader = FrameReader()

    def send(frame_type, payload):
        os.write(fd, wrap(frame_type, payload))

    print(f"[provision] opened {args.port}", flush=True)
    send(TYPE_RPC, rpc_body(CMD_GET_DEVICE_INFO))
    time.sleep(0.2)
    send(TYPE_RPC, rpc_body(CMD_GET_CURRENT_STATE))
    time.sleep(0.2)
    print(f"[provision] sending Wi-Fi settings ssid='{args.ssid}'", flush=True)
    send(TYPE_RPC, wifi_settings_body(args.ssid, args.psk))

    deadline = time.time() + args.timeout
    rc = 1
    while time.time() < deadline:
        try:
            data = os.read(fd, 4096)
        except BlockingIOError:
            data = b""
        if not data:
            time.sleep(0.05)
            continue
        for ftype, payload in reader.feed(data):
            if ftype == TYPE_CURRENT_STATE and payload:
                st = payload[0]
                print(f"\n[provision] state -> {STATE_NAMES.get(st, hex(st))}", flush=True)
                if st == 0x04:  # PROVISIONED
                    print("[provision] SUCCESS: device provisioned", flush=True)
                    rc = 0
                    deadline = min(deadline, time.time() + 3.0)  # linger for logs
            elif ftype == TYPE_ERROR_STATE and payload:
                er = payload[0]
                print(f"\n[provision] error -> {ERROR_NAMES.get(er, hex(er))}", flush=True)
                if er != 0x00:
                    rc = 2
                    break
            elif ftype == TYPE_RPC_RESPONSE:
                print(f"\n[provision] rpc-response: {payload!r}", flush=True)

    os.close(fd)
    if rc == 0:
        print("[provision] done.", flush=True)
    else:
        print("[provision] did not reach PROVISIONED.", flush=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
