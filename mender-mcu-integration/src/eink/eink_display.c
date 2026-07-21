/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dedicated workqueue so 20–60s panel refreshes never stall Mender/TLS.
 * Production path streams ES6F from storage in 4 KiB chunks (no framebuffer).
 * Optional CONFIG_APP_EINK_FULL_FRAMEBUFFER keeps one SDRAM FB for EVK bring-up.
 */
#include "eink_display.h"
#include "eink_frame.h"
#include "eink_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_EL133UF1)
#include <zephyr/drivers/display/el133uf1.h>
#endif

LOG_MODULE_REGISTER(eink_display, LOG_LEVEL_INF);

#define EINK_DISP_STACK_SIZE 4096
#define EINK_DISP_PRIORITY   5
#define EINK_ROW_STRIP       8

K_THREAD_STACK_DEFINE(eink_disp_stack, EINK_DISP_STACK_SIZE);
static struct k_work_q eink_disp_q;

enum eink_cmd_type {
	EINK_CMD_SHOW_PATH = 1,
	EINK_CMD_CLEAR,
#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
	EINK_CMD_SHOW_PAYLOAD,
#endif
};

struct eink_cmd {
	enum eink_cmd_type type;
	char path[256];
	char job_id[64];
};

static struct k_work_delayable eink_work;
static struct k_sem eink_idle_sem;
static struct k_mutex eink_mu;
static struct eink_display_status status;
static struct eink_cmd pending;
static bool has_pending;
static bool inited;

#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
static uint8_t eink_fb[EINK_PAYLOAD_LEN];
static uint8_t eink_payload_slot[EINK_PAYLOAD_LEN];
static bool eink_payload_slot_busy;
#endif

struct stream_file {
	struct fs_file_t f;
	bool open;
#if defined(CONFIG_ARCH_POSIX)
	FILE *host;
#endif
	size_t payload_left;
	uint8_t solid_byte;
	bool solid;
};

static const struct device *display_dev(void)
{
	return DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
}

#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
struct mem_stream {
	const uint8_t *p;
	size_t left;
};

static int mem_fill_cb(void *user, uint8_t *dst, size_t max_len)
{
	struct mem_stream *m = user;
	size_t n = MIN(max_len, m->left);

	if (n == 0) {
		return -EINVAL;
	}
	memcpy(dst, m->p, n);
	m->p += n;
	m->left -= n;
	return (int)n;
}
#endif

static int stream_open_path(struct stream_file *s, const char *path)
{
	int ret;

	memset(s, 0, sizeof(*s));
	s->payload_left = EINK_PAYLOAD_LEN;

#if defined(CONFIG_ARCH_POSIX)
	if (strncmp(path, "/lfs1/", sizeof("/lfs1/") - 1) != 0) {
		s->host = fopen(path, "rb");
		if (s->host == NULL) {
			return -ENOENT;
		}
		if (fseek(s->host, (long)EINK_FRAME_HEADER_SIZE, SEEK_SET) != 0) {
			fclose(s->host);
			s->host = NULL;
			return -EIO;
		}
		return 0;
	}
#endif
	fs_file_t_init(&s->f);
	ret = fs_open(&s->f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	ret = fs_seek(&s->f, (off_t)EINK_FRAME_HEADER_SIZE, FS_SEEK_SET);
	if (ret < 0) {
		(void)fs_close(&s->f);
		return ret;
	}
	s->open = true;
	return 0;
}

static void stream_close(struct stream_file *s)
{
#if defined(CONFIG_ARCH_POSIX)
	if (s->host != NULL) {
		fclose(s->host);
		s->host = NULL;
		return;
	}
#endif
	if (s->open) {
		(void)fs_close(&s->f);
		s->open = false;
	}
}

#if defined(CONFIG_EL133UF1) || defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
static int stream_fill_cb(void *user, uint8_t *dst, size_t max_len)
{
	struct stream_file *s = user;
	size_t want = MIN(max_len, s->payload_left);
	int n;

	if (want == 0) {
		return -EINVAL;
	}
	if (s->solid) {
		memset(dst, s->solid_byte, want);
		s->payload_left -= want;
		return (int)want;
	}
#if defined(CONFIG_ARCH_POSIX)
	if (s->host != NULL) {
		size_t rn = fread(dst, 1, want, s->host);

		if (rn != want) {
			return ferror(s->host) ? -EIO : -EINVAL;
		}
		s->payload_left -= want;
		return (int)want;
	}
#endif
	n = (int)fs_read(&s->f, dst, want);
	if (n < 0) {
		return n;
	}
	if ((size_t)n != want) {
		return -EINVAL;
	}
	s->payload_left -= want;
	return n;
}
#endif

static int write_sdl_from_halves(int (*read_row)(void *user, uint16_t y, bool right, uint8_t *row300),
				 void *user)
{
	const struct device *dev = display_dev();
	static uint32_t rowbuf[EINK_PANEL_WIDTH * EINK_ROW_STRIP];
	uint8_t left_row[300];
	uint8_t right_row[300];
	struct display_buffer_descriptor desc;
	int ret;

	for (uint16_t y = 0; y < EINK_PANEL_HEIGHT; y += EINK_ROW_STRIP) {
		uint16_t rows = MIN(EINK_ROW_STRIP, EINK_PANEL_HEIGHT - y);

		for (uint16_t r = 0; r < rows; r++) {
			uint16_t yy = y + r;

			ret = read_row(user, yy, false, left_row);
			if (ret < 0) {
				return ret;
			}
			ret = read_row(user, yy, true, right_row);
			if (ret < 0) {
				return ret;
			}
			for (uint16_t x = 0; x < EINK_PANEL_WIDTH; x++) {
				const uint8_t *half =
					(x < EINK_HALF_WIDTH) ? left_row : right_row;
				uint16_t hx =
					(x < EINK_HALF_WIDTH) ? x : (uint16_t)(x - EINK_HALF_WIDTH);
				uint8_t nib = ((hx & 1u) == 0) ? (half[hx / 2u] >> 4)
							       : (half[hx / 2u] & 0x0f);

				rowbuf[r * EINK_PANEL_WIDTH + x] = eink_frame_nibble_to_argb(nib);
			}
		}
		desc.buf_size = (uint32_t)EINK_PANEL_WIDTH * rows * 4u;
		desc.width = EINK_PANEL_WIDTH;
		desc.height = rows;
		desc.pitch = EINK_PANEL_WIDTH;
		ret = display_write(dev, 0, y, &desc, rowbuf);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

static int stream_read_row(void *user, uint16_t y, bool right, uint8_t *row300)
{
	struct stream_file *s = user;
	size_t row_off = (size_t)y * 300u;
	size_t base = right ? EINK_BYTES_PER_HALF : 0u;
	int n;

	if (s->solid) {
		memset(row300, s->solid_byte, 300);
		return 0;
	}
#if defined(CONFIG_ARCH_POSIX)
	if (s->host != NULL) {
		if (fseek(s->host, (long)(EINK_FRAME_HEADER_SIZE + base + row_off), SEEK_SET) !=
		    0) {
			return -EIO;
		}
		if (fread(row300, 1, 300, s->host) != 300) {
			return -EIO;
		}
		return 0;
	}
#endif
	n = fs_seek(&s->f, (off_t)(EINK_FRAME_HEADER_SIZE + base + row_off), FS_SEEK_SET);
	if (n < 0) {
		return n;
	}
	n = (int)fs_read(&s->f, row300, 300);
	return (n == 300) ? 0 : (n < 0 ? n : -EINVAL);
}

#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
static int fb_read_row(void *user, uint16_t y, bool right, uint8_t *row300)
{
	const uint8_t *payload = user;
	size_t off = (right ? EINK_BYTES_PER_HALF : 0u) + (size_t)y * 300u;

	memcpy(row300, payload + off, 300);
	return 0;
}
#endif

static int write_stream_to_display(struct stream_file *s)
{
	const struct device *dev = display_dev();
	struct display_capabilities caps;
	int ret;

	if (!device_is_ready(dev)) {
		LOG_ERR("display not ready");
		return -ENODEV;
	}

	display_get_capabilities(dev, &caps);

	ret = display_blanking_on(dev);
	if (ret < 0 && ret != -ENOTSUP) {
		return ret;
	}

	if ((caps.screen_info & SCREEN_INFO_EPD) != 0) {
#if defined(CONFIG_EL133UF1)
		ret = el133uf1_stream_write(dev, stream_fill_cb, s);
#else
#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
		struct display_buffer_descriptor desc = {
			.buf_size = EINK_PAYLOAD_LEN,
			.width = EINK_PANEL_WIDTH,
			.height = EINK_PANEL_HEIGHT,
			.pitch = EINK_PANEL_WIDTH,
		};
		size_t got = 0;

		while (got < EINK_PAYLOAD_LEN) {
			int n = stream_fill_cb(s, eink_fb + got, EINK_PAYLOAD_LEN - got);

			if (n < 0) {
				return n;
			}
			got += (size_t)n;
		}
		ret = display_write(dev, 0, 0, &desc, eink_fb);
#else
		LOG_ERR("EPD requires EL133 stream driver or APP_EINK_FULL_FRAMEBUFFER");
		ret = -ENOTSUP;
#endif
#endif
	} else {
		ret = write_sdl_from_halves(stream_read_row, s);
	}

	if (ret < 0) {
		return ret;
	}
	ret = display_blanking_off(dev);
	if (ret < 0 && ret != -ENOTSUP) {
		return ret;
	}
	return 0;
}

#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
static int write_payload_to_display(const uint8_t *payload)
{
	const struct device *dev = display_dev();
	struct display_capabilities caps;
	int ret;

	display_get_capabilities(dev, &caps);
	ret = display_blanking_on(dev);
	if (ret < 0 && ret != -ENOTSUP) {
		return ret;
	}

	if ((caps.screen_info & SCREEN_INFO_EPD) != 0) {
#if defined(CONFIG_EL133UF1)
		struct mem_stream mem = { .p = payload, .left = EINK_PAYLOAD_LEN };

		ret = el133uf1_stream_write(dev, mem_fill_cb, &mem);
#else
		struct display_buffer_descriptor desc = {
			.buf_size = EINK_PAYLOAD_LEN,
			.width = EINK_PANEL_WIDTH,
			.height = EINK_PANEL_HEIGHT,
			.pitch = EINK_PANEL_WIDTH,
		};

		ret = display_write(dev, 0, 0, &desc, payload);
#endif
	} else {
		ret = write_sdl_from_halves(fb_read_row, (void *)payload);
	}

	if (ret < 0) {
		return ret;
	}
	ret = display_blanking_off(dev);
	if (ret < 0 && ret != -ENOTSUP) {
		return ret;
	}
	return 0;
}
#endif

static void set_idle(int result)
{
	k_mutex_lock(&eink_mu, K_FOREVER);
	status.state = (result == 0) ? EINK_DISPLAY_IDLE : EINK_DISPLAY_ERROR;
	status.last_result = result;
	if (result == 0) {
		status.refresh_count++;
	}
	k_mutex_unlock(&eink_mu);
	k_sem_give(&eink_idle_sem);
}

static void eink_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	struct eink_cmd cmd;
	struct stream_file stream;
	int ret = 0;

	k_mutex_lock(&eink_mu, K_FOREVER);
	if (!has_pending) {
		k_mutex_unlock(&eink_mu);
		return;
	}
	cmd = pending;
	has_pending = false;
	status.state = EINK_DISPLAY_REFRESHING;
	k_mutex_unlock(&eink_mu);

	if (cmd.type == EINK_CMD_CLEAR) {
		memset(&stream, 0, sizeof(stream));
		stream.solid = true;
		stream.solid_byte =
			(uint8_t)(((EINK_COLOR_WHITE & 0x0f) << 4) | (EINK_COLOR_WHITE & 0x0f));
		stream.payload_left = EINK_PAYLOAD_LEN;
		ret = write_stream_to_display(&stream);
#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
	} else if (cmd.type == EINK_CMD_SHOW_PAYLOAD) {
		ret = write_payload_to_display(eink_payload_slot);
		eink_payload_slot_busy = false;
#endif
	} else {
		ret = eink_store_validate_path(cmd.path, NULL);
		if (ret == 0) {
			ret = stream_open_path(&stream, cmd.path);
		}
		if (ret == 0) {
			ret = write_stream_to_display(&stream);
			stream_close(&stream);
		} else {
			stream_close(&stream);
		}
	}

	if (ret == 0 && cmd.job_id[0] != '\0') {
		k_mutex_lock(&eink_mu, K_FOREVER);
		strncpy(status.last_job_id, cmd.job_id, sizeof(status.last_job_id) - 1);
		status.last_job_id[sizeof(status.last_job_id) - 1] = '\0';
		k_mutex_unlock(&eink_mu);
	}

	LOG_INF("refresh done result=%d job=%s", ret, cmd.job_id[0] ? cmd.job_id : "-");
	set_idle(ret);
}

static int queue_cmd(const struct eink_cmd *cmd)
{
	k_mutex_lock(&eink_mu, K_FOREVER);
	if (has_pending || status.state == EINK_DISPLAY_REFRESHING) {
		k_mutex_unlock(&eink_mu);
		return -EBUSY;
	}
	pending = *cmd;
	has_pending = true;
	k_sem_reset(&eink_idle_sem);
	k_mutex_unlock(&eink_mu);
	k_work_reschedule_for_queue(&eink_disp_q, &eink_work, K_NO_WAIT);
	return 0;
}

int eink_display_init(void)
{
	const struct device *dev = display_dev();

	if (inited) {
		return 0;
	}
	if (!device_is_ready(dev)) {
		LOG_ERR("zephyr,display not ready");
		return -ENODEV;
	}

	k_mutex_init(&eink_mu);
	k_sem_init(&eink_idle_sem, 1, 1);
	k_work_queue_init(&eink_disp_q);
	k_work_queue_start(&eink_disp_q, eink_disp_stack,
			   K_THREAD_STACK_SIZEOF(eink_disp_stack), EINK_DISP_PRIORITY, NULL);
	k_thread_name_set(&eink_disp_q.thread, "eink_disp");
	k_work_init_delayable(&eink_work, eink_work_handler);
	memset(&status, 0, sizeof(status));
	status.state = EINK_DISPLAY_IDLE;
	inited = true;
	LOG_INF("eink display ready (%s)%s", dev->name,
		IS_ENABLED(CONFIG_APP_EINK_FULL_FRAMEBUFFER) ? " [full-FB]" : " [stream]");
	return 0;
}

int eink_display_show_path(const char *path, const char *job_id)
{
	struct eink_cmd cmd = { .type = EINK_CMD_SHOW_PATH };

	if (path == NULL) {
		return -EINVAL;
	}
	strncpy(cmd.path, path, sizeof(cmd.path) - 1);
	if (job_id != NULL) {
		strncpy(cmd.job_id, job_id, sizeof(cmd.job_id) - 1);
	}
	return queue_cmd(&cmd);
}

int eink_display_clear(void)
{
	struct eink_cmd cmd = { .type = EINK_CMD_CLEAR };

	return queue_cmd(&cmd);
}

int eink_display_show_payload(const uint8_t *payload, const char *job_id)
{
#if defined(CONFIG_APP_EINK_FULL_FRAMEBUFFER)
	struct eink_cmd cmd = { .type = EINK_CMD_SHOW_PAYLOAD };

	if (payload == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&eink_mu, K_FOREVER);
	if (eink_payload_slot_busy || has_pending || status.state == EINK_DISPLAY_REFRESHING) {
		k_mutex_unlock(&eink_mu);
		return -EBUSY;
	}
	memcpy(eink_payload_slot, payload, EINK_PAYLOAD_LEN);
	eink_payload_slot_busy = true;
	if (job_id != NULL) {
		strncpy(cmd.job_id, job_id, sizeof(cmd.job_id) - 1);
	}
	pending = cmd;
	has_pending = true;
	k_sem_reset(&eink_idle_sem);
	k_mutex_unlock(&eink_mu);
	k_work_reschedule_for_queue(&eink_disp_q, &eink_work, K_NO_WAIT);
	return 0;
#else
	ARG_UNUSED(payload);
	ARG_UNUSED(job_id);
	return -ENOTSUP;
#endif
}

void eink_display_get_status(struct eink_display_status *out)
{
	k_mutex_lock(&eink_mu, K_FOREVER);
	*out = status;
	k_mutex_unlock(&eink_mu);
}

int eink_display_wait_idle(k_timeout_t timeout)
{
	return k_sem_take(&eink_idle_sem, timeout);
}
