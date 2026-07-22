/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EL133_CAPTURE_H_
#define EL133_CAPTURE_H_

#include <stddef.h>
#include <stdint.h>

/* Command opcodes recorded by the capture emulator (DC low == command). */
struct el133_capture {
	uint8_t opcodes[64];
	size_t count;
};

struct el133_capture *el133_capture_get(void);
void el133_capture_reset(void);

#endif /* EL133_CAPTURE_H_ */
