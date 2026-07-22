/*
 * Optional device location fix for e-tabelone telemetry.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lab/native_sim: set via shell. Real GNSS can call eink_location_set() later.
 */
#ifndef EINK_LOCATION_H
#define EINK_LOCATION_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct eink_location_fix {
	double latitude;
	double longitude;
	/** Metres; negative means unknown — omit from telemetry. */
	double accuracy_m;
	bool valid;
};

/**
 * Load the current fix (RAM cache, else store file).
 * On success with no fix: out->valid == false and return 0.
 */
int eink_location_get(struct eink_location_fix *out);

/**
 * Persist a WGS84 fix. Pass accuracy_m < 0 to omit accuracy from telemetry.
 * Rejects out-of-range lat/lng.
 */
int eink_location_set(double latitude, double longitude, double accuracy_m);

/** Clear persisted fix (telemetry omits location fields). */
int eink_location_clear(void);

/**
 * Format a standalone JSON object for selftest / debugging:
 *   no fix → "{}"
 *   fix    → {"latitude":…,"longitude":…[,"location_accuracy_m":…]}
 */
int eink_location_to_json_object(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* EINK_LOCATION_H */
