// SPDX-License-Identifier: GPL-3.0-only
//
// Compare the JIT and Lightrec at the disc boot handoff under qemu-riscv32.
// The runner passes an already-open disc file descriptor as DISC_FD.

#include <stdint.h>
#include <stdio.h>

#include "machine.h"
#include "runtime.h"
#ifdef PSX_DISC_LIGHTREC
#include "psx_lightrec.h"
#endif

#define FRAME_CYCLES 564480u
#define SERVICE_INSTRUCTIONS 256u
#define CODE_BUFFER_BYTES (8u << 20)
#ifndef CHECKPOINT_VBLANKS
#define CHECKPOINT_VBLANKS 600u
#endif

extern const uint8_t psx_bios_image[];

static uint8_t ram[PSX_MAIN_RAM_BYTES] __attribute__((aligned(4096)));
static uint16_t vram[PSX_VRAM_PIXELS];
static struct psx_machine machine;
#ifdef PSX_DISC_LIGHTREC
static uint8_t code_buffer[CODE_BUFFER_BYTES] __attribute__((aligned(4096)));
#endif

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

static int read_sector(void *opaque, uint32_t lba, uint8_t *sector)
{
	register long a0 __asm__("a0") = DISC_FD;
	register long a1 __asm__("a1") = (long)sector;
	register long a2 __asm__("a2") = PSX_CD_SECTOR_BYTES;
	register long a3 __asm__("a3") = (long)(lba * PSX_CD_SECTOR_BYTES);
	register long a4 __asm__("a4") = 0;
	register long a7 __asm__("a7") = 67;
	(void)opaque;
	__asm__ volatile ("ecall" : "+r"(a0) :
		"r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7) : "memory");
	return a0 == PSX_CD_SECTOR_BYTES ? 0 : -1;
}

static void report(void)
{
	const struct psx_cdrom *cd = &machine.cdrom;
	fprintf(stderr, "vblank=%lu cycles=%lu pc=%08lx gpu=%lu "
		"cd_cmds=%lu sectors=%lu last_lba=%lu cd_irq=%u/%u "
		"irq=%08lx/%08lx sr=%08lx epc=%08lx unknown=%lu/%lu\n",
		(unsigned long)machine.vblanks,
		(unsigned long)machine.cpu.cycles,
		(unsigned long)machine.cpu.pc,
		(unsigned long)machine.gpu.command_words,
		(unsigned long)cd->commands,
		(unsigned long)cd->sectors_read,
		(unsigned long)cd->last_lba, cd->irq_flag, cd->irq_enable,
		(unsigned long)machine.irq_status,
		(unsigned long)machine.irq_mask,
		(unsigned long)machine.cpu.cp0[12],
		(unsigned long)machine.cpu.cp0[14],
		(unsigned long)machine.unknown_reads,
		(unsigned long)machine.unknown_writes);
}

int main(void)
{
	const struct psx_disc disc = {
		.read = read_sector, .sectors = 281270u, .region = 'A',
	};
	uint32_t next_vblank = FRAME_CYCLES;
	uint32_t flags = 0;

	psx_machine_reset(&machine, ram, vram, psx_bios_image);
	psx_machine_insert_disc(&machine, &disc);
#ifdef PSX_DISC_LIGHTREC
	if (psx_lightrec_init(&machine, code_buffer, sizeof(code_buffer)))
		return 2;
#endif
	while (machine.vblanks < CHECKPOINT_VBLANKS) {
		if (psx_machine_waiting_for_vblank(&machine))
			psx_machine_idle_to(&machine, next_vblank);
		if ((int32_t)(machine.cpu.cycles - next_vblank) >= 0) {
			psx_machine_vblank(&machine);
			next_vblank += FRAME_CYCLES;
			if (machine.vblanks % 60u == 0)
				report();
		} else {
#ifdef PSX_DISC_LIGHTREC
			flags = psx_lightrec_run(&machine, SERVICE_INSTRUCTIONS);
			if (flags)
				break;
#else
			psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
#endif
		}
	}
	report();
	fprintf(stderr, "exit_flags=%08lx\n", (unsigned long)flags);
	return flags ? 1 : 0;
}

__asm__(
	"	.section .text.start,\"ax\",@progbits\n"
	"	.globl _start\n"
	"_start:\n"
	"	call main\n"
	"	call tpx_runtime_exit\n"
	"	.text\n");
