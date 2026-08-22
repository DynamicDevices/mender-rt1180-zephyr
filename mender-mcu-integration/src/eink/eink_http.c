/*
 * e-tabelone HTTP client (config / packed-frame download / telemetry).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Supports:
 *  - https:// / http:// via Zephyr sockets + HTTP client (TLS when available)
 *  - file:// fixtures for local bring-up
 *  - Bearer auth on API calls; omit Bearer for Amazon S3 pre-signed URLs
 *  - Limited redirects for image downloads
 *  - ES6F or LZ4-framed ES6F acceptance (reject JPEG/PNG by magic)
 */
#include "eink_http.h"

#include "eink_frame.h"
#include "eink_scheduler.h"
#include "eink_store.h"
#if defined(CONFIG_APP_EINK_LOCATION)
#include "eink_location.h"
#endif
#if defined(CONFIG_APP_EINK_LZ4)
#include "eink_lz4.h"
#endif

#include <cJSON.h>
#include <errno.h>
#include <mender/alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_SNTP)
#include <zephyr/net/sntp.h>
#endif

LOG_MODULE_REGISTER(eink_http, LOG_LEVEL_INF);

/* Large enough to drain a full TCP window fragment in fewer http_client loops. */
#define EINK_HTTP_RECV_BUF 32768
#define EINK_HTTP_CFG_BUF 24576
#define EINK_HTTP_TIMEOUT_MS (30 * MSEC_PER_SEC)
#define EINK_HTTP_DOWNLOAD_TIMEOUT_MS (120 * MSEC_PER_SEC)
#define EINK_HTTP_MAX_REDIRECTS 3
/* Config JSON with several ~2 KiB S3 URLs needs headroom beyond the shell stack. */
#define EINK_HTTP_STACK_SIZE 49152
#define EINK_HTTP_PRIORITY 6
/* Dedicated arena so config parse does not depend on libc heap fragmentation. */
#ifdef CONFIG_APP_EINK_HTTP_JSON_ARENA_SIZE
#define EINK_CJSON_ARENA_SIZE CONFIG_APP_EINK_HTTP_JSON_ARENA_SIZE
#else
#define EINK_CJSON_ARENA_SIZE (256 * 1024)
#endif

/* Redirect Location headers are short; image URL path+query uses EINK_HTTP_URL_MAX. */
#define EINK_HTTP_LOCATION_MAX 512

struct url_parts {
	bool https;
	char host[128];
	char port[8];
	char path[EINK_HTTP_URL_MAX];
};

struct download_ctx {
	struct fs_file_t *fp;
	size_t written;
	int64_t flash_write_ms;
	int status_code;
	int err;
	bool headers_done;
	bool magic_checked;
	char location[EINK_HTTP_LOCATION_MAX];
	bool location_set;
};

struct body_ctx {
	uint8_t *buf;
	size_t cap;
	size_t len;
	int status_code;
	int err;
	char location[EINK_HTTP_LOCATION_MAX];
	bool location_set;
	char etag[128];
	bool etag_set;
};

struct hdr_scan {
	bool collecting_location;
	char *location;
	size_t location_cap;
	bool *location_set;
	bool collecting_etag;
	char *etag;
	size_t etag_cap;
	bool *etag_set;
};

static struct eink_http_config cfg;
static bool inited;
static bool started;
static struct k_mutex mu;
static struct k_work_q http_q;
static K_THREAD_STACK_DEFINE(http_stack, EINK_HTTP_STACK_SIZE);
static struct k_work_delayable sync_work;
static struct k_work sync_once_work;
static struct k_sem sync_once_done;
static int sync_once_result;

#if defined(CONFIG_APP_EINK_HTTP_CONFIG_CACHE)
static uint32_t cached_config_crc;
static bool config_cache_valid;
static char cached_config_etag[128];
static struct eink_schedule cached_sched;
static struct eink_http_image cached_images[EINK_HTTP_MAX_IMAGES];
static size_t cached_image_count;
static int cached_orientation;
#endif

#if defined(CONFIG_APP_EINK_HTTP_GALLERY_DEFER) && \
	!defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE) && \
	!defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
static struct k_work gallery_work;
static struct {
	struct eink_http_image images[EINK_HTTP_MAX_IMAGES];
	size_t count;
	char skip_id[EINK_ID_MAX];
	bool pending;
} gallery_job;
#endif

#if defined(CONFIG_APP_EINK_HTTP_TELEMETRY_DEFER)
static struct {
	struct eink_schedule sched;
	char last_job[EINK_ID_MAX];
	int64_t next_wake;
	int64_t now;
	bool pending;
	int result;
} telem_job;
static struct k_work telem_work;
static struct k_sem telem_done;
#endif

static uint8_t cjson_arena[EINK_CJSON_ARENA_SIZE] __aligned(8);
static size_t cjson_arena_used;
static K_MUTEX_DEFINE(cjson_hooks_mutex);
static struct k_thread *mender_wq_thread;
static bool mender_wq_suspended;

/*
 * cJSON uses process-global malloc/free hooks. Mender installs
 * mender_malloc/mender_free at client init. The e-ink HTTP path temporarily
 * swaps in a bump arena. Leaving with cJSON_InitHooks(NULL) restored libc
 * free — so the next Mender cJSON_Delete freed mender-heap blocks via the
 * system heap and panicked (os_heap double-free / corruption on
 * mender_work_queue after eink sync).
 */

static void cjson_restore_mender_hooks(void)
{
	cJSON_Hooks hooks = {
		.malloc_fn = mender_malloc,
		.free_fn = mender_free,
	};

	cJSON_InitHooks(&hooks);
}

static void mender_wq_find_cb(const struct k_thread *thread, void *user_data)
{
	struct k_thread **out = user_data;

	if (*out != NULL) {
		return;
	}
	if (strcmp(thread->name, "mender_work_queue") == 0) {
		*out = (struct k_thread *)thread;
	}
}

static void mender_wq_suspend_for_cjson(void)
{
	if (mender_wq_thread == NULL) {
		k_thread_foreach(mender_wq_find_cb, &mender_wq_thread);
	}
	if (mender_wq_thread != NULL && !mender_wq_suspended) {
		k_thread_suspend(mender_wq_thread);
		mender_wq_suspended = true;
	}
}

static void mender_wq_resume_after_cjson(void)
{
	if (mender_wq_thread != NULL && mender_wq_suspended) {
		k_thread_resume(mender_wq_thread);
		mender_wq_suspended = false;
	}
}

static void *cjson_arena_alloc(size_t size)
{
	size_t aligned = (size + 7U) & ~((size_t)7U);

	if (cjson_arena_used + aligned > sizeof(cjson_arena)) {
		return NULL;
	}
	void *p = &cjson_arena[cjson_arena_used];

	cjson_arena_used += aligned;
	return p;
}

static void cjson_arena_free(void *ptr)
{
	ARG_UNUSED(ptr);
	/* Arena is reset as a whole around each parse/print lifetime. */
}

static void cjson_arena_reset(void)
{
	cjson_arena_used = 0;
}

static void cjson_arena_enter(void)
{
	cJSON_Hooks hooks = {
		.malloc_fn = cjson_arena_alloc,
		.free_fn = cjson_arena_free,
	};

	(void)k_mutex_lock(&cjson_hooks_mutex, K_FOREVER);
	mender_wq_suspend_for_cjson();
	cjson_arena_reset();
	cJSON_InitHooks(&hooks);
}

static void cjson_arena_leave(void)
{
	cjson_restore_mender_hooks();
	cjson_arena_reset();
	mender_wq_resume_after_cjson();
	k_mutex_unlock(&cjson_hooks_mutex);
}

static int eink_http_sync_once_inner(void);
static void ensure_http_queue(void);
#if defined(CONFIG_APP_EINK_HTTP_TELEMETRY_DEFER)
static void telem_work_handler(struct k_work *work);
static int queue_deferred_telem(const struct eink_schedule *sched, const char *last_job,
				int64_t next_wake, int64_t now);
#endif
#if defined(CONFIG_APP_EINK_HTTP_GALLERY_DEFER) && \
	!defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE) && \
	!defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
static void gallery_work_handler(struct k_work *work);
#endif

/* S3 pre-signed URLs reject requests when the client clock is in 1970. */
static int ensure_wall_clock(void)
{
	struct timespec tspec;
	int64_t now = (int64_t)time(NULL);

	/* Already past 2024-01-01 — good enough for Amz signature windows. */
	if (now >= 1704067200LL) {
		return 0;
	}

	/*
	 * Prefer a recently persisted sync timestamp over a cold SNTP RTT
	 * (DNS + UDP to pool.ntp.org). Good enough until the next full sync.
	 */
	{
		int64_t last_sync = 0;

		if (eink_store_load_last_sync(&last_sync) == 0 && last_sync >= 1704067200LL) {
			tspec.tv_sec = (time_t)last_sync;
			tspec.tv_nsec = 0;
			if (sys_clock_settime(SYS_CLOCK_REALTIME, &tspec) == 0) {
				LOG_INF("wall clock restored from last_sync %lld",
					(long long)last_sync);
				return 0;
			}
		}
	}

#if defined(CONFIG_SNTP)
	{
		struct sntp_time sntp_time;
		int ret;

		ret = sntp_simple(CONFIG_APP_EINK_HTTP_SNTP_SERVER, 5000, &sntp_time);
		if (ret) {
			LOG_WRN("SNTP sync failed: %d (S3 downloads may 403)", ret);
			return ret;
		}

		tspec.tv_sec = (time_t)sntp_time.seconds;
		tspec.tv_nsec =
			((uint64_t)sntp_time.fraction * (1000ULL * 1000ULL * 1000ULL)) >> 32;
		ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
		if (ret) {
			LOG_WRN("sys_clock_settime failed: %d", ret);
			return ret;
		}
		LOG_INF("wall clock set from SNTP (%s) to %lld",
			CONFIG_APP_EINK_HTTP_SNTP_SERVER, (long long)tspec.tv_sec);
		return 0;
	}
#else
	LOG_WRN("wall clock unset and CONFIG_SNTP=n; S3 pre-signed URLs will 403");
	return 0;
#endif
}

static bool is_s3_url(const char *url)
{
	return url != NULL && strstr(url, ".amazonaws.com") != NULL;
}

static bool looks_like_jpeg_or_png(const uint8_t *buf, size_t n)
{
	if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
		return true;
	}
	if (n >= 8 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') {
		return true;
	}
	return false;
}

static int parse_url(const char *url, struct url_parts *out)
{
	const char *p;
	const char *slash;
	const char *colon;
	size_t host_len;

	if (url == NULL || out == NULL) {
		return -EINVAL;
	}
	memset(out, 0, sizeof(*out));
	strncpy(out->path, "/", sizeof(out->path) - 1);

	if (strncmp(url, "https://", 8) == 0) {
		out->https = true;
		p = url + 8;
		strncpy(out->port, "443", sizeof(out->port) - 1);
	} else if (strncmp(url, "http://", 7) == 0) {
		out->https = false;
		p = url + 7;
		strncpy(out->port, "80", sizeof(out->port) - 1);
	} else {
		return -EINVAL;
	}

	slash = strchr(p, '/');
	host_len = slash ? (size_t)(slash - p) : strlen(p);
	if (host_len == 0 || host_len >= sizeof(out->host)) {
		return -EINVAL;
	}
	memcpy(out->host, p, host_len);
	out->host[host_len] = '\0';

	colon = strchr(out->host, ':');
	if (colon) {
		size_t hlen = (size_t)(colon - out->host);

		if (hlen == 0 || strlen(colon + 1) >= sizeof(out->port)) {
			return -EINVAL;
		}
		strncpy(out->port, colon + 1, sizeof(out->port) - 1);
		out->host[hlen] = '\0';
	}

	if (slash) {
		if (strlen(slash) >= sizeof(out->path)) {
			return -ENOMEM;
		}
		strncpy(out->path, slash, sizeof(out->path) - 1);
	}
	return 0;
}

static void resolve_redirect(const char *base_url, const char *location, char *out, size_t out_cap)
{
	struct url_parts base;

	if (location == NULL || out == NULL || out_cap == 0) {
		return;
	}
	if (strncmp(location, "http://", 7) == 0 || strncmp(location, "https://", 8) == 0) {
		strncpy(out, location, out_cap - 1);
		out[out_cap - 1] = '\0';
		return;
	}
	if (parse_url(base_url, &base) != 0) {
		strncpy(out, location, out_cap - 1);
		out[out_cap - 1] = '\0';
		return;
	}
	if (location[0] == '/') {
		snprintf(out, out_cap, "%s://%s%s", base.https ? "https" : "http", base.host,
			 location);
	} else {
		snprintf(out, out_cap, "%s://%s/%s", base.https ? "https" : "http", base.host,
			 location);
	}
}

static int connect_url(const struct url_parts *u)
{
	struct zsock_addrinfo hints = { 0 };
	struct zsock_addrinfo *res = NULL;
	int sock = -1;
	int ret;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	ret = zsock_getaddrinfo(u->host, u->port, &hints, &res);
	if (ret != 0 || res == NULL) {
		LOG_ERR("DNS/getaddrinfo failed for %s:%s -> %d", u->host, u->port, ret);
		return -EHOSTUNREACH;
	}

	if (u->https) {
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
		/* Match mender-mcu Zephyr TLS sockopts (hostname length excludes NUL). */
#if defined(CONFIG_MENDER_NET_CA_CERTIFICATE_TAG_SECONDARY_ENABLED)
		sec_tag_t tags[] = {
			cfg.tls_sec_tag,
			CONFIG_MENDER_NET_CA_CERTIFICATE_TAG_SECONDARY,
		};
#else
		sec_tag_t tags[] = { cfg.tls_sec_tag };
#endif
#if defined(CONFIG_MENDER_NET_TLS_PEER_VERIFY)
		int verify = CONFIG_MENDER_NET_TLS_PEER_VERIFY;
#else
		int verify = TLS_PEER_VERIFY_REQUIRED;
#endif

		sock = zsock_socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);
		if (sock < 0) {
			ret = -errno;
			LOG_ERR("TLS socket create failed: %d", ret);
			goto out;
		}
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags));
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("TLS_SEC_TAG_LIST failed: %d", ret);
			goto out;
		}
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, u->host, strlen(u->host));
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("TLS_HOSTNAME failed: %d", ret);
			goto out;
		}
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("TLS_PEER_VERIFY failed: %d", ret);
			goto out;
		}
		{
			int cache = TLS_SESSION_CACHE_ENABLED;

			ret = zsock_setsockopt(sock, SOL_TLS, TLS_SESSION_CACHE, &cache,
					       sizeof(cache));
			if (ret < 0) {
				/* Non-fatal: continue with full handshakes. */
				LOG_DBG("TLS_SESSION_CACHE enable failed: %d", -errno);
			}
		}
#else
		ret = -ENOTSUP;
		goto out;
#endif
	} else {
		sock = zsock_socket(res->ai_family, res->ai_socktype, IPPROTO_TCP);
		if (sock < 0) {
			ret = -errno;
			goto out;
		}
	}

	ret = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("connect %s:%s failed: %d", u->host, u->port, ret);
		goto out;
	}
	ret = sock;

out:
	if (ret < 0 && sock >= 0) {
		zsock_close(sock);
	}
	zsock_freeaddrinfo(res);
	return ret;
}

static int body_response_cb(struct http_response *rsp, enum http_final_call final_data,
			    void *user_data)
{
	struct body_ctx *ctx = user_data;

	ARG_UNUSED(final_data);
	if (ctx == NULL) {
		return -EINVAL;
	}
	ctx->status_code = rsp->http_status_code;
	if (rsp->body_frag_start != NULL && rsp->body_frag_len > 0) {
		if (ctx->len + rsp->body_frag_len > ctx->cap) {
			ctx->err = -ENOMEM;
			return -ENOMEM;
		}
		memcpy(ctx->buf + ctx->len, rsp->body_frag_start, rsp->body_frag_len);
		ctx->len += rsp->body_frag_len;
	}
	return 0;
}

static int download_response_cb(struct http_response *rsp, enum http_final_call final_data,
				void *user_data)
{
	struct download_ctx *ctx = user_data;

	ARG_UNUSED(final_data);
	if (ctx == NULL) {
		return -EINVAL;
	}
	ctx->status_code = rsp->http_status_code;
	if (rsp->body_frag_start != NULL && rsp->body_frag_len > 0) {
		if (ctx->fp == NULL) {
			ctx->err = -EIO;
			return -EIO;
		}
		/* Reject JPEG/PNG early — cloud currently serves those, not ES6F. */
		if (!ctx->magic_checked) {
			uint8_t head[8];
			size_t n = rsp->body_frag_len < sizeof(head) ? rsp->body_frag_len
								    : sizeof(head);

			memcpy(head, rsp->body_frag_start, n);
			ctx->magic_checked = true;
			if (looks_like_jpeg_or_png(head, n)) {
				ctx->err = -ENOTSUP;
				return -ENOTSUP;
			}
		}
		{
			int64_t tw = k_uptime_get();
			ssize_t written = fs_write(ctx->fp, rsp->body_frag_start, rsp->body_frag_len);

			ctx->flash_write_ms += k_uptime_get() - tw;
			if (written != (ssize_t)rsp->body_frag_len) {
				ctx->err = -EIO;
				return -EIO;
			}
		}
		ctx->written += rsp->body_frag_len;
	}
	return 0;
}

/* Fix Location capture: parser->data is the http_request. Use a request-local scan. */
static struct hdr_scan g_scan;

static int on_hdr_field2(struct http_parser *parser, const char *at, size_t length)
{
	ARG_UNUSED(parser);
	g_scan.collecting_location = (length == 8 && strncasecmp(at, "Location", 8) == 0);
	g_scan.collecting_etag = (length == 4 && strncasecmp(at, "ETag", 4) == 0);
	return 0;
}

static int on_hdr_value2(struct http_parser *parser, const char *at, size_t length)
{
	ARG_UNUSED(parser);
	if (g_scan.collecting_location && g_scan.location != NULL) {
		if (length >= g_scan.location_cap) {
			length = g_scan.location_cap - 1;
		}
		memcpy(g_scan.location, at, length);
		g_scan.location[length] = '\0';
		if (g_scan.location_set) {
			*g_scan.location_set = true;
		}
		g_scan.collecting_location = false;
		return 0;
	}
	if (g_scan.collecting_etag && g_scan.etag != NULL) {
		/* Trim leading whitespace common in folded header values. */
		while (length > 0 && (*at == ' ' || *at == '\t')) {
			at++;
			length--;
		}
		if (length >= g_scan.etag_cap) {
			length = g_scan.etag_cap - 1;
		}
		memcpy(g_scan.etag, at, length);
		g_scan.etag[length] = '\0';
		if (g_scan.etag_set) {
			*g_scan.etag_set = true;
		}
		g_scan.collecting_etag = false;
		return 0;
	}
	return 0;
}

static int do_http(const char *url, enum http_method method, const char *payload,
		   size_t payload_len, bool send_auth, struct body_ctx *body,
		   struct download_ctx *dl, int32_t timeout_ms, const char *extra_hdr)
{
	/* Static to keep TLS + large URL paths off the shell stack. */
	static struct url_parts parts;
	static uint8_t recv_buf[EINK_HTTP_RECV_BUF];
	struct http_request req;
	struct http_parser_settings parser_cb;
	char auth_hdr[320];
	const char *headers[5];
	size_t h = 0;
	int sock;
	int ret;

	ret = parse_url(url, &parts);
	if (ret) {
		return ret;
	}
	sock = connect_url(&parts);
	if (sock < 0) {
		return sock;
	}

	memset(&req, 0, sizeof(req));
	memset(&parser_cb, 0, sizeof(parser_cb));
	memset(&g_scan, 0, sizeof(g_scan));
	if (body) {
		g_scan.location = body->location;
		g_scan.location_cap = sizeof(body->location);
		g_scan.location_set = &body->location_set;
		g_scan.etag = body->etag;
		g_scan.etag_cap = sizeof(body->etag);
		g_scan.etag_set = &body->etag_set;
	} else if (dl) {
		g_scan.location = dl->location;
		g_scan.location_cap = sizeof(dl->location);
		g_scan.location_set = &dl->location_set;
	}
	parser_cb.on_header_field = on_hdr_field2;
	parser_cb.on_header_value = on_hdr_value2;

	req.method = method;
	req.url = parts.path;
	req.host = parts.host;
	/*
	 * Zephyr's HTTP client emits "Host: name:port" whenever req.port is set.
	 * S3 pre-signed URLs sign Host without a port — omit default 80/443.
	 */
	if ((parts.https && strcmp(parts.port, "443") == 0) ||
	    (!parts.https && strcmp(parts.port, "80") == 0)) {
		req.port = NULL;
	} else {
		req.port = parts.port;
	}
	req.protocol = "HTTP/1.1";
	req.response = body ? body_response_cb : download_response_cb;
	req.recv_buf = recv_buf;
	req.recv_buf_len = sizeof(recv_buf);
	req.http_cb = &parser_cb;
	if (payload != NULL && payload_len > 0) {
		req.payload = payload;
		req.payload_len = payload_len;
		req.content_type_value = "application/json";
	}

	headers[h++] = "Connection: close\r\n";
	headers[h++] = "Accept: */*\r\n";
	if (send_auth && cfg.auth_token[0] != '\0') {
		snprintk(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s\r\n",
			 cfg.auth_token);
		headers[h++] = auth_hdr;
	}
	if (extra_hdr != NULL && extra_hdr[0] != '\0') {
		headers[h++] = extra_hdr;
	}
	headers[h] = NULL;
	req.header_fields = headers;

	ret = http_client_req(sock, &req, timeout_ms, body ? (void *)body : (void *)dl);
	zsock_close(sock);
	if (body && body->err) {
		return body->err;
	}
	if (dl && dl->err) {
		return dl->err;
	}
	if (ret < 0) {
		return ret;
	}
	return 0;
}

static int http_get_body(const char *url, bool send_auth, uint8_t *buf, size_t cap, size_t *out_len,
			 int *status_code, const char *if_none_match, char *etag_out,
			 size_t etag_cap)
{
	static char current[EINK_HTTP_URL_MAX];
	static char next[EINK_HTTP_URL_MAX];
	static char if_none_hdr[160];
	struct body_ctx ctx = {
		.buf = buf,
		.cap = cap,
	};
	int redirects = 0;
	int ret;
	const char *extra = NULL;

	if (if_none_match != NULL && if_none_match[0] != '\0') {
		snprintk(if_none_hdr, sizeof(if_none_hdr), "If-None-Match: %s\r\n",
			 if_none_match);
		extra = if_none_hdr;
	}

	strncpy(current, url, sizeof(current) - 1);
	current[sizeof(current) - 1] = '\0';
	while (true) {
		memset(buf, 0, cap);
		ctx.len = 0;
		ctx.err = 0;
		ctx.status_code = 0;
		ctx.location_set = false;
		ctx.location[0] = '\0';
		ctx.etag_set = false;
		ctx.etag[0] = '\0';
		ret = do_http(current, HTTP_GET, NULL, 0, send_auth, &ctx, NULL,
			      EINK_HTTP_TIMEOUT_MS, extra);
		if (ret) {
			return ret;
		}
		if (status_code) {
			*status_code = ctx.status_code;
		}
		if (etag_out != NULL && etag_cap > 0 && ctx.etag_set) {
			strncpy(etag_out, ctx.etag, etag_cap - 1);
			etag_out[etag_cap - 1] = '\0';
		}
		if ((ctx.status_code == 301 || ctx.status_code == 302 || ctx.status_code == 307 ||
		     ctx.status_code == 308) &&
		    ctx.location_set && redirects < EINK_HTTP_MAX_REDIRECTS) {
			resolve_redirect(current, ctx.location, next, sizeof(next));
			strncpy(current, next, sizeof(current) - 1);
			current[sizeof(current) - 1] = '\0';
			redirects++;
			/* Conditional headers apply to the original resource only. */
			extra = NULL;
			continue;
		}
		if (ctx.status_code == 304) {
			if (out_len) {
				*out_len = 0;
			}
			return 0;
		}
		if (ctx.status_code < 200 || ctx.status_code >= 300) {
			LOG_WRN("HTTP GET %s -> %d", current, ctx.status_code);
			return -EIO;
		}
		if (out_len) {
			*out_len = ctx.len;
		}
		return 0;
	}
}

static int http_download_to_file(const char *url, bool send_auth, const char *path)
{
	static char current[EINK_HTTP_URL_MAX];
	static char next[EINK_HTTP_URL_MAX];
	struct download_ctx ctx = { 0 };
	struct fs_file_t file;
	char tmp[384];
	int redirects = 0;
	int ret;

	strncpy(current, url, sizeof(current) - 1);
	current[sizeof(current) - 1] = '\0';
	if (strlen(path) + 5 >= sizeof(tmp)) {
		return -ENOMEM;
	}
	memcpy(tmp, path, strlen(path));
	memcpy(tmp + strlen(path), ".tmp", 5);

	while (true) {
		fs_file_t_init(&file);
		ret = fs_open(&file, tmp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
		if (ret < 0) {
			return ret;
		}
		ctx.fp = &file;
		ctx.written = 0;
		ctx.flash_write_ms = 0;
		ctx.err = 0;
		ctx.status_code = 0;
		ctx.location_set = false;
		ctx.magic_checked = false;
		ctx.location[0] = '\0';

		ret = do_http(current, HTTP_GET, NULL, 0, send_auth && !is_s3_url(current), NULL,
			      &ctx, EINK_HTTP_DOWNLOAD_TIMEOUT_MS, NULL);
		if (ret == 0) {
			int64_t tw = k_uptime_get();

			ret = fs_sync(ctx.fp);
			ctx.flash_write_ms += k_uptime_get() - tw;
			if (ret == 0) {
				eink_prof_flash_io("write", ctx.written, ctx.flash_write_ms);
			}
		}
		(void)fs_close(ctx.fp);
		ctx.fp = NULL;
		if (ret) {
			if (ret == -ENOTSUP) {
				LOG_ERR("reject JPEG/PNG (packed ES6F only)");
			} else {
				LOG_WRN("download HTTP transport failed: %d", ret);
			}
			(void)fs_unlink(tmp);
			return ret;
		}
		if ((ctx.status_code == 301 || ctx.status_code == 302 || ctx.status_code == 307 ||
		     ctx.status_code == 308) &&
		    ctx.location_set && redirects < EINK_HTTP_MAX_REDIRECTS) {
			(void)fs_unlink(tmp);
			resolve_redirect(current, ctx.location, next, sizeof(next));
			strncpy(current, next, sizeof(current) - 1);
			current[sizeof(current) - 1] = '\0';
			redirects++;
			continue;
		}
		if (ctx.status_code < 200 || ctx.status_code >= 300) {
			/* Keep a short snippet of error body (S3 XML) for diagnosis. */
			char snippet[160];
			size_t snip_len = 0;
			struct fs_file_t errf;

			fs_file_t_init(&errf);
			ret = fs_open(&errf, tmp, FS_O_READ);
			if (ret == 0) {
				ssize_t nr = fs_read(&errf, snippet, sizeof(snippet) - 1);

				(void)fs_close(&errf);
				snip_len = nr > 0 ? (size_t)nr : 0;
				snippet[snip_len] = '\0';
				for (size_t i = 0; i < snip_len; i++) {
					if (snippet[i] < 32 || snippet[i] > 126) {
						snippet[i] = ' ';
					}
				}
			} else {
				snippet[0] = '\0';
			}
			LOG_WRN("download status=%d bytes=%u url_len=%u body=%.120s",
				ctx.status_code, (unsigned)ctx.written, (unsigned)strlen(current),
				snippet);
			(void)fs_unlink(tmp);
			return -EIO;
		}
		ret = fs_rename(tmp, path);
		if (ret < 0) {
			(void)fs_unlink(tmp);
			return ret;
		}
		LOG_INF("downloaded %s (%u bytes)", path, (unsigned)ctx.written);
		return 0;
	}
}

static int http_post_json(const char *url, const char *json)
{
	struct body_ctx ctx = { 0 };
	static uint8_t discard[512];
	int ret;

	ctx.buf = discard;
	ctx.cap = sizeof(discard);
	ret = do_http(url, HTTP_POST, json, strlen(json), true, &ctx, NULL, EINK_HTTP_TIMEOUT_MS,
		      NULL);
	if (ret) {
		return ret;
	}
	if (ctx.status_code < 200 || ctx.status_code >= 300) {
		LOG_ERR("telemetry HTTP %d", ctx.status_code);
		return -EIO;
	}
	return 0;
}

#if defined(CONFIG_APP_EINK_HTTP_V2_SYNC)
static int http_post_json_body(const char *url, const char *json, uint8_t *buf, size_t cap,
			       size_t *out_len)
{
	struct body_ctx ctx = {
		.buf = buf,
		.cap = cap,
	};
	int ret;

	if (!buf || cap == 0) {
		return -EINVAL;
	}
	memset(buf, 0, cap);
	ret = do_http(url, HTTP_POST, json, strlen(json), true, &ctx, NULL, EINK_HTTP_TIMEOUT_MS,
		      NULL);
	if (ret) {
		return ret;
	}
	if (ctx.status_code < 200 || ctx.status_code >= 300) {
		LOG_WRN("HTTP POST %s -> %d", url, ctx.status_code);
		return -EIO;
	}
	if (out_len) {
		*out_len = ctx.len;
	}
	return 0;
}
#endif

static int load_fs_body(const char *path, uint8_t *buf, size_t cap, size_t *out_len);

static int load_file_body(const char *path, uint8_t *buf, size_t cap, size_t *out_len)
{
#if defined(CONFIG_ARCH_POSIX)
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) {
		return -ENOENT;
	}
	n = fread(buf, 1, cap, f);
	fclose(f);
	if (out_len) {
		*out_len = n;
	}
	return 0;
#else
	/* MCU: host paths are not available — use Zephyr FS (LittleFS). */
	return load_fs_body(path, buf, cap, out_len);
#endif
}

static int load_fs_body(const char *path, uint8_t *buf, size_t cap, size_t *out_len)
{
	struct fs_file_t file;
	ssize_t n;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&file, buf, cap);
	(void)fs_close(&file);
	if (n < 0) {
		return (int)n;
	}
	if (out_len) {
		*out_len = (size_t)n;
	}
	return 0;
}

static int parse_config_json(const char *json, size_t json_len, struct eink_schedule *out_sched,
			     struct eink_http_image *images, size_t image_cap, size_t *image_count,
			     int *orientation)
{
	cJSON *root;
	cJSON *sched;
	cJSON *imgs;
	cJSON *ori;
	cJSON *item;

	cjson_arena_enter();
	root = cJSON_ParseWithLength(json, json_len);
	if (!root) {
		const char *err = cJSON_GetErrorPtr();

		LOG_WRN("cJSON_Parse failed at %.40s (arena used %u/%u)", err ? err : "?",
			(unsigned)cjson_arena_used, (unsigned)sizeof(cjson_arena));
		cjson_arena_leave();
		return -EINVAL;
	}

	out_sched->count = 0;
	if (image_count) {
		*image_count = 0;
	}
	if (orientation) {
		*orientation = 0;
	}

	ori = cJSON_GetObjectItemCaseSensitive(root, "orientation");
	if (orientation && cJSON_IsNumber(ori)) {
		*orientation = ori->valueint;
	}

	/* Optional cloud force-sync hint (cleared server-side after this GET). */
	{
		cJSON *sync_now = cJSON_GetObjectItemCaseSensitive(root, "sync_now");

		if (cJSON_IsTrue(sync_now)) {
			LOG_INF("cloud sync_now requested — full sync this wake");
		}
	}

	sched = cJSON_GetObjectItemCaseSensitive(root, "schedule");
	if (cJSON_IsArray(sched)) {
		cJSON_ArrayForEach(item, sched)
		{
			cJSON *job_id = cJSON_GetObjectItemCaseSensitive(item, "job_id");
			cJSON *image_id = cJSON_GetObjectItemCaseSensitive(item, "image_id");
			cJSON *cron = cJSON_GetObjectItemCaseSensitive(item, "cron");
			struct eink_job *j;

			if (out_sched->count >= EINK_MAX_JOBS) {
				break;
			}
			if (!cJSON_IsString(job_id) || !cJSON_IsString(image_id)) {
				continue;
			}
			j = &out_sched->jobs[out_sched->count++];
			memset(j, 0, sizeof(*j));
			strncpy(j->job_id, job_id->valuestring, sizeof(j->job_id) - 1);
			strncpy(j->image_id, image_id->valuestring, sizeof(j->image_id) - 1);
			if (cJSON_IsString(cron) && cron->valuestring[0]) {
				strncpy(j->cron, cron->valuestring, sizeof(j->cron) - 1);
			} else {
				strncpy(j->cron, "0 0 * * *", sizeof(j->cron) - 1);
			}
		}
	}

	imgs = cJSON_GetObjectItemCaseSensitive(root, "images");
	if (images && image_count && cJSON_IsArray(imgs)) {
		cJSON_ArrayForEach(item, imgs)
		{
			cJSON *image_id = cJSON_GetObjectItemCaseSensitive(item, "image_id");
			cJSON *url = cJSON_GetObjectItemCaseSensitive(item, "url");

			if (*image_count >= image_cap) {
				break;
			}
			if (!cJSON_IsString(image_id) || !cJSON_IsString(url)) {
				continue;
			}
			if (strlen(url->valuestring) >= EINK_HTTP_URL_MAX) {
				LOG_WRN("skip oversized image URL for %s", image_id->valuestring);
				continue;
			}
			memset(&images[*image_count], 0, sizeof(images[*image_count]));
			strncpy(images[*image_count].image_id, image_id->valuestring,
				sizeof(images[*image_count].image_id) - 1);
			strncpy(images[*image_count].url, url->valuestring,
				sizeof(images[*image_count].url) - 1);
			(*image_count)++;
		}
	}

	cJSON_Delete(root);
	cjson_arena_leave();
	return 0;
}

int eink_http_init(const struct eink_http_config *c)
{
	if (c == NULL) {
		return -EINVAL;
	}
	k_mutex_init(&mu);
	k_sem_init(&sync_once_done, 0, 1);
#if defined(CONFIG_APP_EINK_HTTP_TELEMETRY_DEFER)
	k_sem_init(&telem_done, 0, 1);
#endif
	cfg = *c;
	if (cfg.tls_sec_tag == 0) {
#if defined(CONFIG_MENDER_NET_CA_CERTIFICATE_TAG_PRIMARY)
		cfg.tls_sec_tag = CONFIG_MENDER_NET_CA_CERTIFICATE_TAG_PRIMARY;
#endif
	}
	if (cfg.poll_interval_seconds == 0) {
		cfg.poll_interval_seconds = 300;
	}
	inited = true;
	LOG_INF("http %s base=%s device=%s tls_tag=%d", cfg.enabled ? "on" : "off", cfg.api_base,
		cfg.device_id, cfg.tls_sec_tag);
	ensure_http_queue();
	return 0;
}

int eink_http_set_credentials(const char *api_base, const char *device_id, const char *auth_token)
{
	if (!inited) {
		return -EINVAL;
	}
	k_mutex_lock(&mu, K_FOREVER);
	if (api_base) {
		strncpy(cfg.api_base, api_base, sizeof(cfg.api_base) - 1);
		cfg.api_base[sizeof(cfg.api_base) - 1] = '\0';
	}
	if (device_id) {
		strncpy(cfg.device_id, device_id, sizeof(cfg.device_id) - 1);
		cfg.device_id[sizeof(cfg.device_id) - 1] = '\0';
	}
	if (auth_token) {
		if (strcmp(auth_token, "none") == 0 || strcmp(auth_token, "-") == 0) {
			cfg.auth_token[0] = '\0';
		} else {
			strncpy(cfg.auth_token, auth_token, sizeof(cfg.auth_token) - 1);
			cfg.auth_token[sizeof(cfg.auth_token) - 1] = '\0';
		}
	}
	cfg.enabled = (cfg.api_base[0] != '\0' && cfg.device_id[0] != '\0');
	k_mutex_unlock(&mu);
#if defined(CONFIG_APP_EINK_HTTP_CREDS_PERSIST)
	{
		int pret = eink_store_save_http_creds(cfg.api_base, cfg.device_id,
						      cfg.auth_token);

		if (pret != 0) {
			LOG_WRN("http creds persist failed: %d", pret);
			return pret;
		}
	}
#endif
	return 0;
}

int eink_http_load_persisted_credentials(void)
{
#if defined(CONFIG_APP_EINK_HTTP_CREDS_PERSIST)
	char api_base[192];
	char device_id[64];
	char auth_token[256];
	int ret;

	if (!inited) {
		return -EINVAL;
	}
	ret = eink_store_load_http_creds(api_base, sizeof(api_base), device_id,
					 sizeof(device_id), auth_token, sizeof(auth_token));
	if (ret == -ENOENT) {
		return 0;
	}
	if (ret != 0) {
		LOG_WRN("http creds load failed: %d", ret);
		return ret;
	}
	/* Apply without re-writing the same file (set_credentials persists). */
	k_mutex_lock(&mu, K_FOREVER);
	strncpy(cfg.api_base, api_base, sizeof(cfg.api_base) - 1);
	cfg.api_base[sizeof(cfg.api_base) - 1] = '\0';
	strncpy(cfg.device_id, device_id, sizeof(cfg.device_id) - 1);
	cfg.device_id[sizeof(cfg.device_id) - 1] = '\0';
	strncpy(cfg.auth_token, auth_token, sizeof(cfg.auth_token) - 1);
	cfg.auth_token[sizeof(cfg.auth_token) - 1] = '\0';
	cfg.enabled = (cfg.api_base[0] != '\0' && cfg.device_id[0] != '\0');
	k_mutex_unlock(&mu);
	LOG_INF("http creds restored from store (token present=%d)",
		cfg.auth_token[0] != '\0');
	return 0;
#else
	return 0;
#endif
}

int eink_http_fetch_config(struct eink_schedule *out_sched, struct eink_http_image *images,
			   size_t image_cap, size_t *image_count, int *orientation)
{
	static uint8_t body[EINK_HTTP_CFG_BUF];
	static char url[EINK_HTTP_URL_MAX];
	static char resp_etag[128];
	size_t n = 0;
	int status = 0;
	int ret;
	const char *if_none = NULL;

	if (!inited || !cfg.enabled || out_sched == NULL) {
		return -EINVAL;
	}

	resp_etag[0] = '\0';

	if (strncmp(cfg.api_base, "file://", 7) == 0) {
		char path[768];

		snprintf(path, sizeof(path), "%s/%s.config.json", cfg.api_base + 7, cfg.device_id);
		ret = load_file_body(path, body, sizeof(body) - 1, &n);
		if (ret) {
			return ret;
		}
		body[n] = '\0';
	} else {
		snprintf(url, sizeof(url), "%s/node/v0/device/%s/config", cfg.api_base,
			 cfg.device_id);
		LOG_INF("GET %s", url);
#if defined(CONFIG_APP_EINK_HTTP_CONFIG_CACHE)
		if (config_cache_valid && cached_config_etag[0] != '\0') {
			if_none = cached_config_etag;
		}
#endif
		ret = http_get_body(url, true, body, sizeof(body) - 1, &n, &status, if_none,
				    resp_etag, sizeof(resp_etag));
		if (ret) {
			LOG_WRN("config HTTP transport failed: %d status=%d len=%u", ret, status,
				(unsigned)n);
			return ret;
		}
		if (status == 304) {
#if defined(CONFIG_APP_EINK_HTTP_CONFIG_CACHE)
			if (config_cache_valid) {
				memcpy(out_sched, &cached_sched, sizeof(*out_sched));
				if (images != NULL && image_count != NULL) {
					size_t copy = MIN(image_cap, cached_image_count);

					memcpy(images, cached_images, copy * sizeof(images[0]));
					*image_count = copy;
				}
				if (orientation) {
					*orientation = cached_orientation;
				}
				LOG_INF("config 304 Not Modified — reusing %u jobs / %u images",
					(unsigned)out_sched->count,
					image_count ? (unsigned)*image_count : 0U);
				return 0;
			}
#endif
			LOG_WRN("config 304 without local cache");
			return -ENOENT;
		}
		body[n] = '\0';
	}

#if defined(CONFIG_APP_EINK_HTTP_CONFIG_CACHE)
	{
		uint32_t crc = crc32_ieee(body, n);

		if (config_cache_valid && crc == cached_config_crc) {
			memcpy(out_sched, &cached_sched, sizeof(*out_sched));
			if (images != NULL && image_count != NULL) {
				size_t copy = MIN(image_cap, cached_image_count);

				memcpy(images, cached_images, copy * sizeof(images[0]));
				*image_count = copy;
			}
			if (orientation) {
				*orientation = cached_orientation;
			}
			if (resp_etag[0] != '\0') {
				strncpy(cached_config_etag, resp_etag,
					sizeof(cached_config_etag) - 1);
			}
			LOG_INF("config unchanged (crc=0x%08x) — reusing %u jobs / %u images",
				crc, (unsigned)out_sched->count,
				image_count ? (unsigned)*image_count : 0U);
			return 0;
		}
	}
#endif

	ret = parse_config_json((const char *)body, n, out_sched, images, image_cap, image_count,
				orientation);
	if (ret) {
		char debug_path[320];
		struct fs_file_t df;
		int open_ret;

		LOG_WRN("config JSON parse failed: %d len=%u status=%d", ret, (unsigned)n, status);
		snprintf(debug_path, sizeof(debug_path), "%s/config-fail.json",
			 CONFIG_APP_EINK_STORE_ROOT);
		fs_file_t_init(&df);
		open_ret = fs_open(&df, debug_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
		if (open_ret == 0) {
			(void)fs_write(&df, body, n);
			(void)fs_close(&df);
		}
		return ret;
	}
#if defined(CONFIG_APP_EINK_HTTP_CONFIG_CACHE)
	cached_config_crc = crc32_ieee(body, n);
	memcpy(&cached_sched, out_sched, sizeof(cached_sched));
	cached_image_count = image_count ? *image_count : 0;
	if (cached_image_count > ARRAY_SIZE(cached_images)) {
		cached_image_count = ARRAY_SIZE(cached_images);
	}
	if (images != NULL && cached_image_count > 0) {
		memcpy(cached_images, images, cached_image_count * sizeof(cached_images[0]));
	}
	cached_orientation = orientation ? *orientation : 0;
	if (resp_etag[0] != '\0') {
		strncpy(cached_config_etag, resp_etag, sizeof(cached_config_etag) - 1);
		cached_config_etag[sizeof(cached_config_etag) - 1] = '\0';
	}
	config_cache_valid = true;
#endif
	LOG_INF("parsed %u jobs / %u images", (unsigned)out_sched->count,
		image_count ? (unsigned)*image_count : 0U);
	return 0;
}

static int copy_path_chunked(const char *src, const char *dst)
{
	uint8_t chunk[EINK_STREAM_CHUNK_SIZE];
	struct fs_file_t in;
	struct fs_file_t out;
	int ret;
	ssize_t n;

#if defined(CONFIG_ARCH_POSIX)
	if (strncmp(src, "/lfs1/", sizeof("/lfs1/") - 1) != 0) {
		FILE *hf = fopen(src, "rb");
		size_t rn;

		if (hf == NULL) {
			return -ENOENT;
		}
		fs_file_t_init(&out);
		ret = fs_open(&out, dst, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
		if (ret < 0) {
			fclose(hf);
			return ret;
		}
		while ((rn = fread(chunk, 1, sizeof(chunk), hf)) > 0) {
			ret = fs_write(&out, chunk, rn);
			if (ret != (int)rn) {
				ret = ret < 0 ? ret : -EIO;
				(void)fs_close(&out);
				fclose(hf);
				(void)fs_unlink(dst);
				return ret;
			}
		}
		ret = ferror(hf) ? -EIO : fs_sync(&out);
		(void)fs_close(&out);
		fclose(hf);
		if (ret < 0) {
			(void)fs_unlink(dst);
		}
		return ret;
	}
#endif

	fs_file_t_init(&in);
	fs_file_t_init(&out);
	ret = fs_open(&in, src, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	ret = fs_open(&out, dst, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		(void)fs_close(&in);
		return ret;
	}
	while ((n = fs_read(&in, chunk, sizeof(chunk))) > 0) {
		ret = fs_write(&out, chunk, (size_t)n);
		if (ret != (int)n) {
			ret = ret < 0 ? ret : -EIO;
			(void)fs_close(&out);
			(void)fs_close(&in);
			(void)fs_unlink(dst);
			return ret;
		}
	}
	ret = (n < 0) ? (int)n : fs_sync(&out);
	(void)fs_close(&out);
	(void)fs_close(&in);
	if (ret < 0) {
		(void)fs_unlink(dst);
	}
	return ret;
}

int eink_http_download_image(const char *image_id, const char *url)
{
	return eink_http_download_image_hashed(image_id, url, NULL, 0);
}

int eink_http_download_image_hashed(const char *image_id, const char *url,
				    const char *content_sha256, uint32_t byte_size)
{
	char path[300];
	char tmp_path[384];
	size_t n = 0;
	int ret;
	uint8_t magic[8];

	if (!image_id || !url) {
		return -EINVAL;
	}
	if (eink_store_image_path(image_id, path, sizeof(path)) != 0) {
		return -EINVAL;
	}
	if (strlen(path) + 10 >= sizeof(tmp_path)) {
		return -ENOMEM;
	}
	memcpy(tmp_path, path, strlen(path));
	memcpy(tmp_path + strlen(path), ".download", 10);

	if (strncmp(url, "file://", 7) == 0) {
		LOG_INF("importing fixture image %s", image_id);
		ret = copy_path_chunked(url + 7, tmp_path);
		if (ret) {
			return ret;
		}
#if defined(CONFIG_APP_EINK_LZ4)
		{
			uint8_t head[8];
			size_t hn = 0;

			ret = load_fs_body(tmp_path, head, sizeof(head), &hn);
			if (ret) {
				(void)fs_unlink(tmp_path);
				return ret;
			}
			if (looks_like_jpeg_or_png(head, hn)) {
				LOG_ERR("reject JPEG/PNG for image %s (packed ES6F only)",
					image_id);
				(void)fs_unlink(tmp_path);
				return -ENOTSUP;
			}
			if (eink_lz4_is_frame(head, hn)) {
#if defined(CONFIG_APP_EINK_LZ4_EXPAND_ON_DOWNLOAD)
				char unc_path[384];
				int64_t t_lz4;
				int expanded;

				if (strlen(tmp_path) + 5 >= sizeof(unc_path)) {
					(void)fs_unlink(tmp_path);
					return -ENOMEM;
				}
				memcpy(unc_path, tmp_path, strlen(tmp_path));
				memcpy(unc_path + strlen(tmp_path), ".unc", 5);
				LOG_INF("LZ4 frame detected for %s — expanding on download",
					image_id);
				t_lz4 = k_uptime_get();
				expanded = eink_lz4_expand_if_framed(tmp_path, unc_path);
				t_lz4 = k_uptime_get() - t_lz4;
				if (expanded < 0) {
					LOG_WRN("LZ4 expand failed: %d (%lld ms)", expanded,
						(long long)t_lz4);
					(void)fs_unlink(tmp_path);
					return expanded;
				}
				if (expanded == 1) {
					(void)fs_unlink(tmp_path);
					ret = fs_rename(unc_path, tmp_path);
					if (ret < 0) {
						(void)fs_unlink(unc_path);
						return ret;
					}
					LOG_INF("prof: image %s lz4_expand=%lld ms", image_id,
						(long long)t_lz4);
				}
#else
				LOG_INF("LZ4 frame kept compressed for %s (expand-on-display)",
					image_id);
#endif
			}
		}
#endif
	} else if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
		int64_t t0 = k_uptime_get();
		int64_t t_http;
		int64_t t_val;

		LOG_INF("downloading image %s", image_id);
		ret = http_download_to_file(url, true, tmp_path);
		t_http = k_uptime_get() - t0;
		if (ret) {
			LOG_WRN("download transport failed: %d (http %lld ms)", ret,
				(long long)t_http);
			return ret;
		}
		ret = load_fs_body(tmp_path, magic, sizeof(magic), &n);
		if (ret) {
			(void)fs_unlink(tmp_path);
			return ret;
		}
		if (looks_like_jpeg_or_png(magic, n)) {
			LOG_ERR("reject JPEG/PNG for image %s (packed ES6F only)", image_id);
			(void)fs_unlink(tmp_path);
			return -ENOTSUP;
		}
#if defined(CONFIG_APP_EINK_LZ4)
		if (eink_lz4_is_frame(magic, n)) {
#if defined(CONFIG_APP_EINK_LZ4_EXPAND_ON_DOWNLOAD)
			char unc_path[384];
			int64_t t_lz4;
			int expanded;

			if (strlen(tmp_path) + 5 >= sizeof(unc_path)) {
				(void)fs_unlink(tmp_path);
				return -ENOMEM;
			}
			memcpy(unc_path, tmp_path, strlen(tmp_path));
			memcpy(unc_path + strlen(tmp_path), ".unc", 5);
			LOG_INF("LZ4 frame detected for %s — expanding on download", image_id);
			t_lz4 = k_uptime_get();
			expanded = eink_lz4_expand_if_framed(tmp_path, unc_path);
			t_lz4 = k_uptime_get() - t_lz4;
			if (expanded < 0) {
				LOG_WRN("LZ4 expand failed: %d (%lld ms)", expanded,
					(long long)t_lz4);
				(void)fs_unlink(tmp_path);
				return expanded;
			}
			if (expanded == 1) {
				(void)fs_unlink(tmp_path);
				memcpy(tmp_path, unc_path, strlen(unc_path) + 1);
				LOG_INF("prof: image %s lz4_expand=%lld ms", image_id,
					(long long)t_lz4);
			}
#else
			LOG_INF("LZ4 frame kept compressed for %s (expand-on-display)", image_id);
#endif
		}
#endif
		LOG_INF("validating downloaded image %s", image_id);
		t_val = k_uptime_get();
		ret = eink_store_accept_temp_image(image_id, tmp_path);
		t_val = k_uptime_get() - t_val;
		if (ret) {
			(void)fs_unlink(tmp_path);
		} else if (content_sha256 && strlen(content_sha256) == 64) {
			char sha[65];

			for (size_t i = 0; i < 64; i++) {
				char c = content_sha256[i];

				sha[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
			}
			sha[64] = '\0';
			(void)eink_store_save_content_hash(image_id, sha, byte_size);
		}
		LOG_INF("prof: image %s http=%lld ms validate=%lld ms total=%lld ms ret=%d",
			image_id, (long long)t_http, (long long)t_val,
			(long long)(k_uptime_get() - t0), ret);
		return ret;
	} else {
		return -EINVAL;
	}

	LOG_INF("validating downloaded image %s", image_id);
	ret = eink_store_accept_temp_image(image_id, tmp_path);
	if (ret) {
		(void)fs_unlink(tmp_path);
	} else if (content_sha256 && strlen(content_sha256) == 64) {
		char sha[65];

		for (size_t i = 0; i < 64; i++) {
			char c = content_sha256[i];

			sha[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
		}
		sha[64] = '\0';
		(void)eink_store_save_content_hash(image_id, sha, byte_size);
	}
	return ret;
}

static void unix_to_ymd_hms(int64_t t, int *Y, int *M, int *D, int *h, int *m, int *s)
{
	int64_t days = t / 86400;
	int64_t rem = t % 86400;
	int y;

	if (rem < 0) {
		rem += 86400;
		days -= 1;
	}
	*h = (int)(rem / 3600);
	*m = (int)((rem % 3600) / 60);
	*s = (int)(rem % 60);
	days += 719468;
	int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	uint32_t doe = (uint32_t)(days - era * 146097);
	uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = (int)(yoe) + (int)(era * 400);
	uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	uint32_t mp = (5 * doy + 2) / 153;
	*D = (int)(doy - (153 * mp + 2) / 5 + 1);
	*M = (int)(mp < 10 ? mp + 3 : mp - 9);
	*Y = y + (*M <= 2);
}

static void iso8601_utc(int64_t unix_s, char *out, size_t out_cap)
{
	int Y, M, D, h, m, s;

	unix_to_ymd_hms(unix_s, &Y, &M, &D, &h, &m, &s);
	snprintf(out, out_cap, "%04d-%02d-%02dT%02d:%02d:%02dZ", Y, M, D, h, m, s);
}

/*
 * Optional constrained-board pressure metrics. Omit keys when a probe fails
 * (same pattern as GNSS). Wire contract: EINK-CONTRACT.md + cloud handoff.
 */
static void telemetry_add_resource_metrics(cJSON *telemetry)
{
#if defined(CONFIG_APP_EINK_RESOURCE_TELEMETRY)
	struct fs_statvfs vfs;
	int ret;

	if (!telemetry) {
		return;
	}

	ret = fs_statvfs(CONFIG_APP_EINK_STORE_ROOT, &vfs);
	if (ret == 0 && vfs.f_frsize > 0U) {
		uint64_t total = (uint64_t)vfs.f_blocks * (uint64_t)vfs.f_frsize;
		uint64_t free_b = (uint64_t)vfs.f_bfree * (uint64_t)vfs.f_frsize;

		cJSON_AddNumberToObject(telemetry, "storage_total_bytes", (double)total);
		cJSON_AddNumberToObject(telemetry, "storage_free_bytes", (double)free_b);
	} else if (ret != 0) {
		LOG_DBG("fs_statvfs(%s): %d", CONFIG_APP_EINK_STORE_ROOT, ret);
	}

#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS) && defined(CONFIG_HEAP_MEM_POOL_SIZE)
	{
		extern struct k_heap _system_heap;
		struct sys_memory_stats st;

		ret = sys_heap_runtime_stats_get(&_system_heap.heap, &st);
		if (ret == 0) {
			cJSON_AddNumberToObject(telemetry, "ram_heap_free_bytes",
						(double)st.free_bytes);
			cJSON_AddNumberToObject(telemetry, "ram_heap_used_bytes",
						(double)st.allocated_bytes);
			cJSON_AddNumberToObject(telemetry, "ram_heap_max_used_bytes",
						(double)st.max_allocated_bytes);
			cJSON_AddNumberToObject(telemetry, "ram_heap_pool_bytes",
						(double)CONFIG_HEAP_MEM_POOL_SIZE);
		}
	}
#endif
#else
	ARG_UNUSED(telemetry);
#endif
}

int eink_http_post_telemetry(const struct eink_schedule *sched,
			     const char *current_displayed_job_id, int64_t next_wakeup_unix,
			     int battery_capacity)
{
	char url[EINK_HTTP_URL_MAX];
	char wake[40];
	cJSON *root;
	cJSON *telemetry;
	cJSON *schedule;
	char *json;
	int ret = 0;

	if (!inited || !cfg.enabled) {
		return -EINVAL;
	}

	iso8601_utc(next_wakeup_unix > 0 ? next_wakeup_unix : (int64_t)time(NULL) + 300, wake,
		    sizeof(wake));

	cjson_arena_enter();
	root = cJSON_CreateObject();
	telemetry = cJSON_AddObjectToObject(root, "telemetry");
	schedule = cJSON_AddArrayToObject(root, "schedule");
	cJSON_AddNumberToObject(telemetry, "battery_capacity", battery_capacity);
	cJSON_AddStringToObject(telemetry, "next_wakeup_date", wake);
	if (current_displayed_job_id && current_displayed_job_id[0]) {
		cJSON_AddStringToObject(telemetry, "current_displayed_job_id",
					current_displayed_job_id);
	}
#if defined(CONFIG_APP_EINK_LOCATION)
	{
		struct eink_location_fix fix;

		if (eink_location_get(&fix) == 0 && fix.valid) {
			cJSON_AddNumberToObject(telemetry, "latitude", fix.latitude);
			cJSON_AddNumberToObject(telemetry, "longitude", fix.longitude);
			if (fix.accuracy_m >= 0.0) {
				cJSON_AddNumberToObject(telemetry, "location_accuracy_m",
							fix.accuracy_m);
			}
		}
	}
#endif
	telemetry_add_resource_metrics(telemetry);
	if (sched) {
		for (size_t i = 0; i < sched->count; i++) {
			cJSON *ack = cJSON_CreateObject();

			cJSON_AddStringToObject(ack, "job_id", sched->jobs[i].job_id);
			cJSON_AddItemToArray(schedule, ack);
		}
	}
	json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!json) {
		cjson_arena_leave();
		return -ENOMEM;
	}

	if (strncmp(cfg.api_base, "file://", 7) == 0) {
		char path[300];
		struct fs_file_t f;

		snprintf(path, sizeof(path), "%s/telemetry.log", CONFIG_APP_EINK_STORE_ROOT);
		fs_file_t_init(&f);
		ret = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
		if (ret < 0) {
			LOG_WRN("open fixture telemetry log: %d", ret);
		} else {
			ssize_t nw = fs_write(&f, json, strlen(json));

			if (nw == (ssize_t)strlen(json)) {
				nw = fs_write(&f, "\n", 1);
			}
			(void)fs_close(&f);
			ret = nw == 1 ? 0 : (nw < 0 ? (int)nw : -EIO);
			if (ret == 0) {
				LOG_INF("telemetry posted (fixture log)");
			}
		}
	} else {
		snprintf(url, sizeof(url), "%s/node/v0/device/%s/telemetry", cfg.api_base,
			 cfg.device_id);
		LOG_INF("POST %s", url);
		ret = http_post_json(url, json);
		if (ret == 0) {
			LOG_INF("telemetry posted");
		}
	}

	cJSON_free(json);
	cjson_arena_leave();
	return ret;
}

static void ensure_http_queue(void)
{
	static bool queue_ready;

	if (queue_ready) {
		return;
	}
	k_work_queue_init(&http_q);
	k_work_queue_start(&http_q, http_stack, K_THREAD_STACK_SIZEOF(http_stack),
			   EINK_HTTP_PRIORITY, NULL);
	k_thread_name_set(&http_q.thread, "eink_http");
	queue_ready = true;
}

static void sync_once_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	sync_once_result = eink_http_sync_once_inner();
	k_sem_give(&sync_once_done);
}

#if defined(CONFIG_APP_EINK_HTTP_TELEMETRY_DEFER)
static void telem_work_handler(struct k_work *work)
{
	int64_t t0 = k_uptime_get();

	ARG_UNUSED(work);
	if (!telem_job.pending) {
		k_sem_give(&telem_done);
		return;
	}
	telem_job.result = eink_http_post_telemetry(&telem_job.sched, telem_job.last_job,
						    telem_job.next_wake, -1);
	if (telem_job.result == 0) {
		(void)eink_store_save_last_sync(telem_job.now);
	}
	telem_job.pending = false;
	LOG_INF("prof: telem deferred done ret=%d in %lld ms", telem_job.result,
		(long long)(k_uptime_get() - t0));
	k_sem_give(&telem_done);
}

static int queue_deferred_telem(const struct eink_schedule *sched, const char *last_job,
				int64_t next_wake, int64_t now)
{
	ensure_http_queue();
	if (k_work_busy_get(&telem_work) != 0 || telem_job.pending) {
		LOG_WRN("telem defer busy — posting inline");
		return eink_http_post_telemetry(sched, last_job, next_wake, -1);
	}
	memset(&telem_job, 0, sizeof(telem_job));
	if (sched != NULL) {
		memcpy(&telem_job.sched, sched, sizeof(telem_job.sched));
	}
	if (last_job != NULL) {
		strncpy(telem_job.last_job, last_job, sizeof(telem_job.last_job) - 1);
	}
	telem_job.next_wake = next_wake;
	telem_job.now = now;
	telem_job.pending = true;
	telem_job.result = 0;
	(void)k_sem_take(&telem_done, K_NO_WAIT);
	k_work_init(&telem_work, telem_work_handler);
	(void)k_work_submit_to_queue(&http_q, &telem_work);
	LOG_INF("telemetry deferred");
	return 0;
}

int eink_http_flush_deferred(k_timeout_t timeout)
{
	if (!telem_job.pending && k_work_busy_get(&telem_work) == 0) {
		return telem_job.result;
	}
	if (k_sem_take(&telem_done, timeout) != 0) {
		LOG_WRN("telem flush timed out");
		return -ETIMEDOUT;
	}
	return telem_job.result;
}
#else
int eink_http_flush_deferred(k_timeout_t timeout)
{
	ARG_UNUSED(timeout);
	return 0;
}
#endif

#if defined(CONFIG_APP_EINK_HTTP_GALLERY_DEFER) && \
	!defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE) && \
	!defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
static void gallery_work_handler(struct k_work *work)
{
	int64_t t0 = k_uptime_get();
	size_t ok = 0;
	size_t fail = 0;

	ARG_UNUSED(work);
	if (!gallery_job.pending) {
		return;
	}
	LOG_INF("gallery cache start (%u images)", (unsigned)gallery_job.count);
	for (size_t i = 0; i < gallery_job.count; i++) {
		int ret;

		if (gallery_job.skip_id[0] != '\0' &&
		    strcmp(gallery_job.images[i].image_id, gallery_job.skip_id) == 0) {
			continue;
		}
		if (eink_store_has_valid_image(gallery_job.images[i].image_id)) {
			LOG_INF("gallery image %s already cached",
				gallery_job.images[i].image_id);
			continue;
		}
		ret = eink_http_download_image(gallery_job.images[i].image_id,
					       gallery_job.images[i].url);
		if (ret) {
			LOG_WRN("gallery image %s download failed: %d",
				gallery_job.images[i].image_id, ret);
			fail++;
		} else {
			ok++;
		}
	}
	gallery_job.pending = false;
	LOG_INF("prof: gallery deferred done ok=%u fail=%u in %lld ms", (unsigned)ok,
		(unsigned)fail, (long long)(k_uptime_get() - t0));
}
#endif

/* Heavy sync (TLS + large JSON + downloads) always runs on http_q. */

#if defined(CONFIG_APP_EINK_HTTP_FAST_PATH)
static bool sync_is_fresh(int64_t now, int64_t last_sync)
{
	uint32_t max_age = CONFIG_APP_EINK_HTTP_SYNC_MAX_AGE_SEC;

	if (now < 1700000000LL || last_sync <= 0) {
		return false;
	}
	return (now - last_sync) < (int64_t)max_age;
}

bool eink_http_radio_sync_needed(void)
{
	char due[EINK_ID_MAX];
	int64_t last_sync = 0;
	int64_t now;
	int due_ret;

	if (!inited || !cfg.enabled) {
		return false;
	}
	now = (int64_t)time(NULL);
	(void)eink_store_load_last_sync(&last_sync);
	due_ret = eink_scheduler_due_image(due, sizeof(due));
	if (due_ret < 0) {
		return true;
	}
	/* New job to acknowledge / possibly download — radio required. */
	if (due_ret > 0) {
		return true;
	}
	/* Nothing new due: skip radio while last sync is still fresh. */
	return !sync_is_fresh(now, last_sync);
}

static int sync_telem_only(int64_t t_sync)
{
	struct eink_schedule sched;
	char last_job[EINK_ID_MAX];
	int64_t now = (int64_t)time(NULL);
	int64_t next_wake;
	int64_t t_mark;
	int64_t ms_paint = 0;
	int64_t ms_telem = 0;
	int ret;

	memset(&sched, 0, sizeof(sched));
	ret = eink_store_load_schedule(&sched);
	if (ret || sched.count == 0) {
		return ret ? ret : -ENOENT;
	}

	t_mark = k_uptime_get();
	ret = eink_scheduler_tick();
	if (ret == 0) {
		ret = eink_scheduler_repaint();
	}
	ms_paint = k_uptime_get() - t_mark;
	if (ret < 0) {
		LOG_WRN("fast-path paint: %d", ret);
	}

	eink_scheduler_get_last_job(last_job, sizeof(last_job));
	next_wake = eink_scheduler_get_next_wakeup(now, cfg.poll_interval_seconds);
	LOG_INF("schedule next_wake unix=%lld (fast path)", (long long)next_wake);

#if defined(CONFIG_APP_EINK_HTTP_TELEMETRY_DEFER)
	ret = queue_deferred_telem(&sched, last_job, next_wake, now);
	ms_telem = 0;
#else
	t_mark = k_uptime_get();
	ret = eink_http_post_telemetry(&sched, last_job, next_wake, -1);
	ms_telem = k_uptime_get() - t_mark;
	if (ret == 0) {
		(void)eink_store_save_last_sync(now);
	}
#endif
	LOG_INF("prof: sync total=%lld ms config=0 primary=0 paint=%lld "
		"gallery=0 (0 dl) telem=%lld (fast)",
		(long long)(k_uptime_get() - t_sync), (long long)ms_paint,
		(long long)ms_telem);
	return ret;
}
#else
bool eink_http_radio_sync_needed(void)
{
	return inited && cfg.enabled;
}
#endif

#if defined(CONFIG_APP_EINK_HTTP_V2_SYNC)
struct eink_http_download {
	char asset_id[EINK_ID_MAX];
	char url[EINK_HTTP_URL_MAX];
	char content_sha256[65];
	uint32_t byte_size;
};

static void telemetry_fill_object(cJSON *telemetry, const char *current_displayed_job_id,
				  int64_t next_wakeup_unix, int battery_capacity)
{
	char wake[40];

	iso8601_utc(next_wakeup_unix > 0 ? next_wakeup_unix : (int64_t)time(NULL) + 300, wake,
		    sizeof(wake));
	cJSON_AddNumberToObject(telemetry, "battery_capacity", battery_capacity);
	cJSON_AddStringToObject(telemetry, "next_wakeup_date", wake);
	if (current_displayed_job_id && current_displayed_job_id[0]) {
		cJSON_AddStringToObject(telemetry, "current_displayed_job_id",
					current_displayed_job_id);
	}
#if defined(CONFIG_APP_EINK_LOCATION)
	{
		struct eink_location_fix fix;

		if (eink_location_get(&fix) == 0 && fix.valid) {
			cJSON_AddNumberToObject(telemetry, "latitude", fix.latitude);
			cJSON_AddNumberToObject(telemetry, "longitude", fix.longitude);
			if (fix.accuracy_m >= 0.0) {
				cJSON_AddNumberToObject(telemetry, "location_accuracy_m",
							fix.accuracy_m);
			}
		}
	}
#endif
	telemetry_add_resource_metrics(telemetry);
}

static int sync_v2_once_inner(void)
{
	static struct eink_schedule sched;
	static struct eink_schedule prior;
	static struct eink_http_image images[EINK_HTTP_MAX_IMAGES];
	static struct eink_http_download downloads[EINK_HTTP_MAX_IMAGES];
	static uint8_t body[EINK_HTTP_CFG_BUF];
	size_t image_count = 0;
	size_t download_count = 0;
	size_t body_len = 0;
	char last_job[EINK_ID_MAX];
	char due_image[EINK_ID_MAX];
	char url[EINK_HTTP_URL_MAX];
	int64_t now;
	int64_t next_wake;
	int64_t t_sync = k_uptime_get();
	int64_t t_mark;
	int64_t ms_plan = 0;
	int64_t ms_primary = 0;
	int64_t ms_paint = 0;
	int64_t ms_gallery = 0;
	size_t gallery_downloads = 0;
	cJSON *root;
	cJSON *telemetry;
	cJSON *schedule_acks;
	cJSON *gallery;
	cJSON *client;
	char *json;
	int ret;
	bool noop = false;
	bool sync_now = false;

	if (!inited || !cfg.enabled) {
		return -EINVAL;
	}
	if (strncmp(cfg.api_base, "file://", 7) == 0) {
		return -ENOTSUP;
	}

	due_image[0] = '\0';
	memset(&sched, 0, sizeof(sched));
	memset(&prior, 0, sizeof(prior));
	(void)ensure_wall_clock();
	now = (int64_t)time(NULL);
	(void)eink_store_load_schedule(&prior);
	eink_scheduler_get_last_job(last_job, sizeof(last_job));
	next_wake = eink_scheduler_get_next_wakeup(now, cfg.poll_interval_seconds);

	cjson_arena_enter();
	root = cJSON_CreateObject();
	telemetry = cJSON_AddObjectToObject(root, "telemetry");
	schedule_acks = cJSON_AddArrayToObject(root, "schedule");
	gallery = cJSON_AddArrayToObject(root, "gallery");
	client = cJSON_AddObjectToObject(root, "client");
	telemetry_fill_object(telemetry, last_job, next_wake, -1);
	for (size_t i = 0; i < prior.count; i++) {
		cJSON *ack = cJSON_CreateObject();
		cJSON *g;
		char sha[65];
		uint32_t nbytes = 0;
		bool seen = false;

		cJSON_AddStringToObject(ack, "job_id", prior.jobs[i].job_id);
		cJSON_AddItemToArray(schedule_acks, ack);

		for (size_t j = 0; j < i; j++) {
			if (strcmp(prior.jobs[j].image_id, prior.jobs[i].image_id) == 0) {
				seen = true;
				break;
			}
		}
		if (seen || !eink_store_has_valid_image(prior.jobs[i].image_id)) {
			continue;
		}
		if (eink_store_load_content_hash(prior.jobs[i].image_id, sha, sizeof(sha),
						 &nbytes) != 0) {
			continue;
		}
		g = cJSON_CreateObject();
		cJSON_AddStringToObject(g, "asset_id", prior.jobs[i].image_id);
		cJSON_AddStringToObject(g, "content_sha256", sha);
		cJSON_AddNumberToObject(g, "byte_size", (double)nbytes);
		cJSON_AddItemToArray(gallery, g);
	}
	cJSON_AddBoolToObject(client, "supports_lz4", true);
	cJSON_AddNumberToObject(client, "max_download_bytes", 2000000);
	cJSON_AddNumberToObject(client, "radio_budget_ms", 120000);
	json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!json) {
		cjson_arena_leave();
		return -ENOMEM;
	}

	snprintf(url, sizeof(url), "%s/node/v2/device/%s/sync", cfg.api_base, cfg.device_id);
	LOG_INF("POST %s (v2)", url);
	t_mark = k_uptime_get();
	ret = http_post_json_body(url, json, body, sizeof(body) - 1, &body_len);
	ms_plan = k_uptime_get() - t_mark;
	cJSON_free(json);
	cjson_arena_leave();
	if (ret) {
		LOG_WRN("v2 sync plan failed: %d", ret);
		return ret;
	}
	body[body_len] = '\0';

	cjson_arena_enter();
	root = cJSON_ParseWithLength((char *)body, body_len);
	if (!root) {
		cjson_arena_leave();
		return -EINVAL;
	}
	{
		cJSON *proto = cJSON_GetObjectItemCaseSensitive(root, "protocol");
		cJSON *noop_j = cJSON_GetObjectItemCaseSensitive(root, "noop");
		cJSON *sn = cJSON_GetObjectItemCaseSensitive(root, "sync_now");
		cJSON *sched_j = cJSON_GetObjectItemCaseSensitive(root, "schedule");
		cJSON *imgs = cJSON_GetObjectItemCaseSensitive(root, "images");
		cJSON *dls = cJSON_GetObjectItemCaseSensitive(root, "download");
		cJSON *item;

		if (!cJSON_IsString(proto) || strcmp(proto->valuestring, "v2") != 0) {
			cJSON_Delete(root);
			cjson_arena_leave();
			return -EINVAL;
		}
		noop = cJSON_IsTrue(noop_j);
		sync_now = cJSON_IsTrue(sn);
		if (sync_now) {
			LOG_INF("cloud sync_now requested — full sync this wake");
		}

		sched.count = 0;
		if (cJSON_IsArray(sched_j)) {
			cJSON_ArrayForEach(item, sched_j)
			{
				cJSON *job_id = cJSON_GetObjectItemCaseSensitive(item, "job_id");
				cJSON *image_id =
					cJSON_GetObjectItemCaseSensitive(item, "image_id");
				cJSON *cron = cJSON_GetObjectItemCaseSensitive(item, "cron");
				struct eink_job *j;

				if (sched.count >= EINK_MAX_JOBS) {
					break;
				}
				if (!cJSON_IsString(job_id) || !cJSON_IsString(image_id)) {
					continue;
				}
				j = &sched.jobs[sched.count++];
				memset(j, 0, sizeof(*j));
				strncpy(j->job_id, job_id->valuestring, sizeof(j->job_id) - 1);
				strncpy(j->image_id, image_id->valuestring,
					sizeof(j->image_id) - 1);
				if (cJSON_IsString(cron) && cron->valuestring[0]) {
					strncpy(j->cron, cron->valuestring, sizeof(j->cron) - 1);
				} else {
					strncpy(j->cron, "0 0 * * *", sizeof(j->cron) - 1);
				}
			}
		}

		image_count = 0;
		if (cJSON_IsArray(imgs)) {
			cJSON_ArrayForEach(item, imgs)
			{
				cJSON *image_id =
					cJSON_GetObjectItemCaseSensitive(item, "image_id");
				cJSON *u = cJSON_GetObjectItemCaseSensitive(item, "url");

				if (image_count >= ARRAY_SIZE(images)) {
					break;
				}
				if (!cJSON_IsString(image_id) || !cJSON_IsString(u)) {
					continue;
				}
				strncpy(images[image_count].image_id, image_id->valuestring,
					sizeof(images[image_count].image_id) - 1);
				strncpy(images[image_count].url, u->valuestring,
					sizeof(images[image_count].url) - 1);
				image_count++;
			}
		}

		download_count = 0;
		if (cJSON_IsArray(dls)) {
			cJSON_ArrayForEach(item, dls)
			{
				cJSON *aid = cJSON_GetObjectItemCaseSensitive(item, "asset_id");
				cJSON *u = cJSON_GetObjectItemCaseSensitive(item, "url");
				cJSON *sha =
					cJSON_GetObjectItemCaseSensitive(item, "content_sha256");
				cJSON *bs = cJSON_GetObjectItemCaseSensitive(item, "byte_size");
				struct eink_http_download *d;

				if (download_count >= ARRAY_SIZE(downloads)) {
					break;
				}
				if (!cJSON_IsString(aid) || !cJSON_IsString(u) ||
				    !cJSON_IsString(sha)) {
					continue;
				}
				d = &downloads[download_count++];
				memset(d, 0, sizeof(*d));
				strncpy(d->asset_id, aid->valuestring, sizeof(d->asset_id) - 1);
				strncpy(d->url, u->valuestring, sizeof(d->url) - 1);
				strncpy(d->content_sha256, sha->valuestring,
					sizeof(d->content_sha256) - 1);
				if (cJSON_IsNumber(bs)) {
					d->byte_size = (uint32_t)bs->valuedouble;
				}
			}
		}
	}
	cJSON_Delete(root);
	cjson_arena_leave();

	LOG_INF("v2 plan noop=%d sync_now=%d images=%u download=%u plan=%lld ms", noop ? 1 : 0,
		sync_now ? 1 : 0, (unsigned)image_count, (unsigned)download_count,
		(long long)ms_plan);

	ret = eink_scheduler_set_schedule(&sched, now);
	if (ret) {
		return ret;
	}

	ret = eink_scheduler_due_image(due_image, sizeof(due_image));
	if (ret < 0) {
		return ret;
	}
	if (ret == 0) {
		LOG_INF("no new scheduled image due");
		ret = eink_scheduler_current_image(due_image, sizeof(due_image));
		if (ret > 0) {
			LOG_INF("will show current scheduled image %s", due_image);
		} else {
			due_image[0] = '\0';
		}
	}

	if (due_image[0] != '\0') {
		const char *dl_url = NULL;
		const char *dl_sha = NULL;
		uint32_t dl_sz = 0;

		t_mark = k_uptime_get();
		for (size_t i = 0; i < download_count; i++) {
			if (strcmp(downloads[i].asset_id, due_image) == 0) {
				dl_url = downloads[i].url;
				dl_sha = downloads[i].content_sha256;
				dl_sz = downloads[i].byte_size;
				break;
			}
		}
		if (!dl_url) {
			for (size_t i = 0; i < image_count; i++) {
				if (strcmp(images[i].image_id, due_image) == 0) {
					dl_url = images[i].url;
					break;
				}
			}
		}
		if (eink_store_has_valid_image(due_image) && !dl_sha) {
			LOG_INF("display image %s already cached", due_image);
		} else if (dl_url) {
			ret = eink_http_download_image_hashed(due_image, dl_url, dl_sha, dl_sz);
			if (ret) {
				LOG_WRN("display image %s download failed: %d", due_image, ret);
			}
		} else {
			LOG_WRN("display image %s missing from v2 plan", due_image);
		}
		ms_primary = k_uptime_get() - t_mark;
	}

	t_mark = k_uptime_get();
	LOG_INF("scheduler tick after primary image");
	ret = eink_scheduler_tick();
	LOG_INF("scheduler tick result=%d", ret);
	if (ret == 0) {
		ret = eink_scheduler_repaint();
		LOG_INF("scheduler repaint result=%d", ret);
	}
	ms_paint = k_uptime_get() - t_mark;

	t_mark = k_uptime_get();
	gallery_downloads = 0;
#if defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE) || defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
#if defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
	LOG_INF("gallery skipped (SKIP_GALLERY — due image only)");
#else
	LOG_INF("gallery skipped (battery duty-cycle)");
#endif
#else
	for (size_t i = 0; i < download_count; i++) {
		if (due_image[0] && strcmp(downloads[i].asset_id, due_image) == 0) {
			continue;
		}
		if (eink_store_has_valid_image(downloads[i].asset_id)) {
			char sha[65];

			if (eink_store_load_content_hash(downloads[i].asset_id, sha, sizeof(sha),
							 NULL) == 0) {
				continue;
			}
		}
		gallery_downloads++;
		ret = eink_http_download_image_hashed(downloads[i].asset_id, downloads[i].url,
						      downloads[i].content_sha256,
						      downloads[i].byte_size);
		if (ret) {
			LOG_WRN("gallery image %s download failed: %d", downloads[i].asset_id,
				ret);
		}
	}
#endif
	ms_gallery = k_uptime_get() - t_mark;
	(void)eink_store_save_last_sync(now);

	LOG_INF("prof: sync total=%lld ms plan=%lld primary=%lld paint=%lld "
		"gallery=%lld (%u dl) telem=0 (v2)",
		(long long)(k_uptime_get() - t_sync), (long long)ms_plan, (long long)ms_primary,
		(long long)ms_paint, (long long)ms_gallery, (unsigned)gallery_downloads);
	return 0;
}
#endif /* CONFIG_APP_EINK_HTTP_V2_SYNC */

static int eink_http_sync_once_inner(void)
{
	static struct eink_schedule sched;
	/* Static: EINK_HTTP_MAX_IMAGES * ~2 KiB URLs won't fit on the shell stack. */
	static struct eink_http_image images[EINK_HTTP_MAX_IMAGES];
	size_t image_count = 0;
	int orientation = 0;
	char last_job[EINK_ID_MAX];
	char due_image[EINK_ID_MAX];
	int64_t now;
	int ret;
	int64_t t_sync = k_uptime_get();
	int64_t t_mark;
	int64_t ms_config = 0;
	int64_t ms_primary = 0;
	int64_t ms_paint = 0;
	int64_t ms_gallery = 0;
	int64_t ms_telem = 0;
	size_t gallery_downloads = 0;

	if (!inited || !cfg.enabled) {
		return -EINVAL;
	}

#if defined(CONFIG_APP_EINK_HTTP_V2_SYNC)
	if (strncmp(cfg.api_base, "file://", 7) != 0) {
		return sync_v2_once_inner();
	}
#endif

	due_image[0] = '\0';
	memset(&sched, 0, sizeof(sched));
	(void)ensure_wall_clock();

#if defined(CONFIG_APP_EINK_HTTP_FAST_PATH)
	{
		int64_t last_sync = 0;
		char probe_due[EINK_ID_MAX];
		int due_ret;
		bool need_download = false;

		now = (int64_t)time(NULL);
		(void)eink_store_load_last_sync(&last_sync);
		due_ret = eink_scheduler_due_image(probe_due, sizeof(probe_due));
		if (due_ret > 0) {
			need_download = !eink_store_has_valid_image(probe_due);
		} else if (due_ret == 0) {
			if (eink_scheduler_current_image(probe_due, sizeof(probe_due)) > 0) {
				need_download = !eink_store_has_valid_image(probe_due);
			}
		}
		if (sync_is_fresh(now, last_sync) && !need_download) {
			LOG_INF("sync fast path (telem-only, age=%lld s)",
				(long long)(now - last_sync));
			ret = sync_telem_only(t_sync);
			if (ret == 0) {
				return 0;
			}
			LOG_WRN("fast path failed (%d) — full sync", ret);
		}
	}
#endif

	t_mark = k_uptime_get();
	ret = eink_http_fetch_config(&sched, images, ARRAY_SIZE(images), &image_count,
				     &orientation);
	ms_config = k_uptime_get() - t_mark;
	if (ret) {
		LOG_WRN("config fetch failed: %d", ret);
		return ret;
	}

	now = (int64_t)time(NULL);
	ret = eink_scheduler_set_schedule(&sched, now);
	if (ret) {
		return ret;
	}

	/*
	 * Fast path to panel: fetch the frame that should be visible now, paint
	 * it, THEN cache the rest of the gallery. Otherwise SDL/boot stays on
	 * the blank transparency grid for minutes while 7× ES6F downloads.
	 */
	ret = eink_scheduler_due_image(due_image, sizeof(due_image));
	if (ret < 0) {
		return ret;
	}
	if (ret == 0) {
		LOG_INF("no new scheduled image due");
		ret = eink_scheduler_current_image(due_image, sizeof(due_image));
		if (ret > 0) {
			LOG_INF("will show current scheduled image %s", due_image);
		} else {
			due_image[0] = '\0';
		}
	}

	if (due_image[0] != '\0') {
		bool found = false;

		t_mark = k_uptime_get();
		for (size_t i = 0; i < image_count; i++) {
			if (strcmp(images[i].image_id, due_image) != 0) {
				continue;
			}
			found = true;
			if (eink_store_has_valid_image(images[i].image_id)) {
				LOG_INF("display image %s already cached", images[i].image_id);
			} else {
				ret = eink_http_download_image(images[i].image_id, images[i].url);
				if (ret) {
					LOG_WRN("display image %s download failed: %d; trying cache",
						images[i].image_id, ret);
				}
			}
			break;
		}
		ms_primary = k_uptime_get() - t_mark;
		if (!found) {
			LOG_WRN("display image %s missing from config", due_image);
		}
	}

	t_mark = k_uptime_get();
	LOG_INF("scheduler tick after primary image");
	ret = eink_scheduler_tick();
	LOG_INF("scheduler tick result=%d", ret);
	if (ret == 0) {
		ret = eink_scheduler_repaint();
		LOG_INF("scheduler repaint result=%d", ret);
	}
	ms_paint = k_uptime_get() - t_mark;

	eink_scheduler_get_last_job(last_job, sizeof(last_job));
	{
		int64_t next_wake =
			eink_scheduler_get_next_wakeup(now, cfg.poll_interval_seconds);
		int telem_ret;

		LOG_INF("schedule next_wake unix=%lld", (long long)next_wake);
#if defined(CONFIG_APP_EINK_HTTP_TELEMETRY_DEFER)
		/* Telemetry off critical path; gallery follows on same queue. */
		t_mark = k_uptime_get();
		telem_ret = queue_deferred_telem(&sched, last_job, next_wake, now);
		ms_telem = k_uptime_get() - t_mark;
#else
		/* Telemetry before gallery so awake-to-ack stays short. */
		t_mark = k_uptime_get();
		telem_ret = eink_http_post_telemetry(&sched, last_job, next_wake, -1);
		ms_telem = k_uptime_get() - t_mark;
		if (telem_ret == 0) {
			(void)eink_store_save_last_sync(now);
		}
#endif

#if defined(CONFIG_APP_EINK_HTTP_GALLERY_DEFER)
		ms_gallery = 0;
		gallery_downloads = 0;
#if defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE) || defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
		/* Keep radio/flash budget for due image only. */
#if defined(CONFIG_APP_EINK_HTTP_SKIP_GALLERY)
		LOG_INF("gallery skipped (SKIP_GALLERY — due image only)");
#else
		LOG_INF("gallery skipped (battery duty-cycle)");
#endif
#else
		if (k_work_busy_get(&gallery_work) != 0) {
			LOG_INF("gallery defer busy — leave prior job running");
		} else {
			gallery_job.count = 0;
			gallery_job.skip_id[0] = '\0';
			if (due_image[0] != '\0') {
				strncpy(gallery_job.skip_id, due_image,
					sizeof(gallery_job.skip_id) - 1);
			}
			for (size_t i = 0;
			     i < image_count &&
			     gallery_job.count < ARRAY_SIZE(gallery_job.images);
			     i++) {
				if (due_image[0] != '\0' &&
				    strcmp(images[i].image_id, due_image) == 0) {
					continue;
				}
				if (eink_store_has_valid_image(images[i].image_id)) {
					continue;
				}
				gallery_job.images[gallery_job.count++] = images[i];
			}
			if (gallery_job.count == 0) {
				gallery_job.pending = false;
			} else {
				gallery_job.pending = true;
				gallery_downloads = gallery_job.count;
				k_work_init(&gallery_work, gallery_work_handler);
				(void)k_work_submit_to_queue(&http_q, &gallery_work);
				LOG_INF("gallery deferred (%u images)",
					(unsigned)gallery_job.count);
			}
		}
#endif /* BATTERY_DUTY_CYCLE */
#else
		t_mark = k_uptime_get();
		for (size_t i = 0; i < image_count; i++) {
			if (due_image[0] != '\0' && strcmp(images[i].image_id, due_image) == 0) {
				continue;
			}
			if (eink_store_has_valid_image(images[i].image_id)) {
				LOG_INF("gallery image %s already cached", images[i].image_id);
				continue;
			}
			gallery_downloads++;
			ret = eink_http_download_image(images[i].image_id, images[i].url);
			if (ret) {
				LOG_WRN("gallery image %s download failed: %d", images[i].image_id,
					ret);
			}
		}
		ms_gallery = k_uptime_get() - t_mark;
#endif

		LOG_INF("prof: sync total=%lld ms config=%lld primary=%lld paint=%lld "
			"gallery=%lld (%u dl) telem=%lld",
			(long long)(k_uptime_get() - t_sync), (long long)ms_config,
			(long long)ms_primary, (long long)ms_paint, (long long)ms_gallery,
			(unsigned)gallery_downloads, (long long)ms_telem);
		return telem_ret;
	}
}

int eink_http_sync_once(void)
{
	if (!inited || !cfg.enabled) {
		return -EINVAL;
	}

	ensure_http_queue();

	/* Already on http_q (periodic sync) — run inline. */
	if (k_current_get() == &http_q.thread) {
		return eink_http_sync_once_inner();
	}

	k_work_init(&sync_once_work, sync_once_work_handler);
	(void)k_sem_take(&sync_once_done, K_NO_WAIT);
	k_work_submit_to_queue(&http_q, &sync_once_work);
	/* Gallery cache can pull many ~1 MiB ES6F frames (live bridge convert). */
	if (k_sem_take(&sync_once_done, K_MINUTES(10)) != 0) {
		return -ETIMEDOUT;
	}
	return sync_once_result;
}

static void sync_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)eink_http_sync_once_inner();
	if (started) {
		k_work_reschedule_for_queue(&http_q, &sync_work,
					    K_SECONDS(cfg.poll_interval_seconds));
	}
}

int eink_http_start(void)
{
	if (!inited || !cfg.enabled) {
		return -EINVAL;
	}
	if (started) {
		return 0;
	}
	ensure_http_queue();
	k_work_init_delayable(&sync_work, sync_work_handler);
	started = true;
	k_work_reschedule_for_queue(&http_q, &sync_work, K_NO_WAIT);
	LOG_INF("http sync started (interval=%u s)", cfg.poll_interval_seconds);
	return 0;
}

void eink_http_stop(void)
{
	started = false;
	(void)k_work_cancel_delayable(&sync_work);
}
