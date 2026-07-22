/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Software contract for SNVS duty-cycle. Board-specific rail GPIOs land here
 * once the custom schematic is frozen; until then helpers keep the software
 * gate enforceable on every wake (and drive DT GPIOs when present).
 */
#include "eink_power.h"

#include "eink_display.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_GPIO)
#include <zephyr/drivers/gpio.h>
#endif

LOG_MODULE_REGISTER(eink_power, LOG_LEVEL_INF);

static struct eink_power_status g_status = {
	.wake_reason = EINK_WAKE_COLD,
	.cm4_held_in_reset = true,
};

#if defined(CONFIG_GPIO) && defined(CONFIG_APP_EINK_IW612_GPIO) && \
	DT_NODE_EXISTS(DT_PATH(zephyr_user)) && \
	DT_NODE_HAS_PROP(DT_PATH(zephyr_user), iw612_enable_gpios)
static const struct gpio_dt_spec iw612_en =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), iw612_enable_gpios);
#define EINK_HAS_IW612_GPIO 1
#endif

#if defined(CONFIG_GPIO) && defined(CONFIG_APP_EINK_PANEL_GPIO) && \
	DT_NODE_EXISTS(DT_PATH(zephyr_user)) && \
	DT_NODE_HAS_PROP(DT_PATH(zephyr_user), panel_enable_gpios)
static const struct gpio_dt_spec panel_en =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), panel_enable_gpios);
#define EINK_HAS_PANEL_GPIO 1
#endif

#if defined(EINK_HAS_IW612_GPIO) || defined(EINK_HAS_PANEL_GPIO)
static int gpio_configure_output(const struct gpio_dt_spec *spec, bool *bound_out)
{
	int ret;

	if (!device_is_ready(spec->port)) {
		LOG_ERR("power: GPIO port %s not ready", spec->port->name);
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("power: gpio configure failed: %d", ret);
		return ret;
	}
	*bound_out = true;
	return 0;
}
#endif

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

	if (ret < 0) {
		return ret;
	}

#if defined(EINK_HAS_IW612_GPIO)
	ret = gpio_configure_output(&iw612_en, &g_status.iw612_gpio_bound);
	if (ret < 0) {
		return ret;
	}
	LOG_INF("power: IW612 enable GPIO bound");
#else
	g_status.iw612_gpio_bound = false;
	LOG_INF("power: IW612 software gate only (no iw612-enable-gpios)");
#endif

#if defined(EINK_HAS_PANEL_GPIO)
	ret = gpio_configure_output(&panel_en, &g_status.panel_gpio_bound);
	if (ret < 0) {
		return ret;
	}
	LOG_INF("power: panel enable GPIO bound");
#else
	g_status.panel_gpio_bound = false;
#endif

	LOG_INF("power: CM4 held in reset; field sleep is SNVS/main-rail-off");
	return 0;
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
#if defined(EINK_HAS_IW612_GPIO)
	if (g_status.iw612_gpio_bound) {
		int ret = gpio_pin_set_dt(&iw612_en, on ? 1 : 0);

		if (ret < 0) {
			LOG_ERR("power: IW612 GPIO set failed: %d", ret);
			return ret;
		}
	}
#endif
	g_status.iw612_powered = on;
	if (!on) {
		g_status.iw612_wowlan = false;
	}
	LOG_INF("power: IW612 %s%s", on ? "ON" : "HARD-GATE",
		g_status.iw612_gpio_bound ? " (gpio)" : " (sw)");
	return 0;
}

int eink_power_iw612_arm_wowlan(void)
{
	int ret = eink_power_iw612_set(true);

	if (ret < 0) {
		return ret;
	}
	g_status.iw612_wowlan = true;
	LOG_INF("power: IW612 WoWLAN armed (separately budgeted mode)");
	return 0;
}

int eink_power_panel_set(bool on)
{
#if defined(EINK_HAS_PANEL_GPIO)
	if (g_status.panel_gpio_bound) {
		int ret = gpio_pin_set_dt(&panel_en, on ? 1 : 0);

		if (ret < 0) {
			LOG_ERR("power: panel GPIO set failed: %d", ret);
			return ret;
		}
	}
#endif
	g_status.panel_powered = on;
	LOG_INF("power: panel rail %s%s", on ? "ON" : "OFF",
		g_status.panel_gpio_bound ? " (gpio)" : " (sw)");
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
	bool refresh_inflight = false;
	int ret = eink_power_assert_cm4_held();

	if (ret < 0) {
		return ret;
	}

	/* Never sleep with WiFi up — network work must flush first. */
	(void)eink_power_iw612_set(false);

#if defined(CONFIG_APP_EINK_DISPLAY_FIRE_AND_FORGET)
	refresh_inflight = eink_display_is_busy();
#endif
	if (refresh_inflight) {
		/*
		 * Panel is still streaming / updating. Leave panel + data-NOR
		 * powered so the waveform can finish after main-rail policy
		 * allows; do not cut storage under an in-flight ES6F stream.
		 */
		LOG_INF("power: panel+data-NOR stay up (display refresh in flight)");
	} else {
		(void)eink_power_panel_set(false);
		(void)eink_power_data_nor_set(false);
	}

	LOG_INF("power: requesting SNVS / main-rail-off (DCDC_IN+VDD_LPSR_IN gated)");
#if defined(CONFIG_ARCH_POSIX)
	{
		uint32_t hold = 0;

#if defined(CONFIG_APP_EINK_SNVS_SIM_HOLD_SEC)
		hold = CONFIG_APP_EINK_SNVS_SIM_HOLD_SEC;
#endif
		if (hold > 0) {
			uint32_t sleep_s = hold;

			if (g_status.next_wake_unix > 1700000000LL) {
				int64_t rem = g_status.next_wake_unix - (int64_t)time(NULL);

				if (rem > 0 && rem < (int64_t)hold) {
					sleep_s = (uint32_t)rem;
				}
			}
			LOG_INF("power: native_sim SNVS stub holding %u s (next_wake=%lld)",
				sleep_s, (long long)g_status.next_wake_unix);
			k_sleep(K_SECONDS(sleep_s));
		} else {
			LOG_INF("power: native_sim SNVS stub return (next_wake=%lld)",
				(long long)g_status.next_wake_unix);
		}
		return 0;
	}
#else
	/* Board bring-up: wire PMIC_ON_REQ / SNVS entry here. */
	return -ENOTSUP;
#endif
}
