// SPDX-License-Identifier: GPL-3.0-only
//
// Run SCPH-1001 to the logo checkpoint through the RV32 JIT under
// qemu-riscv32, using the same event-driven service loop as the hardware
// psx_bios program. Statistics go to stderr and the 640x480 display to stdout
// as a binary PPM.

#include <stddef.h>
#include <stdint.h>

#include "machine.h"

#define SERVICE_INSTRUCTIONS 256u
#define MAX_SERVICE_CALLS 4000000u

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES];
static uint16_t psx_vram[PSX_VRAM_PIXELS];
static uint16_t display[640u * 480u];
static uint8_t ppm_row[640u * 3u];
static struct psx_machine machine;

void *memset(void *destination, int value, size_t length)
{
	uint8_t *d = destination;
	while (length--)
		*d++ = (uint8_t)value;
	return destination;
}

void *memcpy(void *destination, const void *source, size_t length)
{
	uint8_t *d = destination;
	const uint8_t *s = source;
	while (length--)
		*d++ = *s++;
	return destination;
}

static long sys_write(int fd, const void *buffer, size_t length)
{
	register long a0 __asm__("a0") = fd;
	register long a1 __asm__("a1") = (long)buffer;
	register long a2 __asm__("a2") = (long)length;
	register long a7 __asm__("a7") = 64;
	__asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7)
		: "memory");
	return a0;
}

static void write_all(int fd, const void *buffer, size_t length)
{
	const uint8_t *p = buffer;
	while (length) {
		long written = sys_write(fd, p, length);
		if (written <= 0)
			return;
		p += written;
		length -= (size_t)written;
	}
}

static void put_text(const char *text)
{
	size_t length = 0;
	while (text[length])
		++length;
	write_all(2, text, length);
}

static void put_stat(const char *label, uint32_t value)
{
	char digits[11];
	int n = 10;
	digits[n] = 0;
	do {
		digits[--n] = (char)('0' + value % 10u);
		value /= 10u;
	} while (value);
	put_text(label);
	put_text("=");
	put_text(&digits[n]);
	put_text(" ");
}

static int logo_complete(void)
{
	return machine.gpu.command_words >= 10768u &&
		machine.gpu.primitives >= 414u && machine.gpu.uploads >= 63u &&
		machine.dma_words >= 158497u;
}

static void write_display(void)
{
	static const char header[] = "P6\n640 480\n255\n";
	uint32_t y;
	psx_machine_copy_display(&machine, display, 640u, 480u);
	write_all(1, header, sizeof(header) - 1u);
	for (y = 0; y < 480u; ++y) {
		uint32_t x;
		for (x = 0; x < 640u; ++x) {
			uint16_t pixel = display[y * 640u + x];
			ppm_row[x * 3u] = (uint8_t)((pixel >> 8) & 0xf8u);
			ppm_row[x * 3u + 1u] = (uint8_t)((pixel >> 3) & 0xfcu);
			ppm_row[x * 3u + 2u] = (uint8_t)((pixel << 3) & 0xf8u);
		}
		write_all(1, ppm_row, sizeof(ppm_row));
	}
}

__attribute__((used)) int psx_bios_rv32_main(void)
{
	const struct psx_jit *jit = &machine.jit;
	uint32_t calls = 0;

	if ((uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES)
		return 2;
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	while (!logo_complete() && calls < MAX_SERVICE_CALLS) {
		if (psx_machine_waiting_for_vblank(&machine))
			psx_machine_vblank(&machine);
		else
			psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
		++calls;
	}
	put_stat("calls", calls);
	put_stat("instructions", machine.cpu.cycles);
	put_stat("accelerated", machine.accelerated_instructions);
	put_stat("vblank", machine.vblanks);
	put_stat("gpu_words", machine.gpu.command_words);
	put_stat("primitives", machine.gpu.primitives);
	put_stat("uploads", machine.gpu.uploads);
	put_stat("dma_words", machine.dma_words);
	put_stat("complete", (uint32_t)logo_complete());
	put_text("\n");
	put_stat("jit_compiled", jit->compiled_blocks);
	put_stat("jit_compiled_words", jit->compiled_words);
	put_stat("jit_rejected", jit->rejected_blocks);
	put_stat("jit_flushes", jit->cache_flushes);
	put_stat("jit_evictions", jit->evictions);
	put_stat("jit_invalidations", jit->invalidations);
	put_text("\n");
	put_stat("jit_blocks", jit->executed_blocks);
	put_stat("jit_instructions", jit->executed_instructions);
	put_stat("jit_early_exits", jit->early_exits);
	put_stat("jit_fallback", jit->interpreter_instructions);
	put_text("\n");
	put_stat("fallback_state", jit->fallback_state);
	put_stat("fallback_rejected", jit->fallback_rejected);
	put_stat("fallback_cold", jit->fallback_cold);
	put_stat("fallback_budget", jit->fallback_budget);
	put_stat("fallback_bailout", jit->fallback_bailout);
	put_text("\n");
	/* qemu-user rdcycle counts host ticks: only the proportions are useful. */
	put_stat("kcycles_cpu", (uint32_t)(machine.profile_cpu_cycles >> 10));
	put_stat("kcycles_gpu", (uint32_t)(machine.profile_gpu_cycles >> 10));
	put_stat("kcycles_accel", (uint32_t)(machine.profile_accel_cycles >> 10));
	put_text("\n");
	write_display();
	return logo_complete() ? 0 : 1;
}

__asm__(
	"	.section .text.start,\"ax\",@progbits\n"
	"	.globl _start\n"
	"_start:\n"
	"	call psx_bios_rv32_main\n"
	"	li a7, 93\n"
	"	ecall\n"
	"	.text\n");
