// SPDX-License-Identifier: GPL-3.0-only

#include <stddef.h>
#include <stdint.h>

#include "runtime.h"
#include "tpx_api.h"
#include "tpx_runtime.h"

static const struct tpx_api *loader;
static uint32_t exit_stage;

void tpx_runtime_bind(const struct tpx_api *api, uint32_t failure_stage)
{
	loader = api;
	exit_stage = failure_stage;
}

long tpx_runtime_write(int fd, const void *buffer, size_t length)
{
	const uint8_t *bytes = buffer;
	size_t i;
	if (fd != 1 && fd != 2)
		return -1;
	for (i = 0; i < length; ++i)
		loader->putc((char)bytes[i]);
	return (long)length;
}

void tpx_runtime_exit(int status)
{
	loader->set_reg(TPX_REG_FAILURE, (uint32_t)status);
	loader->set_reg(TPX_REG_STAGE, exit_stage);
	for (;;)
		__asm__ volatile ("wfi");
}
