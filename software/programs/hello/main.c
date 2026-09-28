// SPDX-License-Identifier: GPL-3.0-only
//
// Minimal loader check: log a line, publish a marker, and return a known value.

#include "tpx_api.h"

#define HELLO_MARKER 0x48454c4fu    /* "HELO" */
#define HELLO_RESULT 0x600d0001u

static void puts_log(const struct tpx_api *api, const char *text)
{
	while (*text)
		api->putc(*text++);
}

uint32_t main(const struct tpx_api *api)
{
	puts_log(api, "hello from ddr3\n");
	api->set_reg(TPX_REG_CHECKSUM, HELLO_MARKER);
	return HELLO_RESULT;
}
