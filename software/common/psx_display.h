// SPDX-License-Identifier: GPL-3.0-only

#ifndef TANG_PSX_DISPLAY_H
#define TANG_PSX_DISPLAY_H

#include <stdint.h>

#include "gpu.h"

#define PSX_DISPLAY_BASE  0xea000000u
#define PSX_DISPLAY_MAGIC 0x44535031u

static volatile uint32_t *const psx_display =
	(volatile uint32_t *)(uintptr_t)PSX_DISPLAY_BASE;

static inline int psx_display_available(void)
{
	return psx_display[0] == PSX_DISPLAY_MAGIC;
}

static inline void psx_display_blit(const struct psx_gpu *gpu)
{
	uint32_t width = gpu->display_width;
	uint32_t height = gpu->display_height;
	if (width == 0u || width > 640u)
		width = 320u;
	if (height == 0u || height > 480u)
		height = 240u;
	while (!(psx_display[1] & 1u))
		;
	psx_display[2] = (gpu->display_x & 0x3ffu) |
		((gpu->display_y & 0x1ffu) << 16);
	psx_display[3] = width | (height << 16);
	psx_display[1] = 1u;
	while (!(psx_display[1] & 1u))
		;
}

#endif
