#!/usr/bin/env bash
# One-time (or rare) install of a persistent user-owned zeth TAP so native_sim
# can attach without CAP_NET_ADMIN / sudo, and start/stop the host DHCP+NAT
# service without typing a password (polkit).
#
# Usage:
#   sudo ./scripts/setup-native-sim-tap.sh install   # once per machine/user
#   ./scripts/setup-native-sim-tap.sh status
#   sudo ./scripts/setup-native-sim-tap.sh uninstall
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT}/scripts/native-sim-tap"
LIBEXEC="/usr/local/libexec/native-sim-zeth"
ETC="/etc/native-sim-zeth"
UNIT="/etc/systemd/system/native-sim-zeth.service"
NM_CONF="/etc/NetworkManager/conf.d/99-zeth-unmanaged.conf"
POLKIT="/etc/polkit-1/rules.d/49-native-sim-zeth.rules"

OWNER="${ZETH_USER:-$(logname 2>/dev/null || echo "${SUDO_USER:-$USER}")}"
GROUP="${ZETH_GROUP:-$(id -gn "${OWNER}" 2>/dev/null || echo "${OWNER}")}"

usage() {
  cat <<'USAGE'
Usage: setup-native-sim-tap.sh {install|uninstall|status|help}

  install     One-time root setup: user-owned zeth TAP + DHCP/NAT systemd
              service + NetworkManager unmanaged + polkit (no-sudo start/stop)
  uninstall   Remove the unit, helpers, NM/polkit snippets
  status      Show whether the install is present and zeth is up (no root)

After install:
  systemctl start native-sim-zeth     # no sudo (polkit)
  ./build-native_sim/zephyr/zephyr.exe   # attaches to zeth without sudo
  ./scripts/run-native-sim-network.sh status
USAGE
}

need_root() {
  if [[ "$(id -u)" -ne 0 ]]; then
    echo "error: '$1' requires root — re-run as: sudo $0 $1" >&2
    exit 1
  fi
}

cmd_status() {
  echo "=== native_sim TAP install status ==="
  echo "owner: ${OWNER}:${GROUP}"
  if [[ -f "${UNIT}" ]]; then
    echo "unit:  installed (${UNIT})"
    systemctl is-enabled native-sim-zeth.service 2>/dev/null || true
    systemctl is-active native-sim-zeth.service 2>/dev/null || true
  else
    echo "unit:  NOT installed (run: sudo $0 install)"
  fi
  if [[ -f "${NM_CONF}" ]]; then
    echo "nm:    unmanaged snippet present"
  else
    echo "nm:    unmanaged snippet missing"
  fi
  if [[ -f "${POLKIT}" ]]; then
    echo "polkit: start/stop without sudo enabled for ${OWNER}"
  else
    echo "polkit: missing"
  fi
  if ip link show zeth &>/dev/null; then
    ip -br link show zeth
    ip -4 -br addr show zeth 2>/dev/null || true
  else
    echo "zeth:  not present"
  fi
  if [[ -f /run/dnsmasq_zeth.pid ]] && kill -0 "$(cat /run/dnsmasq_zeth.pid)" 2>/dev/null; then
    echo "dnsmasq: running (pid $(cat /run/dnsmasq_zeth.pid))"
  else
    echo "dnsmasq: not running"
  fi
}

cmd_install() {
  need_root install
  if [[ ! -d "${SRC}" ]]; then
    echo "error: missing ${SRC}" >&2
    exit 1
  fi
  if ! id "${OWNER}" &>/dev/null; then
    echo "error: user '${OWNER}' does not exist" >&2
    exit 1
  fi
  if ! command -v dnsmasq >/dev/null; then
    echo "error: dnsmasq not installed (sudo apt install dnsmasq-base)" >&2
    exit 1
  fi

  install -d -m 0755 "${LIBEXEC}" "${ETC}"
  install -m 0755 "${SRC}/zeth-start.sh" "${LIBEXEC}/zeth-start.sh"
  install -m 0755 "${SRC}/zeth-stop.sh" "${LIBEXEC}/zeth-stop.sh"
  install -m 0644 "${SRC}/dnsmasq.conf" "${ETC}/dnsmasq.conf"

  sed -e "s/__ZETH_USER__/${OWNER}/g" -e "s/__ZETH_GROUP__/${GROUP}/g" \
    "${SRC}/native-sim-zeth.service" >"${UNIT}"
  chmod 0644 "${UNIT}"

  install -m 0644 "${SRC}/99-zeth-unmanaged.conf" "${NM_CONF}"
  sed -e "s/__ZETH_USER__/${OWNER}/g" "${SRC}/49-native-sim-zeth.rules" >"${POLKIT}"
  chmod 0644 "${POLKIT}"

  if systemctl is-active NetworkManager >/dev/null 2>&1; then
    systemctl reload NetworkManager || true
  fi

  systemctl daemon-reload
  systemctl enable --now native-sim-zeth.service

  echo
  echo "Installed. Day-to-day (no sudo):"
  echo "  systemctl start|stop|restart|status native-sim-zeth"
  echo "  ./build-native_sim-improv/zephyr/zephyr.exe   # or build-native_sim"
  echo
  cmd_status
}

cmd_uninstall() {
  need_root uninstall
  if systemctl list-unit-files native-sim-zeth.service &>/dev/null; then
    systemctl disable --now native-sim-zeth.service 2>/dev/null || true
  fi
  if [[ -x "${LIBEXEC}/zeth-stop.sh" ]]; then
    ZETH_USER="${OWNER}" ZETH_GROUP="${GROUP}" "${LIBEXEC}/zeth-stop.sh" || true
  elif ip link show zeth &>/dev/null; then
    ip link set zeth down || true
    ip tuntap del dev zeth mode tap || true
  fi
  rm -f "${UNIT}" "${NM_CONF}" "${POLKIT}"
  rm -rf "${LIBEXEC}" "${ETC}"
  systemctl daemon-reload
  if systemctl is-active NetworkManager >/dev/null 2>&1; then
    systemctl reload NetworkManager || true
  fi
  echo "Uninstalled native-sim-zeth."
}

case "${1:-help}" in
  install) cmd_install ;;
  uninstall) cmd_uninstall ;;
  status) cmd_status ;;
  help|-h|--help) usage ;;
  *)
    echo "error: unknown command: ${1}" >&2
    usage >&2
    exit 1
    ;;
esac
