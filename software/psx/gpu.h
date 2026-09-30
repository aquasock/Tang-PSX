// SPDX-License-Identifier: GPL-3.0-only

#ifndef TANG_PSX_GPU_H
#define TANG_PSX_GPU_H

#include <stdint.h>

#define PSX_VRAM_WIDTH  1024u
#define PSX_VRAM_HEIGHT 512u
#define PSX_VRAM_PIXELS (PSX_VRAM_WIDTH * PSX_VRAM_HEIGHT)

struct psx_gpu {
	uint16_t *vram;
	uint32_t status;
	uint32_t data_read;
	uint32_t packet[16];
	uint32_t packet_words;
	uint32_t packet_expected;
	uint32_t image_x;
	uint32_t image_y;
	uint32_t image_w;
	uint32_t image_h;
	uint32_t image_pixel;
	uint32_t image_pixels;
	uint32_t read_pixel;
	uint32_t read_pixels;
	uint32_t draw_x0;
	uint32_t draw_y0;
	uint32_t draw_x1;
	uint32_t draw_y1;
	int32_t draw_offset_x;
	int32_t draw_offset_y;
	uint32_t draw_mode;
	uint32_t texture_window;
	uint32_t mask_bits;
	uint32_t display_x;
	uint32_t display_y;
	uint32_t display_width;
	uint32_t display_height;
	uint32_t horizontal_range;
	uint32_t vertical_range;
	uint32_t command_words;
	uint32_t primitives;
	uint32_t uploads;
	uint32_t unknown_commands;
	uint8_t hardware_accel;
	uint8_t vram_cpu_dirty;
};

void psx_gpu_reset(struct psx_gpu *gpu, uint16_t *vram);
void psx_gpu_write_gp0(struct psx_gpu *gpu, uint32_t value);
void psx_gpu_write_gp1(struct psx_gpu *gpu, uint32_t value);
uint32_t psx_gpu_read_data(struct psx_gpu *gpu);
uint32_t psx_gpu_read_status(const struct psx_gpu *gpu);
/* Drain fabric rendering and invalidate cached VRAM before CPU reads it. */
void psx_gpu_sync(struct psx_gpu *gpu);

/*
 * Cumulative cost of feeding the fabric rasterizer (PSX_GPU_ACCEL builds on
 * the AE350; zero otherwise). Cycles are AE350 rdcycle counts; push includes
 * the part stalled on a full fabric FIFO.
 */
struct psx_gpu_accel_stats {
	uint64_t push_cycles;
	uint64_t stall_cycles;
	uint64_t wait_cycles;
	uint64_t flush_cycles;
	uint32_t words;
	uint32_t primitives;
	uint32_t pixels;
};
void psx_gpu_accel_stats(struct psx_gpu_accel_stats *stats);

#endif
