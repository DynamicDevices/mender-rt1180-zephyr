# Lane channel (this workspace)

**Stable identity**
- [`LANE.id`](../../LANE.id) → `LANE_ID` (ticket address)
- [`PROJECT.wing`](../../PROJECT.wing) → MemPalace wing (customer/product cluster)

Do **not** derive either from the folder name — Alex renames workspaces.

**Channel map SoT:** MemPalace `LANE_DIRECTORY` (`STATUS: canonical`, wing =
`PROJECT.wing`, room `lanes`). This file is a mirror.

## How an agent finds the channel

1. Read `LANE_ID` from `./LANE.id` and wing from `./PROJECT.wing` (trim).
2. Exact-search MemPalace: `LANE_DIRECTORY` + `STATUS: canonical`  
   Wing: **contents of `PROJECT.wing`** · Room: **`lanes`** (fallback `handoffs`)
3. Find the `### <LANE_ID>` section. Use `channels:` as peer `TO_LANE` values.
4. If `path:` ≠ this workspace root, refresh path in MemPalace — keep `LANE_ID`.
5. File tickets in that wing’s room **`handoffs`**.

Skills: `parallel-agent-lanes`, `zephyr-imx-rt-project`.  
Alex snapshot: `/esl-status` (wing `esl`). New customers: copy `/project-status`.

## This lane

| Field | Value |
|-------|--------|
| `LANE_ID` | `zephyr-rt1170-eink` |
| `PROJECT.wing` | `esl` |
| Path (mutable) | current checkout |
| Repo | [DynamicDevices/zephyr-rt1170-eink](https://github.com/DynamicDevices/zephyr-rt1170-eink) |
| Channels | `cloud-eink` |
| Owns | RT1170 Zephyr e-ink firmware, native_sim, device contract |
| Never | cloud-eink / etablone-cloud tree |

## Peer

| `LANE_ID` | Must have |
|-----------|-----------|
| `cloud-eink` | `LANE.id` + same `PROJECT.wing` (`esl`) |
