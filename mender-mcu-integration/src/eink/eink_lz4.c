/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stream LZ4 frame → ES6F file. Loads the compressed object into a heap
 * buffer (gallery frames are typically << 960 KiB compressed) then pumps
 * LZ4F_decompress into LittleFS in EINK_LZ4_IO_CHUNK writes.
 */
#include "eink_lz4.h"

#include "eink_frame.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "lz4frame.h"

LOG_MODULE_REGISTER(eink_lz4, LOG_LEVEL_INF);

#ifndef EINK_LZ4_IO_CHUNK
#define EINK_LZ4_IO_CHUNK 8192u
#endif

#ifndef EINK_LZ4_COMPRESSED_MAX
#define EINK_LZ4_COMPRESSED_MAX (768u * 1024u)
#endif

bool eink_lz4_is_frame(const uint8_t *buf, size_t n)
{
	if (buf == NULL || n < 4) {
		return false;
	}
	return buf[0] == EINK_LZ4_FRAME_MAGIC_0 && buf[1] == EINK_LZ4_FRAME_MAGIC_1 &&
	       buf[2] == EINK_LZ4_FRAME_MAGIC_2 && buf[3] == EINK_LZ4_FRAME_MAGIC_3;
}

static bool is_es6f_magic(const uint8_t *buf, size_t n)
{
	uint32_t magic;

	if (buf == NULL || n < 4) {
		return false;
	}
	memcpy(&magic, buf, sizeof(magic));
	return sys_le32_to_cpu(magic) == EINK_FRAME_MAGIC;
}

static int read_all(const char *path, uint8_t **out_buf, size_t *out_len)
{
	struct fs_file_t f;
	struct fs_dirent ent;
	uint8_t *buf;
	size_t off = 0;
	int ret;

	ret = fs_stat(path, &ent);
	if (ret < 0) {
		return ret;
	}
	if (ent.size == 0 || ent.size > EINK_LZ4_COMPRESSED_MAX) {
		LOG_ERR("LZ4 compressed size %u out of range", (unsigned)ent.size);
		return -EFBIG;
	}
	buf = malloc(ent.size);
	if (buf == NULL) {
		return -ENOMEM;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		free(buf);
		return ret;
	}
	while (off < ent.size) {
		ssize_t n = fs_read(&f, buf + off, ent.size - off);

		if (n < 0) {
			ret = (int)n;
			(void)fs_close(&f);
			free(buf);
			return ret;
		}
		if (n == 0) {
			break;
		}
		off += (size_t)n;
	}
	(void)fs_close(&f);
	if (off != ent.size) {
		free(buf);
		return -EIO;
	}
	*out_buf = buf;
	*out_len = off;
	return 0;
}

int eink_lz4_decompress_file(const char *src_path, const char *dst_path)
{
	static uint8_t out_buf[EINK_LZ4_IO_CHUNK];
	struct fs_file_t out;
	LZ4F_dctx *dctx = NULL;
	LZ4F_errorCode_t lret;
	uint8_t *src = NULL;
	size_t src_len = 0;
	size_t src_off = 0;
	size_t total_out = 0;
	int ret;

	if (src_path == NULL || dst_path == NULL) {
		return -EINVAL;
	}

	ret = read_all(src_path, &src, &src_len);
	if (ret < 0) {
		return ret;
	}

	fs_file_t_init(&out);
	ret = fs_open(&out, dst_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		free(src);
		return ret;
	}

	lret = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
	if (LZ4F_isError(lret)) {
		LOG_ERR("LZ4F create: %s", LZ4F_getErrorName(lret));
		ret = -ENOMEM;
		goto out;
	}

	while (src_off < src_len || total_out < EINK_FRAME_FILE_SIZE) {
		size_t dst_size = sizeof(out_buf);
		size_t avail = src_len - src_off;
		size_t consumed = avail;

		lret = LZ4F_decompress(dctx, out_buf, &dst_size, src + src_off, &consumed, NULL);
		if (LZ4F_isError(lret)) {
			LOG_ERR("LZ4F decompress: %s @off=%u", LZ4F_getErrorName(lret),
				(unsigned)src_off);
			ret = -EIO;
			goto out;
		}
		src_off += consumed;
		if (dst_size > 0) {
			ssize_t nw;

			if (total_out + dst_size > EINK_FRAME_FILE_SIZE) {
				ret = -EFBIG;
				goto out;
			}
			nw = fs_write(&out, out_buf, dst_size);
			if (nw != (ssize_t)dst_size) {
				ret = nw < 0 ? (int)nw : -EIO;
				goto out;
			}
			total_out += dst_size;
		}
		if (lret == 0) {
			break;
		}
		/* Need more input but none left — truncated. */
		if (consumed == 0 && dst_size == 0 && src_off >= src_len) {
			LOG_ERR("LZ4F truncated (out=%u)", (unsigned)total_out);
			ret = -EIO;
			goto out;
		}
	}

	ret = fs_sync(&out);
	if (ret < 0) {
		goto out;
	}
	if (total_out != EINK_FRAME_FILE_SIZE) {
		LOG_ERR("LZ4 expand size %u != ES6F %u", (unsigned)total_out,
			(unsigned)EINK_FRAME_FILE_SIZE);
		ret = -EINVAL;
		goto out;
	}
	LOG_INF("LZ4 expand %s -> %s (%u bytes, src=%u)", src_path, dst_path,
		(unsigned)total_out, (unsigned)src_len);
	ret = 0;

out:
	if (dctx != NULL) {
		(void)LZ4F_freeDecompressionContext(dctx);
	}
	(void)fs_close(&out);
	free(src);
	if (ret < 0) {
		(void)fs_unlink(dst_path);
	}
	return ret;
}

int eink_lz4_expand_if_framed(const char *src_path, const char *dst_path)
{
	uint8_t head[8];
	struct fs_file_t f;
	ssize_t n;
	int ret;

	if (src_path == NULL || dst_path == NULL) {
		return -EINVAL;
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, src_path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, head, sizeof(head));
	(void)fs_close(&f);
	if (n < 4) {
		return n < 0 ? (int)n : -EINVAL;
	}

	if (is_es6f_magic(head, (size_t)n)) {
		return 0;
	}
	if (!eink_lz4_is_frame(head, (size_t)n)) {
		LOG_WRN("download neither ES6F nor LZ4 frame (head %02x %02x %02x %02x)",
			head[0], head[1], head[2], head[3]);
		return -ENOTSUP;
	}

	LOG_INF("LZ4 frame detected — expanding");
	ret = eink_lz4_decompress_file(src_path, dst_path);
	return ret < 0 ? ret : 1;
}
