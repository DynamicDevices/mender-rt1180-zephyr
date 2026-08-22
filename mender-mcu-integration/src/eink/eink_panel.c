/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Soft autodetection of connected e-ink controller (FRDM lab).
 */
#include "eink_panel.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_EL133UF1)
#include <zephyr/drivers/display/el133uf1.h>
#endif
#if defined(CONFIG_APP_EINK_T2000)
#include "eink_t2000.h"
#endif

LOG_MODULE_REGISTER(eink_panel, LOG_LEVEL_INF);

#ifndef CONFIG_APP_EINK_SCREEN_TYPE
#define CONFIG_APP_EINK_SCREEN_TYPE ""
#endif

#ifndef CONFIG_APP_EINK_PANEL_T2000_WAIT_MS
#define CONFIG_APP_EINK_PANEL_T2000_WAIT_MS 3000
#endif

static struct eink_panel_info s_info = {
	.kind = EINK_PANEL_NONE,
	.name = "none",
	.screen_type = "",
	.width = 0,
	.height = 0,
};
static bool s_detected;

#if defined(CONFIG_EL133UF1)
static bool probe_el133(struct eink_panel_info *out)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(dev)) {
		LOG_INF("panel probe: EL133 display device not ready");
		return false;
	}
	if (!el133uf1_glass_present(dev)) {
		LOG_INF("panel probe: EL133 driver up, no glass (BUSY timeout)");
		return false;
	}
	out->kind = EINK_PANEL_EL133;
	out->name = "el133uf1";
	out->screen_type = "13in";
	out->width = EL133_PANEL_WIDTH;
	out->height = EL133_PANEL_HEIGHT;
	return true;
}
#endif

#if defined(CONFIG_APP_EINK_T2000)
static bool probe_t2000(struct eink_panel_info *out)
{
	struct eink_t2000_info ti;
	int ret;

	ret = eink_t2000_init();
	if (ret) {
		LOG_INF("panel probe: T2000 USB host init failed (%d)", ret);
		return false;
	}
	ret = eink_t2000_wait_ready(CONFIG_APP_EINK_PANEL_T2000_WAIT_MS);
	if (ret) {
		LOG_INF("panel probe: no T2000 on USB within %d ms",
			CONFIG_APP_EINK_PANEL_T2000_WAIT_MS);
		return false;
	}
	ret = eink_t2000_get_info(&ti);
	if (ret) {
		LOG_INF("panel probe: T2000 get_info failed (%d)", ret);
		return false;
	}
	out->kind = EINK_PANEL_T2000;
	out->name = "t2000";
	/* 25.3″ Spectra 6 via T2000 Mini-LVDS — portal class 25in. */
	out->screen_type = "25in";
	out->width = ti.panel_width;
	out->height = ti.panel_height;
	LOG_INF("panel probe: T2000 %ux%u panel_id=%u", ti.panel_width, ti.panel_height,
		ti.panel_id);
	return true;
}
#endif

int eink_panel_detect(void)
{
	struct eink_panel_info found = {
		.kind = EINK_PANEL_NONE,
		.name = "none",
		.screen_type = "",
		.width = 0,
		.height = 0,
	};

	if (s_detected) {
		return 0;
	}

#if defined(CONFIG_APP_EINK_T2000)
	/* Prefer hot-plug TCON when this image includes the USB host stack. */
	if (probe_t2000(&found)) {
		goto done;
	}
#endif
#if defined(CONFIG_EL133UF1)
	if (probe_el133(&found)) {
		goto done;
	}
#endif

done:
	s_info = found;
	s_detected = true;
	if (s_info.kind == EINK_PANEL_NONE) {
		LOG_INF("panel detect: none (continuing without panel)");
	} else {
		LOG_INF("panel detect: %s %ux%u screen_type=%s", s_info.name, s_info.width,
			s_info.height, s_info.screen_type[0] ? s_info.screen_type : "-");
	}
	return 0;
}

const struct eink_panel_info *eink_panel_get(void)
{
	if (!s_detected) {
		(void)eink_panel_detect();
	}
	return &s_info;
}

const char *eink_panel_screen_type(void)
{
	const struct eink_panel_info *info = eink_panel_get();

	if (info->screen_type && info->screen_type[0]) {
		return info->screen_type;
	}
	return CONFIG_APP_EINK_SCREEN_TYPE;
}
