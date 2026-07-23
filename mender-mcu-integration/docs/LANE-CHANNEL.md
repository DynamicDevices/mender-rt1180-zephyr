# Lane channel (this workspace)

**SoT is MemPalace**, not this file. Mirror only — update MemPalace first.

## How an agent finds the channel

1. Your `LANE_ID` = basename of this workspace → **`zephyr-rt1170-eink`**
2. Exact-search MemPalace: `LANE_DIRECTORY` + `STATUS: canonical`  
   Wing: **`esl`** · Room: **`lanes`** (fallback `handoffs`)
3. Read your row’s `channels:` → those are valid `TO_LANE` / peer `FROM_LANE`
4. File tickets in wing **`esl`** room **`handoffs`** with those lane IDs

Skill: `parallel-agent-lanes` (session start + reference § Lane directory).
Alex snapshot: Cursor command `/esl-status` (Collaboration section).

## This lane

| Field | Value |
|-------|--------|
| `LANE_ID` / `FROM_LANE` | `zephyr-rt1170-eink` |
| Path | `/data_drive/dd/zephyr-rt1170-eink` |
| Repo | [DynamicDevices/zephyr-rt1170-eink](https://github.com/DynamicDevices/zephyr-rt1170-eink) |
| Channels (peers) | `cloud-eink` |
| Owns | RT1170 Zephyr e-ink firmware, native_sim, device contract |
| Never | `etablone-cloud/**` (wrangler / D1 / UI) |

## Peer note

`cloud-eink` currently lives at `/data_drive/esl/etablone-cloud` (folder
basename ≠ lane id). Prefer renaming that folder to `cloud-eink`, or keep the
`override:` in `LANE_DIRECTORY` until then.
