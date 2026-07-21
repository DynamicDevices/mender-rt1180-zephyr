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

int eink_scheduler_init(void)
{
	k_mutex_init(&mu);
	memset(&sched, 0, sizeof(sched));
	last_job[0] = '\0';
	(void)eink_store_load_state(last_job, sizeof(last_job));
	LOG_INF("scheduler init last_job=%s", last_job[0] ? last_job : "-");
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

int eink_scheduler_tick(void)
{
	struct eink_sched_decision d;
	struct eink_job job;
	char path[300];
	int ret;

	k_mutex_lock(&mu, K_FOREVER);
	d = eink_scheduler_decide(&sched, now_unix(), last_job);
	if (d.action != EINK_SCHED_SHOW) {
		k_mutex_unlock(&mu);
		return 0;
	}
	job = sched.jobs[d.job_index];
	k_mutex_unlock(&mu);

	if (eink_store_image_path(job.image_id, path, sizeof(path)) != 0) {
		return -ENOENT;
	}

	LOG_INF("show job=%s image=%s path=%s", job.job_id, job.image_id, path);
	ret = eink_display_show_path(path, job.job_id);
	if (ret != 0) {
		/* Do not advance state on queue/display failure. */
		return ret;
	}
	ret = eink_display_wait_idle(K_MINUTES(2));
	if (ret != 0) {
		return ret;
	}
	struct eink_display_status st;

	eink_display_get_status(&st);
	if (st.last_result != 0) {
		LOG_WRN("display failed (%d); state not advanced", st.last_result);
		return st.last_result;
	}

	k_mutex_lock(&mu, K_FOREVER);
	strncpy(last_job, job.job_id, sizeof(last_job) - 1);
	(void)eink_store_save_state(last_job);
	k_mutex_unlock(&mu);
	return 1;
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

void eink_scheduler_get_last_job(char *out, size_t cap)
{
	k_mutex_lock(&mu, K_FOREVER);
	strncpy(out, last_job, cap - 1);
	out[cap - 1] = '\0';
	k_mutex_unlock(&mu);
}
