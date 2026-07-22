/*
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_SCHEDULER_H
#define EINK_SCHEDULER_H

#include "eink_scheduler_core.h"

int eink_scheduler_init(void);

/** Evaluate schedule once; returns 1 if a show was queued, 0 if nop, <0 on error. */
int eink_scheduler_tick(void);

/** Replace in-memory schedule (fixture / HTTP). Recomputes next_run from cron. */
int eink_scheduler_set_schedule(const struct eink_schedule *in, int64_t now_unix);

/** Return 1 and copy the currently due image id, 0 if no display is due. */
int eink_scheduler_due_image(char *out, size_t cap);

void eink_scheduler_get_last_job(char *out, size_t cap);

/**
 * Schedule-driven next wake (min of future crons and poll deadline).
 * Uses in-memory schedule under lock.
 */
int64_t eink_scheduler_get_next_wakeup(int64_t now_unix, uint32_t poll_interval_sec);

#endif
