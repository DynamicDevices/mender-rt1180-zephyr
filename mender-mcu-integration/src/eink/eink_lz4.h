/*
 * LZ4-framed ES6F download expand (streaming file → file).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wire contract (for server / bridge):
 *   - Preferred: an LZ4 *frame* (magic 04 22 4D 18), as produced by
 *     `lz4 -f input.es6f output.es6f.lz4`. Decompressed payload MUST be a
 *     complete ES6F v1 file (header + 960000-byte payload).
 *   - Still accepted: raw ES6F (magic 'ES6F') — no decompression.
 *   - Rejected: JPEG/PNG (unchanged policy).
 */
#ifndef EINK_LZ4_H
#define EINK_LZ4_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** LZ4 frame magic (little-endian 0x184D2204). */
#define EINK_LZ4_FRAME_MAGIC_0 0x04u
#define EINK_LZ4_FRAME_MAGIC_1 0x22u
#define EINK_LZ4_FRAME_MAGIC_2 0x4Du
#define EINK_LZ4_FRAME_MAGIC_3 0x18u

/** True if @a n bytes look like an LZ4 frame. */
bool eink_lz4_is_frame(const uint8_t *buf, size_t n);

/**
 * Stream-decompress LZ4 frame at @a src_path into ES6F at @a dst_path.
 * Returns 0 on success, negative errno on failure.
 */
int eink_lz4_decompress_file(const char *src_path, const char *dst_path);

/**
 * If @a src_path is an LZ4 frame, decompress to @a dst_path and return 1.
 * If @a src_path is already ES6F, return 0 (dst unused).
 * Negative errno on I/O / format / decompress errors.
 */
int eink_lz4_expand_if_framed(const char *src_path, const char *dst_path);

#endif /* EINK_LZ4_H */
