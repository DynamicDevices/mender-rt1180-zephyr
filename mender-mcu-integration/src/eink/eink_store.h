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

/** True if a valid ES6F already exists for image_id. */
bool eink_store_has_valid_image(const char *image_id);

/** Write payload file for image_id (atomic replace). */
int eink_store_put_image(const char *image_id, const uint8_t *es6f, size_t len);

/**
 * Atomically accept a previously written temporary ES6F path as image_id.
 * Validates the file with streaming CRC (no full-frame RAM copy) before rename.
 */
int eink_store_accept_temp_image(const char *image_id, const char *temp_path);

/** Stream-validate an ES6F file on the Zephyr FS (or host path on native_sim). */
int eink_store_validate_path(const char *path, struct eink_frame_header *out_hdr);

#endif
