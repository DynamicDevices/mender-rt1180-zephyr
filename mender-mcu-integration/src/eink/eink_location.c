/*
 * Optional device location fix for e-tabelone telemetry.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_location.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_location, LOG_LEVEL_INF);

static struct eink_location_fix cached;
static bool cache_loaded;

static void path_location(char *out, size_t n)
{
	snprintf(out, n, "%s/location.json", CONFIG_APP_EINK_STORE_ROOT);
}

static bool is_finite_d(double v)
{
	/* NaN != NaN; reject ±Inf via magnitude guard. */
	return v == v && v < 1e300 && v > -1e300;
}

static bool ranges_ok(double lat, double lng)
{
	return is_finite_d(lat) && is_finite_d(lng) && lat >= -90.0 && lat <= 90.0 &&
	       lng >= -180.0 && lng <= 180.0;
}

static int write_file_atomic(const char *path, const char *json, int len)
{
	char tmp[320];
	struct fs_file_t f;
	int ret;

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fs_file_t_init(&f);
	ret = fs_open(&f, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		return ret;
	}
	ret = fs_write(&f, json, len);
	if (ret != len) {
		(void)fs_close(&f);
		(void)fs_unlink(tmp);
		return ret < 0 ? ret : -EIO;
	}
	ret = fs_sync(&f);
	(void)fs_close(&f);
	if (ret < 0) {
		(void)fs_unlink(tmp);
		return ret;
	}
	ret = fs_rename(tmp, path);
	if (ret < 0) {
		(void)fs_unlink(tmp);
		return ret;
	}
	return 0;
}

static int parse_number_after(const char *json, const char *key, double *out)
{
	const char *p = strstr(json, key);
	char *end = NULL;
	double v;

	if (!p) {
		return -ENOENT;
	}
	p += strlen(key);
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	v = strtod(p, &end);
	if (end == p || !is_finite_d(v)) {
		return -EINVAL;
	}
	*out = v;
	return 0;
}

static int load_from_store(struct eink_location_fix *out)
{
	char path[300];
	char line[192];
	struct fs_file_t f;
	struct fs_dirent entry;
	ssize_t n;
	int ret;
	double lat = 0.0;
	double lng = 0.0;
	double acc = -1.0;

	memset(out, 0, sizeof(*out));
	out->accuracy_m = -1.0;
	path_location(path, sizeof(path));
	ret = fs_stat(path, &entry);
	if (ret == -ENOENT) {
		return 0;
	}
	if (ret < 0) {
		return ret;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, line, sizeof(line) - 1);
	(void)fs_close(&f);
	if (n <= 0) {
		return n < 0 ? (int)n : 0;
	}
	line[n] = '\0';
	if (parse_number_after(line, "\"latitude\":", &lat) != 0 ||
	    parse_number_after(line, "\"longitude\":", &lng) != 0) {
		LOG_WRN("location.json missing lat/lng");
		return 0;
	}
	if (!ranges_ok(lat, lng)) {
		LOG_WRN("location.json out of range");
		return 0;
	}
	if (parse_number_after(line, "\"location_accuracy_m\":", &acc) != 0 ||
	    !is_finite_d(acc) || acc < 0.0) {
		acc = -1.0;
	}
	out->latitude = lat;
	out->longitude = lng;
	out->accuracy_m = acc;
	out->valid = true;
	return 0;
}

static void ensure_cache(void)
{
	if (cache_loaded) {
		return;
	}
	if (load_from_store(&cached) != 0) {
		memset(&cached, 0, sizeof(cached));
		cached.accuracy_m = -1.0;
	}
	cache_loaded = true;
}

int eink_location_get(struct eink_location_fix *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	ensure_cache();
	*out = cached;
	return 0;
}

int eink_location_set(double latitude, double longitude, double accuracy_m)
{
	char path[300];
	char json[160];
	int len;
	int ret;

	if (!ranges_ok(latitude, longitude)) {
		return -ERANGE;
	}
	if (is_finite_d(accuracy_m) && accuracy_m >= 0.0) {
		len = snprintf(json, sizeof(json),
			       "{\"latitude\":%.7f,\"longitude\":%.7f,"
			       "\"location_accuracy_m\":%.3f}\n",
			       latitude, longitude, accuracy_m);
	} else {
		accuracy_m = -1.0;
		len = snprintf(json, sizeof(json),
			       "{\"latitude\":%.7f,\"longitude\":%.7f}\n", latitude,
			       longitude);
	}
	if (len <= 0 || (size_t)len >= sizeof(json)) {
		return -ENOMEM;
	}
	path_location(path, sizeof(path));
	ret = write_file_atomic(path, json, len);
	if (ret != 0) {
		return ret;
	}
	cached.latitude = latitude;
	cached.longitude = longitude;
	cached.accuracy_m = accuracy_m;
	cached.valid = true;
	cache_loaded = true;
	LOG_INF("location set (WGS84 fix stored)");
	return 0;
}

int eink_location_clear(void)
{
	char path[300];
	struct fs_dirent entry;
	int ret;

	path_location(path, sizeof(path));
	ret = fs_stat(path, &entry);
	if (ret == 0) {
		ret = fs_unlink(path);
		if (ret < 0) {
			return ret;
		}
	} else if (ret != -ENOENT) {
		return ret;
	}
	memset(&cached, 0, sizeof(cached));
	cached.accuracy_m = -1.0;
	cached.valid = false;
	cache_loaded = true;
	LOG_INF("location cleared");
	return 0;
}

int eink_location_to_json_object(char *buf, size_t cap)
{
	struct eink_location_fix fix;
	int n;

	if (buf == NULL || cap == 0) {
		return -EINVAL;
	}
	if (eink_location_get(&fix) != 0 || !fix.valid) {
		if (cap < 3) {
			return -ENOMEM;
		}
		memcpy(buf, "{}", 3);
		return 0;
	}
	if (fix.accuracy_m >= 0.0) {
		n = snprintf(buf, cap,
			     "{\"latitude\":%.7f,\"longitude\":%.7f,"
			     "\"location_accuracy_m\":%.3f}",
			     fix.latitude, fix.longitude, fix.accuracy_m);
	} else {
		n = snprintf(buf, cap, "{\"latitude\":%.7f,\"longitude\":%.7f}",
			     fix.latitude, fix.longitude);
	}
	if (n < 0 || (size_t)n >= cap) {
		return -ENOMEM;
	}
	return 0;
}
