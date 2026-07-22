/*
 * Bridge Zephyr GNSS → eink_location telemetry store.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Requires a DT alias `gnss` pointing at an enabled GNSS device
 * (zephyr,gnss-emul on native_sim, or UART NMEA / vendor driver on HW).
 */
#include "eink_gnss.h"
#include "eink_location.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(eink_gnss, LOG_LEVEL_INF);

static const struct device *gnss_dev;
static bool gnss_ready;

static double nanodeg_to_deg(int64_t nd)
{
	return (double)nd / 1000000000.0;
}

/**
 * Rough horizontal accuracy from HDOP (1/1000 units).
 * Treat UERE ≈ 5 m; accuracy_m = (hdop/1000) * 5.
 */
static double accuracy_from_hdop(uint32_t hdop_milli)
{
	if (hdop_milli == 0) {
		return -1.0;
	}
	return ((double)hdop_milli / 1000.0) * 5.0;
}

int eink_gnss_apply_data(const struct gnss_data *data)
{
	double lat;
	double lng;
	double acc;
	int ret;

	if (data == NULL) {
		return -EINVAL;
	}
	if (data->info.fix_status == GNSS_FIX_STATUS_NO_FIX ||
	    data->info.fix_quality == GNSS_FIX_QUALITY_INVALID) {
		LOG_DBG("gnss: no fix (status=%d)", (int)data->info.fix_status);
		return 0;
	}

	lat = nanodeg_to_deg(data->nav_data.latitude);
	lng = nanodeg_to_deg(data->nav_data.longitude);
	acc = accuracy_from_hdop(data->info.hdop);

	ret = eink_location_set(lat, lng, acc);
	if (ret == 0) {
		/* Avoid LOG %f — not all backends enable CBPRINTF float. */
		LOG_INF("gnss fix lat_mdeg=%d lng_mdeg=%d acc_dm=%d sats=%u",
			(int)(lat * 1000.0), (int)(lng * 1000.0),
			acc >= 0.0 ? (int)(acc * 10.0) : -1, data->info.satellites_cnt);
	}
	return ret;
}

#if DT_NODE_EXISTS(DT_ALIAS(gnss))
static void eink_gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	ARG_UNUSED(dev);
	(void)eink_gnss_apply_data(data);
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), eink_gnss_data_cb);
#endif

bool eink_gnss_ready(void)
{
	return gnss_ready;
}

int eink_gnss_init(void)
{
#if !DT_NODE_EXISTS(DT_ALIAS(gnss))
	LOG_WRN("gnss: no DT alias 'gnss' — location remains shell/manual");
	gnss_ready = false;
	return -ENODEV;
#else
	int ret;

	gnss_dev = DEVICE_DT_GET(DT_ALIAS(gnss));
	if (!device_is_ready(gnss_dev)) {
		LOG_ERR("gnss device not ready");
		gnss_ready = false;
		return -ENODEV;
	}

	ret = pm_device_action_run(gnss_dev, PM_DEVICE_ACTION_RESUME);
	if (ret < 0 && ret != -ENOTSUP && ret != -EALREADY) {
		LOG_WRN("gnss resume: %d (continuing)", ret);
	}

	gnss_ready = true;
	LOG_INF("gnss ready (%s)", gnss_dev->name);
	return 0;
#endif
}
