#!/usr/bin/env bash
# Privileged helper: tear down zeth TAP, NAT rule, and dnsmasq.
set -euo pipefail

IFACE="${ZETH_IFACE:-zeth}"
NAT_SRC="${ZETH_NAT_SRC:-192.0.2.0/24}"
PIDFILE="${ZETH_DNSMASQ_PID:-/run/dnsmasq_zeth.pid}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "zeth-stop.sh must run as root (via systemd/native-sim-zeth.service)" >&2
  exit 1
fi

if [[ -f "${PIDFILE}" ]]; then
  kill "$(cat "${PIDFILE}")" 2>/dev/null || true
  rm -f "${PIDFILE}"
fi

iptables -t nat -D POSTROUTING -s "${NAT_SRC}" -j MASQUERADE 2>/dev/null || true

if ip link show "${IFACE}" &>/dev/null; then
  ip link set "${IFACE}" down || true
  ip tuntap del dev "${IFACE}" mode tap || true
fi

echo "${IFACE}: removed"
