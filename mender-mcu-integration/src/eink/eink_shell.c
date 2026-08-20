/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_display.h"
#include "eink_frame.h"
#include "eink_power.h"
#include "eink_scheduler.h"
#if defined(CONFIG_APP_EINK_HTTP)
#include "eink_http.h"
#endif
#if defined(CONFIG_APP_EINK_LOCATION)
#include "eink_location.h"
#endif
#if defined(CONFIG_APP_EINK_GNSS)
#include "eink_gnss.h"
#endif
#include "utils/soc_uid.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_shell, LOG_LEVEL_INF);

static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	if (argc < 2) {
		shell_error(sh, "usage: eink show <path> [job_id]");
		return -EINVAL;
	}
	ret = eink_display_show_path(argv[1], argc >= 3 ? argv[2] : NULL);
	if (ret) {
		shell_error(sh, "queue failed: %d", ret);
		return ret;
	}
	shell_print(sh, "queued show %s", argv[1]);
	return 0;
}

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = eink_display_clear();

	if (ret) {
		shell_error(sh, "clear failed: %d", ret);
		return ret;
	}
	shell_print(sh, "queued clear");
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct eink_display_status st;
	const char *state;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	eink_display_get_status(&st);
	switch (st.state) {
	case EINK_DISPLAY_IDLE:
		state = "idle";
		break;
	case EINK_DISPLAY_REFRESHING:
		state = "refreshing";
		break;
	default:
		state = "error";
		break;
	}
	shell_print(sh, "state=%s last_result=%d refreshes=%u job=%s", state, st.last_result,
		    st.refresh_count, st.last_job_id[0] ? st.last_job_id : "-");
	return 0;
}


#if defined(CONFIG_APP_EINK_LOCATION)
static int cmd_location(const struct shell *sh, size_t argc, char **argv)
{
	struct eink_location_fix fix;
	int ret;

	if (argc < 2) {
		ret = eink_location_get(&fix);
		if (ret) {
			shell_error(sh, "location get failed: %d", ret);
			return ret;
		}
#if defined(CONFIG_APP_EINK_GNSS)
		shell_print(sh, "gnss: %s", eink_gnss_ready() ? "ready" : "unavailable");
#endif
		if (!fix.valid) {
			shell_print(sh, "location: none");
			return 0;
		}
		if (fix.accuracy_m >= 0.0) {
			shell_print(sh, "location: lat=%.7f lng=%.7f accuracy_m=%.3f",
				    fix.latitude, fix.longitude, fix.accuracy_m);
		} else {
			shell_print(sh, "location: lat=%.7f lng=%.7f", fix.latitude,
				    fix.longitude);
		}
		return 0;
	}
	if (strcmp(argv[1], "clear") == 0) {
		ret = eink_location_clear();
		if (ret) {
			shell_error(sh, "location clear failed: %d", ret);
			return ret;
		}
		shell_print(sh, "location cleared");
		return 0;
	}
	if (strcmp(argv[1], "set") == 0) {
		double lat;
		double lng;
		double acc = -1.0;
		char *end = NULL;

		if (argc < 4) {
			shell_error(sh,
				    "usage: eink location set <lat> <lng> [accuracy_m]");
			return -EINVAL;
		}
		lat = strtod(argv[2], &end);
		if (end == argv[2] || *end != '\0') {
			shell_error(sh, "invalid latitude");
			return -EINVAL;
		}
		end = NULL;
		lng = strtod(argv[3], &end);
		if (end == argv[3] || *end != '\0') {
			shell_error(sh, "invalid longitude");
			return -EINVAL;
		}
		if (argc >= 5) {
			end = NULL;
			acc = strtod(argv[4], &end);
			if (end == argv[4] || *end != '\0') {
				shell_error(sh, "invalid accuracy_m");
				return -EINVAL;
			}
		}
		ret = eink_location_set(lat, lng, acc);
		if (ret) {
			shell_error(sh, "location set failed: %d", ret);
			return ret;
		}
		shell_print(sh, "location set");
		return 0;
	}
	shell_error(sh, "usage: eink location [set <lat> <lng> [accuracy_m]|clear]");
	return -EINVAL;
}
#endif

#if defined(CONFIG_APP_EINK_HTTP)
static int cmd_sync(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = eink_http_sync_once();

	if (ret) {
		shell_error(sh, "sync failed: %d", ret);
		return ret;
	}
	shell_print(sh, "sync ok");
	return 0;
}

static int cmd_http_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = eink_http_start();

	if (ret) {
		shell_error(sh, "http start failed: %d", ret);
		return ret;
	}
	shell_print(sh, "http sync started");
	return 0;
}

static int cmd_creds(const struct shell *sh, size_t argc, char **argv)
{
	/* eink creds <base_url> <device_id> <token>
	 * device_id should be SoC UID hex (or leave product default empty).
	 */
	if (argc < 4) {
		shell_error(sh, "usage: eink creds <base_url> <device_id|soc_uid_hex> <token>");
		return -EINVAL;
	}
	int ret = eink_http_set_credentials(argv[1], argv[2], argv[3]);

	if (ret) {
		shell_error(sh, "creds failed: %d", ret);
		return ret;
	}
	shell_print(sh, "credentials updated (token not echoed)");
	return 0;
}
#endif

static int cmd_uid(const struct shell *sh, size_t argc, char **argv)
{
	char uid[SOC_UID_HEX_MAX];
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = soc_uid_get_hex(uid, sizeof(uid));
	if (ret) {
		shell_error(sh, "SoC UID unavailable: %d", ret);
		return ret;
	}
	shell_print(sh, "soc_uid=%s", uid);
	return 0;
}

static int cmd_sched_tick(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int n = eink_scheduler_tick();

	shell_print(sh, "scheduler tick displayed=%d", n);
	return 0;
}

static int cmd_snvs(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t sec = 20;
	bool cut = false;
	int ret;

	if (argc >= 2) {
		sec = (uint32_t)strtoul(argv[1], NULL, 10);
	}
	if (argc >= 3 && strcmp(argv[2], "cut") == 0) {
		cut = true;
	}
	if (sec < 2U) {
		sec = 2U;
	}
	shell_print(sh,
		    "SNVS: BBNSM alarm in %u s%s (FRDM will not hit uA; cut may hang until POR)",
		    sec, cut ? ", PMIC_ON_REQ TOSP" : ", WAIT+WFI only");
	ret = eink_power_enter_snvs_in(sec, cut);
	if (ret == -EAGAIN) {
		shell_print(sh, "returned from WFI — RTC woke; rails stayed up");
		return 0;
	}
	if (ret) {
		shell_error(sh, "snvs failed: %d", ret);
	}
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(eink_cmds,
	SHELL_CMD_ARG(show, NULL, "Show ES6F frame file", cmd_show, 2, 1),
	SHELL_CMD(clear, NULL, "Clear panel (white)", cmd_clear),
	SHELL_CMD(status, NULL, "Display status", cmd_status),
	SHELL_CMD(uid, NULL, "Print SoC UID hex (device identity SoT)", cmd_uid),
	SHELL_CMD(tick, NULL, "Run one scheduler tick (local/fixture)", cmd_sched_tick),
	SHELL_CMD_ARG(snvs, NULL, "SNVS try: eink snvs [seconds] [cut]", cmd_snvs, 1, 2),
#if defined(CONFIG_APP_EINK_LOCATION)
	SHELL_CMD_ARG(location, NULL,
		      "Show/set/clear WGS84 fix for telemetry", cmd_location, 1, 4),
#endif
#if defined(CONFIG_APP_EINK_HTTP)
	SHELL_CMD(sync, NULL, "One-shot e-tabelone sync", cmd_sync),
	SHELL_CMD(http_start, NULL, "Start periodic e-tabelone sync", cmd_http_start),
	SHELL_CMD_ARG(creds, NULL, "Set API base/device/token", cmd_creds, 4, 0),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(eink, &eink_cmds, "E-ink display / scheduler", NULL);
