/*
 * Frame / schedule persistence abstraction.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_STORE_H
#define EINK_STORE_H

#include "eink_frame.h"
#include "eink_scheduler_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int eink_store_init(const char *root_dir);

int eink_store_save_state(const char *last_job_id);
int eink_store_load_state(char *last_job_id, size_t cap);

int eink_store_save_schedule(const struct eink_schedule *sched);
int eink_store_load_schedule(struct eink_schedule *sched);

/** Resolve image_id → absolute path of ES6F file (null-terminated). */
int eink_store_image_path(const char *image_id, char *out, size_t out_cap);

/** Resolve image_id → absolute path of LZ4-framed object (.es6f.lz4). */
int eink_store_image_lz4_path(const char *image_id, char *out, size_t out_cap);

/**
 * Path to show for image_id: prefers raw .es6f, else .es6f.lz4.
 * @param is_lz4 set true when the resolved path is an LZ4 frame.
 */
int eink_store_resolve_show_path(const char *image_id, char *out, size_t out_cap,
				 bool *is_lz4);

/** True if a valid ES6F or (when enabled) LZ4-framed object exists. */
bool eink_store_has_valid_image(const char *image_id);
/** Header + file-size check only (no payload CRC). */
bool eink_store_has_image_quick(const char *image_id);

/** Persist / reload last successful e-tabelone sync time (unix seconds). */
int eink_store_save_last_sync(int64_t unix_sec);
int eink_store_load_last_sync(int64_t *unix_sec);

/**
 * Persist / reload SHA-256 of the LZ4-framed bytes for /node/v2 gallery hash-diff.
 * Hex is lowercase; byte_size is the compressed .es6f.lz4 length.
 */
int eink_store_save_content_hash(const char *image_id, const char *sha256_hex,
				 uint32_t byte_size);
int eink_store_load_content_hash(const char *image_id, char *sha256_hex, size_t cap,
				 uint32_t *byte_size);

/** Write payload file for image_id (atomic replace). */
int eink_store_put_image(const char *image_id, const uint8_t *es6f, size_t len);

/**
 * Atomically accept a previously written temporary path as image_id.
 * Raw ES6F → images/<id>.es6f after CRC validate.
 * LZ4 frame → images/<id>.es6f.lz4 when EXPAND_ON_DISPLAY (magic/size check).
 */
int eink_store_accept_temp_image(const char *image_id, const char *temp_path);

/** Stream-validate an ES6F file on the Zephyr FS (or host path on native_sim). */
int eink_store_validate_path(const char *path, struct eink_frame_header *out_hdr);

/**
 * Ensure @a path is a raw ES6F for display streaming.
 * If @a path is LZ4-framed, expand into scratch and return that path in @a out.
 * If already ES6F, copies @a path into @a out.
 */
int eink_store_materialize_es6f(const char *path, char *out, size_t out_cap);

#endif
