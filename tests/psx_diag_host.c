// SPDX-License-Identifier: GPL-3.0-only

#include <stdio.h>

#include "diag.h"

int main(void)
{
	struct psx_diag_result result;
	int failed = psx_diag_run(&result);

	printf("PSX vectors: %u/%u passed, crc32=%08x checksum=%08x\n",
		result.passed, result.total, result.vector_crc32, result.checksum);
	if (failed)
		printf("failure=%08x expected=%08x observed=%08x\n",
			result.failure, result.expected, result.observed);
	return failed != 0;
}
