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
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_SOC_SERIES_IMXRT118X)
#include <soc.h>
#endif

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

#if defined(CONFIG_SOC_SERIES_IMXRT118X)

static void bbnsm_isr(const void *arg)
{
	ARG_UNUSED(arg);
	/* NXP kBBNSM_RTC_AlarmInterruptFlag is TA(0x2) (0b10). W1C that value. */
	BBNSM->BBNSM_EVENTS = BBNSM_BBNSM_EVENTS_TA(0x2);
}

static bool bbnsm_ta_fired(void)
{
	return (BBNSM->BBNSM_EVENTS & BBNSM_BBNSM_EVENTS_TA_MASK) ==
	       BBNSM_BBNSM_EVENTS_TA(0x2);
}

static void bbnsm_ta_w1c(void)
{
	BBNSM->BBNSM_EVENTS = BBNSM_BBNSM_EVENTS_TA(0x2);
}

static uint32_t bbnsm_rtc_seconds(void)
{
	uint32_t a = 0;
	uint32_t b;

	b = (BBNSM->BBNSM_RTC_MS << 17U) | (BBNSM->BBNSM_RTC_LS >> 15U);
	for (int i = 0; i < 8; i++) {
		a = b;
		b = (BBNSM->BBNSM_RTC_MS << 17U) | (BBNSM->BBNSM_RTC_LS >> 15U);
		if (a == b) {
			return b;
		}
	}
	return b;
}

static void bbnsm_rtc_start(void)
{
	uint32_t ctrl = BBNSM->BBNSM_CTRL;

	ctrl &= ~BBNSM_BBNSM_CTRL_RTC_EN_MASK;
	BBNSM->BBNSM_CTRL = ctrl | BBNSM_BBNSM_CTRL_RTC_EN(0x2);
	for (int i = 0; i < 10000; i++) {
		if ((BBNSM->BBNSM_CTRL & BBNSM_BBNSM_CTRL_RTC_EN(0x2)) != 0U) {
			return;
		}
	}
	LOG_WRN("power: BBNSM RTC_EN did not stick");
}

static int bbnsm_program_alarm(uint32_t delay_sec)
{
	uint32_t now;
	uint32_t alarm;
	uint32_t tmp;

	if (delay_sec < 2U) {
		delay_sec = 2U;
	}
	printk("power: BBNSM program alarm +%u\n", delay_sec);
	bbnsm_rtc_start();
	now = bbnsm_rtc_seconds();
	alarm = now + delay_sec;

	tmp = BBNSM->BBNSM_CTRL & ~BBNSM_BBNSM_CTRL_TA_EN_MASK;
	BBNSM->BBNSM_CTRL = tmp | BBNSM_BBNSM_CTRL_TA_EN(0x1);
	bbnsm_ta_w1c();

	tmp = BBNSM->BBNSM_INT_EN & ~BBNSM_BBNSM_INT_EN_TA_INT_EN_MASK;
	BBNSM->BBNSM_INT_EN = tmp | BBNSM_BBNSM_INT_EN_TA_INT_EN(0x1);
	for (int i = 0; i < 10000; i++) {
		if ((BBNSM->BBNSM_INT_EN & BBNSM_BBNSM_INT_EN_TA_INT_EN(0x1)) != 0U) {
			break;
		}
	}
	BBNSM->BBNSM_TA = alarm;
	tmp = BBNSM->BBNSM_INT_EN & ~BBNSM_BBNSM_INT_EN_TA_INT_EN_MASK;
	BBNSM->BBNSM_INT_EN = tmp | BBNSM_BBNSM_INT_EN_TA_INT_EN(0x2);
	tmp = BBNSM->BBNSM_CTRL & ~BBNSM_BBNSM_CTRL_TA_EN_MASK;
	BBNSM->BBNSM_CTRL = tmp | BBNSM_BBNSM_CTRL_TA_EN(0x2);

	LOG_INF("power: BBNSM RTC now=%u alarm=+%u s (abs=%u)", now, delay_sec, alarm);
	return 0;
}

#define RT118X_NVIC_WORDS 8

static uint32_t irq_popcount(const uint32_t *iser, size_t n)
{
	uint32_t c = 0;

	for (size_t i = 0; i < n; i++) {
		c += (uint32_t)__builtin_popcount(iser[i]);
	}
	return c;
}

/* Park every NVIC IRQ except BBNSM, and stop SysTick. Restore after WFI. */
static void irq_park_for_rtc(uint32_t *iser_save, uint32_t *systick_ctrl)
{
	*systick_ctrl = SysTick->CTRL;
	SysTick->CTRL = 0;
#ifdef SCB_ICSR_STTNS_Msk
	SCB->ICSR = (SCB->ICSR & SCB_ICSR_STTNS_Msk) | SCB_ICSR_PENDSTCLR_Msk;
#else
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
#endif
	for (int i = 0; i < RT118X_NVIC_WORDS; i++) {
		iser_save[i] = NVIC->ISER[i];
		NVIC->ICER[i] = 0xFFFFFFFFU;
	}
	barrier_dsync_fence_full();
	barrier_isync_fence_full();
	irq_enable(BBNSM_IRQn);
}

static void irq_unpark(const uint32_t *iser_save, uint32_t systick_ctrl)
{
	for (int i = 0; i < RT118X_NVIC_WORDS; i++) {
		NVIC->ISER[i] = iser_save[i];
	}
	SysTick->CTRL = systick_ctrl;
	barrier_dsync_fence_full();
	barrier_isync_fence_full();
}

static int rt118x_enter_snvs(uint32_t delay_sec, bool pmic_off)
{
	static bool irq_hooked;
	uint32_t iser_save[RT118X_NVIC_WORDS];
	uint32_t systick_ctrl;
	uint32_t t0;
	uint32_t t1;
	uint32_t n_en;

	if (!irq_hooked) {
		IRQ_CONNECT(BBNSM_IRQn, 0, bbnsm_isr, NULL, 0);
		irq_hooked = true;
	}
	/* Stale TA can storm the ISR the moment NVIC is enabled. Clear, then
	 * enable only after the new alarm is programmed (irq_park does that).
	 */
	bbnsm_ta_w1c();
	NVIC_ClearPendingIRQ(BBNSM_IRQn);

	bbnsm_program_alarm(delay_sec);
	bbnsm_ta_w1c();
	NVIC_ClearPendingIRQ(BBNSM_IRQn);

	if (pmic_off) {
		uint32_t ctrl = BBNSM->BBNSM_CTRL;

		ctrl |= BBNSM_BBNSM_CTRL_DP_EN_MASK | BBNSM_BBNSM_CTRL_TOSP_MASK;
		LOG_WRN("power: BBNSM TOSP (PMIC_ON_REQ off) — FRDM may ignore this");
		barrier_dsync_fence_full();
		BBNSM->BBNSM_CTRL = ctrl;
		barrier_dsync_fence_full();
		barrier_isync_fence_full();
	}

	for (int i = 0; i < RT118X_NVIC_WORDS; i++) {
		iser_save[i] = NVIC->ISER[i];
	}
	n_en = irq_popcount(iser_save, RT118X_NVIC_WORDS);
	t0 = bbnsm_rtc_seconds();
	LOG_INF("power: NVIC IRQs enabled=%u SysTick=0x%x CTRL=0x%x INT_EN=0x%x TA=%u",
		n_en, (unsigned)SysTick->CTRL, BBNSM->BBNSM_CTRL, BBNSM->BBNSM_INT_EN,
		BBNSM->BBNSM_TA);
	/*
	 * TA hardware is proven (SysTick-on poll, 5 s → ta=2). Flush UART,
	 * park IRQs, shallow WFI. Do not k_busy_wait after SysTick is off.
	 */
	printk("power: WFI (shallow, irq parked) t0=%u alarm=%u ta_fired=%u\n", t0,
	       t0 + delay_sec, (unsigned)bbnsm_ta_fired());
	k_busy_wait(10000);

	irq_park_for_rtc(iser_save, &systick_ctrl);
	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
	barrier_dsync_fence_full();
	barrier_isync_fence_full();
	__WFI();

	t1 = bbnsm_rtc_seconds();
	irq_unpark(iser_save, systick_ctrl);
	LOG_WRN("power: woke from WFI after %u s BBNSM events=0x%x (rails stayed up)",
		t1 - t0, BBNSM->BBNSM_EVENTS);
	return -EAGAIN;
}

static int rt118x_sys_init(void)
{
	/* Do not IRQ_CONNECT/enable BBNSM here: Zephyr prints the boot
	 * banner *after* POST_KERNEL, so a BBNSM fault or pending alarm
	 * ISR leaves UART dead after MCUboot jump (Gemba 2026-08-19).
	 */
	bbnsm_rtc_start();
	return 0;
}

SYS_INIT(rt118x_sys_init, POST_KERNEL, 90);

#endif /* CONFIG_SOC_SERIES_IMXRT118X */

int eink_power_enter_snvs_in(uint32_t delay_sec, bool pmic_off)
{
#if defined(CONFIG_SOC_SERIES_IMXRT118X)
	(void)eink_power_iw612_set(false);
	if (delay_sec < 2U) {
		delay_sec = 2U;
	}
	if (delay_sec > 86400U) {
		delay_sec = 86400U;
	}
	g_status.next_wake_unix = (int64_t)time(NULL) + (int64_t)delay_sec;
	return rt118x_enter_snvs(delay_sec, pmic_off);
#else
	ARG_UNUSED(delay_sec);
	ARG_UNUSED(pmic_off);
	return -ENOTSUP;
#endif
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
#elif defined(CONFIG_SOC_SERIES_IMXRT118X)
	{
		uint32_t delay = CONFIG_APP_EINK_SNVS_DEFAULT_ALARM_SEC;
		bool cut = IS_ENABLED(CONFIG_APP_EINK_SNVS_PMIC_OFF);

		if (g_status.next_wake_unix > 1700000000LL) {
			int64_t rem = g_status.next_wake_unix - (int64_t)time(NULL);

			if (rem >= 2) {
				delay = (uint32_t)MIN(rem, 86400);
			}
		}
		return rt118x_enter_snvs(delay, cut);
	}
#else
	return -ENOTSUP;
#endif
}
