/*
 * e-tabelone HTTP compatibility (packed-frame only in v1).
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_HTTP_H
#define EINK_HTTP_H

#include "eink_scheduler_core.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Dev S3 presigned URLs are ~1.7–2 KiB; keep headroom for redirects/query growth. */
#define EINK_HTTP_URL_MAX 2048
#define EINK_HTTP_MAX_IMAGES 16

struct eink_http_config {
	char api_base[192];   /* e.g. https://api.dev.e-tabelone.com */
	char device_id[64];
	char auth_token[256]; /* Bearer; empty disables Authorization */
	int tls_sec_tag;      /* CA tag already registered with Zephyr TLS */
	uint32_t poll_interval_seconds;
	bool enabled;
};

struct eink_http_image {
	char image_id[EINK_ID_MAX];
	char url[EINK_HTTP_URL_MAX];
};

int eink_http_init(const struct eink_http_config *cfg);

/**
 * GET /node/v0/device/{id}/config — parse images/schedule into out_sched.
 * Images retain their image_id → URL mapping.
 */
int eink_http_fetch_config(struct eink_schedule *out_sched,
			   struct eink_http_image *images, size_t image_cap,
			   size_t *image_count, int *orientation);

/** Download URL into store as image_id.es6f; reject non-ES6F magic. */
int eink_http_download_image(const char *image_id, const char *url);

/** POST telemetry with held job_ids + last displayed. */
int eink_http_post_telemetry(const struct eink_schedule *sched,
			     const char *current_displayed_job_id,
			     int64_t next_wakeup_unix,
			     int battery_capacity);

/** Fetch config, download/validate frames, update scheduler, and post telemetry. */
int eink_http_sync_once(void);

/**
 * True when this wake should power the radio for e-tabelone.
 * False when offline paint is enough (nothing new due + last sync still fresh).
 */
bool eink_http_radio_sync_needed(void);

/** Start periodic sync on a dedicated workqueue. */
int eink_http_start(void);

/** Stop periodic sync and wait for no new work to be queued. */
void eink_http_stop(void);

/**
 * Replace credentials at runtime. This is the provisioning boundary: callers
 * should source the token from Bitwarden-backed device settings, never source.
 */
int eink_http_set_credentials(const char *api_base, const char *device_id,
			      const char *auth_token);

#endif
