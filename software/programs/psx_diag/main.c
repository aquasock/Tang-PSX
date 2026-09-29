// SPDX-License-Identifier: GPL-3.0-only

#include "diag.h"
#include "psx_vectors.h"
#include "tpx_api.h"

#define RESULT_PASS 0x3000a001u
#define RESULT_FAIL 0xdead3000u

static const uint8_t font[26][7] = {
	['A' - 'A'] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
	['C' - 'A'] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
	['E' - 'A'] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
	['F' - 'A'] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
	['G' - 'A'] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
	['I' - 'A'] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f},
	['L' - 'A'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
	['P' - 'A'] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
	['S' - 'A'] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
	['T' - 'A'] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
	['U' - 'A'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
	['X' - 'A'] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
};

static uint32_t read_cycle(void)
{
	uint32_t value;
	__asm__ volatile ("rdcycle %0" : "=r"(value));
	return value;
}

static void log_text(const struct tpx_api *api, const char *text)
{
	while (*text)
		api->putc(*text++);
}

static void fill_rect(volatile uint16_t *pixels, uint32_t x0, uint32_t y0,
	uint32_t width, uint32_t height, uint16_t color)
{
	uint32_t y;
	for (y = y0; y < y0 + height; ++y) {
		uint32_t x;
		for (x = x0; x < x0 + width; ++x)
			pixels[y * TPX_FRAMEBUFFER_WIDTH + x] = color;
	}
}

static void draw_text(volatile uint16_t *pixels, uint32_t x, uint32_t y,
	const char *text, uint32_t scale, uint16_t color)
{
	while (*text) {
		char ch = *text++;
		uint32_t row;
		if (ch == ' ') {
			x += 6u * scale;
			continue;
		}
		for (row = 0; row < 7u; ++row) {
			uint32_t column;
			uint8_t bits = font[(uint32_t)(ch - 'A')][row];
			for (column = 0; column < 5u; ++column) {
				if (bits & (1u << (4u - column)))
					fill_rect(pixels, x + column * scale,
						y + row * scale, scale, scale, color);
			}
		}
		x += 6u * scale;
	}
}

static void draw_result(const struct tpx_api *api, int passed)
{
	volatile uint16_t *pixels =
		(volatile uint16_t *)(uintptr_t)TPX_FRAMEBUFFER_BASE;
	uint16_t background = passed ? 0x020cu : 0x4000u;
	uint16_t panel = passed ? 0x0448u : 0x7800u;
	uint16_t accent = passed ? 0x07e0u : 0xf800u;

	fill_rect(pixels, 0, 0, TPX_FRAMEBUFFER_WIDTH,
		TPX_FRAMEBUFFER_HEIGHT, background);
	fill_rect(pixels, 16, 16, 608, 448, 0xffffu);
	fill_rect(pixels, 24, 24, 592, 432, background);
	fill_rect(pixels, 64, 88, 512, 112, panel);
	fill_rect(pixels, 64, 280, 512, 112, panel);
	draw_text(pixels, 194, 118, "PSX CPU", 6, 0xffffu);
	draw_text(pixels, passed ? 176u : 158u, 310,
		passed ? "GTE PASS" : "GTE FAIL", 6, accent);
	api->flush_dcache();
}

uint32_t main(const struct tpx_api *api)
{
	struct psx_diag_result result;
	uint32_t start = read_cycle();
	int failed;

	api->set_reg(TPX_REG_STAGE, 0x00003000u);
	api->set_reg(TPX_REG_FAILURE, 0);
	log_text(api, "psx cpu/gte vectors\n");
	failed = psx_diag_run(&result);
	api->set_reg(TPX_REG_WORDS, result.passed);
	api->set_reg(TPX_REG_CHECKSUM, result.checksum);
	api->set_reg(TPX_REG_JIT,
		(PSX_CPU_VECTOR_COUNT << 16) | PSX_GTE_VECTOR_COUNT);
	api->set_reg(TPX_REG_CYCLES, read_cycle() - start);
	api->set_reg(TPX_REG_FAIL_ADDRESS, result.failure);
	api->set_reg(TPX_REG_FAIL_EXPECTED, result.expected);
	api->set_reg(TPX_REG_FAIL_OBSERVED, result.observed);
	if (failed) {
		api->set_reg(TPX_REG_FAILURE, result.failure);
		api->set_reg(TPX_REG_FEATURES, 0);
		api->set_reg(TPX_REG_STAGE, 0x80003badu);
		log_text(api, "psx vector fail\n");
		draw_result(api, 0);
		return RESULT_FAIL | (result.failure & 0xfffu);
	}
	api->set_reg(TPX_REG_FEATURES, 0x3fu);
	api->set_reg(TPX_REG_STAGE, 0x80003001u);
	log_text(api, "psx cpu/gte pass\n");
	draw_result(api, 1);
	return RESULT_PASS;
}
