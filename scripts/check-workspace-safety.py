#!/usr/bin/env python3
"""Enforce single-writer / loss-resistant workspace policy for this Mender checkout.

Modes:
  (default)   Local session checks: canonical path, no linked worktrees,
              no shared West/build symlinks, ignore-policy sentinels,
              not on a protected branch.
  --handoff   Also require a clean tree (no untracked/ignored *source*),
              and HEAD pushed to origin.
  --ci        CI-safe subset: ignore-policy + tracked-source sentinels only
              (no absolute-path / worktree / push checks).
  --diag      Print diagnostics and exit 0 (never fails).

Exit 0 on success, 1 on policy failure.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

CANONICAL_ROOT = Path("/data_drive/dd/mender")
PROTECTED = {"main", "master"}
SOURCE_SUFFIXES = {".c", ".h", ".py", ".sh", ".yml", ".yaml", ".cmake", ".overlay", ".conf", ".md", ".txt", ".json"}
SOURCE_NAMES = {"CMakeLists.txt", "Kconfig", "module.yml", "prj.conf", "west.yml"}
WEST_NAMES = (".west", "zephyr", "modules", "bootloader")


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=False)


def fail(errors: list[str], msg: str) -> None:
    errors.append(msg)


def is_source_path(rel: str) -> bool:
    name = Path(rel).name
    if name in SOURCE_NAMES:
        return True
    return Path(rel).suffix in SOURCE_SUFFIXES


def diag(root: Path) -> None:
    real = root.resolve()
    print("=== workspace safety diagnostics ===")
    print(f"cwd:            {Path.cwd()}")
    print(f"script root:    {root}")
    print(f"realpath:       {real}")
    print(f"canonical:      {CANONICAL_ROOT}")
    print(f"match:          {real == CANONICAL_ROOT.resolve()}")
    wt = run(["git", "worktree", "list", "-v"], cwd=root)
    print("worktrees:")
    print(wt.stdout.rstrip() or "  (none)")
    br = run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=root)
    print(f"branch:         {br.stdout.strip()}")
    upstream = run(["git", "rev-parse", "--abbrev-ref", "@{u}"], cwd=root)
    print(f"upstream:       {upstream.stdout.strip() or '(none)'}")
    status = run(["git", "status", "-sb"], cwd=root)
    print(f"status:         {status.stdout.splitlines()[0] if status.stdout else ''}")
    print("policy:         single-writer; GitHub checkpoints; no linked worktrees by default")


def check_canonical(root: Path, errors: list[str]) -> None:
    real = root.resolve()
    if real != CANONICAL_ROOT.resolve():
        fail(
            errors,
            f"realpath {real} is not canonical {CANONICAL_ROOT} "
            "(open /data_drive/dd/mender directly; do not use symlink aliases)",
        )


def check_worktrees(root: Path, errors: list[str]) -> None:
    if (root / ".git").is_file():
        fail(errors, "this checkout is a linked worktree (.git is a file); use the primary tree")
    wt = run(["git", "worktree", "list", "--porcelain"], cwd=root)
    if wt.returncode != 0:
        fail(errors, f"git worktree list failed: {wt.stderr.strip()}")
        return
    paths = []
    for line in wt.stdout.splitlines():
        if line.startswith("worktree "):
            paths.append(line.split(" ", 1)[1])
    if len(paths) > 1:
        fail(errors, f"extra git worktrees present: {paths}")
    elif len(paths) == 1 and Path(paths[0]).resolve() != root.resolve():
        fail(errors, f"worktree path mismatch: {paths[0]} vs {root}")


def check_no_shared_symlinks(root: Path, errors: list[str]) -> None:
    for name in WEST_NAMES:
        p = root / name
        if p.is_symlink():
            target = os.readlink(p)
            fail(errors, f"{name}/ is a symlink ({target}); West deps must be per-checkout")
    for p in sorted(root.glob("build*")):
        if p.is_symlink():
            target = os.readlink(p)
            fail(errors, f"{p.name} is a symlink ({target}); build dirs must be local to this checkout")


def check_ignore_policy(root: Path, errors: list[str]) -> None:
    """Root /modules/ must be ignored; app modules must remain trackable."""
    # Sentinel: create ephemeral paths conceptually via git check-ignore on known paths.
    west_mod = root / "modules"
    if west_mod.exists():
        ci = run(["git", "check-ignore", "-v", "modules"], cwd=root)
        if ci.returncode != 0:
            fail(errors, "root modules/ must be gitignored (West dependency)")
    # Even if west modules dir is absent, the rule must ignore /modules/foo
    # Use a dry path through check-ignore with stdin? git check-ignore needs the path to exist
    # for some versions; use `git check-ignore -v --stdin` with nonexistent? Better:
    # verify .gitignore content has anchored rules.
    gi = (root / ".gitignore").read_text()
    for needle in ("/modules/", "/zephyr/", "/bootloader/", "/.west/"):
        if needle not in gi:
            fail(errors, f".gitignore missing anchored rule {needle}")
    # Unanchored `modules/` (without leading slash on its own line) is the historical footgun.
    for line in gi.splitlines():
        stripped = line.strip()
        if stripped in {"modules/", "modules", "zephyr/", "zephyr", "bootloader/", "bootloader"}:
            fail(errors, f".gitignore has unanchored rule {stripped!r}; use /{stripped.rstrip('/')}/")

    app_probe = "mender-mcu-integration/modules/eink-el133/CMakeLists.txt"
    if (root / app_probe).exists():
        ci = run(["git", "check-ignore", "-v", app_probe], cwd=root)
        if ci.returncode == 0:
            fail(errors, f"app module is incorrectly ignored: {ci.stdout.strip()}")


def check_ignored_source(root: Path, errors: list[str]) -> None:
    """Fail if any source-like path under app/scripts is currently ignored."""
    for base in ("mender-mcu-integration", "scripts"):
        base_path = root / base
        if not base_path.exists():
            continue
        for path in base_path.rglob("*"):
            if not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            # Skip known secret/local patterns and nested backups
            if rel.endswith("-local.conf") or "/.git" in rel or rel.endswith(".pyc"):
                continue
            if not is_source_path(rel):
                continue
            ci = run(["git", "check-ignore", "-v", rel], cwd=root)
            if ci.returncode == 0:
                fail(errors, f"tracked-area source is ignored: {ci.stdout.strip()}")


def check_branch(root: Path, errors: list[str]) -> None:
    br = run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=root)
    name = br.stdout.strip()
    if name in PROTECTED:
        fail(errors, f"write work must not be on protected branch {name}")


def check_handoff(root: Path, errors: list[str]) -> None:
    porcelain = run(["git", "status", "--porcelain=v1", "--untracked-files=all"], cwd=root)
    for line in porcelain.stdout.splitlines():
        if not line:
            continue
        code = line[:2]
        path = line[3:]
        if path.endswith("-local.conf") or path.endswith("secrets.conf"):
            continue
        # Any remaining dirty/untracked path blocks handoff.
        fail(errors, f"handoff requires clean tree: {code} {path}")

    ignored = run(
        ["git", "status", "--porcelain=v1", "--ignored", "--untracked-files=all"],
        cwd=root,
    )
    for line in ignored.stdout.splitlines():
        if not line.startswith("!! "):
            continue
        path = line[3:]
        if not (path.startswith("mender-mcu-integration/") or path.startswith("scripts/")):
            continue
        if path.endswith("-local.conf") or path.endswith("secrets.conf") or "/.git" in path:
            continue
        if is_source_path(path):
            fail(errors, f"handoff: source file is ignored (would be lost): {path}")

    upstream = run(["git", "rev-parse", "--abbrev-ref", "@{u}"], cwd=root)
    if upstream.returncode != 0 or not upstream.stdout.strip():
        fail(errors, "handoff requires an upstream tracking branch (git push -u)")
        return
    syn = run(["git", "status", "-sb"], cwd=root)
    first = syn.stdout.splitlines()[0] if syn.stdout else ""
    if "ahead" in first or "behind" in first:
        fail(errors, f"handoff requires HEAD in sync with upstream: {first}")
    head = run(["git", "rev-parse", "HEAD"], cwd=root).stdout.strip()
    remote = run(["git", "rev-parse", "@{u}"], cwd=root).stdout.strip()
    if head and remote and head != remote:
        fail(errors, f"HEAD {head[:12]} != upstream {remote[:12]}")


def check_tracked_el133(root: Path, errors: list[str]) -> None:
    """CI sentinel: the recovered driver must remain tracked."""
    path = "mender-mcu-integration/modules/eink-el133/drivers/display/display_el133uf1.c"
    ls = run(["git", "ls-files", "--error-unmatch", path], cwd=root)
    if ls.returncode != 0:
        fail(errors, f"required tracked file missing from index: {path}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ci", action="store_true", help="CI subset of checks")
    ap.add_argument("--handoff", action="store_true", help="strict handoff/completion checks")
    ap.add_argument("--diag", action="store_true", help="print diagnostics only")
    ap.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: parent of scripts/)",
    )
    args = ap.parse_args()
    root = args.root.resolve()
    os.chdir(root)

    if args.diag:
        diag(root)
        return 0

    errors: list[str] = []

    if args.ci:
        check_ignore_policy(root, errors)
        check_ignored_source(root, errors)
        check_tracked_el133(root, errors)
    else:
        check_canonical(root, errors)
        check_worktrees(root, errors)
        check_no_shared_symlinks(root, errors)
        check_ignore_policy(root, errors)
        check_ignored_source(root, errors)
        check_branch(root, errors)
        check_tracked_el133(root, errors)
        if args.handoff:
            check_handoff(root, errors)

    diag(root)
    print()
    if errors:
        print("FAIL: workspace safety policy violations:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("OK: workspace safety checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
