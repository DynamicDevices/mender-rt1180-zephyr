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

void eink_scheduler_get_last_job(char *out, size_t cap);

#endif
