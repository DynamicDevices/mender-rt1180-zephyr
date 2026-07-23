# Lane channel (this workspace)

**Stable identity:** `LANE_ID` is the single line in repo-root [`LANE.id`](../../LANE.id)
(committed). **Do not** derive it from the folder name — Alex renames workspaces.

**Channel map SoT:** MemPalace `LANE_DIRECTORY` (`STATUS: canonical`, wing `esl`,
room `lanes`). This file is a mirror.

## How an agent finds the channel

1. Read `LANE_ID` from `./LANE.id` (trim whitespace). If missing, stop and add
   one — do **not** fall back to `basename($PWD)` except as a one-shot proposal
   for Alex to confirm into `LANE.id`.
2. Exact-search MemPalace: `LANE_DIRECTORY` + `STATUS: canonical`  
   Wing: **`esl`** · Room: **`lanes`** (fallback `handoffs`)
3. Find the `### <LANE_ID>` section. Use `channels:` as peer `TO_LANE` values.
4. If `path:` in the directory ≠ this workspace root, **refresh path** in
   MemPalace (or REQUEST the directory owner) — keep `LANE_ID` unchanged.
5. File tickets in wing **`esl`** room **`handoffs`** with those lane IDs.

Skill: `parallel-agent-lanes` · Alex snapshot: `/esl-status`.

## This lane

| Field | Value |
|-------|--------|
| `LANE_ID` / `FROM_LANE` | `zephyr-rt1170-eink` (from `LANE.id`) |
| Path (mutable) | whatever the checkout is right now |
| Repo | [DynamicDevices/zephyr-rt1170-eink](https://github.com/DynamicDevices/zephyr-rt1170-eink) |
| Channels (peers) | `cloud-eink` |
| Owns | RT1170 Zephyr e-ink firmware, native_sim, device contract |
| Never | etablone-cloud / cloud-eink tree (wrangler / D1 / UI) |

## Peer

| `LANE_ID` | Must have | Notes |
|-----------|-----------|--------|
| `cloud-eink` | `LANE.id` in their repo root | Path may be `…/etablone-cloud` or renamed; ID stays `cloud-eink` |
