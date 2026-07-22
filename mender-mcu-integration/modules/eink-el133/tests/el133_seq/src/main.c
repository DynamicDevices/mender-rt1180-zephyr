/* SPDX-License-Identifier: Apache-2.0
 *
 * Runtime verification of the recreated EL133UF1 driver on native_sim:
 *   1. the driver compiles/links and the device initialises over emulated SPI
 *      (proves epd_init_registers() ran without error),
 *   2. the power-up register opcodes are emitted in the reference order,
 *   3. a full-frame write emits DTM(x2) then PON,PON,DRF,POF,POF.
 */
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/el133uf1.h>
#include <zephyr/sys/util.h>

#include "el133_capture.h"

#define DISP DEVICE_DT_GET(DT_NODELABEL(el133uf1))

/* Opcode order from el133uf1_epd_init() in the E Ink reference driver. */
static const uint8_t expected_init[] = {
	0x74, 0xF0, 0x00, 0xA5, 0xE6, 0x30, 0x50, 0x60, 0x03, 0x86,
	0xE3, 0xE0, 0x61, 0x01, 0xB6, 0x06, 0xB7, 0x05, 0xB0, 0xB1,
};

/* DTM(CS0), DTM(CS1), PON(CS0), PON(CS1), DRF(both), POF(CS0), POF(CS1). */
static const uint8_t expected_refresh[] = {0x10, 0x10, 0x04, 0x04, 0x12, 0x02, 0x02};

static uint8_t frame[EL133_PAYLOAD_BYTES];

ZTEST(el133_seq, test_sequences)
{
	const struct device *disp = DISP;
	struct el133_capture *cap = el133_capture_get();

	zassert_true(device_is_ready(disp),
		     "EL133 device not ready (init/epd_init_registers failed)");

	/* Power-up opcodes captured during POST_KERNEL device init. */
	zassert_true(cap->count >= ARRAY_SIZE(expected_init),
		     "captured only %u init opcodes, expected >= %u",
		     (unsigned)cap->count, (unsigned)ARRAY_SIZE(expected_init));
	for (size_t i = 0; i < ARRAY_SIZE(expected_init); i++) {
		zassert_equal(cap->opcodes[i], expected_init[i],
			      "init opcode[%u] = 0x%02x, expected 0x%02x",
			      (unsigned)i, cap->opcodes[i], expected_init[i]);
	}

	/* Full-frame write → load DTM halves, then run the refresh cycle. */
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(frame),
		.width = 1200,
		.height = 1600,
		.pitch = 1200,
	};

	el133_capture_reset();
	zassert_ok(display_write(disp, 0, 0, &desc, frame), "display_write failed");

	zassert_equal(cap->count, ARRAY_SIZE(expected_refresh),
		      "captured %u refresh opcodes, expected %u",
		      (unsigned)cap->count, (unsigned)ARRAY_SIZE(expected_refresh));
	for (size_t i = 0; i < ARRAY_SIZE(expected_refresh); i++) {
		zassert_equal(cap->opcodes[i], expected_refresh[i],
			      "refresh opcode[%u] = 0x%02x, expected 0x%02x",
			      (unsigned)i, cap->opcodes[i], expected_refresh[i]);
	}
}

ZTEST_SUITE(el133_seq, NULL, NULL, NULL, NULL, NULL);
