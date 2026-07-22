#!/usr/bin/env bash
# Privileged helper: create user-owned zeth TAP, host addressing, NAT, DHCP.
# Installed under /usr/local/libexec by setup-native-sim-tap.sh.
set -euo pipefail

IFACE="${ZETH_IFACE:-zeth}"
OWNER="${ZETH_USER:?ZETH_USER required}"
GROUP="${ZETH_GROUP:-$OWNER}"
HOST_V4="${ZETH_HOST_V4:-192.0.2.2/24}"
HOST_HWADDR="${ZETH_HWADDR:-00:00:5e:00:53:ff}"
NAT_SRC="${ZETH_NAT_SRC:-192.0.2.0/24}"
DNSMASQ_CONF="${ZETH_DNSMASQ_CONF:-/etc/native-sim-zeth/dnsmasq.conf}"
PIDFILE="${ZETH_DNSMASQ_PID:-/run/dnsmasq_zeth.pid}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "zeth-start.sh must run as root (via systemd/native-sim-zeth.service)" >&2
  exit 1
fi

if ! ip link show "${IFACE}" &>/dev/null; then
  ip tuntap add dev "${IFACE}" mode tap user "${OWNER}" group "${GROUP}"
fi

# Ensure ownership even if an older root-owned zeth was left behind.
if ip tuntap show 2>/dev/null | grep -q "^${IFACE}:"; then
  :
fi

ip link set dev "${IFACE}" address "${HOST_HWADDR}"
ip link set dev "${IFACE}" up

if ! ip -4 addr show dev "${IFACE}" | grep -q "inet ${HOST_V4%%/*}"; then
  ip address add "${HOST_V4}" dev "${IFACE}"
fi
ip route replace "${NAT_SRC}" dev "${IFACE}" >/dev/null 2>&1 || true

sysctl -w net.ipv4.ip_forward=1 >/dev/null
if ! iptables -t nat -C POSTROUTING -s "${NAT_SRC}" -j MASQUERADE 2>/dev/null; then
  iptables -t nat -A POSTROUTING -s "${NAT_SRC}" -j MASQUERADE
fi
iptables -P FORWARD ACCEPT 2>/dev/null || true

if [[ -f "${PIDFILE}" ]] && kill -0 "$(cat "${PIDFILE}")" 2>/dev/null; then
  echo "${IFACE}: dnsmasq already running (pid $(cat "${PIDFILE}"))"
else
  rm -f "${PIDFILE}"
  dnsmasq -C "${DNSMASQ_CONF}" -x "${PIDFILE}"
fi

echo "${IFACE}: up (owner=${OWNER}:${GROUP}, host=${HOST_V4}, NAT+DHCP ready)"
