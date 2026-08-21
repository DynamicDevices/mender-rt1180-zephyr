/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * USBH bulk shim for T2000 (VID 0x3558). Replaces Linux libusb_controller.
 */

#include "eink_t2000.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/drivers/usb/uhc.h>

#include <fsl_clock.h>

#include "usbh_device.h"
#include "usbh_class.h"

LOG_MODULE_REGISTER(eink_t2000_usb, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_NODE_EXISTS(DT_NODELABEL(zephyr_uhc0))
#error "FRDM T2000 build requires zephyr_uhc0 (eink_t2000.overlay)"
#endif

USBH_CONTROLLER_DEFINE(eink_t2000_uhs, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

struct t2000_priv {
	struct usb_device *udev;
	uint8_t ep_out;
	uint8_t ep_in;
	uint8_t iface;
	atomic_t ready;
};

static struct t2000_priv t2000_priv;
static K_SEM_DEFINE(xfer_done, 0, 1);
static K_MUTEX_DEFINE(xfer_lock);
static volatile int last_xfer_err;

static int t2000_xfer_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	ARG_UNUSED(udev);
	last_xfer_err = xfer->err;
	if (xfer->err && xfer->err != -ECONNRESET) {
		LOG_WRN("bulk xfer err %d ep=0x%02x", xfer->err, xfer->ep);
	}
	k_sem_give(&xfer_done);
	return 0;
}

static int bulk_xfer(uint8_t ep, uint8_t *data, size_t len, bool is_out)
{
	struct usb_device *udev = t2000_priv.udev;
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	int ret;

	if (udev == NULL) {
		return -ENODEV;
	}

	k_mutex_lock(&xfer_lock, K_FOREVER);

	xfer = usbh_xfer_alloc(udev, ep, t2000_xfer_cb, NULL);
	if (xfer == NULL) {
		k_mutex_unlock(&xfer_lock);
		return -ENOMEM;
	}

	buf = usbh_xfer_buf_alloc(udev, len);
	if (buf == NULL) {
		usbh_xfer_free(udev, xfer);
		k_mutex_unlock(&xfer_lock);
		return -ENOMEM;
	}

	if (is_out) {
		net_buf_add_mem(buf, data, len);
	}

	xfer->buf = buf;
	k_sem_reset(&xfer_done);
	last_xfer_err = 0;

	ret = usbh_xfer_enqueue(udev, xfer);
	if (ret) {
		usbh_xfer_buf_free(udev, buf);
		usbh_xfer_free(udev, xfer);
		k_mutex_unlock(&xfer_lock);
		return ret;
	}

	if (k_sem_take(&xfer_done, K_MSEC(20000)) != 0) {
		(void)usbh_xfer_dequeue(udev, xfer);
		k_mutex_unlock(&xfer_lock);
		return -ETIMEDOUT;
	}

	ret = last_xfer_err;
	if (!is_out && ret == 0 && data != NULL && buf->len > 0) {
		memcpy(data, buf->data, MIN(len, buf->len));
	}

	usbh_xfer_buf_free(udev, buf);
	usbh_xfer_free(udev, xfer);
	k_mutex_unlock(&xfer_lock);
	return ret;
}

int eink_t2000_usb_bulk_out(const uint8_t *data, size_t len)
{
	return bulk_xfer(t2000_priv.ep_out, (uint8_t *)data, len, true);
}

int eink_t2000_usb_bulk_in(uint8_t *data, size_t len)
{
	return bulk_xfer(t2000_priv.ep_in, data, len, false);
}

static void pick_bulk_eps(struct usb_device *udev)
{
	uint8_t out = 0;
	uint8_t in = 0;

	for (int i = 1; i < 16; i++) {
		struct usb_ep_descriptor *od = udev->ep_out[i].desc;
		struct usb_ep_descriptor *id = udev->ep_in[i].desc;

		if (od != NULL && (od->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) ==
					  USB_EP_TYPE_BULK && out == 0) {
			out = od->bEndpointAddress;
		}
		if (id != NULL && (id->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) ==
					  USB_EP_TYPE_BULK && in == 0) {
			in = id->bEndpointAddress;
		}
	}

	/* Linux hard-codes IN 0x82 when discovery is ambiguous. */
	if (in == 0) {
		in = 0x82;
	}
	if (out == 0) {
		out = 0x01;
	}

	t2000_priv.ep_out = out;
	t2000_priv.ep_in = in;
	LOG_INF("T2000 bulk OUT=0x%02x IN=0x%02x", out, in);
}

static bool vid_pid_ok(uint16_t vid, uint16_t pid)
{
	return vid == EINK_T2000_VID &&
	       (pid == EINK_T2000_PID || pid == EINK_T2000_PID_MAIN);
}

static int t2000_class_init(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	return 0;
}

static int t2000_class_completion(struct usbh_class_data *const c_data,
				  struct uhc_transfer *const xfer)
{
	ARG_UNUSED(c_data);
	ARG_UNUSED(xfer);
	return 0;
}

static int t2000_class_probe(struct usbh_class_data *const c_data,
			     struct usb_device *const udev, const uint8_t iface)
{
	uint16_t vid = udev->dev_desc.idVendor;
	uint16_t pid = udev->dev_desc.idProduct;

	if (!vid_pid_ok(vid, pid)) {
		LOG_DBG("skip VID=%04x PID=%04x", vid, pid);
		return -ENOTSUP;
	}

	t2000_priv.udev = udev;
	t2000_priv.iface = iface;
	c_data->priv = &t2000_priv;
	pick_bulk_eps(udev);
	atomic_set(&t2000_priv.ready, 1);
	LOG_INF("T2000 bound addr=%u VID=%04x PID=%04x iface=%u", udev->addr, vid, pid,
		iface);
	return 0;
}

static int t2000_class_removed(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	atomic_set(&t2000_priv.ready, 0);
	t2000_priv.udev = NULL;
	LOG_INF("T2000 removed");
	return 0;
}

static struct usbh_class_api t2000_class_api = {
	.init = t2000_class_init,
	.completion_cb = t2000_class_completion,
	.probe = t2000_class_probe,
	.removed = t2000_class_removed,
};

/* Match any interface; probe() filters VID/PID (two PIDs). */
USBH_DEFINE_CLASS(eink_t2000_class, &t2000_class_api, &t2000_priv, NULL);

static int usbhs0_clocks_enable(void)
{
	const uint32_t freq = 24000000U;

	(void)CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usb480M, freq);
	(void)CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, freq);
	return 0;
}

bool eink_t2000_ready(void)
{
	return atomic_get(&t2000_priv.ready) != 0 && t2000_priv.udev != NULL;
}

int eink_t2000_wait_ready(int timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	while (!eink_t2000_ready()) {
		if (timeout_ms >= 0 && k_uptime_get() >= end) {
			return -ETIMEDOUT;
		}
		k_msleep(50);
	}
	return 0;
}

int eink_t2000_init(void)
{
	int ret;

	usbhs0_clocks_enable();

	ret = usbh_init(&eink_t2000_uhs);
	if (ret && ret != -EALREADY) {
		LOG_ERR("usbh_init failed %d", ret);
		return ret;
	}

	ret = usbh_enable(&eink_t2000_uhs);
	if (ret && ret != -EALREADY) {
		LOG_ERR("usbh_enable failed %d", ret);
		return ret;
	}

	LOG_INF("USB host enabled (await T2000 VID=%04x)", EINK_T2000_VID);
	return 0;
}
