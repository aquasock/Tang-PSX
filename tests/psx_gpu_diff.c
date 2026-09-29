// SPDX-License-Identifier: GPL-3.0-only
//
// Differential GPU check: feed identical random GP0 streams to the reference
// rasterizer (ref_ prefixed, built from a pinned revision of gpu.c) and the
// current one, and require identical VRAM after every primitive.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu.h"

void ref_gpu_reset(struct psx_gpu *gpu, uint16_t *vram);
void ref_gpu_write_gp0(struct psx_gpu *gpu, uint32_t value);

static struct psx_gpu gpu;
static struct psx_gpu reference;
static uint16_t vram[PSX_VRAM_PIXELS];
static uint16_t reference_vram[PSX_VRAM_PIXELS];
static uint32_t state = 0x2545f491u;

static uint32_t random32(void)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static uint32_t below(uint32_t limit)
{
	return random32() % limit;
}

static void both(uint32_t word)
{
	psx_gpu_write_gp0(&gpu, word);
	ref_gpu_write_gp0(&reference, word);
}

static uint32_t coordinate(uint32_t span)
{
	/* Signed 11-bit x and y around the drawing area, some far outside. */
	int32_t x = (int32_t)below(span) - (int32_t)span / 8;
	int32_t y = (int32_t)below(span / 2u) - (int32_t)span / 16;
	return ((uint32_t)x & 0x7ffu) | (((uint32_t)y & 0x7ffu) << 16);
}

static void random_state(void)
{
	uint32_t x0 = below(512);
	uint32_t y0 = below(256);
	both(0xe1000000u | below(0x4000u));                    /* draw mode */
	both(0xe3000000u | x0 | (y0 << 10));                    /* area start */
	both(0xe4000000u | (x0 + below(1024u - x0)) |
		((y0 + below(512u - y0)) << 10));               /* area end */
	both(0xe5000000u | (below(256) & 0x7ffu) |
		((below(128) & 0x7ffu) << 11));                 /* offset */
	both(0xe6000000u | below(4));                           /* mask bits */
}

static void random_polygon(uint32_t span)
{
	uint32_t command = 0x20u | (below(32) & 0x1du);
	uint32_t vertices = (command & 8u) ? 4u : 3u;
	uint32_t n;

	both((command << 24) | (random32() & 0xffffffu));
	for (n = 0; n < vertices; ++n) {
		if (n != 0u && (command & 16u))
			both(random32() & 0xffffffu);
		both(coordinate(span));
		if (command & 4u) {
			uint32_t attribute = n == 0u ? below(0x8000u) :
				(n == 1u ? below(0x200u) : 0u);
			both((attribute << 16) | (random32() & 0xffffu));
		}
	}
}

int main(void)
{
	uint32_t primitives = 0;
	uint32_t n;

	ref_gpu_reset(&reference, reference_vram);
	psx_gpu_reset(&gpu, vram);
	/* Random VRAM contents give textures and CLUTs something to sample. */
	both(0xa0000000u);
	both(0);
	both((512u << 16) | 1024u);
	for (n = 0; n < PSX_VRAM_PIXELS / 2u; ++n)
		both(random32());
	for (n = 0; n < 40000u; ++n) {
		if (n % 64u == 0u)
			random_state();
		/* Mostly small primitives; every 16th spans the whole area. */
		random_polygon(n % 16u == 0u ? 2048u : 96u);
		++primitives;
		if (memcmp(vram, reference_vram, sizeof(vram)) != 0) {
			uint32_t i;
			for (i = 0; vram[i] == reference_vram[i]; ++i)
				;
			printf("mismatch after primitive %u at x=%u y=%u: "
				"%04x reference %04x\n", n, i % 1024u, i / 1024u,
				vram[i], reference_vram[i]);
			return 1;
		}
	}
	printf("PSX GPU: %u random polygons match the reference rasterizer "
		"(%u primitives)\n", primitives, gpu.primitives);
	return gpu.primitives == reference.primitives ? 0 : 1;
}
