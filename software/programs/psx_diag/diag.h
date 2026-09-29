// SPDX-License-Identifier: GPL-3.0-only

#ifndef PSX_DIAG_H
#define PSX_DIAG_H

#include <stdint.h>

struct psx_diag_result {
	uint32_t passed;
	uint32_t total;
	uint32_t vector_crc32;
	uint32_t checksum;
	uint32_t failure;
	uint32_t expected;
	uint32_t observed;
};

int psx_diag_run(struct psx_diag_result *result);

#endif
