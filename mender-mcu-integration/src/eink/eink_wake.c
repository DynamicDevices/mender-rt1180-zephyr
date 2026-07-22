/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_wake.h"

#include "eink_http.h"
#include "eink_power.h"
#include "eink_scheduler.h"

#include <errno.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_wake, LOG_LEVEL_INF);

int eink_wake_run_once(void)
{
	int ret;
	int64_t now;
	int64_t next;
	uint32_t poll = 300;

	ret = eink_power_init();
	if (ret < 0) {
		return ret;
	}

	/* Offline display first — never power IW612 for a cached transition.
	 * tick advances to a new due job; repaint covers power-on blank panel
	 * when the due job was already recorded as last_job.
	 */
	ret = eink_scheduler_tick();
	if (ret == 0) {
		ret = eink_scheduler_repaint();
	}
	if (ret < 0) {
		LOG_WRN("scheduler display: %d", ret);
	}

#if defined(CONFIG_APP_EINK_HTTP)
	ret = eink_power_iw612_set(true);
	if (ret == 0) {
		ret = eink_http_sync_once();
		if (ret < 0) {
			LOG_WRN("sync_once failed (%d) — backoff then sleep", ret);
		}
	}
	(void)eink_power_iw612_set(false);
	poll = CONFIG_APP_EINK_HTTP_POLL_INTERVAL;
#endif

	now = (int64_t)time(NULL);
	if (now < 1700000000LL) {
		/* Wall clock unset — fall back to poll interval from epoch-ish now. */
		next = now + (int64_t)poll;
		if (next < now + 60) {
			next = now + 12 * 3600;
		}
		LOG_WRN("wake: wall clock unset; next_wake fallback=%lld", (long long)next);
	} else {
		next = eink_scheduler_get_next_wakeup(now, poll);
	}
	(void)eink_power_set_next_wake(next);

	return eink_power_enter_snvs();
}
