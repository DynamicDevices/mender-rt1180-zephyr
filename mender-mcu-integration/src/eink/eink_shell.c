/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_display.h"
#include "eink_store.h"
#include "eink_frame.h"
#include "eink_power.h"
#include "eink_scheduler.h"
#if defined(CONFIG_APP_EINK_T2000)
#include "eink_t2000.h"
#endif
#if defined(CONFIG_APP_EINK_PANEL_AUTODETECT)
#include "eink_panel.h"
#endif
#if defined(CONFIG_APP_EINK_HTTP)
#include "eink_http.h"
#endif
#if defined(CONFIG_APP_EINK_DEBUG_LOG_UPLOAD)
#include "eink_log_ring.h"
#endif
#if defined(CONFIG_APP_EINK_LOCATION)
#include "eink_location.h"
#endif
#if defined(CONFIG_APP_EINK_GNSS)
#include "eink_gnss.h"
#endif
#include "utils/soc_uid.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
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

#if defined(CONFIG_APP_EINK_PANEL_AUTODETECT)
static int cmd_panel(const struct shell *sh, size_t argc, char **argv)
{
	const struct eink_panel_info *info;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	(void)eink_panel_detect();
	info = eink_panel_get();
	shell_print(sh, "panel=%s %ux%u screen_type=%s", info->name, info->width, info->height,
		    info->screen_type[0] ? info->screen_type : "-");
	return 0;
}
#endif

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
	shell_print(sh, "credentials updated and persisted (token not echoed)");
	return 0;
}

#if defined(CONFIG_APP_EINK_DEBUG_LOG_UPLOAD)
static int cmd_log_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "debug log ring: %u bytes, %u newlines (cap %u)",
		    (unsigned)eink_log_ring_bytes(), (unsigned)eink_log_ring_line_count(),
		    (unsigned)CONFIG_APP_EINK_DEBUG_LOG_RING_SIZE);
	return 0;
}

static int cmd_log_upload(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = eink_http_debug_log_upload_now();
	if (ret) {
		shell_error(sh, "debug log upload failed: %d", ret);
		return ret;
	}
	shell_print(sh, "debug log upload ok");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(eink_log_cmds,
	SHELL_CMD(status, NULL, "Show RAM debug log ring fill", cmd_log_status),
	SHELL_CMD(upload, NULL, "POST debug log ring to cloud", cmd_log_upload),
	SHELL_SUBCMD_SET_END);
#endif /* CONFIG_APP_EINK_DEBUG_LOG_UPLOAD */
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
		    sec, cut ? ", PMIC_ON_REQ TOSP" : ", GPC STOP+WFI");
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

#if defined(CONFIG_APP_EINK_T2000)
static int cmd_t2000_info(const struct shell *sh, size_t argc, char **argv)
{
	struct eink_t2000_info info;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!eink_t2000_ready()) {
		ret = eink_t2000_wait_ready(5000);
		if (ret) {
			shell_error(sh, "t2000: not connected (%d)", ret);
			return ret;
		}
	}
	ret = eink_t2000_get_info(&info);
	if (ret) {
		shell_error(sh, "t2000 info failed: %d", ret);
		return ret;
	}
	shell_print(sh, "t2000: %ux%u panel_id=%u tcon=0x%x vcom=%d", info.panel_width,
		    info.panel_height, info.panel_id, info.tcon_ver, info.vcom);
	shell_print(sh, "t2000: lut=%s", info.wf_lut_version);
	return 0;
}

static int cmd_t2000_clear(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!eink_t2000_ready() && eink_t2000_wait_ready(5000)) {
		shell_error(sh, "t2000: not connected");
		return -ENODEV;
	}
	ret = eink_t2000_clear();
	if (ret) {
		shell_error(sh, "t2000 clear failed: %d", ret);
		return ret;
	}
	ret = eink_t2000_wait_idle(30000);
	shell_print(sh, "t2000 clear done (%d)", ret);
	return ret;
}

static int cmd_t2000_fill(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t idx = 0;
	uint8_t wf = 0;
	int ret;

	if (argc >= 2) {
		idx = (uint8_t)strtoul(argv[1], NULL, 0);
	}
	if (argc >= 3) {
		wf = (uint8_t)strtoul(argv[2], NULL, 0);
	}
	if (!eink_t2000_ready() && eink_t2000_wait_ready(5000)) {
		shell_error(sh, "t2000: not connected");
		return -ENODEV;
	}
	shell_print(sh, "t2000 fill idx=%u wf=%u ...", idx, wf);
	ret = eink_t2000_fill(idx, wf);
	if (ret) {
		shell_error(sh, "t2000 fill failed: %d", ret);
		return ret;
	}
	shell_print(sh, "t2000 fill done");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(eink_t2000_cmds,
	SHELL_CMD(info, NULL, "T2000 get_dev_info", cmd_t2000_info),
	SHELL_CMD(clear, NULL, "T2000 clear + wait idle", cmd_t2000_clear),
	SHELL_CMD_ARG(fill, NULL, "T2000 solid fill: fill [y8_idx] [wf_mode]", cmd_t2000_fill, 1,
		      2),
	SHELL_SUBCMD_SET_END);
#endif


static int cmd_flash_bench(const struct shell *sh, size_t argc, char **argv)
{
	struct fs_file_t f;
	struct fs_dirent ent;
	static uint8_t chunk[4096];
	const char *path;
	char tmp[340];
	size_t total = 0;
	int64_t t0;
	int64_t ms;
	int ret;
	bool do_write = false;

	if (argc < 2) {
		shell_error(sh, "usage: eink flash_bench <path> [write]");
		return -EINVAL;
	}
	path = argv[1];
	if (argc >= 3 && strcmp(argv[2], "write") == 0) {
		do_write = true;
	}

	ret = fs_stat(path, &ent);
	if (ret < 0) {
		shell_error(sh, "stat %s: %d", path, ret);
		return ret;
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		shell_error(sh, "open %s: %d", path, ret);
		return ret;
	}
	t0 = k_uptime_get();
	while (total < ent.size) {
		size_t want = ent.size - total;

		if (want > sizeof(chunk)) {
			want = sizeof(chunk);
		}
		ssize_t n = fs_read(&f, chunk, want);

		if (n < 0) {
			(void)fs_close(&f);
			shell_error(sh, "read: %d", (int)n);
			return (int)n;
		}
		if (n == 0) {
			break;
		}
		total += (size_t)n;
	}
	(void)fs_close(&f);
	ms = k_uptime_get() - t0;
	eink_prof_flash_io("read", total, ms);
	shell_print(sh, "flash_bench read %u bytes in %lld ms", (unsigned)total, (long long)ms);

	if (!do_write) {
		return 0;
	}
	if (strlen(path) + 12 >= sizeof(tmp)) {
		return -ENOMEM;
	}
	snprintf(tmp, sizeof(tmp), "%s.bench", path);
	(void)fs_unlink(tmp);
	fs_file_t_init(&f);
	ret = fs_open(&f, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		shell_error(sh, "create %s: %d", tmp, ret);
		return ret;
	}
	/* Re-read source while writing — measures write path of same size. */
	{
		struct fs_file_t in;
		size_t written = 0;

		fs_file_t_init(&in);
		ret = fs_open(&in, path, FS_O_READ);
		if (ret < 0) {
			(void)fs_close(&f);
			(void)fs_unlink(tmp);
			return ret;
		}
		t0 = k_uptime_get();
		while (written < ent.size) {
			size_t want = ent.size - written;
			ssize_t n;
			ssize_t nw;

			if (want > sizeof(chunk)) {
				want = sizeof(chunk);
			}
			n = fs_read(&in, chunk, want);
			if (n <= 0) {
				ret = n < 0 ? (int)n : -EIO;
				break;
			}
			nw = fs_write(&f, chunk, (size_t)n);
			if (nw != n) {
				ret = nw < 0 ? (int)nw : -EIO;
				break;
			}
			written += (size_t)n;
			ret = 0;
		}
		if (ret == 0) {
			ret = fs_sync(&f);
		}
		ms = k_uptime_get() - t0;
		(void)fs_close(&in);
		(void)fs_close(&f);
		if (ret < 0) {
			(void)fs_unlink(tmp);
			shell_error(sh, "write failed: %d", ret);
			return ret;
		}
		eink_prof_flash_io("write", written, ms);
		shell_print(sh, "flash_bench write %u bytes in %lld ms -> %s", (unsigned)written,
			    (long long)ms, tmp);
		(void)fs_unlink(tmp);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(eink_cmds,
	SHELL_CMD_ARG(show, NULL, "Show ES6F frame file", cmd_show, 2, 1),
	SHELL_CMD_ARG(flash_bench, NULL,
		      "Time LittleFS image R/W: flash_bench <path> [write]", cmd_flash_bench, 2,
		      1),
	SHELL_CMD(clear, NULL, "Clear panel (white)", cmd_clear),
	SHELL_CMD(status, NULL, "Display status", cmd_status),
#if defined(CONFIG_APP_EINK_PANEL_AUTODETECT)
	SHELL_CMD(panel, NULL, "Show autodetection result", cmd_panel),
#endif
	SHELL_CMD(uid, NULL, "Print SoC UID hex (device identity SoT)", cmd_uid),
	SHELL_CMD(tick, NULL, "Run one scheduler tick (local/fixture)", cmd_sched_tick),
	SHELL_CMD_ARG(snvs, NULL, "SNVS try: eink snvs [seconds] [cut]", cmd_snvs, 1, 2),
#if defined(CONFIG_APP_EINK_T2000)
	SHELL_CMD(t2000, &eink_t2000_cmds, "T2000 USB host (info|clear|fill)", NULL),
#endif
#if defined(CONFIG_APP_EINK_LOCATION)
	SHELL_CMD_ARG(location, NULL,
		      "Show/set/clear WGS84 fix for telemetry", cmd_location, 1, 4),
#endif
#if defined(CONFIG_APP_EINK_HTTP)
	SHELL_CMD(sync, NULL, "One-shot e-tabelone sync", cmd_sync),
	SHELL_CMD(http_start, NULL, "Start periodic e-tabelone sync", cmd_http_start),
	SHELL_CMD_ARG(creds, NULL, "Set API base/device/token", cmd_creds, 4, 0),
#if defined(CONFIG_APP_EINK_DEBUG_LOG_UPLOAD)
	SHELL_CMD(log, &eink_log_cmds, "Debug log ring (status|upload)", NULL),
#endif
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(eink, &eink_cmds, "E-ink display / scheduler", NULL);
