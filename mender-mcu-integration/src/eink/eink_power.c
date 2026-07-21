/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Software contract for SNVS duty-cycle. Board-specific rail GPIOs land here
 * once the custom schematic is frozen; until then helpers are stubs that keep
 * CM4 held-in-reset policy enforceable on every wake.
 */
#include "eink_power.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_power, LOG_LEVEL_INF);

static struct eink_power_status g_status = {
	.wake_reason = EINK_WAKE_COLD,
	.cm4_held_in_reset = true,
};

int eink_power_assert_cm4_held(void)
{
#if defined(CONFIG_SECOND_CORE_MCUX)
#error "CONFIG_SECOND_CORE_MCUX must not be enabled on the battery e-ink product"
#endif

#if defined(CONFIG_SOC_MIMXRT1176_CM7) && defined(SRC)
	/* BT_RELEASE_M4 clear means CM4 has not been kicked out of reset. */
	if ((SRC->SCR & SRC_SCR_BT_RELEASE_M4_MASK) != 0U) {
		LOG_ERR("CM4 boot-release asserted — production policy violated");
		g_status.cm4_held_in_reset = false;
		return -EFAULT;
	}
#endif
	g_status.cm4_held_in_reset = true;
	return 0;
}

int eink_power_init(void)
{
	int ret = eink_power_assert_cm4_held();

	if (ret == 0) {
		LOG_INF("power: CM4 held in reset; field sleep is SNVS/main-rail-off");
	}
	return ret;
}

void eink_power_get_status(struct eink_power_status *out)
{
	if (out != NULL) {
		*out = g_status;
	}
}

int eink_power_set_next_wake(int64_t unix_sec)
{
	g_status.next_wake_unix = unix_sec;
	LOG_INF("power: next wake unix=%lld (SRTC program is board-specific)",
		(long long)unix_sec);
	return 0;
}

int eink_power_iw612_set(bool on)
{
	g_status.iw612_powered = on;
	LOG_INF("power: IW612 %s", on ? "ON" : "HARD-GATE");
	return 0;
}

int eink_power_iw612_arm_wowlan(void)
{
	g_status.iw612_powered = true;
	LOG_INF("power: IW612 WoWLAN armed (separately budgeted mode)");
	return 0;
}

int eink_power_panel_set(bool on)
{
	g_status.panel_powered = on;
	LOG_INF("power: panel rail %s", on ? "ON" : "OFF");
	return 0;
}

int eink_power_data_nor_set(bool on)
{
	g_status.data_nor_powered = on;
	LOG_INF("power: data NOR %s", on ? "ON" : "DPD/OFF");
	return 0;
}

int eink_power_enter_snvs(void)
{
	int ret = eink_power_assert_cm4_held();

	if (ret < 0) {
		return ret;
	}

	(void)eink_power_panel_set(false);
	(void)eink_power_data_nor_set(false);
	(void)eink_power_iw612_set(false);

	LOG_INF("power: requesting SNVS / main-rail-off (DCDC_IN+VDD_LPSR_IN gated)");
#if defined(CONFIG_ARCH_POSIX)
	LOG_WRN("power: native_sim cannot enter SNVS — sleeping forever for duty-cycle shape");
	k_sleep(K_FOREVER);
	return 0;
#else
	/* Board bring-up: wire PMIC_ON_REQ / SNVS entry here. */
	return -ENOTSUP;
#endif
}
