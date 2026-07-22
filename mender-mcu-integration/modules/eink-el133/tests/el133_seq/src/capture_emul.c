/* SPDX-License-Identifier: Apache-2.0
 *
 * SPI capture emulator: records the opcode of every command-phase transfer
 * (DC line low) issued by the EL133UF1 driver, so a ztest can assert the exact
 * power-up and refresh command sequences.
 */
#define DT_DRV_COMPAT test_el133_capture

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/sys/util.h>

#include "el133_capture.h"

/* DC line of the panel node — low means the transfer carries a command byte. */
static const struct gpio_dt_spec dc =
	GPIO_DT_SPEC_GET(DT_NODELABEL(el133uf1), dc_gpios);

static struct el133_capture capture;

struct el133_capture *el133_capture_get(void)
{
	return &capture;
}

void el133_capture_reset(void)
{
	capture.count = 0;
}

static int capture_io(const struct emul *target, const struct spi_config *config,
		      const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	ARG_UNUSED(target);
	ARG_UNUSED(config);
	ARG_UNUSED(rx_bufs);

	if (tx_bufs == NULL || tx_bufs->count == 0 || tx_bufs->buffers[0].len < 1) {
		return 0;
	}

	/* gpio_emul returns the raw output level; dc is active-high so 0 == command. */
	if (gpio_emul_output_get_dt(&dc) == 0) {
		if (capture.count < ARRAY_SIZE(capture.opcodes)) {
			capture.opcodes[capture.count++] =
				((const uint8_t *)tx_bufs->buffers[0].buf)[0];
		}
	}
	return 0;
}

static const struct spi_emul_api capture_api = {
	.io = capture_io,
};

static int capture_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);
	return 0;
}

/* Stub device so spi_emul's DT_FOREACH_CHILD / DEVICE_DT_GET resolves. */
static int capture_dev_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

/* After the SPI controller (CONFIG_SPI_INIT_PRIORITY=50) so the DT
 * parent/child init-order check passes. emul_init_for_bus only needs the
 * DEVICE object (link-time); it does not call this init function. */
DEVICE_DT_INST_DEFINE(0, capture_dev_init, NULL, NULL, NULL, POST_KERNEL, 51,
		      NULL);

EMUL_DT_INST_DEFINE(0, capture_emul_init, NULL, NULL, &capture_api, NULL)
