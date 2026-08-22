/*
 * Custom-board SNVS / rail-gate contract (software side).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Battery duty-cycle state machine (field product):
 *
 *   cold/SRTC wake
 *     -> offline paint (optional fire-and-forget display)
 *     -> eink_power_iw612_set(true)   // WiFi rail up only if radio needed
 *     -> network sync + eink_http_flush_deferred()
 *     -> eink_power_iw612_set(false)  // hard-gate WiFi before sleep
 *     -> program SRTC next_wake
 *     -> eink_power_enter_snvs()      // main rail off; panel may stay up
 *
 * Board overlays may provide GPIOs under /zephyr,user:
 *   iw612-enable-gpios = <&gpioX N flags>;
 *   panel-enable-gpios = <&gpioY M flags>;
 * Without those properties the API still enforces the software contract
 * (status bits + logs) so callers stay identical on EVK / native_sim.
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
	bool iw612_wowlan;
	bool panel_powered;
	bool data_nor_powered;
	bool iw612_gpio_bound;
	bool panel_gpio_bound;
	int64_t next_wake_unix;
};

/** Initialise power helpers; asserts CM4 remains held in reset. */
int eink_power_init(void);

/** Snapshot current power / wake status. */
void eink_power_get_status(struct eink_power_status *out);

/** Persist next SRTC wake time (unix seconds). */
int eink_power_set_next_wake(int64_t unix_sec);

/**
 * Enter true SNVS / main-rail-off sleep. Always hard-gates IW612 first.
 * If a fire-and-forget panel refresh is still in flight, leaves panel and
 * data-NOR rails powered so the waveform can finish. On platforms without
 * board support this logs the request and returns -ENOTSUP (EVK) or the
 * native_sim stub.
 */
int eink_power_enter_snvs(void);

/**
 * Lab/product SNVS entry with an explicit delay.
 * @param delay_sec  BBNSM RTC alarm in seconds from now (clamped).
 * @param pmic_off   true: BBNSM DP_EN+TOSP (PMIC_ON_REQ drop). FRDM may hang.
 * Returns -ENOTSUP on SoCs without BBNSM; -EAGAIN if rails did not drop
 * (alarm WFI returned — RTC path worked, PMIC did not).
 */
int eink_power_enter_snvs_in(uint32_t delay_sec, bool pmic_off);

/**
 * Lab: forever BOM (TOSP) + RTC hold for current measurement.
 * Does not return when rails drop (POR is the loop). If WFI returns
 * because main 3V3 stayed up, retries after PRE_SEC.
 */
#if defined(CONFIG_APP_EINK_BOM_POWER_LOOP)
void eink_power_bom_power_loop(void);
#endif

/**
 * Hard-gate or power IW612 WiFi (battery mode).
 * Call true only around network work; false immediately after
 * eink_http_flush_deferred() — never leave the radio up into SNVS.
 */
int eink_power_iw612_set(bool on);

/** Arm WoWLAN keep-alive rail + host-wake (separately budgeted). */
int eink_power_iw612_arm_wowlan(void);

/** Panel / data-NOR rail helpers. */
int eink_power_panel_set(bool on);
int eink_power_data_nor_set(bool on);

/** Verify CM4 is still held in reset (production gate). */
int eink_power_assert_cm4_held(void);

#endif
