/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_frame.h"

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

uint32_t eink_frame_crc32(const uint8_t *payload, size_t len)
{
	return crc32_ieee(payload, len);
}

int eink_frame_header_parse(const uint8_t *buf, size_t len, struct eink_frame_header *out)
{
	struct eink_frame_header hdr;

	if (buf == NULL || out == NULL || len < EINK_FRAME_HEADER_SIZE) {
		return -EINVAL;
	}

	memcpy(&hdr, buf, sizeof(hdr));
	hdr.magic = sys_le32_to_cpu(hdr.magic);
	hdr.version = sys_le16_to_cpu(hdr.version);
	hdr.width = sys_le16_to_cpu(hdr.width);
	hdr.height = sys_le16_to_cpu(hdr.height);
	hdr.flags = sys_le16_to_cpu(hdr.flags);
	hdr.payload_len = sys_le32_to_cpu(hdr.payload_len);
	hdr.crc32 = sys_le32_to_cpu(hdr.crc32);

	if (hdr.magic != EINK_FRAME_MAGIC) {
		return -EINVAL;
	}
	if (hdr.version != EINK_FRAME_VERSION) {
		return -ENOTSUP;
	}
	if (hdr.width != EINK_PANEL_WIDTH || hdr.height != EINK_PANEL_HEIGHT) {
		return -EINVAL;
	}
	if (hdr.pixel_format != EINK_PIXEL_FORMAT_L4_S6) {
		return -ENOTSUP;
	}
	if (hdr.payload_len != EINK_PAYLOAD_LEN) {
		return -EINVAL;
	}

	*out = hdr;
	return 0;
}

void eink_frame_stream_begin(struct eink_frame_stream_ctx *ctx,
			     const struct eink_frame_header *hdr)
{
	if (ctx == NULL || hdr == NULL) {
		return;
	}
	ctx->hdr = *hdr;
	ctx->crc = 0U;
	ctx->payload_seen = 0U;
	ctx->header_ok = true;
}

int eink_frame_stream_update(struct eink_frame_stream_ctx *ctx, const uint8_t *data, size_t len)
{
	if (ctx == NULL || !ctx->header_ok || (len > 0 && data == NULL)) {
		return -EINVAL;
	}
	if (ctx->payload_seen + len > ctx->hdr.payload_len) {
		return -EFBIG;
	}
	if (len > 0) {
		ctx->crc = crc32_ieee_update(ctx->crc, data, len);
		ctx->payload_seen += (uint32_t)len;
	}
	return 0;
}

int eink_frame_stream_finish(struct eink_frame_stream_ctx *ctx)
{
	if (ctx == NULL || !ctx->header_ok) {
		return -EINVAL;
	}
	if (ctx->payload_seen != ctx->hdr.payload_len) {
		return -EINVAL;
	}
	if (ctx->crc != ctx->hdr.crc32) {
		return -EILSEQ;
	}
	return 0;
}

int eink_frame_validate_stream(eink_frame_read_cb read_cb, eink_frame_seek_cb seek_cb,
			       void *user, struct eink_frame_header *out_hdr)
{
	uint8_t hdr_raw[EINK_FRAME_HEADER_SIZE];
	uint8_t chunk[EINK_STREAM_CHUNK_SIZE];
	struct eink_frame_header hdr;
	struct eink_frame_stream_ctx ctx;
	int n;
	int ret;

	if (read_cb == NULL) {
		return -EINVAL;
	}
	if (seek_cb != NULL) {
		ret = seek_cb(user, 0);
		if (ret < 0) {
			return ret;
		}
	}

	n = read_cb(user, hdr_raw, sizeof(hdr_raw));
	if (n < 0) {
		return n;
	}
	if ((size_t)n < sizeof(hdr_raw)) {
		return -EINVAL;
	}

	ret = eink_frame_header_parse(hdr_raw, sizeof(hdr_raw), &hdr);
	if (ret < 0) {
		return ret;
	}

	eink_frame_stream_begin(&ctx, &hdr);
	for (;;) {
		n = read_cb(user, chunk, sizeof(chunk));
		if (n < 0) {
			return n;
		}
		if (n == 0) {
			break;
		}
		ret = eink_frame_stream_update(&ctx, chunk, (size_t)n);
		if (ret < 0) {
			return ret;
		}
	}

	ret = eink_frame_stream_finish(&ctx);
	if (ret < 0) {
		return ret;
	}
	if (out_hdr != NULL) {
		*out_hdr = hdr;
	}
	return 0;
}

int eink_frame_validate(const uint8_t *buf, size_t len, struct eink_frame_view *out)
{
	struct eink_frame_header hdr;
	const uint8_t *payload;
	int ret;

	if (buf == NULL || out == NULL) {
		return -EINVAL;
	}

	ret = eink_frame_header_parse(buf, len, &hdr);
	if (ret < 0) {
		return ret;
	}
	if (len < EINK_FRAME_HEADER_SIZE + (size_t)hdr.payload_len) {
		return -EINVAL;
	}

	payload = buf + EINK_FRAME_HEADER_SIZE;
	if (eink_frame_crc32(payload, hdr.payload_len) != hdr.crc32) {
		return -EILSEQ;
	}

	out->hdr = hdr;
	out->payload = payload;
	return 0;
}

void eink_frame_fill_solid_chunk(uint8_t *chunk, size_t chunk_len, uint8_t color_nibble)
{
	uint8_t b = (uint8_t)(((color_nibble & 0x0f) << 4) | (color_nibble & 0x0f));

	if (chunk == NULL || chunk_len == 0) {
		return;
	}
	memset(chunk, b, chunk_len);
}

void eink_frame_fill_solid(uint8_t *payload, uint8_t color_nibble)
{
	eink_frame_fill_solid_chunk(payload, EINK_PAYLOAD_LEN, color_nibble);
}

void eink_frame_fill_lr(uint8_t *payload, uint8_t left, uint8_t right)
{
	uint8_t lb = (uint8_t)(((left & 0x0f) << 4) | (left & 0x0f));
	uint8_t rb = (uint8_t)(((right & 0x0f) << 4) | (right & 0x0f));

	memset(payload, lb, EINK_BYTES_PER_HALF);
	memset(payload + EINK_BYTES_PER_HALF, rb, EINK_BYTES_PER_HALF);
}

int eink_frame_build(uint8_t *out, size_t out_cap, const uint8_t *payload, uint8_t orientation)
{
	struct eink_frame_header hdr = { 0 };

	if (out == NULL || payload == NULL) {
		return -EINVAL;
	}
	if (out_cap < EINK_FRAME_HEADER_SIZE + EINK_PAYLOAD_LEN) {
		return -ENOMEM;
	}

	hdr.magic = sys_cpu_to_le32(EINK_FRAME_MAGIC);
	hdr.version = sys_cpu_to_le16(EINK_FRAME_VERSION);
	hdr.width = sys_cpu_to_le16(EINK_PANEL_WIDTH);
	hdr.height = sys_cpu_to_le16(EINK_PANEL_HEIGHT);
	hdr.pixel_format = EINK_PIXEL_FORMAT_L4_S6;
	hdr.orientation = orientation;
	hdr.flags = 0;
	hdr.payload_len = sys_cpu_to_le32(EINK_PAYLOAD_LEN);
	hdr.crc32 = sys_cpu_to_le32(eink_frame_crc32(payload, EINK_PAYLOAD_LEN));

	memcpy(out, &hdr, sizeof(hdr));
	memcpy(out + EINK_FRAME_HEADER_SIZE, payload, EINK_PAYLOAD_LEN);
	return (int)(EINK_FRAME_HEADER_SIZE + EINK_PAYLOAD_LEN);
}

uint32_t eink_frame_nibble_to_argb(uint8_t nibble)
{
	switch (nibble & 0x0f) {
	case EINK_COLOR_BLACK:
		return 0xFF000000u;
	case EINK_COLOR_WHITE:
		return 0xFFFFFFFFu;
	case EINK_COLOR_YELLOW:
		return 0xFFFFFF00u;
	case EINK_COLOR_RED:
		return 0xFFFF0000u;
	case EINK_COLOR_BLUE:
		return 0xFF0000FFu;
	case EINK_COLOR_GREEN:
		return 0xFF00FF00u;
	default:
		return 0xFF808080u;
	}
}

uint16_t eink_frame_nibble_to_rgb565(uint8_t nibble)
{
	uint32_t argb = eink_frame_nibble_to_argb(nibble);
	uint8_t r = (uint8_t)((argb >> 16) & 0xffu);
	uint8_t g = (uint8_t)((argb >> 8) & 0xffu);
	uint8_t b = (uint8_t)(argb & 0xffu);

	return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
