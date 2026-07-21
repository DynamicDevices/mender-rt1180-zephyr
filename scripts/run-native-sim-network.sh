#!/usr/bin/env bash
# Start/stop/status for Zephyr native_sim TAP + DHCP + NAT.
#
# Preferred path (no sudo day-to-day):
#   sudo ./scripts/setup-native-sim-tap.sh install   # once
#   ./scripts/run-native-sim-network.sh start|stop|status
#
# Fallback (legacy, needs sudo each time): Zephyr net-tools nat.conf.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NET_TOOLS="${ROOT}/tools/net-tools"
NET_SETUP="${NET_TOOLS}/net-setup.sh"
NAT_CONF="${NET_TOOLS}/nat.conf"
UNIT="native-sim-zeth.service"
OWNER="$(id -un)"

usage() {
  cat <<'USAGE'
Usage: run-native-sim-network.sh {start|stop|status|help}

  start   Bring up zeth + DHCP + NAT
  stop    Tear them down
  status  Show zeth / dnsmasq / ip_forward (no root)

Preferred (after one-time install — no sudo):
  sudo ./scripts/setup-native-sim-tap.sh install
  ./scripts/run-native-sim-network.sh start

Legacy fallback (sudo every time):
  uses tools/net-tools/net-setup.sh --config nat.conf with user-owned TAP

After start, run the simulator (no sudo):
  ./scripts/test-mender-native-sim.sh --run-only
  # or: ./build-native_sim/zephyr/zephyr.exe
  # or: ./build-native_sim-improv/zephyr/zephyr.exe
USAGE
}

have_unit() {
  systemctl cat "${UNIT}" &>/dev/null
}

cmd_status() {
  echo "=== native_sim network status ==="
  if have_unit; then
    echo "backend: systemd (${UNIT})"
    systemctl is-enabled "${UNIT}" 2>/dev/null || true
    systemctl is-active "${UNIT}" 2>/dev/null || true
  else
    echo "backend: legacy net-tools (unit not installed — see setup-native-sim-tap.sh)"
  fi
  if ip link show zeth &>/dev/null; then
    ip -br link show zeth
    ip -4 -br addr show zeth 2>/dev/null || true
  else
    echo "zeth: not present (run: $0 start)"
  fi
  if [[ -f /run/dnsmasq_zeth.pid ]] && kill -0 "$(cat /run/dnsmasq_zeth.pid)" 2>/dev/null; then
    echo "dnsmasq: running (pid $(cat /run/dnsmasq_zeth.pid))"
  elif pgrep -a dnsmasq 2>/dev/null | grep -q zeth; then
    echo "dnsmasq: running"
    pgrep -a dnsmasq | grep zeth || true
  else
    echo "dnsmasq: not running"
  fi
  if [[ -r /proc/sys/net/ipv4/ip_forward ]]; then
    echo "net.ipv4.ip_forward=$(cat /proc/sys/net/ipv4/ip_forward)"
  fi
}

start_systemd() {
  echo "Starting ${UNIT} (no sudo; polkit)..."
  systemctl start "${UNIT}"
  cmd_status
}

stop_systemd() {
  echo "Stopping ${UNIT} (no sudo; polkit)..."
  systemctl stop "${UNIT}"
  cmd_status
}

check_net_tools() {
  if [[ ! -x "${NET_SETUP}" ]]; then
    echo "error: missing ${NET_SETUP}" >&2
    echo "Clone net-tools: mkdir -p tools && git clone https://github.com/zephyrproject-rtos/net-tools.git tools/net-tools" >&2
    echo "Or install the no-sudo path: sudo ./scripts/setup-native-sim-tap.sh install" >&2
    exit 1
  fi
  if [[ ! -f "${NAT_CONF}" ]]; then
    echo "error: missing ${NAT_CONF}" >&2
    exit 1
  fi
}

start_legacy() {
  check_net_tools
  echo "Starting TAP + DHCP + NAT via net-tools (sudo required)..."
  echo "Tip: sudo ./scripts/setup-native-sim-tap.sh install  # then no sudo day-to-day"
  # Pass user/group so zephyr.exe can TUNSETIFF without CAP_NET_ADMIN.
  exec sudo bash -c "cd '${NET_TOOLS}' && ./net-setup.sh start --config nat.conf user '${OWNER}' group '${OWNER}'"
}

stop_legacy() {
  check_net_tools
  echo "Stopping net-setup (sudo required)..."
  exec sudo bash -c "cd '${NET_TOOLS}' && ./net-setup.sh stop --config nat.conf"
}

cmd="${1:-help}"
case "${cmd}" in
  start)
    if have_unit; then
      start_systemd
    else
      start_legacy
    fi
    ;;
  stop)
    if have_unit; then
      stop_systemd
    else
      stop_legacy
    fi
    ;;
  status)
    cmd_status
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "error: unknown command: ${cmd}" >&2
    usage >&2
    exit 1
    ;;
esac
