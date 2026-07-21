/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_wake.h"

#include "eink_display.h"
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

	ret = eink_power_init();
	if (ret < 0) {
		return ret;
	}

	/* Offline display first — never power IW612 for a cached transition. */
	ret = eink_scheduler_tick();
	if (ret < 0) {
		LOG_WRN("scheduler tick: %d", ret);
	}
	(void)eink_display_wait_idle(K_SECONDS(120));

#if defined(CONFIG_APP_EINK_HTTP)
	ret = eink_power_iw612_set(true);
	if (ret == 0) {
		ret = eink_http_sync_once();
		if (ret < 0) {
			LOG_WRN("sync_once failed (%d) — backoff then sleep", ret);
		}
	}
	(void)eink_power_iw612_set(false);
#endif

	now = (int64_t)time(NULL);
	if (now < 1700000000LL) {
		next = 12 * 3600;
	} else {
		next = now + 12 * 3600;
	}
	(void)eink_power_set_next_wake(next);

	return eink_power_enter_snvs();
}
