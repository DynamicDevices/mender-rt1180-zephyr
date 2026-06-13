#!/usr/bin/env bash
# Start/stop Zephyr native_sim TAP + DHCP + NAT (net-tools nat.conf).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NET_TOOLS="${ROOT}/tools/net-tools"
NET_SETUP="${NET_TOOLS}/net-setup.sh"
NAT_CONF="${NET_TOOLS}/nat.conf"

usage() {
  cat <<EOF
Usage: $(basename "$0") {start|stop|status|help}

  start   sudo net-setup start --config nat.conf (TAP + DHCP + NAT)
  stop    sudo net-setup stop --config nat.conf (cleans dnsmasq + NAT)
  status  Check zeth, dnsmasq, ip_forward (no root)

Prerequisites:
  git clone https://github.com/zephyrproject-rtos/net-tools.git tools/net-tools

After start, run the simulator in another terminal:
  ./scripts/test-mender-native-sim.sh --run-only
  # or: ./build-native_sim/zephyr/zephyr.exe
EOF
}

check_net_tools() {
  if [[ ! -x "${NET_SETUP}" ]]; then
    echo "error: missing ${NET_SETUP}" >&2
    echo "Clone net-tools: mkdir -p tools && git clone https://github.com/zephyrproject-rtos/net-tools.git tools/net-tools" >&2
    exit 1
  fi
  if [[ ! -f "${NAT_CONF}" ]]; then
    echo "error: missing ${NAT_CONF}" >&2
    exit 1
  fi
}

cmd_status() {
  echo "=== native_sim network status ==="
  if ip link show zeth &>/dev/null; then
    ip link show zeth
  else
    echo "zeth: not present (run: sudo $0 start)"
  fi
  if pgrep -a dnsmasq >/dev/null 2>&1; then
    echo "dnsmasq: running"
    pgrep -a dnsmasq
  else
    echo "dnsmasq: not running"
  fi
  if [[ -r /proc/sys/net/ipv4/ip_forward ]]; then
    echo "net.ipv4.ip_forward=$(cat /proc/sys/net/ipv4/ip_forward)"
  fi
}

cmd="${1:-help}"
case "${cmd}" in
  start)
    check_net_tools
    echo "Starting TAP + DHCP + NAT (sudo required)..."
    exec sudo bash -c "cd '${NET_TOOLS}' && ./net-setup.sh start --config nat.conf"
    ;;
  stop)
    check_net_tools
    echo "Stopping net-setup (sudo required)..."
    exec sudo bash -c "cd '${NET_TOOLS}' && ./net-setup.sh stop --config nat.conf"
    ;;
  status)
    cmd_status
    ;;
  help | -h | --help)
    usage
    ;;
  *)
    echo "error: unknown command: ${cmd}" >&2
    usage >&2
    exit 1
    ;;
esac
