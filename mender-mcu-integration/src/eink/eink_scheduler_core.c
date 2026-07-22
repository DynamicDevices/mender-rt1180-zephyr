/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_scheduler_core.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Civil UTC helpers without libc timegm (not always present). */
static void unix_to_ymd_hms(int64_t t, int *Y, int *M, int *D, int *h, int *m, int *s)
{
	int64_t days = t / 86400;
	int64_t rem = t % 86400;

	if (rem < 0) {
		rem += 86400;
		days -= 1;
	}
	*h = (int)(rem / 3600);
	*m = (int)((rem % 3600) / 60);
	*s = (int)(rem % 60);

	/* Howard Hinnant civil_from_days */
	days += 719468;
	int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	uint32_t doe = (uint32_t)(days - era * 146097);
	uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int y = (int)(yoe) + (int)(era * 400);
	uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	uint32_t mp = (5 * doy + 2) / 153;
	*D = (int)(doy - (153 * mp + 2) / 5 + 1);
	*M = (int)(mp < 10 ? mp + 3 : mp - 9);
	*Y = y + (*M <= 2);
}

static bool is_leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int64_t ymd_hms_to_unix(int Y, int M, int D, int h, int m, int s)
{
	static const int mdays[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	int64_t days = 0;
	int y;

	for (y = 1970; y < Y; y++) {
		days += is_leap(y) ? 366 : 365;
	}
	days += mdays[M - 1];
	if (M > 2 && is_leap(Y)) {
		days += 1;
	}
	days += D - 1;
	return days * 86400 + h * 3600 + m * 60 + s;
}

int64_t eink_cron_next_run(const char *cron, int64_t now_unix)
{
	int minute = -1;
	int hour = -1;
	int Y, M, D, h, m, s;

	if (cron == NULL) {
		return now_unix + 3600;
	}
	if (sscanf(cron, "%d %d", &minute, &hour) < 2) {
		return now_unix + 3600;
	}
	if (minute < 0 || minute > 59 || hour < 0 || hour > 23) {
		return now_unix + 3600;
	}

	unix_to_ymd_hms(now_unix, &Y, &M, &D, &h, &m, &s);
	/* Today's scheduled time — may be overdue (Rust behaviour). */
	return ymd_hms_to_unix(Y, M, D, hour, minute, 0);
}

int64_t eink_cron_next_after(const char *cron, int64_t now_unix)
{
	int64_t today = eink_cron_next_run(cron, now_unix);

	if (today > now_unix) {
		return today;
	}
	/* Tomorrow same HH:MM. */
	return today + 86400;
}

int64_t eink_scheduler_next_wakeup(const struct eink_schedule *sched, int64_t now_unix,
				   uint32_t poll_interval_sec)
{
	int64_t best = -1;
	int64_t poll_deadline;
	uint32_t poll = poll_interval_sec ? poll_interval_sec : 300;

	if (sched != NULL) {
		for (size_t i = 0; i < sched->count; i++) {
			int64_t t = eink_cron_next_after(sched->jobs[i].cron, now_unix);

			if (best < 0 || t < best) {
				best = t;
			}
		}
	}

	poll_deadline = now_unix + (int64_t)poll;
	if (best < 0 || poll_deadline < best) {
		best = poll_deadline;
	}
	/* Avoid spin if clock/cron glitch. */
	if (best < now_unix + 60) {
		best = now_unix + 60;
	}
	return best;
}

struct eink_sched_decision eink_scheduler_decide(const struct eink_schedule *sched,
						 int64_t now_unix,
						 const char *last_displayed_job_id)
{
	struct eink_sched_decision d = { .action = EINK_SCHED_NOP, .job_index = 0 };
	size_t i;
	int64_t best = -1;
	size_t best_i = 0;
	bool found = false;

	if (sched == NULL) {
		return d;
	}
	for (i = 0; i < sched->count; i++) {
		if (sched->jobs[i].next_run_unix <= now_unix) {
			if (!found || sched->jobs[i].next_run_unix >= best) {
				best = sched->jobs[i].next_run_unix;
				best_i = i;
				found = true;
			}
		}
	}
	if (!found) {
		return d;
	}
	if (last_displayed_job_id != NULL && last_displayed_job_id[0] != '\0' &&
	    strcmp(sched->jobs[best_i].job_id, last_displayed_job_id) == 0) {
		return d;
	}
	d.action = EINK_SCHED_SHOW;
	d.job_index = best_i;
	return d;
}
