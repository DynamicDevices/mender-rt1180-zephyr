# Integration contract — e-ink (`zephyr-rt1170-eink`)

**Lane:** `zephyr-rt1170-eink` · **Wing:** `esl` · **Primary:** `/data_drive/dd/zephyr-rt1170-eink`

Parallel agents must not share this checkout. Rule: workspace
`multi-agent-worktrees` · helper: `ESL_ROOT=/data_drive/dd esl-worktree`.

## Worktree layout

| Repo | Lane | Directory | Branch | Notes |
|------|------|-----------|--------|-------|
| `zephyr-rt1170-eink` | *(primary)* | `/data_drive/dd/zephyr-rt1170-eink` | `main` | Clean primary; west topdir |
| — | — | — | — | Add rows when a lane worktree is live |

When adding a lane:

```bash
ESL_ROOT=/data_drive/dd esl-worktree add zephyr-rt1170-eink <lane> <branch>
# then: update this table + MemPalace wing=esl room=decisions
```

Remove finished lanes with `esl-worktree rm` and drop the row here.

## Shared surfaces (do not fork locally)

| Surface | SoT |
|---------|-----|
| Mender MCU client pin | [`DD_PIN` / `PIN-POLICY`](https://github.com/DynamicDevices/mender-mcu/blob/feature/zephyr-ram-stage-on-main/PIN-POLICY.md) |
| Pin / Zephyr gate | `./scripts/check-west-pins.sh` |
| Lab / matrix helpers | `dd-zephyr-lab` / `dd-zephyr-matrix` |
| Hosted Mender / RT118x notes | [PROJECT-NOTES.md](../mender-mcu-integration/PROJECT-NOTES.md) |
| Lane channel | [LANE-CHANNEL.md](../mender-mcu-integration/docs/LANE-CHANNEL.md) |

## Peers

| Lane / path | Role |
|-------------|------|
| `cloud-eink` | Cloud peer (same wing `esl`) |
| `zephyr-rt1170-room-display` / `zephyr-rt1186-f1` | Sibling Zephyr products (shared `mender-mcu` pin) |
