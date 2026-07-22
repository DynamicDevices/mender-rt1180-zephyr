/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash-backed OTA staging scaffold. Uses LittleFS path until FlexSPI2
 * raw partition DTS is enabled on the custom board.
 */
#include "eink_ota_stage.h"

#include "eink_power.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(eink_ota_stage, LOG_LEVEL_INF);

#define EINK_OTA_STAGE_PATH "/lfs1/ota/staged.bin"
#define EINK_OTA_STAGE_TMP  "/lfs1/ota/staged.bin.tmp"

static struct eink_ota_stage_status g_st;
static struct fs_file_t g_file;
static bool g_writing;
static uint32_t g_crc;

static int ensure_ota_dir(void)
{
	struct fs_dirent e;
	int ret = fs_stat("/lfs1/ota", &e);

	if (ret == 0) {
		return 0;
	}
	return fs_mkdir("/lfs1/ota");
}

int eink_ota_stage_init(void)
{
	memset(&g_st, 0, sizeof(g_st));
	return ensure_ota_dir();
}

void eink_ota_stage_get_status(struct eink_ota_stage_status *out)
{
	if (out) {
		*out = g_st;
	}
}

int eink_ota_stage_write(const uint8_t *data, size_t len)
{
	int ret;

	if (data == NULL && len != 0) {
		return -EINVAL;
	}
	if (!g_writing) {
		ret = ensure_ota_dir();
		if (ret < 0) {
			return ret;
		}
		fs_file_t_init(&g_file);
		ret = fs_open(&g_file, EINK_OTA_STAGE_TMP, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
		if (ret < 0) {
			return ret;
		}
		g_writing = true;
		g_crc = 0;
		g_st.staged_bytes = 0;
		g_st.copy_in_progress = false;
		g_st.staging_valid = false;
	}
	if (len == 0) {
		return 0;
	}
	ret = fs_write(&g_file, data, len);
	if (ret != (int)len) {
		return ret < 0 ? ret : -EIO;
	}
	g_crc = crc32_ieee_update(g_crc, data, len);
	g_st.staged_bytes += (uint32_t)len;
	return 0;
}

int eink_ota_stage_finish(uint32_t expected_crc32)
{
	int ret;

	if (!g_writing) {
		return -EINVAL;
	}
	ret = fs_sync(&g_file);
	(void)fs_close(&g_file);
	g_writing = false;
	if (ret < 0) {
		(void)fs_unlink(EINK_OTA_STAGE_TMP);
		g_st.last_result = ret;
		return ret;
	}
	if (g_crc != expected_crc32) {
		(void)fs_unlink(EINK_OTA_STAGE_TMP);
		g_st.last_result = -EILSEQ;
		return -EILSEQ;
	}
	(void)fs_unlink(EINK_OTA_STAGE_PATH);
	ret = fs_rename(EINK_OTA_STAGE_TMP, EINK_OTA_STAGE_PATH);
	g_st.staging_valid = (ret == 0);
	g_st.last_result = ret;
	return ret;
}

int eink_ota_stage_install_slot1(void)
{
	if (!g_st.staging_valid) {
		return -ENOENT;
	}

	/* Network must be gated before FlexSPI1 programming. */
	(void)eink_power_iw612_set(false);
	g_st.copy_in_progress = true;
	LOG_INF("ota: copy staging (%u bytes) → slot1 (internal-RAM copy TBD on dual-FlexSPI)",
		g_st.staged_bytes);
	/*
	 * Production: relocate erase/program + this loop into ITCM/OCRAM and
	 * stream FlexSPI2 → FlexSPI1 slot1 in bounded chunks with resume marks.
	 */
	g_st.copy_in_progress = false;
	g_st.last_result = -ENOTSUP;
	return -ENOTSUP;
}

int eink_ota_stage_invalidate(void)
{
	g_st.staging_valid = false;
	g_st.staged_bytes = 0;
	(void)fs_unlink(EINK_OTA_STAGE_PATH);
	(void)fs_unlink(EINK_OTA_STAGE_TMP);
	return 0;
}
