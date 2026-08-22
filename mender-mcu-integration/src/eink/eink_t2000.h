/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thin T2000 USB host client (vendor bulk F1/F2).
 * Protocol SoT: DynamicDevices/eink-t2000-usb (Linux libusb).
 */

#ifndef EINK_T2000_H
#define EINK_T2000_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EINK_T2000_VID           0x3558U
#define EINK_T2000_PID           0x2002U
#define EINK_T2000_PID_MAIN      0x4002U
#define EINK_T2000_STRIPE_BYTES  (4096U * 4U) /* first Display cut in Linux */

struct eink_t2000_info {
	uint16_t panel_width;
	uint16_t panel_height;
	uint32_t tcon_ver;
	uint8_t firm_ver[8];
	uint8_t panel_id;
	int16_t vcom;
	char wf_lut_version[49];
};

/** Enable USBHS clocks, init USBH, enable host. Safe to call once. */
int eink_t2000_init(void);

/** True when a matching T2000 has been probed and bulk eps are known. */
bool eink_t2000_ready(void);

/** Block until ready or timeout_ms elapses. */
int eink_t2000_wait_ready(int timeout_ms);

int eink_t2000_get_info(struct eink_t2000_info *info);
int eink_t2000_clear(void);
int eink_t2000_set_mode(uint8_t wf_mode);

/**
 * Stream a solid Y8 fill (palette index) then multi-trigger refresh.
 * Uses a 16 KiB stripe; does not allocate a full frame.
 */
int eink_t2000_fill(uint8_t y8_index, uint8_t wf_mode);

/** Poll TCON busy (nonzero = busy). */
int eink_t2000_get_status(void);

/** Wait until status idle or timeout. */
int eink_t2000_wait_idle(int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EINK_T2000_H */
