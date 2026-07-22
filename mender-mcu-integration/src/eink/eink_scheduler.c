/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_scheduler.h"
#include "eink_display.h"
#include "eink_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_scheduler, LOG_LEVEL_INF);

static struct eink_schedule sched;
static char last_job[EINK_ID_MAX];
static struct k_mutex mu;

static int64_t now_unix(void)
{
	return (int64_t)time(NULL);
}

/* native_sim boots at unix 0 until SNTP; cron "current slot" is meaningless then. */
static bool wall_clock_ok(int64_t now)
{
	return now >= 1700000000LL;
}

int eink_scheduler_init(void)
{
	int64_t now = now_unix();

	k_mutex_init(&mu);
	memset(&sched, 0, sizeof(sched));
	last_job[0] = '\0';
	(void)eink_store_load_state(last_job, sizeof(last_job));
	if (eink_store_load_schedule(&sched) == 0 && sched.count > 0) {
		for (size_t i = 0; i < sched.count; i++) {
			sched.jobs[i].next_run_unix =
				eink_cron_next_run(sched.jobs[i].cron, now);
		}
		LOG_INF("scheduler init last_job=%s jobs=%u (from store)",
			last_job[0] ? last_job : "-", (unsigned)sched.count);
	} else {
		sched.count = 0;
		LOG_INF("scheduler init last_job=%s", last_job[0] ? last_job : "-");
	}
	return 0;
}

int eink_scheduler_set_schedule(const struct eink_schedule *in, int64_t now)
{
	if (!in) {
		return -EINVAL;
	}
	if (now <= 0) {
		now = now_unix();
	}
	k_mutex_lock(&mu, K_FOREVER);
	sched = *in;
	for (size_t i = 0; i < sched.count; i++) {
		sched.jobs[i].next_run_unix =
			eink_cron_next_run(sched.jobs[i].cron, now);
	}
	(void)eink_store_save_schedule(&sched);
	k_mutex_unlock(&mu);
	return 0;
}

static int show_job_locked_copy(const struct eink_job *job, bool advance_state)
{
	char path[300];
	int ret;
	struct eink_display_status st;

	if (eink_store_image_path(job->image_id, path, sizeof(path)) != 0) {
		return -ENOENT;
	}
	if (!eink_store_has_valid_image(job->image_id)) {
		LOG_WRN("repaint: image %s not in store", job->image_id);
		return -ENOENT;
	}

	LOG_INF("show job=%s image=%s path=%s%s", job->job_id, job->image_id, path,
		advance_state ? "" : " (repaint)");
	ret = eink_display_show_path(path, job->job_id);
	if (ret != 0) {
		return ret;
	}
	ret = eink_display_wait_idle(K_MINUTES(2));
	if (ret != 0) {
		return ret;
	}
	eink_display_get_status(&st);
	if (st.last_result != 0) {
		LOG_WRN("display failed (%d); state not advanced", st.last_result);
		return st.last_result;
	}
	if (advance_state) {
		k_mutex_lock(&mu, K_FOREVER);
		strncpy(last_job, job->job_id, sizeof(last_job) - 1);
		last_job[sizeof(last_job) - 1] = '\0';
		(void)eink_store_save_state(last_job);
		k_mutex_unlock(&mu);
	}
	return 1;
}

int eink_scheduler_tick(void)
{
	struct eink_sched_decision d;
	struct eink_job job;

	k_mutex_lock(&mu, K_FOREVER);
	d = eink_scheduler_decide(&sched, now_unix(), last_job);
	if (d.action != EINK_SCHED_SHOW) {
		k_mutex_unlock(&mu);
		return 0;
	}
	job = sched.jobs[d.job_index];
	k_mutex_unlock(&mu);

	return show_job_locked_copy(&job, true);
}

int eink_scheduler_repaint(void)
{
	struct eink_job job;
	bool found = false;
	bool advance = false;
	int64_t now = now_unix();
	int64_t best_occ = -1;
	size_t best_i = 0;

	k_mutex_lock(&mu, K_FOREVER);
	/*
	 * Current panel content = latest cron occurrence that is already in the
	 * past (today's HH:MM if overdue, else yesterday's). Matches ESL
	 * "keep showing last scheduled frame" when nothing new is due.
	 * Before wall clock is set (native_sim pre-SNTP), prefer last_job.
	 */
	if (wall_clock_ok(now)) {
		for (size_t i = 0; i < sched.count; i++) {
			int64_t occ = eink_cron_next_run(sched.jobs[i].cron, now);

			if (occ > now) {
				occ -= 86400;
			}
			if (occ <= now && (!found || occ >= best_occ)) {
				best_occ = occ;
				best_i = i;
				found = true;
			}
		}
	}
	if (found) {
		job = sched.jobs[best_i];
		advance = (last_job[0] == '\0') || (strcmp(last_job, job.job_id) != 0);
	} else if (last_job[0] != '\0') {
		for (size_t i = 0; i < sched.count; i++) {
			if (strcmp(sched.jobs[i].job_id, last_job) == 0) {
				job = sched.jobs[i];
				found = true;
				advance = false;
				break;
			}
		}
	}
	k_mutex_unlock(&mu);

	if (!found) {
		LOG_INF("repaint: nothing to show");
		return 0;
	}
	return show_job_locked_copy(&job, advance);
}

int eink_scheduler_due_image(char *out, size_t cap)
{
	struct eink_sched_decision d;

	if (out == NULL || cap == 0) {
		return -EINVAL;
	}
	out[0] = '\0';
	k_mutex_lock(&mu, K_FOREVER);
	d = eink_scheduler_decide(&sched, now_unix(), last_job);
	if (d.action == EINK_SCHED_SHOW) {
		strncpy(out, sched.jobs[d.job_index].image_id, cap - 1);
		out[cap - 1] = '\0';
	}
	k_mutex_unlock(&mu);
	return d.action == EINK_SCHED_SHOW ? 1 : 0;
}

int eink_scheduler_current_image(char *out, size_t cap)
{
	bool found = false;
	int64_t now = now_unix();
	int64_t best_occ = -1;
	size_t best_i = 0;

	if (out == NULL || cap == 0) {
		return -EINVAL;
	}
	out[0] = '\0';
	k_mutex_lock(&mu, K_FOREVER);
	if (wall_clock_ok(now)) {
		for (size_t i = 0; i < sched.count; i++) {
			int64_t occ = eink_cron_next_run(sched.jobs[i].cron, now);

			if (occ > now) {
				occ -= 86400;
			}
			if (occ <= now && (!found || occ >= best_occ)) {
				best_occ = occ;
				best_i = i;
				found = true;
			}
		}
	}
	if (found) {
		strncpy(out, sched.jobs[best_i].image_id, cap - 1);
		out[cap - 1] = '\0';
	} else if (last_job[0] != '\0') {
		for (size_t i = 0; i < sched.count; i++) {
			if (strcmp(sched.jobs[i].job_id, last_job) == 0) {
				strncpy(out, sched.jobs[i].image_id, cap - 1);
				out[cap - 1] = '\0';
				found = true;
				break;
			}
		}
	}
	k_mutex_unlock(&mu);
	return found ? 1 : 0;
}

void eink_scheduler_get_last_job(char *out, size_t cap)
{
	k_mutex_lock(&mu, K_FOREVER);
	strncpy(out, last_job, cap - 1);
	out[cap - 1] = '\0';
	k_mutex_unlock(&mu);
}

int64_t eink_scheduler_get_next_wakeup(int64_t now_unix_in, uint32_t poll_interval_sec)
{
	int64_t wake;

	if (now_unix_in <= 0) {
		now_unix_in = now_unix();
	}
	k_mutex_lock(&mu, K_FOREVER);
	wake = eink_scheduler_next_wakeup(&sched, now_unix_in, poll_interval_sec);
	k_mutex_unlock(&mu);
	return wake;
}
