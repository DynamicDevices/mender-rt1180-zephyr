/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * T2000 F1/F2 vendor-bulk protocol (from eink-t2000-usb epd_device.cpp).
 */

#include "eink_t2000.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_t2000_proto, CONFIG_LOG_DEFAULT_LEVEL);

/* Provided by eink_t2000_usb.c */
int eink_t2000_usb_bulk_out(const uint8_t *data, size_t len);
int eink_t2000_usb_bulk_in(uint8_t *data, size_t len);

#define T2000_CMD_LEN 21
#define T2000_DATA_MODE_Y8 1

static uint8_t stripe[EINK_T2000_STRIPE_BYTES];

static int cmd_f1(uint8_t opcode, const uint8_t *payload, uint8_t payload_len)
{
	uint8_t cmd[T2000_CMD_LEN];

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xF1;
	cmd[1] = opcode;
	cmd[2] = payload_len;
	if (payload != NULL && payload_len > 0) {
		memcpy(&cmd[5], payload, MIN(payload_len, (uint8_t)(T2000_CMD_LEN - 5)));
	}
	return eink_t2000_usb_bulk_out(cmd, T2000_CMD_LEN);
}

static int cmd_f2_read(uint8_t opcode, uint16_t length, uint8_t *out, size_t out_len)
{
	uint8_t req[5];
	int ret;

	req[0] = 0xF2;
	req[1] = opcode;
	req[2] = 0;
	req[3] = (uint8_t)(length & 0xff);
	req[4] = (uint8_t)((length >> 8) & 0xff);

	ret = eink_t2000_usb_bulk_out(req, sizeof(req));
	if (ret) {
		return ret;
	}
	k_msleep(50);
	return eink_t2000_usb_bulk_in(out, MIN(out_len, (size_t)length));
}

int eink_t2000_get_info(struct eink_t2000_info *info)
{
	uint8_t data_in[86];
	uint8_t cmd[T2000_CMD_LEN];
	int ret;

	if (info == NULL) {
		return -EINVAL;
	}
	if (!eink_t2000_ready()) {
		return -ENODEV;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xF1;
	cmd[1] = 0xE0;
	ret = eink_t2000_usb_bulk_out(cmd, T2000_CMD_LEN);
	if (ret) {
		return ret;
	}
	k_msleep(200);

	ret = cmd_f2_read(0xE0, 86, data_in, sizeof(data_in));
	if (ret) {
		return ret;
	}

	memset(info, 0, sizeof(*info));
	info->panel_width = ((uint16_t)data_in[0] << 8) | data_in[1];
	info->panel_height = ((uint16_t)data_in[2] << 8) | data_in[3];
	memcpy(&info->tcon_ver, &data_in[4], 4);
	memcpy(info->firm_ver, &data_in[8], 8);
	memcpy(info->wf_lut_version, &data_in[24], 48);
	info->wf_lut_version[48] = '\0';
	info->vcom = (int16_t)(data_in[80] | ((uint16_t)data_in[81] << 8));
	info->panel_id = data_in[84];
	return 0;
}

int eink_t2000_clear(void)
{
	uint8_t payload[2] = { 0x01, 0x00 };

	if (!eink_t2000_ready()) {
		return -ENODEV;
	}
	return cmd_f1(0x40, payload, 2);
}

int eink_t2000_set_mode(uint8_t wf_mode)
{
	uint8_t payload[4];

	if (!eink_t2000_ready()) {
		return -ENODEV;
	}
	payload[0] = wf_mode;
	payload[1] = wf_mode;
	payload[2] = wf_mode;
	payload[3] = 0;
	return cmd_f1(0x43, payload, 4);
}

int eink_t2000_get_status(void)
{
	uint8_t data_in[20];
	uint8_t cmd[T2000_CMD_LEN];
	int ret;
	int status = 0;

	if (!eink_t2000_ready()) {
		return -ENODEV;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xF1;
	cmd[1] = 0x4F;
	ret = eink_t2000_usb_bulk_out(cmd, T2000_CMD_LEN);
	if (ret) {
		return ret;
	}
	k_msleep(200);

	ret = cmd_f2_read(0x4F, 2, data_in, sizeof(data_in));
	if (ret) {
		return ret;
	}
	status = data_in[0] | ((int)data_in[1] << 8);
	return status;
}

int eink_t2000_wait_idle(int timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;
	int st;

	while (true) {
		st = eink_t2000_get_status();
		if (st < 0) {
			return st;
		}
		if (st == 0) {
			return 0;
		}
		if (timeout_ms >= 0 && k_uptime_get() >= end) {
			return -ETIMEDOUT;
		}
		k_msleep(100);
	}
}

/* Linux main enables multi-trigger once before Display (opcode 0x4C). */
static int multi_trigger_enable(uint8_t enable)
{
	uint8_t payload[2] = { enable, 0x00 };

	return cmd_f1(0x4C, payload, 2);
}

static int multi_trigger(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	uint8_t cmd[T2000_CMD_LEN];

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xF1;
	cmd[1] = 0x4D;
	cmd[2] = 0x0A;
	cmd[5] = 0x01;
	memcpy(&cmd[7], &x, 2);
	memcpy(&cmd[9], &y, 2);
	memcpy(&cmd[11], &w, 2);
	memcpy(&cmd[13], &h, 2);
	return eink_t2000_usb_bulk_out(cmd, 5 + cmd[2]);
}

static int display_chunk(const uint8_t *y8, uint32_t chunk_len, uint32_t totalsize,
			 uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode)
{
	uint8_t cmd[32];
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0xF1;
	cmd[1] = 0xA1;
	cmd[2] = 25;
	cmd[5] = mode;
	memcpy(&cmd[6], &w, 2);
	memcpy(&cmd[8], &h, 2);
	memcpy(&cmd[10], &x, 2);
	memcpy(&cmd[12], &y, 2);
	memcpy(&cmd[14], &chunk_len, 4);
	memcpy(&cmd[18], &totalsize, 4);

	ret = eink_t2000_usb_bulk_out(cmd, 5 + cmd[2]);
	if (ret) {
		return ret;
	}
	k_msleep(1);
	return eink_t2000_usb_bulk_out(y8, chunk_len);
}

int eink_t2000_fill(uint8_t y8_index, uint8_t wf_mode)
{
	struct eink_t2000_info info;
	uint32_t totalsize;
	uint32_t sent = 0;
	uint32_t first_cut;
	uint32_t cut;
	int ret;

	if (!eink_t2000_ready()) {
		return -ENODEV;
	}

	ret = eink_t2000_get_info(&info);
	if (ret) {
		return ret;
	}
	if (info.panel_width == 0 || info.panel_height == 0) {
		return -EINVAL;
	}

	totalsize = (uint32_t)info.panel_width * (uint32_t)info.panel_height;
	memset(stripe, y8_index, sizeof(stripe));

	ret = multi_trigger_enable(1);
	if (ret) {
		return ret;
	}

	ret = eink_t2000_set_mode(wf_mode);
	if (ret) {
		return ret;
	}

	first_cut = MIN(totalsize, (uint32_t)EINK_T2000_STRIPE_BYTES);
	ret = display_chunk(stripe, first_cut, totalsize, 0, 0, info.panel_width,
			    info.panel_height, T2000_DATA_MODE_Y8);
	if (ret) {
		return ret;
	}
	sent = first_cut;

	/* Subsequent cuts: stream same stripe repeatedly (solid fill). */
	cut = MIN((uint32_t)(524288U * 4U), (uint32_t)sizeof(stripe));
	while (sent < totalsize) {
		uint32_t n = MIN(cut, totalsize - sent);

		/* Re-fill stripe if last piece shorter — already solid. */
		ret = display_chunk(stripe, n, totalsize, 0, 0, info.panel_width,
				    info.panel_height, T2000_DATA_MODE_Y8);
		if (ret) {
			return ret;
		}
		sent += n;
	}

	ret = multi_trigger(0, 0, info.panel_width, info.panel_height);
	if (ret) {
		return ret;
	}
	return eink_t2000_wait_idle(30000);
}
