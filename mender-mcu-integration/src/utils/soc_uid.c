#include "utils/soc_uid.h"

#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/printk.h>

int soc_uid_get_hex(char *out, size_t out_len)
{
	uint8_t id[16];
	ssize_t n;
	size_t i;
	size_t need;

	if (out == NULL || out_len < 3) {
		return -EINVAL;
	}

	n = hwinfo_get_device_id(id, sizeof(id));
	if (n < 0) {
		return (int)n;
	}
	if (n == 0) {
		return -ENOENT;
	}

	need = (size_t)n * 2U + 1U;
	if (out_len < need) {
		return -ENOMEM;
	}

	for (i = 0; i < (size_t)n; i++) {
		snprintk(&out[i * 2U], 3, "%02X", id[i]);
	}
	out[(size_t)n * 2U] = '\0';

	return 0;
}
