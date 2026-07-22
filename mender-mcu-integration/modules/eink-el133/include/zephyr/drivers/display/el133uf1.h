/*
 * Copyright (c) 2026 Dynamic Devices Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * EL133UF1 Spectra 6 — Zephyr display driver public API.
 * Protocol constants align with /data_drive/esl/eink-spectra6 (MIT core).
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_EL133UF1_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_EL133UF1_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EL133_PANEL_WIDTH  1200U
#define EL133_PANEL_HEIGHT 1600U
/* 4 bpp packed: width * height / 2 */
#define EL133_PAYLOAD_BYTES (EL133_PANEL_WIDTH * EL133_PANEL_HEIGHT / 2U)

/* Zephyr has no upstream L_4 yet — use the private format slot. */
#ifndef PIXEL_FORMAT_L_4
#define PIXEL_FORMAT_L_4 PIXEL_FORMAT_PRIV_START
#endif

/**
 * Fill callback for streaming a full ES6F payload into the panel.
 * Must write up to @p max_len bytes into @p dst and return the count, or a
 * negative errno. The driver requests the full payload across calls.
 */
typedef int (*el133uf1_fill_cb_t)(void *user, uint8_t *dst, size_t max_len);

/**
 * Stream a full-frame L_4 payload into the panel without a driver-owned FB.
 * Requires blanking to be on; pairs with display_blanking_off() to refresh.
 */
int el133uf1_stream_write(const struct device *dev, el133uf1_fill_cb_t fill, void *user);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_EL133UF1_H_ */
