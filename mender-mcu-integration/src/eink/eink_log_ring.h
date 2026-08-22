/*
 * Circular RAM log ring for on-demand portal upload.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_LOG_RING_H
#define EINK_LOG_RING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void eink_log_ring_init(void);

/** Copy oldest→newest bytes into dst (NUL-terminated). Returns bytes copied. */
size_t eink_log_ring_snapshot(char *dst, size_t dst_len);

size_t eink_log_ring_bytes(void);
size_t eink_log_ring_line_count(void);

/** Best-effort redact of Bearer tokens and eink creds lines in place. */
void eink_log_ring_redact_inplace(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EINK_LOG_RING_H */
