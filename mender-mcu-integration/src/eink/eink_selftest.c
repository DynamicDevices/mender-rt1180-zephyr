/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Streaming self-tests — no full-frame static buffers.
 */
#include "eink_frame.h"
#include "eink_scheduler_core.h"
#if defined(CONFIG_APP_EINK_LOCATION)
#include "eink_location.h"
#endif
#if defined(CONFIG_APP_EINK_LZ4)
#include "eink_lz4.h"
#include "lz4frame.h"
#endif

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(eink_selftest, LOG_LEVEL_INF);

struct solid_reader {
	struct eink_frame_header hdr_le;
	uint32_t payload_left;
	uint8_t solid_byte;
	bool hdr_sent;
	bool corrupt;
};

static int solid_read_cb(void *user, void *buf, size_t len)
{
	struct solid_reader *r = user;
	uint8_t *out = buf;

	if (!r->hdr_sent) {
		if (len < EINK_FRAME_HEADER_SIZE) {
			return -EINVAL;
		}
		memcpy(out, &r->hdr_le, EINK_FRAME_HEADER_SIZE);
		r->hdr_sent = true;
		return (int)EINK_FRAME_HEADER_SIZE;
	}
	if (r->payload_left == 0) {
		return 0;
	}
	{
		size_t n = MIN(len, r->payload_left);

		memset(out, r->solid_byte, n);
		if (r->corrupt && r->payload_left == EINK_PAYLOAD_LEN) {
			out[0] ^= 0x01;
		}
		r->payload_left -= (uint32_t)n;
		return (int)n;
	}
}

static int solid_seek_cb(void *user, size_t offset)
{
	struct solid_reader *r = user;

	if (offset != 0) {
		return -EINVAL;
	}
	r->hdr_sent = false;
	r->payload_left = EINK_PAYLOAD_LEN;
	return 0;
}

static int test_frame_stream_crc(void)
{
	struct solid_reader r = { 0 };
	struct eink_frame_header hdr = { 0 };
	uint8_t chunk[EINK_STREAM_CHUNK_SIZE];
	uint32_t crc = 0;
	uint32_t left = EINK_PAYLOAD_LEN;
	int ret;

	r.solid_byte = (uint8_t)(((EINK_COLOR_RED & 0x0f) << 4) | (EINK_COLOR_RED & 0x0f));
	while (left > 0) {
		size_t n = MIN(sizeof(chunk), left);

		memset(chunk, r.solid_byte, n);
		crc = crc32_ieee_update(crc, chunk, n);
		left -= (uint32_t)n;
	}

	r.hdr_le.magic = sys_cpu_to_le32(EINK_FRAME_MAGIC);
	r.hdr_le.version = sys_cpu_to_le16(EINK_FRAME_VERSION);
	r.hdr_le.width = sys_cpu_to_le16(EINK_PANEL_WIDTH);
	r.hdr_le.height = sys_cpu_to_le16(EINK_PANEL_HEIGHT);
	r.hdr_le.pixel_format = EINK_PIXEL_FORMAT_L4_S6;
	r.hdr_le.payload_len = sys_cpu_to_le32(EINK_PAYLOAD_LEN);
	r.hdr_le.crc32 = sys_cpu_to_le32(crc);
	r.payload_left = EINK_PAYLOAD_LEN;

	ret = eink_frame_validate_stream(solid_read_cb, solid_seek_cb, &r, &hdr);
	if (ret != 0) {
		return ret;
	}

	r.corrupt = true;
	r.hdr_sent = false;
	r.payload_left = EINK_PAYLOAD_LEN;
	ret = eink_frame_validate_stream(solid_read_cb, solid_seek_cb, &r, &hdr);
	return (ret == -EILSEQ) ? 0 : -EFAULT;
}

static int test_scheduler_latest_overdue(void)
{
	struct eink_schedule s = { 0 };
	struct eink_sched_decision d;
	int64_t now = 1700000000LL;

	strncpy(s.jobs[0].job_id, "old", sizeof(s.jobs[0].job_id));
	s.jobs[0].next_run_unix = now - 3600;
	strncpy(s.jobs[1].job_id, "new", sizeof(s.jobs[1].job_id));
	s.jobs[1].next_run_unix = now - 60;
	s.count = 2;

	d = eink_scheduler_decide(&s, now, "");
	if (d.action != EINK_SCHED_SHOW || d.job_index != 1) {
		return -EINVAL;
	}
	d = eink_scheduler_decide(&s, now, "new");
	if (d.action != EINK_SCHED_NOP) {
		return -EINVAL;
	}
	return 0;
}

static int test_cron_overdue_today(void)
{
	const int64_t day_start = 1704067200LL;
	const int64_t one_am = day_start + 3600;
	int64_t next = eink_cron_next_run("0 0 * * *", one_am);

	if (next != day_start) {
		return -EINVAL;
	}
	next = eink_cron_next_run("30 12 * * *", day_start + 13 * 3600);
	if (next != day_start + 12 * 3600 + 30 * 60) {
		return -EINVAL;
	}
	return 0;
}

static int test_cron_next_after(void)
{
	const int64_t day_start = 1704067200LL; /* 2024-01-01 00:00 UTC */
	const int64_t one_am = day_start + 3600;
	int64_t next = eink_cron_next_after("0 0 * * *", one_am);

	/* Midnight today is past → tomorrow midnight. */
	if (next != day_start + 86400) {
		return -EINVAL;
	}
	next = eink_cron_next_after("30 12 * * *", day_start + 10 * 3600);
	if (next != day_start + 12 * 3600 + 30 * 60) {
		return -EINVAL;
	}
	return 0;
}

static int test_scheduler_next_wakeup(void)
{
	struct eink_schedule s = { 0 };
	const int64_t day_start = 1704067200LL;
	const int64_t now = day_start + 13 * 3600; /* 13:00 */
	int64_t wake;

	strncpy(s.jobs[0].job_id, "a", sizeof(s.jobs[0].job_id));
	strncpy(s.jobs[0].cron, "0 0 * * *", sizeof(s.jobs[0].cron));
	strncpy(s.jobs[1].job_id, "b", sizeof(s.jobs[1].job_id));
	strncpy(s.jobs[1].cron, "0 18 * * *", sizeof(s.jobs[1].cron));
	s.count = 2;

	/* Earliest future is 18:00 today (poll=12h would be later). */
	wake = eink_scheduler_next_wakeup(&s, now, 12 * 3600);
	if (wake != day_start + 18 * 3600) {
		return -EINVAL;
	}
	/* Empty schedule → poll deadline (floored at +60). */
	s.count = 0;
	wake = eink_scheduler_next_wakeup(&s, now, 300);
	if (wake != now + 300) {
		return -EINVAL;
	}
	return 0;
}

#if defined(CONFIG_APP_EINK_LZ4)
static int test_lz4_frame_roundtrip(void)
{
	uint8_t src[128];
	uint8_t comp[256];
	uint8_t out[128];
	size_t clen;
	LZ4F_dctx *dctx = NULL;
	LZ4F_errorCode_t lret;
	size_t src_size;
	size_t dst_size;
	size_t consumed;

	for (size_t i = 0; i < sizeof(src); i++) {
		src[i] = (uint8_t)(i * 3u);
	}
	clen = LZ4F_compressFrame(comp, sizeof(comp), src, sizeof(src), NULL);
	if (LZ4F_isError(clen) || clen < 4) {
		return -EIO;
	}
	if (!eink_lz4_is_frame(comp, clen)) {
		return -EINVAL;
	}

	lret = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
	if (LZ4F_isError(lret)) {
		return -ENOMEM;
	}
	src_size = clen;
	dst_size = sizeof(out);
	consumed = src_size;
	lret = LZ4F_decompress(dctx, out, &dst_size, comp, &consumed, NULL);
	(void)LZ4F_freeDecompressionContext(dctx);
	if (LZ4F_isError(lret) || dst_size != sizeof(src) ||
	    memcmp(src, out, sizeof(src)) != 0) {
		return -EILSEQ;
	}
	return 0;
}
#endif

#if defined(CONFIG_APP_EINK_LOCATION)
static int test_location_telemetry_json(void)
{
	struct eink_location_fix fix;
	char json[192];
	int ret;

	ret = eink_location_clear();
	if (ret != 0) {
		return ret;
	}
	ret = eink_location_to_json_object(json, sizeof(json));
	if (ret != 0 || strcmp(json, "{}") != 0) {
		return -EINVAL;
	}
	if (strstr(json, "latitude") != NULL) {
		return -EFAULT;
	}

	ret = eink_location_set(53.4808, -2.2426, 12.5);
	if (ret != 0) {
		return ret;
	}
	ret = eink_location_get(&fix);
	if (ret != 0 || !fix.valid) {
		return -EINVAL;
	}
	if (fix.latitude < 53.48 || fix.latitude > 53.49 || fix.longitude > -2.24 ||
	    fix.longitude < -2.25 || fix.accuracy_m < 12.0 || fix.accuracy_m > 13.0) {
		return -ERANGE;
	}
	ret = eink_location_to_json_object(json, sizeof(json));
	if (ret != 0 || strstr(json, "\"latitude\"") == NULL ||
	    strstr(json, "\"longitude\"") == NULL ||
	    strstr(json, "\"location_accuracy_m\"") == NULL) {
		return -EINVAL;
	}

	/* Out of range must fail and leave prior fix intact. */
	if (eink_location_set(91.0, 0.0, -1.0) != -ERANGE) {
		return -EFAULT;
	}

	ret = eink_location_clear();
	if (ret != 0) {
		return ret;
	}
	ret = eink_location_to_json_object(json, sizeof(json));
	if (ret != 0 || strcmp(json, "{}") != 0 || strstr(json, "latitude") != NULL) {
		return -EINVAL;
	}
	return 0;
}
#endif

int eink_selftest_run(void)
{
	int fails = 0;
	int r;

	r = test_frame_stream_crc();
	LOG_INF("test_frame_stream_crc: %d", r);
	fails += (r != 0);
	r = test_scheduler_latest_overdue();
	LOG_INF("test_scheduler_latest_overdue: %d", r);
	fails += (r != 0);
	r = test_cron_overdue_today();
	LOG_INF("test_cron_overdue_today: %d", r);
	fails += (r != 0);
	r = test_cron_next_after();
	LOG_INF("test_cron_next_after: %d", r);
	fails += (r != 0);
	r = test_scheduler_next_wakeup();
	LOG_INF("test_scheduler_next_wakeup: %d", r);
	fails += (r != 0);
#if defined(CONFIG_APP_EINK_LZ4)
	r = test_lz4_frame_roundtrip();
	LOG_INF("test_lz4_frame_roundtrip: %d", r);
	fails += (r != 0);
#endif
#if defined(CONFIG_APP_EINK_LOCATION)
	r = test_location_telemetry_json();
	LOG_INF("test_location_telemetry_json: %d", r);
	fails += (r != 0);
#endif
	if (fails) {
		LOG_ERR("eink selftest FAILED (%d)", fails);
		return -EFAULT;
	}
	LOG_INF("eink selftest OK");
	return 0;
}
