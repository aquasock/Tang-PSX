// SPDX-License-Identifier: GPL-3.0-only

#include <stddef.h>

#include "runtime.h"
#include "tpx_platform.h"

static const struct tpx_api *platform_api;

void tpx_platform_attach(const struct tpx_api *api)
{
	platform_api = api;
}

long tpx_runtime_write(int fd, const void *buffer, size_t length)
{
	const char *text = buffer;
	size_t n;

	(void)fd;
	if (platform_api)
		for (n = 0; n < length; ++n)
			platform_api->putc(text[n]);
	return (long)length;
}

void tpx_runtime_exit(int status)
{
	if (platform_api) {
		platform_api->set_reg(TPX_REG_FAILURE,
			TPX_PLATFORM_EXIT_FAILURE | ((uint32_t)status & 0xffffu));
		platform_api->set_reg(TPX_REG_STAGE, TPX_PLATFORM_EXIT_STAGE);
	}
	for (;;)
		;
}
