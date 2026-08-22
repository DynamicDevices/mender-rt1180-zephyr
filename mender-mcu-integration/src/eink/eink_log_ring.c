/*
 * Circular RAM ring capturing Zephyr log output for on-demand portal upload.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eink_log_ring.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(eink_log_ring, CONFIG_LOG_DEFAULT_LEVEL);

#if defined(CONFIG_APP_EINK_DEBUG_LOG_UPLOAD)

#define RING_SIZE CONFIG_APP_EINK_DEBUG_LOG_RING_SIZE

static uint8_t ring[RING_SIZE];
static size_t ring_head; /* next write index */
static size_t ring_len;
static size_t ring_lines;
static struct k_mutex ring_mu;
static bool ring_ready;

static void ring_putc_locked(uint8_t c)
{
	ring[ring_head] = c;
	ring_head = (ring_head + 1U) % RING_SIZE;
	if (ring_len < RING_SIZE) {
		ring_len++;
	}
	if (c == '\n') {
		ring_lines++;
	}
}

static void ring_write_locked(const uint8_t *data, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		ring_putc_locked(data[i]);
	}
}

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	if (!ring_ready || data == NULL || length == 0) {
		return (int)length;
	}
	k_mutex_lock(&ring_mu, K_FOREVER);
	ring_write_locked(data, length);
	k_mutex_unlock(&ring_mu);
	return (int)length;
}

static uint8_t log_out_buf[256];
LOG_OUTPUT_DEFINE(log_output_eink_ring, char_out, log_out_buf, sizeof(log_out_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	uint32_t flags = log_backend_std_get_flags();

	ARG_UNUSED(backend);
	log_output_msg_process(&log_output_eink_ring, &msg->log, flags);
}

static void panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	log_output_flush(&log_output_eink_ring);
}

static void init_backend(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	k_mutex_init(&ring_mu);
	ring_head = 0;
	ring_len = 0;
	ring_lines = 0;
	ring_ready = true;
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(cnt);
}

const struct log_backend_api log_backend_eink_ring_api = {
	.process = process,
	.panic = panic,
	.init = init_backend,
	.dropped = dropped,
};

LOG_BACKEND_DEFINE(log_backend_eink_ring, log_backend_eink_ring_api, true);

void eink_log_ring_init(void)
{
	/* Backend init runs via LOG_BACKEND_DEFINE; ensure mutex if early. */
	if (!ring_ready) {
		init_backend(NULL);
	}
}

size_t eink_log_ring_snapshot(char *dst, size_t dst_len)
{
	size_t n;
	size_t start;

	if (dst == NULL || dst_len == 0) {
		return 0;
	}
	k_mutex_lock(&ring_mu, K_FOREVER);
	n = MIN(ring_len, dst_len - 1U);
	if (n == 0) {
		dst[0] = '\0';
		k_mutex_unlock(&ring_mu);
		return 0;
	}
	if (ring_len < RING_SIZE) {
		start = 0;
	} else {
		start = ring_head; /* oldest byte when full */
	}
	for (size_t i = 0; i < n; i++) {
		dst[i] = (char)ring[(start + i) % RING_SIZE];
	}
	dst[n] = '\0';
	k_mutex_unlock(&ring_mu);
	return n;
}

size_t eink_log_ring_bytes(void)
{
	size_t n;

	k_mutex_lock(&ring_mu, K_FOREVER);
	n = ring_len;
	k_mutex_unlock(&ring_mu);
	return n;
}

size_t eink_log_ring_line_count(void)
{
	size_t n;

	k_mutex_lock(&ring_mu, K_FOREVER);
	n = ring_lines;
	k_mutex_unlock(&ring_mu);
	return n;
}

void eink_log_ring_redact_inplace(char *buf, size_t len)
{
	char *line = buf;
	char *end = buf + len;

	if (buf == NULL) {
		return;
	}
	while (line < end && *line) {
		char *nl = memchr(line, '\n', (size_t)(end - line));
		size_t line_len = nl ? (size_t)(nl - line) : strlen(line);
		bool kill = false;

		if (line_len >= 9) {
			/* case-insensitive-ish: look for "eink creds" */
			for (size_t i = 0; i + 9 <= line_len; i++) {
				if ((line[i] == 'e' || line[i] == 'E') &&
				    (line[i + 1] == 'i' || line[i + 1] == 'I') &&
				    (line[i + 2] == 'n' || line[i + 2] == 'N') &&
				    (line[i + 3] == 'k' || line[i + 3] == 'K') &&
				    line[i + 4] == ' ' &&
				    (line[i + 5] == 'c' || line[i + 5] == 'C') &&
				    (line[i + 6] == 'r' || line[i + 6] == 'R') &&
				    (line[i + 7] == 'e' || line[i + 7] == 'E') &&
				    (line[i + 8] == 'd' || line[i + 8] == 'D')) {
					kill = true;
					break;
				}
			}
		}
		if (kill) {
			const char *rep = "[redacted: eink creds line]";
			size_t rlen = strlen(rep);
			size_t copy = MIN(rlen, line_len);

			memcpy(line, rep, copy);
			for (size_t i = copy; i < line_len; i++) {
				line[i] = ' ';
			}
		} else {
			/* Bearer <token> */
			char *p = line;
			while (p + 7 <= line + line_len) {
				if ((p[0] == 'B' || p[0] == 'b') &&
				    (p[1] == 'e' || p[1] == 'E') &&
				    (p[2] == 'a' || p[2] == 'A') &&
				    (p[3] == 'r' || p[3] == 'R') &&
				    (p[4] == 'e' || p[4] == 'E') &&
				    (p[5] == 'r' || p[5] == 'R') && p[6] == ' ') {
					char *tok = p + 7;
					while (tok < line + line_len &&
					       *tok != ' ' && *tok != '\r') {
						*tok++ = 'x';
					}
					p = tok;
					continue;
				}
				p++;
			}
		}
		if (!nl) {
			break;
		}
		line = nl + 1;
	}
}

#else /* !CONFIG_APP_EINK_DEBUG_LOG_UPLOAD */

void eink_log_ring_init(void)
{
}

size_t eink_log_ring_snapshot(char *dst, size_t dst_len)
{
	if (dst && dst_len) {
		dst[0] = '\0';
	}
	return 0;
}

size_t eink_log_ring_bytes(void)
{
	return 0;
}

size_t eink_log_ring_line_count(void)
{
	return 0;
}

void eink_log_ring_redact_inplace(char *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
}

#endif /* CONFIG_APP_EINK_DEBUG_LOG_UPLOAD */
