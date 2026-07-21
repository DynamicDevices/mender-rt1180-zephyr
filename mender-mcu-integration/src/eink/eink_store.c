/*
 * Host-directory store for native_sim; same API later maps to LittleFS on EVK.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eink_store, LOG_LEVEL_INF);

static char root[256] = "/tmp/eink-zephyr";

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
	if (ret == len) {
		ret = fs_sync(&f);
	}
	(void)fs_close(&f);
	if (ret < 0 || ret != len) {
		(void)fs_unlink(tmp);
		return ret < 0 ? ret : -EIO;
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
	/* Minimal loader: fixture mode fills schedule in eink_scheduler_init. */
	if (!sched) {
		return -EINVAL;
	}
	sched->count = 0;
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
	char path[300];
	struct eink_frame_header hdr;
	int ret;

	if (image_id == NULL || temp_path == NULL) {
		return -EINVAL;
	}
	if (eink_store_image_path(image_id, path, sizeof(path)) != 0) {
		return -EINVAL;
	}

	ret = eink_store_validate_path(temp_path, &hdr);
	if (ret < 0) {
		LOG_ERR("reject temp ES6F %s: %d", image_id, ret);
		return ret;
	}

	(void)fs_unlink(path);
	ret = fs_rename(temp_path, path);
	if (ret < 0) {
		LOG_ERR("accept rename %s -> %s: %d", temp_path, path, ret);
		return ret;
	}
	LOG_INF("accepted image %s (%u payload bytes)", image_id,
		(unsigned)hdr.payload_len);
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
	ret = fs_write(&f, es6f, len);
	if (ret == (int)len) {
		ret = fs_sync(&f);
	}
	(void)fs_close(&f);
	if (ret < 0 || ret != (int)len) {
		(void)fs_unlink(tmp);
		return ret < 0 ? ret : -EIO;
	}
	ret = fs_rename(tmp, path);
	if (ret < 0) {
		(void)fs_unlink(tmp);
		return ret;
	}
	return 0;
}
