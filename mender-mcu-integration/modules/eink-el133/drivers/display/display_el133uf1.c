/*
 * Copyright (c) 2026 Dynamic Devices Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr display driver for EL133UF1 Spectra 6 (dual SPI controller).
 *
 * Faithful port of the MIT-licensed E Ink reference driver at
 * /data_drive/esl/eink-spectra6/src/core/el133uf1_driver.c
 * (opcodes: include/el133uf1.h). Register values and the init/refresh
 * command ordering match that reference exactly.
 *
 * No driver-owned framebuffer — the caller's full L_4 payload is streamed
 * in 4 KiB chunks (BSS, not stack — eink_disp is only ~4–8 KiB). Refresh is
 * blanking-gated: display_write()/stream_write() load the DTM halves;
 * blanking_off() runs PON(CS0)→PON(CS1)→DRF(both)→POF(CS0)→POF(CS1).
 *
 * BUSY polarity follows the reference: the panel drives BUSY HIGH when ready
 * (idle) and LOW while working. Declare busy-gpios ACTIVE_HIGH in the overlay
 * so the logical level read here matches (1 == ready).
 */
#define DT_DRV_COMPAT eink_el133uf1

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/display/el133uf1.h>

#include <string.h>

LOG_MODULE_REGISTER(el133uf1, CONFIG_DISPLAY_LOG_LEVEL);

/* Command opcodes (E Ink reference include/el133uf1.h). */
#define EL133_PSR             0x00
#define EL133_PWR             0x01
#define EL133_POF             0x02
#define EL133_POFS            0x03
#define EL133_PON             0x04
#define EL133_BTST_N          0x05
#define EL133_BTST_P          0x06
#define EL133_DTM             0x10
#define EL133_DRF             0x12
#define EL133_PLL             0x30
#define EL133_CDI             0x50
#define EL133_TCON            0x60
#define EL133_TRES            0x61
#define EL133_AN_TM           0x74
#define EL133_AGID            0x86
#define EL133_DCDC            0xA5
#define EL133_BUCK_BOOST_VDDN 0xB0
#define EL133_TFT_VCOM_POWER  0xB1
#define EL133_EN_BUF          0xB6
#define EL133_BOOST_VDDP_EN   0xB7
#define EL133_CCSET           0xE0
#define EL133_PWS             0xE3
#define EL133_SPIM            0xE6
#define EL133_CMD66           0xF0

/* Register payloads (verbatim from the reference driver). */
static const uint8_t V_PSR[]             = {0xDF, 0x6B};
static const uint8_t V_PWR[]             = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static const uint8_t V_POF[]             = {0x01};
static const uint8_t V_POFS[]            = {0x00, 0xC0, 0x03, 0xA4};
static const uint8_t V_DRF[]             = {0x00};
static const uint8_t V_PLL[]             = {0x08};
static const uint8_t V_CDI[]             = {0xF7};
static const uint8_t V_TCON[]            = {0x03, 0x03};
static const uint8_t V_TRES[]            = {0x04, 0xB0, 0x03, 0x20};
static const uint8_t V_CMD66[]           = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static const uint8_t V_EN_BUF[]          = {0x07};
static const uint8_t V_CCSET[]           = {0x01};
static const uint8_t V_PWS[]             = {0x22};
static const uint8_t V_AN_TM[]           = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
static const uint8_t V_AGID[]            = {0x10};
static const uint8_t V_DCDC[]            = {0x44, 0x54, 0x00};
static const uint8_t V_BTST_P[]          = {0xD8, 0x18};
static const uint8_t V_BOOST_VDDP_EN[]   = {0x01};
static const uint8_t V_BTST_N[]          = {0xD8, 0x18};
static const uint8_t V_BUCK_BOOST_VDDN[] = {0x01};
static const uint8_t V_TFT_VCOM_POWER[]  = {0x02};
static const uint8_t V_SPIM[]            = {0x00}; /* 0x00 = single SPI */

enum el133_cs {
	EL133_CS0 = 0,
	EL133_CS1 = 1,
	EL133_CS_BOTH = 2,
};

#define EL133_HALF_BYTES (EL133_PAYLOAD_BYTES / 2U)
#define EL133_CHUNK      4096U

struct el133_config {
	const struct device *spi_dev;
	struct spi_config spi_cfg;
	struct gpio_dt_spec busy;
	struct gpio_dt_spec cs0;
	struct gpio_dt_spec cs1;
	struct gpio_dt_spec dc;
	struct gpio_dt_spec reset;
	uint16_t width;
	uint16_t height;
};

struct el133_data {
	bool blanking_on;
	bool dirty;
	bool glass_present;
	/* Stream SPI chunk lives in BSS — must not sit on eink_disp stack. */
	uint8_t stream_chunk[EL133_CHUNK];
};

static void cs_set(const struct el133_config *cfg, enum el133_cs which, bool selected)
{
	/* Logical level — ACTIVE_LOW flags make selected==true drive the pin low. */
	int level = selected ? 1 : 0;

	if (which == EL133_CS0 || which == EL133_CS_BOTH) {
		gpio_pin_set_dt(&cfg->cs0, level);
	}
	if (which == EL133_CS1 || which == EL133_CS_BOTH) {
		gpio_pin_set_dt(&cfg->cs1, level);
	}
}

/* Reference semantics: BUSY HIGH == ready. Wait until the panel reports ready. */
static int wait_ready(const struct el133_config *cfg, int32_t timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	while (gpio_pin_get_dt(&cfg->busy) != 1) {
		if (k_uptime_get() > end) {
			LOG_WRN("BUSY still low after %d ms (panel not ready)", timeout_ms);
			return -ETIMEDOUT;
		}
		k_msleep(5);
	}
	return 0;
}

static int xfer_cmd(const struct el133_config *cfg, enum el133_cs which, uint8_t cmd,
		    const uint8_t *data, size_t len)
{
	struct spi_buf tx_buf;
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	int ret;

	cs_set(cfg, which, true);
	gpio_pin_set_dt(&cfg->dc, 0); /* command */
	tx_buf.buf = &cmd;
	tx_buf.len = 1;
	ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
	if (ret < 0) {
		cs_set(cfg, which, false);
		return ret;
	}
	if (data != NULL && len > 0) {
		gpio_pin_set_dt(&cfg->dc, 1); /* data */
		tx_buf.buf = (void *)data;
		tx_buf.len = len;
		ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
	}
	cs_set(cfg, which, false);
	return ret;
}

#define EL133_WRITE_REG(cfg, which, cmd, val)                                                      \
	xfer_cmd((cfg), (which), (cmd), (val), sizeof(val))

/*
 * Panel power-up register sequence. Order and per-controller targeting match
 * el133uf1_epd_init() in the reference driver exactly.
 */
static int epd_init_registers(const struct el133_config *cfg)
{
	int ret;

	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_AN_TM, V_AN_TM);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_CMD66, V_CMD66);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_PSR, V_PSR);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_DCDC, V_DCDC);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_SPIM, V_SPIM);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_PLL, V_PLL);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_CDI, V_CDI);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_TCON, V_TCON);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_POFS, V_POFS);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_AGID, V_AGID);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_PWS, V_PWS);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_CCSET, V_CCSET);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS_BOTH, EL133_TRES, V_TRES);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_PWR, V_PWR);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_EN_BUF, V_EN_BUF);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_BTST_P, V_BTST_P);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_BOOST_VDDP_EN, V_BOOST_VDDP_EN);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_BTST_N, V_BTST_N);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_BUCK_BOOST_VDDN, V_BUCK_BOOST_VDDN);
	if (ret < 0) {
		return ret;
	}
	ret = EL133_WRITE_REG(cfg, EL133_CS0, EL133_TFT_VCOM_POWER, V_TFT_VCOM_POWER);
	if (ret < 0) {
		return ret;
	}
	return 0;
}

static int load_dtm(const struct el133_config *cfg, enum el133_cs which, const uint8_t *src,
		    size_t len)
{
	uint8_t cmd = EL133_DTM;
	struct spi_buf tx_buf;
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	size_t off = 0;
	int ret;

	cs_set(cfg, which, true);
	gpio_pin_set_dt(&cfg->dc, 0);
	tx_buf.buf = &cmd;
	tx_buf.len = 1;
	ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
	if (ret < 0) {
		cs_set(cfg, which, false);
		return ret;
	}

	gpio_pin_set_dt(&cfg->dc, 1);
	while (off < len) {
		size_t n = MIN(EL133_CHUNK, len - off);

		tx_buf.buf = (void *)(src + off);
		tx_buf.len = n;
		ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
		if (ret < 0) {
			cs_set(cfg, which, false);
			return ret;
		}
		off += n;
	}
	cs_set(cfg, which, false);
	return 0;
}

/*
 * Refresh cycle — matches el133uf1_display_refresh():
 *   PON(CS0) → PON(CS1) → 30 ms → DRF(both) → POF(CS0) → POF(CS1).
 */
static int refresh(const struct device *dev)
{
	const struct el133_config *cfg = dev->config;
	int ret;

	/* PON takes no payload; send opcode only. */
	ret = xfer_cmd(cfg, EL133_CS0, EL133_PON, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	ret = wait_ready(cfg, 30000);
	if (ret < 0) {
		return ret;
	}

	ret = xfer_cmd(cfg, EL133_CS1, EL133_PON, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	ret = wait_ready(cfg, 30000);
	if (ret < 0) {
		return ret;
	}

	k_msleep(30);

	ret = xfer_cmd(cfg, EL133_CS_BOTH, EL133_DRF, V_DRF, sizeof(V_DRF));
	if (ret < 0) {
		return ret;
	}
	ret = wait_ready(cfg, 60000);
	if (ret < 0) {
		return ret;
	}

	ret = xfer_cmd(cfg, EL133_CS0, EL133_POF, V_POF, sizeof(V_POF));
	if (ret < 0) {
		return ret;
	}
	ret = wait_ready(cfg, 30000);
	if (ret < 0) {
		return ret;
	}

	ret = xfer_cmd(cfg, EL133_CS1, EL133_POF, V_POF, sizeof(V_POF));
	if (ret < 0) {
		return ret;
	}
	return wait_ready(cfg, 30000);
}

static int el133_write(const struct device *dev, const uint16_t x, const uint16_t y,
		       const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct el133_config *cfg = dev->config;
	struct el133_data *data = dev->data;
	const uint8_t *payload = buf;
	int ret;

	ARG_UNUSED(x);
	ARG_UNUSED(y);

	if (buf == NULL || desc == NULL) {
		return -EINVAL;
	}
	if (desc->buf_size < EL133_PAYLOAD_BYTES) {
		LOG_ERR("need full frame %u bytes", EL133_PAYLOAD_BYTES);
		return -EINVAL;
	}

	ret = load_dtm(cfg, EL133_CS0, payload, EL133_HALF_BYTES);
	if (ret < 0) {
		return ret;
	}
	ret = load_dtm(cfg, EL133_CS1, payload + EL133_HALF_BYTES, EL133_HALF_BYTES);
	if (ret < 0) {
		return ret;
	}

	data->dirty = true;
	if (!data->blanking_on) {
		ret = refresh(dev);
		if (ret < 0) {
			return ret;
		}
		data->dirty = false;
	}
	return 0;
}

static int stream_half(const struct el133_config *cfg, enum el133_cs which,
		       el133uf1_fill_cb_t fill, void *user, uint8_t *chunk, size_t chunk_len)
{
	uint8_t cmd = EL133_DTM;
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	size_t filled = 0;
	int ret;

	cs_set(cfg, which, true);
	gpio_pin_set_dt(&cfg->dc, 0);
	ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
	if (ret < 0) {
		cs_set(cfg, which, false);
		return ret;
	}
	gpio_pin_set_dt(&cfg->dc, 1);

	while (filled < EL133_HALF_BYTES) {
		size_t want = MIN(chunk_len, EL133_HALF_BYTES - filled);
		int n = fill(user, chunk, want);

		if (n < 0) {
			cs_set(cfg, which, false);
			return n;
		}
		if ((size_t)n != want) {
			cs_set(cfg, which, false);
			return -EINVAL;
		}
		tx_buf.buf = chunk;
		tx_buf.len = (size_t)n;
		ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
		if (ret < 0) {
			cs_set(cfg, which, false);
			return ret;
		}
		filled += (size_t)n;
	}
	cs_set(cfg, which, false);
	return 0;
}

int el133uf1_stream_write(const struct device *dev, el133uf1_fill_cb_t fill, void *user)
{
	const struct el133_config *cfg = dev->config;
	struct el133_data *data = dev->data;
	uint8_t *chunk = data->stream_chunk;
	int ret;

	if (fill == NULL) {
		return -EINVAL;
	}

	ret = stream_half(cfg, EL133_CS0, fill, user, chunk, EL133_CHUNK);
	if (ret < 0) {
		return ret;
	}
	ret = stream_half(cfg, EL133_CS1, fill, user, chunk, EL133_CHUNK);
	if (ret < 0) {
		return ret;
	}

	data->dirty = true;
	if (!data->blanking_on) {
		ret = refresh(dev);
		if (ret < 0) {
			return ret;
		}
		data->dirty = false;
	}
	return 0;
}

static int el133_blanking_on(const struct device *dev)
{
	struct el133_data *data = dev->data;

	data->blanking_on = true;
	return 0;
}

static int el133_blanking_off(const struct device *dev)
{
	struct el133_data *data = dev->data;
	int ret = 0;

	if (data->dirty) {
		ret = refresh(dev);
		if (ret == 0) {
			data->dirty = false;
		}
	}
	data->blanking_on = false;
	return ret;
}

static void el133_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct el133_config *cfg = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = cfg->width;
	caps->y_resolution = cfg->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_L_4;
	caps->current_pixel_format = PIXEL_FORMAT_L_4;
	caps->screen_info = SCREEN_INFO_EPD;
}

static int el133_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
	ARG_UNUSED(dev);
	return (pf == PIXEL_FORMAT_L_4) ? 0 : -ENOTSUP;
}

static int el133_init(const struct device *dev)
{
	const struct el133_config *cfg = dev->config;
	struct el133_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->spi_dev)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&cfg->busy, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->cs0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->cs1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->dc, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	/* Hardware reset: assert 20 ms, release 20 ms, settle 100 ms (reference). */
	gpio_pin_set_dt(&cfg->reset, 1); /* assert (active-low pin driven low) */
	k_msleep(20);
	gpio_pin_set_dt(&cfg->reset, 0); /* release */
	k_msleep(20);
	k_msleep(100);
	ret = wait_ready(cfg, 10000);
	if (ret < 0) {
		/* No glass / floating BUSY: skip register programming (up to ~3 min of waits). */
		LOG_WRN("no panel (BUSY timeout) — skip EPD register init");
		data->blanking_on = false;
		data->dirty = false;
		data->glass_present = false;
		return 0;
	}

	ret = epd_init_registers(cfg);
	if (ret < 0) {
		LOG_ERR("EPD register init failed (%d)", ret);
		data->glass_present = false;
		return ret;
	}

	data->blanking_on = false;
	data->dirty = false;
	data->glass_present = true;
	LOG_INF("EL133UF1 ready (%ux%u)", cfg->width, cfg->height);
	return 0;
}

bool el133uf1_glass_present(const struct device *dev)
{
	const struct el133_data *data;

	if (dev == NULL || !device_is_ready(dev)) {
		return false;
	}
	data = dev->data;
	return data->glass_present;
}

static const struct display_driver_api el133_api = {
	.blanking_on = el133_blanking_on,
	.blanking_off = el133_blanking_off,
	.write = el133_write,
	.get_capabilities = el133_get_capabilities,
	.set_pixel_format = el133_set_pixel_format,
};

#define EL133_INIT(n)                                                                          \
	static struct el133_data el133_data_##n;                                               \
	static const struct el133_config el133_config_##n = {                                  \
		.spi_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, spi_dev)),                          \
		.spi_cfg =                                                                     \
			{                                                                      \
				.frequency = DT_INST_PROP(n, spi_max_frequency),               \
				.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |            \
					     SPI_TRANSFER_MSB,                                 \
				.slave = 0,                                                    \
			},                                                                     \
		.busy = GPIO_DT_SPEC_INST_GET(n, busy_gpios),                                  \
		.cs0 = GPIO_DT_SPEC_INST_GET(n, cs0_gpios),                                    \
		.cs1 = GPIO_DT_SPEC_INST_GET(n, cs1_gpios),                                    \
		.dc = GPIO_DT_SPEC_INST_GET(n, dc_gpios),                                      \
		.reset = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                                \
		.width = DT_INST_PROP(n, width),                                               \
		.height = DT_INST_PROP(n, height),                                             \
	};                                                                                     \
	DEVICE_DT_INST_DEFINE(n, el133_init, NULL, &el133_data_##n, &el133_config_##n,         \
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &el133_api);

DT_INST_FOREACH_STATUS_OKAY(EL133_INIT)
