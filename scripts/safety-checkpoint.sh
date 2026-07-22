#!/usr/bin/env bash
# Checkpoint current work to GitHub on a non-protected feature branch.
# Never amends, never force-pushes, never git add -A.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <commit-message>" >&2
  exit 2
fi
MSG="$1"

PROTECTED_BRANCHES='^(main|master)$'
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [[ "$BRANCH" =~ $PROTECTED_BRANCHES ]]; then
  echo "error: refusing checkpoint on protected branch '$BRANCH'" >&2
  exit 1
fi

python3 scripts/check-workspace-safety.py

# Explicitly stage only approved source roots + already-tracked modifications.
# Do not use git add -A.
mapfile -t DIRTY < <(git status --porcelain=v1 --untracked-files=all | awk '{print substr($0,4)}')

stage_path() {
  local p="$1"
  case "$p" in
    *-local.conf|secrets.conf|*.mender|*.bin|*.elf|*.hex) return 0 ;;
  esac
  if [[ "$p" == mender-mcu-integration/* || "$p" == scripts/* || "$p" == .gitignore || "$p" == .cursor/* || "$p" == .github/* ]]; then
    git add -- "$p"
  fi
}

for p in "${DIRTY[@]:-}"; do
  [[ -z "$p" ]] && continue
  # Skip ignored West/build trees if they somehow appear
  case "$p" in
    zephyr/*|modules/*|bootloader/*|.west/*|build/*|build-*/*|.tools/*|tools/net-tools/*) continue ;;
  esac
  stage_path "$p"
done

if git diff --cached --quiet; then
  echo "nothing to checkpoint (index empty after policy filter)"
  # Still ensure branch is pushed if we have commits ahead
  if git rev-parse --abbrev-ref '@{u}' >/dev/null 2>&1; then
    git status -sb | head -1
  else
    git push -u origin HEAD
  fi
  exit 0
fi

# Refuse secrets in the index
if git diff --cached --name-only | grep -E '(^|/)([^/]*-local\.conf|secrets\.conf)$' >/dev/null; then
  echo "error: refusing to checkpoint local/secret config files" >&2
  git diff --cached --name-only
  exit 1
fi

git commit -m "checkpoint: ${MSG}"

git push -u origin HEAD

python3 scripts/check-workspace-safety.py --handoff
echo "OK: checkpoint pushed on ${BRANCH}"
