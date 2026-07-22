/*
 * Packed Spectra-6 frame (ES6F) — validation and palette.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_FRAME_H
#define EINK_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <zephyr/toolchain.h>

#define EINK_FRAME_MAGIC          0x46365345u /* 'ES6F' LE */
#define EINK_FRAME_VERSION        1u
#define EINK_PANEL_WIDTH          1200u
#define EINK_PANEL_HEIGHT         1600u
#define EINK_HALF_WIDTH           600u
#define EINK_BYTES_PER_HALF       480000u
#define EINK_PAYLOAD_LEN          960000u
#define EINK_FRAME_HEADER_SIZE    32u
#define EINK_FRAME_FILE_SIZE      (EINK_FRAME_HEADER_SIZE + EINK_PAYLOAD_LEN)
#define EINK_PIXEL_FORMAT_L4_S6   1u

/* Default streaming chunk for CRC / SPI / LittleFS validation. */
#ifndef EINK_STREAM_CHUNK_SIZE
#define EINK_STREAM_CHUNK_SIZE    4096u
#endif

/* Nibble colour indices (Spectra 6). */
#define EINK_COLOR_BLACK   0x0
#define EINK_COLOR_WHITE   0x1
#define EINK_COLOR_YELLOW  0x2
#define EINK_COLOR_RED     0x3
#define EINK_COLOR_BLUE    0x5
#define EINK_COLOR_GREEN   0x6

struct eink_frame_header {
	uint32_t magic;
	uint16_t version;
	uint16_t width;
	uint16_t height;
	uint8_t  pixel_format;
	uint8_t  orientation; /* degrees / 90: 0,1,2,3 */
	uint16_t flags;
	uint32_t payload_len;
	uint32_t crc32;
	uint8_t  reserved[10];
} __packed;

struct eink_frame_view {
	struct eink_frame_header hdr;
	const uint8_t *payload; /* EINK_PAYLOAD_LEN when valid */
};

/** Incremental CRC / length validation of an ES6F payload stream. */
struct eink_frame_stream_ctx {
	struct eink_frame_header hdr;
	uint32_t crc;
	uint32_t payload_seen;
	bool header_ok;
};

/** Decode and validate an ES6F header from the first 32 bytes. */
int eink_frame_header_parse(const uint8_t *buf, size_t len, struct eink_frame_header *out);

/** Begin streaming validation after a successful header parse. */
void eink_frame_stream_begin(struct eink_frame_stream_ctx *ctx,
			     const struct eink_frame_header *hdr);

/** Feed a payload chunk; returns 0, -EFBIG if too much data, or -EINVAL. */
int eink_frame_stream_update(struct eink_frame_stream_ctx *ctx, const uint8_t *data,
			     size_t len);

/**
 * Finish streaming validation. Returns 0 when length and CRC match,
 * -EILSEQ on CRC mismatch, -EINVAL on short payload.
 */
int eink_frame_stream_finish(struct eink_frame_stream_ctx *ctx);

/**
 * Validate an ES6F file via a read callback (no full-frame RAM copy).
 * @param read_cb returns bytes read (>0), 0 on EOF, or negative errno.
 * @param seek_cb optional seek; may be NULL if the stream is already at offset 0.
 */
typedef int (*eink_frame_read_cb)(void *user, void *buf, size_t len);
typedef int (*eink_frame_seek_cb)(void *user, size_t offset);

int eink_frame_validate_stream(eink_frame_read_cb read_cb, eink_frame_seek_cb seek_cb,
			       void *user, struct eink_frame_header *out_hdr);

/** Parse and validate header+payload in a contiguous buffer. */
int eink_frame_validate(const uint8_t *buf, size_t len, struct eink_frame_view *out);

/** CRC32 (IEEE) of payload. */
uint32_t eink_frame_crc32(const uint8_t *payload, size_t len);

/** Pack solid colour into a full payload (both halves). */
void eink_frame_fill_solid(uint8_t *payload, uint8_t color_nibble);

/** Left half solid c0, right half solid c1 (for split identity tests). */
void eink_frame_fill_lr(uint8_t *payload, uint8_t left, uint8_t right);

/** Fill @a chunk_len bytes of a solid colour packed L4 pattern. */
void eink_frame_fill_solid_chunk(uint8_t *chunk, size_t chunk_len, uint8_t color_nibble);

/** Build a complete ES6F buffer (header+payload) into out; out_cap >= header+payload. */
int eink_frame_build(uint8_t *out, size_t out_cap, const uint8_t *payload, uint8_t orientation);

/** Map Spectra6 nibble to approximate ARGB8888 for SDL. */
uint32_t eink_frame_nibble_to_argb(uint8_t nibble);

#endif /* EINK_FRAME_H */
