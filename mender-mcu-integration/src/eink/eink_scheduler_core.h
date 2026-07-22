/*
 * Pure scheduler decision core (no I/O) — ztestable / future Rust swap.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_SCHEDULER_CORE_H
#define EINK_SCHEDULER_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define EINK_MAX_JOBS 32
#define EINK_ID_MAX   64

struct eink_job {
	char job_id[EINK_ID_MAX];
	char image_id[EINK_ID_MAX];
	char cron[32];
	int64_t next_run_unix; /* UTC seconds */
};

struct eink_schedule {
	struct eink_job jobs[EINK_MAX_JOBS];
	size_t count;
};

enum eink_sched_action {
	EINK_SCHED_NOP = 0,
	EINK_SCHED_SHOW,
};

struct eink_sched_decision {
	enum eink_sched_action action;
	size_t job_index; /* into schedule.jobs when SHOW */
};

/** Parse "minute hour ..." → today's UTC unix for that HH:MM (may be overdue). */
int64_t eink_cron_next_run(const char *cron, int64_t now_unix);

/**
 * Next future occurrence of cron at/after now: today HH:MM if still upcoming,
 * otherwise tomorrow HH:MM (Rust sleep semantics).
 */
int64_t eink_cron_next_after(const char *cron, int64_t now_unix);

/**
 * Among overdue jobs (next_run <= now), pick the latest next_run.
 * Skip if that job_id equals last_displayed.
 */
struct eink_sched_decision eink_scheduler_decide(const struct eink_schedule *sched,
						 int64_t now_unix,
						 const char *last_displayed_job_id);

/**
 * Earliest future job wake from eink_cron_next_after, else now+poll_interval_sec.
 * Floor: at least now+60 so duty-cycle never spins.
 */
int64_t eink_scheduler_next_wakeup(const struct eink_schedule *sched, int64_t now_unix,
				   uint32_t poll_interval_sec);

#endif
