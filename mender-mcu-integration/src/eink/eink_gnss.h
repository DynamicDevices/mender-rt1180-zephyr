/*
 * Bridge Zephyr GNSS driver API → eink_location_* (telemetry).
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_GNSS_H
#define EINK_GNSS_H

#include <zephyr/drivers/gnss.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bind to DT alias `gnss` (or chosen), resume the device, register data CB.
 * Safe to call once at boot when CONFIG_APP_EINK_GNSS=y.
 */
int eink_gnss_init(void);

/**
 * Apply one GNSS sample into the e-ink location store.
 * On a valid fix: eink_location_set(). On NO_FIX: leave last fix unchanged.
 * Exported for selftest without waiting on the emulator timer.
 */
int eink_gnss_apply_data(const struct gnss_data *data);

/** True when a GNSS device was found and resumed. */
bool eink_gnss_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* EINK_GNSS_H */
