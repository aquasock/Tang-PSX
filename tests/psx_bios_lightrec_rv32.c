// SPDX-License-Identifier: GPL-3.0-only
//
// Run SCPH-1001 to the logo checkpoint with Lightrec as the CPU core under
// qemu-riscv32, using the hardware psx_bios program's event-driven service
// loop (VBlank when the BIOS is seen waiting for it). Bare metal: no libc
// startup, newlib-nano over software/lightrec/runtime.c. Statistics go to
// stderr and the 640x480 display to stdout as a binary PPM. START_CYCLES
// starts the guest clock elsewhere, to cross bit 31 or the 32-bit wrap; the
// statistics count from it.

#include <stdint.h>
#include <stdio.h>

#include "machine.h"
#include "memmanager.h"
#include "psx_lightrec.h"
#include "runtime.h"

#define SERVICE_INSTRUCTIONS 256u
#define MAX_GUEST_CYCLES 400000000u
#define CODE_BUFFER_BYTES (8u << 20)
#ifndef START_CYCLES
#define START_CYCLES 0u
#endif

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES] __attribute__((aligned(4096)));
static uint16_t psx_vram[PSX_VRAM_PIXELS];
static uint8_t code_buffer[CODE_BUFFER_BYTES] __attribute__((aligned(4096)));
static uint16_t display[640u * 480u];
static uint8_t ppm_row[640u * 3u];
static struct psx_machine machine;

long tpx_runtime_write(int fd, const void *buffer, size_t length)
{
	register long a0 __asm__("a0") = fd;
	register long a1 __asm__("a1") = (long)buffer;
	register long a2 __asm__("a2") = (long)length;
	register long a7 __asm__("a7") = 64;
	__asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7)
		: "memory");
	return a0;
}

void tpx_runtime_exit(int status)
{
	register long a0 __asm__("a0") = status;
	register long a7 __asm__("a7") = 93;
	for (;;)
		__asm__ volatile ("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void write_all(int fd, const void *buffer, size_t length)
{
	const uint8_t *p = buffer;
	while (length) {
		long written = tpx_runtime_write(fd, p, length);
		if (written <= 0)
			return;
		p += written;
		length -= (size_t)written;
	}
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

int main(void)
{
	const struct psx_lightrec_stats *stats;
	uint32_t calls = 0;
	uint32_t flags = 0;

	if ((uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES)
		return 2;
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	machine.cpu.cycles = START_CYCLES;
	if (psx_lightrec_init(&machine, code_buffer, sizeof(code_buffer))) {
		fprintf(stderr, "psx_lightrec_init failed\n");
		return 3;
	}
	while (!logo_complete() &&
	       machine.cpu.cycles - START_CYCLES < MAX_GUEST_CYCLES) {
		if (psx_machine_waiting_for_vblank(&machine))
			psx_machine_vblank(&machine);
		else if ((flags = psx_lightrec_run(&machine,
			  SERVICE_INSTRUCTIONS)))
			break;
		++calls;
	}
	stats = psx_lightrec_stats();
	fprintf(stderr, "calls=%lu instructions=%lu vblank=%lu gpu_words=%lu "
		"primitives=%lu uploads=%lu dma_words=%lu complete=%d\n",
		(unsigned long)calls,
		(unsigned long)(machine.cpu.cycles - START_CYCLES),
		(unsigned long)machine.vblanks,
		(unsigned long)machine.gpu.command_words,
		(unsigned long)machine.gpu.primitives,
		(unsigned long)machine.gpu.uploads,
		(unsigned long)machine.dma_words, logo_complete());
	fprintf(stderr, "lightrec_runs=%lu interrupts=%lu syscalls=%lu "
		"breaks=%lu gte=%lu io_reads=%lu "
		"io_writes=%lu\n",
		(unsigned long)stats->runs, (unsigned long)stats->interrupts,
		(unsigned long)stats->syscalls, (unsigned long)stats->breaks,
		(unsigned long)stats->gte_commands,
		(unsigned long)stats->io_reads, (unsigned long)stats->io_writes);
	fprintf(stderr, "code_emissions=%lu code_bytes=%u ir_bytes=%u "
		"dma_invalidations=%lu heap_bytes=%lu exit_flags=0x%lx "
		"unknown_reads=%lu unknown_writes=%lu\n",
		(unsigned long)stats->code_emissions,
		lightrec_get_mem_usage(MEM_FOR_CODE),
		lightrec_get_mem_usage(MEM_FOR_IR),
		(unsigned long)stats->dma_invalidations,
		(unsigned long)tpx_runtime_heap_used(), (unsigned long)flags,
		(unsigned long)machine.unknown_reads,
		(unsigned long)machine.unknown_writes);
	write_display();
	return logo_complete() ? 0 : 1;
}

__asm__(
	"	.section .text.start,\"ax\",@progbits\n"
	"	.globl _start\n"
	"_start:\n"
	"	call main\n"
	"	call tpx_runtime_exit\n"
	"	.text\n");
