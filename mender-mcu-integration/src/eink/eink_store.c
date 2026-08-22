/*
 * Host-directory store for native_sim; same API later maps to LittleFS on EVK.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_store.h"

#if defined(CONFIG_APP_EINK_LZ4)
#include "eink_lz4.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(eink_store, LOG_LEVEL_INF);

void eink_prof_flash_io(const char *op, size_t bytes, int64_t ms)
{
	unsigned kib_s = 0;

	if (op == NULL) {
		op = "?";
	}
	if (ms > 0) {
		kib_s = (unsigned)(((uint64_t)bytes * 1000ull) / ((uint64_t)ms * 1024ull));
	}
	LOG_INF("prof: flash_%s=%lld ms bytes=%u (%u KiB/s)", op, (long long)ms, (unsigned)bytes,
		kib_s);
}

static char root[256] = "/tmp/eink-zephyr";

/** Unlink if present; avoid Zephyr ERR logs on missing paths (-ENOENT). */
static void unlink_if_exists(const char *path)
{
	struct fs_dirent entry;

	if (path == NULL || path[0] == '\0') {
		return;
	}
	if (fs_stat(path, &entry) == 0) {
		(void)fs_unlink(path);
	}
}

static int ensure_dir(const char *path)
{
	struct fs_dirent entry;
	int ret;

	ret = fs_stat(path, &entry);
	if (ret == 0) {
		return entry.type == FS_DIR_ENTRY_DIR ? 0 : -ENOTDIR;
	}
	if (ret != -ENOENT) {
		return ret;
	}
	return fs_mkdir(path);
}

int eink_store_init(const char *root_dir)
{
	char img[300];

	if (root_dir != NULL && root_dir[0] != '\0') {
		strncpy(root, root_dir, sizeof(root) - 1);
		root[sizeof(root) - 1] = '\0';
	}
	int ret = ensure_dir(root);

	if (ret != 0) {
		LOG_ERR("create store root %s: %d", root, ret);
		return ret;
	}
	snprintf(img, sizeof(img), "%s/images", root);
	ret = ensure_dir(img);
	if (ret != 0) {
		LOG_ERR("create image directory %s: %d", img, ret);
		return ret;
	}
	LOG_INF("eink store root=%s", root);
	return 0;
}

static void path_state(char *out, size_t n)
{
	snprintf(out, n, "%s/state.json", root);
}

static void path_sched(char *out, size_t n)
{
	snprintf(out, n, "%s/schedule.json", root);
}

static void path_sync_meta(char *out, size_t n)
{
	snprintf(out, n, "%s/sync_meta.json", root);
}

static void path_http_creds(char *out, size_t n)
{
	snprintf(out, n, "%s/creds.json", root);
}

/** Append JSON string (quoted + escaped) into @a out; returns bytes used or -ENOMEM. */
static int json_quote_append(char *out, size_t out_cap, size_t *pos, const char *s)
{
	size_t i = *pos;

	if (i >= out_cap) {
		return -ENOMEM;
	}
	out[i++] = '"';
	for (; s && *s; s++) {
		if (*s == '"' || *s == '\\') {
			if (i + 2 >= out_cap) {
				return -ENOMEM;
			}
			out[i++] = '\\';
			out[i++] = *s;
		} else if ((unsigned char)*s < 0x20) {
			/* Control chars not expected in URLs/tokens — drop. */
			continue;
		} else {
			if (i + 1 >= out_cap) {
				return -ENOMEM;
			}
			out[i++] = *s;
		}
	}
	if (i >= out_cap) {
		return -ENOMEM;
	}
	out[i++] = '"';
	*pos = i;
	return 0;
}

static int json_extract_string(const char *buf, const char *key, char *out, size_t out_cap)
{
	const char *p;
	size_t o = 0;

	if (!buf || !key || !out || out_cap == 0) {
		return -EINVAL;
	}
	out[0] = '\0';
	p = strstr(buf, key);
	if (!p) {
		return -ENOENT;
	}
	p += strlen(key);
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p != '"') {
		return -EINVAL;
	}
	p++;
	while (*p && *p != '"') {
		if (*p == '\\' && p[1]) {
			p++;
		}
		if (o + 1 >= out_cap) {
			return -ENOMEM;
		}
		out[o++] = *p++;
	}
	out[o] = '\0';
	return (*p == '"') ? 0 : -EINVAL;
}

int eink_store_save_last_sync(int64_t unix_sec)
{
	char path[300];
	char tmp[320];
	struct fs_file_t f;
	char json[80];
	int len;
	int ret;

	path_sync_meta(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	len = snprintf(json, sizeof(json), "{\"last_sync_unix\":%lld}\n",
		       (long long)unix_sec);
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

int eink_store_load_last_sync(int64_t *unix_sec)
{
	char path[300];
	char line[96];
	struct fs_file_t f;
	struct fs_dirent entry;
	const char *key = "\"last_sync_unix\":";
	char *p;
	ssize_t n;
	int ret;

	if (unix_sec == NULL) {
		return -EINVAL;
	}
	*unix_sec = 0;
	path_sync_meta(path, sizeof(path));
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
	p = strstr(line, key);
	if (!p) {
		return 0;
	}
	p += strlen(key);
	*unix_sec = (int64_t)strtoll(p, NULL, 10);
	return 0;
}

int eink_store_save_http_creds(const char *api_base, const char *device_id,
			       const char *auth_token)
{
	char path[300];
	char tmp[320];
	char json[768];
	struct fs_file_t f;
	size_t pos = 0;
	int ret;

	path_http_creds(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	if (pos + 13 >= sizeof(json)) {
		return -ENOMEM;
	}
	memcpy(json + pos, "{\"api_base\":", 12);
	pos += 12;
	ret = json_quote_append(json, sizeof(json), &pos, api_base ? api_base : "");
	if (ret) {
		return ret;
	}
	if (pos + 14 >= sizeof(json)) {
		return -ENOMEM;
	}
	memcpy(json + pos, ",\"device_id\":", 13);
	pos += 13;
	ret = json_quote_append(json, sizeof(json), &pos, device_id ? device_id : "");
	if (ret) {
		return ret;
	}
	if (pos + 15 >= sizeof(json)) {
		return -ENOMEM;
	}
	memcpy(json + pos, ",\"auth_token\":", 14);
	pos += 14;
	ret = json_quote_append(json, sizeof(json), &pos, auth_token ? auth_token : "");
	if (ret) {
		return ret;
	}
	if (pos + 3 >= sizeof(json)) {
		return -ENOMEM;
	}
	json[pos++] = '}';
	json[pos++] = '\n';
	json[pos] = '\0';

	fs_file_t_init(&f);
	ret = fs_open(&f, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		return ret;
	}
	ret = fs_write(&f, json, pos);
	if (ret != (int)pos) {
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
	LOG_INF("http creds saved (token not logged)");
	return 0;
}

int eink_store_load_http_creds(char *api_base, size_t api_cap, char *device_id,
			       size_t id_cap, char *auth_token, size_t tok_cap)
{
	char path[300];
	char buf[768];
	struct fs_file_t f;
	struct fs_dirent entry;
	ssize_t n;
	int ret;

	if (!api_base || api_cap == 0 || !device_id || id_cap == 0 || !auth_token ||
	    tok_cap == 0) {
		return -EINVAL;
	}
	api_base[0] = '\0';
	device_id[0] = '\0';
	auth_token[0] = '\0';
	path_http_creds(path, sizeof(path));
	ret = fs_stat(path, &entry);
	if (ret == -ENOENT) {
		return -ENOENT;
	}
	if (ret < 0) {
		return ret;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	(void)fs_close(&f);
	if (n <= 0) {
		return n < 0 ? (int)n : -ENOENT;
	}
	buf[n] = '\0';
	ret = json_extract_string(buf, "\"api_base\":", api_base, api_cap);
	if (ret) {
		return ret;
	}
	ret = json_extract_string(buf, "\"device_id\":", device_id, id_cap);
	if (ret) {
		return ret;
	}
	ret = json_extract_string(buf, "\"auth_token\":", auth_token, tok_cap);
	if (ret) {
		return ret;
	}
	return 0;
}

int eink_store_save_state(const char *last_job_id)
{
	char path[300];
	char tmp[320];
	struct fs_file_t f;
	char json[192];
	int len;
	int ret;

	path_state(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	len = snprintf(json, sizeof(json), "{\"last_displayed_job_id\":\"%s\"}\n",
		       last_job_id ? last_job_id : "");
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

int eink_store_load_state(char *last_job_id, size_t cap)
{
	char path[300];
	char line[256];
	struct fs_file_t f;
	struct fs_dirent entry;
	const char *key = "\"last_displayed_job_id\":\"";
	char *p;
	ssize_t n;
	int ret;

	if (last_job_id == NULL || cap == 0) {
		return -EINVAL;
	}
	last_job_id[0] = '\0';
	path_state(path, sizeof(path));
	ret = fs_stat(path, &entry);
	if (ret == -ENOENT) {
		return 0; /* missing is OK */
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
	p = strstr(line, key);
	if (!p) {
		return 0;
	}
	p += strlen(key);
	for (size_t i = 0; i + 1 < cap && p[i] && p[i] != '"'; i++) {
		last_job_id[i] = p[i];
		last_job_id[i + 1] = '\0';
	}
	return 0;
}

int eink_store_save_schedule(const struct eink_schedule *sched)
{
	char path[300];
	char tmp[320];
	struct fs_file_t f;
	char entry[384];
	int ret;

	if (!sched) {
		return -EINVAL;
	}
	path_sched(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fs_file_t_init(&f);
	ret = fs_open(&f, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		return ret;
	}
	ret = fs_write(&f, "{\"jobs\":[", sizeof("{\"jobs\":[") - 1);
	if (ret != sizeof("{\"jobs\":[") - 1) {
		ret = ret < 0 ? ret : -EIO;
	}
	for (size_t i = 0; ret >= 0 && i < sched->count; i++) {
		const struct eink_job *j = &sched->jobs[i];
		int len = snprintk(entry, sizeof(entry),
				   "%s{\"job_id\":\"%s\",\"image_id\":\"%s\",\"cron\":\"%s\","
				   "\"next_run\":%lld}",
				   i ? "," : "", j->job_id, j->image_id, j->cron,
				   (long long)j->next_run_unix);

		if (len < 0 || (size_t)len >= sizeof(entry)) {
			ret = -ENOMEM;
			break;
		}
		ret = fs_write(&f, entry, len);
		if (ret != len) {
			ret = ret < 0 ? ret : -EIO;
		}
	}
	if (ret >= 0) {
		ret = fs_write(&f, "]}\n", sizeof("]}\n") - 1);
	}
	if (ret == sizeof("]}\n") - 1) {
		ret = fs_sync(&f);
	}
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

int eink_store_load_schedule(struct eink_schedule *sched)
{
	char path[300];
	struct fs_file_t f;
	/* Static: 8 KiB on main stack overflows CONFIG_MAIN_STACK_SIZE=8192 (FRDM hang). */
	static char buf[8192];
	ssize_t n;
	const char *p;
	size_t count = 0;

	if (!sched) {
		return -EINVAL;
	}
	memset(sched, 0, sizeof(*sched));
	path_sched(path, sizeof(path));
	fs_file_t_init(&f);
	n = fs_open(&f, path, FS_O_READ);
	if (n < 0) {
		return (int)n;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	(void)fs_close(&f);
	if (n < 0) {
		return (int)n;
	}
	buf[n] = '\0';

	p = strstr(buf, "\"jobs\"");
	if (p == NULL) {
		return -EINVAL;
	}
	p = strchr(p, '[');
	if (p == NULL) {
		return -EINVAL;
	}
	p++;

	while (*p && count < EINK_MAX_JOBS) {
		char job_id[EINK_ID_MAX];
		char image_id[EINK_ID_MAX];
		char cron[32];
		long long next_run = 0;
		const char *obj;
		const char *end;
		char objbuf[384];
		size_t olen;

		while (*p == ' ' || *p == '\n' || *p == '\r' || *p == ',') {
			p++;
		}
		if (*p == ']') {
			break;
		}
		if (*p != '{') {
			return -EINVAL;
		}
		obj = p;
		end = strchr(obj, '}');
		if (end == NULL) {
			return -EINVAL;
		}
		olen = (size_t)(end - obj + 1);
		if (olen >= sizeof(objbuf)) {
			return -ENOMEM;
		}
		memcpy(objbuf, obj, olen);
		objbuf[olen] = '\0';

		job_id[0] = image_id[0] = cron[0] = '\0';
		{
			const char *k = strstr(objbuf, "\"job_id\"");
			const char *q;

			if (k) {
				q = strchr(k + 8, '"');
				if (q) {
					q++;
					for (size_t i = 0; i + 1 < sizeof(job_id) && q[i] && q[i] != '"';
					     i++) {
						job_id[i] = q[i];
						job_id[i + 1] = '\0';
					}
				}
			}
			k = strstr(objbuf, "\"image_id\"");
			if (k) {
				q = strchr(k + 10, '"');
				if (q) {
					q++;
					for (size_t i = 0; i + 1 < sizeof(image_id) && q[i] &&
							    q[i] != '"';
					     i++) {
						image_id[i] = q[i];
						image_id[i + 1] = '\0';
					}
				}
			}
			k = strstr(objbuf, "\"cron\"");
			if (k) {
				q = strchr(k + 6, '"');
				if (q) {
					q++;
					for (size_t i = 0; i + 1 < sizeof(cron) && q[i] && q[i] != '"';
					     i++) {
						cron[i] = q[i];
						cron[i + 1] = '\0';
					}
				}
			}
			k = strstr(objbuf, "\"next_run\"");
			if (k) {
				k = strchr(k + 10, ':');
				if (k) {
					next_run = strtoll(k + 1, NULL, 10);
				}
			}
		}
		if (job_id[0] == '\0' || image_id[0] == '\0') {
			return -EINVAL;
		}
		strncpy(sched->jobs[count].job_id, job_id, sizeof(sched->jobs[count].job_id) - 1);
		strncpy(sched->jobs[count].image_id, image_id,
			sizeof(sched->jobs[count].image_id) - 1);
		strncpy(sched->jobs[count].cron, cron, sizeof(sched->jobs[count].cron) - 1);
		sched->jobs[count].next_run_unix = (int64_t)next_run;
		count++;
		p = end + 1;
	}
	sched->count = count;
	return 0;
}

int eink_store_image_path(const char *image_id, char *out, size_t out_cap)
{
	if (!image_id || !out) {
		return -EINVAL;
	}
	snprintf(out, out_cap, "%s/images/%s.es6f", root, image_id);
	return 0;
}

int eink_store_image_lz4_path(const char *image_id, char *out, size_t out_cap)
{
	if (!image_id || !out) {
		return -EINVAL;
	}
	snprintf(out, out_cap, "%s/images/%s.es6f.lz4", root, image_id);
	return 0;
}

static int eink_store_content_hash_path(const char *image_id, char *out, size_t out_cap)
{
	if (!image_id || !out) {
		return -EINVAL;
	}
	snprintf(out, out_cap, "%s/images/%s.es6f.lz4.sha", root, image_id);
	return 0;
}

int eink_store_save_content_hash(const char *image_id, const char *sha256_hex,
				 uint32_t byte_size)
{
	char path[300];
	char tmp[320];
	char line[96];
	struct fs_file_t f;
	int ret;
	ssize_t n;
	size_t len;

	if (!image_id || !sha256_hex || strlen(sha256_hex) != 64) {
		return -EINVAL;
	}
	if (eink_store_content_hash_path(image_id, path, sizeof(path)) != 0) {
		return -EINVAL;
	}
	if (strlen(path) + 5 >= sizeof(tmp)) {
		return -ENOMEM;
	}
	memcpy(tmp, path, strlen(path));
	memcpy(tmp + strlen(path), ".tmp", 5);
	snprintk(line, sizeof(line), "%s\n%u\n", sha256_hex, (unsigned)byte_size);
	len = strlen(line);

	fs_file_t_init(&f);
	ret = fs_open(&f, tmp, FS_O_CREATE | FS_O_WRITE);
	if (ret < 0) {
		return ret;
	}
	n = fs_write(&f, line, len);
	(void)fs_close(&f);
	if (n != (ssize_t)len) {
		(void)fs_unlink(tmp);
		return n < 0 ? (int)n : -EIO;
	}
	(void)fs_unlink(path);
	ret = fs_rename(tmp, path);
	if (ret < 0) {
		(void)fs_unlink(tmp);
	}
	return ret;
}

int eink_store_load_content_hash(const char *image_id, char *sha256_hex, size_t cap,
				 uint32_t *byte_size)
{
	char path[300];
	char buf[96];
	struct fs_file_t f;
	ssize_t n;
	int ret;
	char *nl;
	unsigned long size = 0;

	if (!image_id || !sha256_hex || cap < 65) {
		return -EINVAL;
	}
	if (eink_store_content_hash_path(image_id, path, sizeof(path)) != 0) {
		return -EINVAL;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	(void)fs_close(&f);
	if (n < 65) {
		return n < 0 ? (int)n : -EINVAL;
	}
	buf[n] = '\0';
	nl = strchr(buf, '\n');
	if (!nl || (size_t)(nl - buf) != 64) {
		return -EINVAL;
	}
	*nl = '\0';
	for (size_t i = 0; i < 64; i++) {
		char c = buf[i];

		if (c >= 'A' && c <= 'F') {
			buf[i] = (char)(c - 'A' + 'a');
		} else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return -EINVAL;
		}
	}
	memcpy(sha256_hex, buf, 64);
	sha256_hex[64] = '\0';
	if (byte_size) {
		size = strtoul(nl + 1, NULL, 10);
		*byte_size = (uint32_t)size;
	}
	return 0;
}

static bool path_is_file(const char *path)
{
	struct fs_dirent ent;

	return fs_stat(path, &ent) == 0 && ent.type == FS_DIR_ENTRY_FILE && ent.size > 0;
}

int eink_store_resolve_show_path(const char *image_id, char *out, size_t out_cap, bool *is_lz4)
{
	char es6f[300];
	char lz4p[300];

	if (is_lz4) {
		*is_lz4 = false;
	}
	if (eink_store_image_path(image_id, es6f, sizeof(es6f)) != 0) {
		return -EINVAL;
	}
	if (path_is_file(es6f)) {
		if (strlen(es6f) + 1 > out_cap) {
			return -ENOMEM;
		}
		memcpy(out, es6f, strlen(es6f) + 1);
		return 0;
	}
#if defined(CONFIG_APP_EINK_LZ4)
	if (eink_store_image_lz4_path(image_id, lz4p, sizeof(lz4p)) != 0) {
		return -EINVAL;
	}
	if (path_is_file(lz4p)) {
		if (strlen(lz4p) + 1 > out_cap) {
			return -ENOMEM;
		}
		memcpy(out, lz4p, strlen(lz4p) + 1);
		if (is_lz4) {
			*is_lz4 = true;
		}
		return 0;
	}
#else
	ARG_UNUSED(lz4p);
#endif
	return -ENOENT;
}

bool eink_store_has_valid_image(const char *image_id)
{
	char path[300];

	if (eink_store_resolve_show_path(image_id, path, sizeof(path), NULL) != 0) {
		return false;
	}
#if defined(CONFIG_APP_EINK_STORE_QUICK_CACHE_CHECK)
	return eink_store_has_image_quick(image_id);
#else
	{
		bool is_lz4 = false;

		(void)eink_store_resolve_show_path(image_id, path, sizeof(path), &is_lz4);
		if (is_lz4) {
#if defined(CONFIG_APP_EINK_LZ4)
			uint8_t head[8];
			struct fs_file_t f;
			ssize_t n;

			fs_file_t_init(&f);
			if (fs_open(&f, path, FS_O_READ) < 0) {
				return false;
			}
			n = fs_read(&f, head, sizeof(head));
			(void)fs_close(&f);
			return n >= 4 && eink_lz4_is_frame(head, (size_t)n);
#else
			return false;
#endif
		}
		return eink_store_validate_path(path, NULL) == 0;
	}
#endif
}

struct eink_fs_stream {
	struct fs_file_t f;
	bool open;
#if defined(CONFIG_ARCH_POSIX)
	FILE *host;
#endif
};

static int fs_stream_open(struct eink_fs_stream *s, const char *path)
{
	int ret;

	memset(s, 0, sizeof(*s));
#if defined(CONFIG_ARCH_POSIX)
	if (strncmp(path, "/lfs1/", sizeof("/lfs1/") - 1) != 0) {
		s->host = fopen(path, "rb");
		return s->host != NULL ? 0 : -ENOENT;
	}
#endif
	fs_file_t_init(&s->f);
	ret = fs_open(&s->f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	s->open = true;
	return 0;
}

static void fs_stream_close(struct eink_fs_stream *s)
{
#if defined(CONFIG_ARCH_POSIX)
	if (s->host != NULL) {
		fclose(s->host);
		s->host = NULL;
		return;
	}
#endif
	if (s->open) {
		(void)fs_close(&s->f);
		s->open = false;
	}
}

static int fs_stream_read(void *user, void *buf, size_t len)
{
	struct eink_fs_stream *s = user;

#if defined(CONFIG_ARCH_POSIX)
	if (s->host != NULL) {
		size_t n = fread(buf, 1, len, s->host);

		if (n == 0) {
			return ferror(s->host) ? -EIO : 0;
		}
		return (int)n;
	}
#endif
	{
		ssize_t n = fs_read(&s->f, buf, len);

		if (n < 0) {
			return (int)n;
		}
		return (int)n;
	}
}

static int fs_stream_seek(void *user, size_t offset)
{
	struct eink_fs_stream *s = user;

#if defined(CONFIG_ARCH_POSIX)
	if (s->host != NULL) {
		return fseek(s->host, (long)offset, SEEK_SET) == 0 ? 0 : -EIO;
	}
#endif
	return fs_seek(&s->f, (off_t)offset, FS_SEEK_SET);
}

bool eink_store_has_image_quick(const char *image_id)
{
	char path[300];
	bool is_lz4 = false;
	struct fs_dirent ent;
	struct eink_fs_stream stream;
	uint8_t hdr_raw[EINK_FRAME_HEADER_SIZE];
	struct eink_frame_header hdr;
	int ret;
	int n;

	if (eink_store_resolve_show_path(image_id, path, sizeof(path), &is_lz4) != 0) {
		return false;
	}
	ret = fs_stat(path, &ent);
	if (ret < 0 || ent.type != FS_DIR_ENTRY_FILE) {
		return false;
	}
#if defined(CONFIG_APP_EINK_LZ4)
	if (is_lz4) {
		uint8_t head[8];
		struct fs_file_t f;
		ssize_t nr;

		if (ent.size < 16 || ent.size > (768u * 1024u)) {
			return false;
		}
		fs_file_t_init(&f);
		if (fs_open(&f, path, FS_O_READ) < 0) {
			return false;
		}
		nr = fs_read(&f, head, sizeof(head));
		(void)fs_close(&f);
		return nr >= 4 && eink_lz4_is_frame(head, (size_t)nr);
	}
#endif
	if (ent.size != EINK_FRAME_FILE_SIZE) {
		return false;
	}
	ret = fs_stream_open(&stream, path);
	if (ret < 0) {
		return false;
	}
	n = fs_stream_read(&stream, hdr_raw, sizeof(hdr_raw));
	fs_stream_close(&stream);
	if (n != (int)sizeof(hdr_raw)) {
		return false;
	}
	if (eink_frame_header_parse(hdr_raw, sizeof(hdr_raw), &hdr) != 0) {
		return false;
	}
	return hdr.payload_len == EINK_PAYLOAD_LEN;
}

int eink_store_validate_path(const char *path, struct eink_frame_header *out_hdr)
{
	struct eink_fs_stream stream;
	int ret;

	if (path == NULL) {
		return -EINVAL;
	}
	ret = fs_stream_open(&stream, path);
	if (ret < 0) {
		return ret;
	}
	ret = eink_frame_validate_stream(fs_stream_read, fs_stream_seek, &stream, out_hdr);
	fs_stream_close(&stream);
	return ret;
}

int eink_store_accept_temp_image(const char *image_id, const char *temp_path)
{
	char path_es6f[300];
	char path_lz4[300];
	uint8_t head[8];
	struct fs_file_t f;
	ssize_t n;
	int ret;

	if (image_id == NULL || temp_path == NULL) {
		return -EINVAL;
	}
	if (eink_store_image_path(image_id, path_es6f, sizeof(path_es6f)) != 0) {
		return -EINVAL;
	}
	(void)eink_store_image_lz4_path(image_id, path_lz4, sizeof(path_lz4));

	fs_file_t_init(&f);
	ret = fs_open(&f, temp_path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, head, sizeof(head));
	(void)fs_close(&f);
	if (n < 4) {
		return n < 0 ? (int)n : -EINVAL;
	}

#if defined(CONFIG_APP_EINK_LZ4)
	if (eink_lz4_is_frame(head, (size_t)n)) {
#if defined(CONFIG_APP_EINK_LZ4_EXPAND_ON_DISPLAY)
		struct fs_dirent ent;

		ret = fs_stat(temp_path, &ent);
		if (ret < 0 || ent.size < 16 || ent.size > (1024u * 1024u)) {
			LOG_ERR("reject temp LZ4 %s: bad size", image_id);
			return -EINVAL;
		}
		unlink_if_exists(path_es6f);
		unlink_if_exists(path_lz4);
		ret = fs_rename(temp_path, path_lz4);
		if (ret < 0) {
			LOG_ERR("accept rename %s -> %s: %d", temp_path, path_lz4, ret);
			return ret;
		}
		LOG_INF("accepted LZ4 image %s (%u compressed bytes)", image_id,
			(unsigned)ent.size);
		return 0;
#else
		LOG_ERR("LZ4 temp for %s but EXPAND_ON_DOWNLOAD expected expand first",
			image_id);
		return -EINVAL;
#endif
	}
#endif

	{
		struct eink_frame_header hdr;

		ret = eink_store_validate_path(temp_path, &hdr);
		if (ret < 0) {
			LOG_ERR("reject temp ES6F %s: %d", image_id, ret);
			return ret;
		}

		unlink_if_exists(path_es6f);
		unlink_if_exists(path_lz4);
		ret = fs_rename(temp_path, path_es6f);
		if (ret < 0) {
			LOG_ERR("accept rename %s -> %s: %d", temp_path, path_es6f, ret);
			return ret;
		}
		LOG_INF("accepted image %s (%u payload bytes)", image_id,
			(unsigned)hdr.payload_len);
		return 0;
	}
}

int eink_store_materialize_es6f(const char *path, char *out, size_t out_cap)
{
	uint8_t head[8];
	struct fs_file_t f;
	ssize_t n;
	int ret;

	if (path == NULL || out == NULL || out_cap < 16) {
		return -EINVAL;
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, head, sizeof(head));
	(void)fs_close(&f);
	if (n < 4) {
		return n < 0 ? (int)n : -EINVAL;
	}

#if defined(CONFIG_APP_EINK_LZ4)
	if (eink_lz4_is_frame(head, (size_t)n)) {
		char scratch[320];
		int64_t t0 = k_uptime_get();

		snprintf(scratch, sizeof(scratch), "%s/images/.paint.es6f", root);
		LOG_INF("LZ4 materialize for display");
		ret = eink_lz4_decompress_file(path, scratch);
		LOG_INF("prof: lz4_materialize=%lld ms ret=%d",
			(long long)(k_uptime_get() - t0), ret);
		if (ret < 0) {
			return ret;
		}
		if (strlen(scratch) + 1 > out_cap) {
			return -ENOMEM;
		}
		memcpy(out, scratch, strlen(scratch) + 1);
		return 0;
	}
#endif
	if (strlen(path) + 1 > out_cap) {
		return -ENOMEM;
	}
	memcpy(out, path, strlen(path) + 1);
	return 0;
}

int eink_store_put_image(const char *image_id, const uint8_t *es6f, size_t len)
{
	char path[300];
	char tmp[320];
	struct fs_file_t f;
	struct eink_frame_view view;
	int ret;

	if (es6f == NULL) {
		return -EINVAL;
	}
	ret = eink_frame_validate(es6f, len, &view);
	if (ret < 0) {
		return ret;
	}
	if (eink_store_image_path(image_id, path, sizeof(path)) != 0) {
		return -EINVAL;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fs_file_t_init(&f);
	ret = fs_open(&f, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		return ret;
	}
	{
		int64_t t0 = k_uptime_get();

		ret = fs_write(&f, es6f, len);
		if (ret == (int)len) {
			int sync_ret = fs_sync(&f);

			eink_prof_flash_io("write", len, k_uptime_get() - t0);
			ret = sync_ret;
		} else {
			(void)fs_close(&f);
			(void)fs_unlink(tmp);
			return ret < 0 ? ret : -EIO;
		}
	}
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
