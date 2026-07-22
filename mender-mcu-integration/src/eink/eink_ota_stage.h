/*
 * FlexSPI2 flash-backed Mender OTA staging.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_OTA_STAGE_H
#define EINK_OTA_STAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct eink_ota_stage_status {
	bool staging_valid;
	bool copy_in_progress;
	uint32_t staged_bytes;
	int last_result;
};

int eink_ota_stage_init(void);
void eink_ota_stage_get_status(struct eink_ota_stage_status *out);

/**
 * Begin / append a download into the FlexSPI2 staging region.
 * Production implementation programs the raw partition; until dual-FlexSPI
 * DTS is live this retains a LittleFS file under /lfs1/ota/.
 */
int eink_ota_stage_write(const uint8_t *data, size_t len);
int eink_ota_stage_finish(uint32_t expected_crc32);

/**
 * Copy validated staging → FlexSPI1 slot1 with network off.
 * Flash-critical routines are intended to run from internal RAM.
 */
int eink_ota_stage_install_slot1(void);

/** Invalidate staging after successful confirmation. */
int eink_ota_stage_invalidate(void);

#endif
