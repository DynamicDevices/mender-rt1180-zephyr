# Borg restore probe (Mender)

## Coverage

- Vorta profile **Default** sources: `/data_drive` and `/home/ajlennon`
- Repo: `hetzner-storagebox:backups/framework-repo`
- Excludes include `**/.git/` and `**/build/` (among others)

## Probe result (2026-07-22)

- Latest Framework archive found via `borg list --last`: `Framework-2026-06-08-020000`
- That archive **does not contain** `data_drive/dd/mender` (mender tree was not present / not yet created in that snapshot).
- Sample extract of `data_drive/dd/backups/recovery_and_backup_guide.md` from that archive **succeeded**; restore tree was moved to Trash with `gio trash`.
- Implication: Borg is a valid off-device safety net for `/data_drive` contents that existed at archive time, but **GitHub checkpoints are required** for current Mender work. Also investigate why Framework archives appear to stop at 2026-06-08 while Vorta is still scheduled daily.

## Policy reminder

- Do not commit secrets from `backups/borg_env.sh`.
- Primary durability for this repo: push feature-branch checkpoints to GitHub.
