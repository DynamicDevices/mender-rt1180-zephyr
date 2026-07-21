/*
 * Custom-board SNVS / rail-gate contract (software side).
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_POWER_H
#define EINK_POWER_H

#include <stdint.h>
#include <stdbool.h>

enum eink_wake_reason {
	EINK_WAKE_UNKNOWN = 0,
	EINK_WAKE_SRTC,
	EINK_WAKE_SERVICE,
	EINK_WAKE_IW612_HOST,
	EINK_WAKE_COLD,
};

struct eink_power_status {
	enum eink_wake_reason wake_reason;
	bool cm4_held_in_reset;
	bool iw612_powered;
	bool panel_powered;
	bool data_nor_powered;
	int64_t next_wake_unix;
};

/** Initialise power helpers; asserts CM4 remains held in reset. */
int eink_power_init(void);

/** Snapshot current power / wake status. */
void eink_power_get_status(struct eink_power_status *out);

/** Persist next SRTC wake time (unix seconds). */
int eink_power_set_next_wake(int64_t unix_sec);

/**
 * Enter true SNVS / main-rail-off sleep. On platforms without board support
 * this logs the request and returns -ENOTSUP (native_sim / EVK lab).
 */
int eink_power_enter_snvs(void);

/** Hard-gate or power IW612 (battery mode). */
int eink_power_iw612_set(bool on);

/** Arm WoWLAN keep-alive rail + host-wake (separately budgeted). */
int eink_power_iw612_arm_wowlan(void);

/** Panel / data-NOR rail helpers. */
int eink_power_panel_set(bool on);
int eink_power_data_nor_set(bool on);

/** Verify CM4 is still held in reset (production gate). */
int eink_power_assert_cm4_held(void);

#endif
