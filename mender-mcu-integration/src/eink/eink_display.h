/*
 * Serialized e-ink display command service.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_DISPLAY_H
#define EINK_DISPLAY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

enum eink_display_state {
	EINK_DISPLAY_IDLE = 0,
	EINK_DISPLAY_REFRESHING,
	EINK_DISPLAY_ERROR,
};

struct eink_display_status {
	enum eink_display_state state;
	int last_result;
	char last_job_id[64];
	uint32_t refresh_count;
};

int eink_display_init(void);
int eink_display_show_path(const char *path, const char *job_id);
int eink_display_clear(void);
int eink_display_show_payload(const uint8_t *payload, const char *job_id);
void eink_display_get_status(struct eink_display_status *out);
int eink_display_wait_idle(k_timeout_t timeout);

/** True while a show/clear is queued or the panel workqueue is refreshing. */
bool eink_display_is_busy(void);

#endif
