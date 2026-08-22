/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Soft autodetection of the connected e-ink controller on FRDM lab images.
 * Compile-time still gates which stacks exist (EL133 DT vs T2000 USB).
 */
#ifndef EINK_PANEL_H
#define EINK_PANEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum eink_panel_kind {
	EINK_PANEL_NONE = 0,
	EINK_PANEL_EL133,
	EINK_PANEL_T2000,
};

struct eink_panel_info {
	enum eink_panel_kind kind;
	const char *name;
	/** Portal telemetry screen_type ("13in" | "25in") or empty. */
	const char *screen_type;
	uint16_t width;
	uint16_t height;
};

/**
 * Probe available backends once.
 * Prefer T2000 when CONFIG_APP_EINK_T2000 and a TCON enumerates; else EL133
 * when glass answers BUSY after reset; else NONE.
 * Safe to call after eink_display_init() (EL133 GPIO/SPI already configured).
 */
int eink_panel_detect(void);

const struct eink_panel_info *eink_panel_get(void);

/** Runtime screen_type for telemetry; falls back to CONFIG_APP_EINK_SCREEN_TYPE. */
const char *eink_panel_screen_type(void);

#ifdef __cplusplus
}
#endif

#endif /* EINK_PANEL_H */
