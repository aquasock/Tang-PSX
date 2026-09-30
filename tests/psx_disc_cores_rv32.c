// SPDX-License-Identifier: GPL-3.0-only
//
// Boot SCPH-1001 with a disc image under qemu-riscv32, using the hardware
// psx_disc program's service loop (VBlank every NTSC frame of guest cycles,
// the clock skipping ahead while the BIOS only waits for it) with a
// selectable R3000A core, as tests/psx_bios_cores_rv32.c does for the logo:
//
//   PSX_DISC_LIGHTREC        software/lightrec (link liblightrec.a)
//   otherwise                psx_machine_run (the RV32 JIT)
//
// DISC_PATH names the .bin image and SECONDS the emulated time to run. One
// line per emulated second goes to stderr: VBlanks, CD sectors read, GPU
// words, the guest PC and the Lightrec exit flags. The image is read with
// qemu-user's Linux system calls. CD_TRACE also logs every CD-ROM command and
// response with the guest cycle, for comparing cores. RUN_TRACE prints the
// guest PC, cycle count and a register checksum after every Lightrec run, so
// compiled and interpreted runs can be compared line by line.
//
// Built natively (no __riscv), the same harness uses libc and an executable
// mmap code buffer, so Lightrec runs on upstream GNU Lightning's host backend
// instead of the RV32 port.

#include <stdint.h>
#include <stdio.h>
#ifndef __riscv
#include <stdlib.h>
#include <sys/mman.h>
#endif

#include "machine.h"
#include "runtime.h"
#ifdef PSX_DISC_LIGHTREC
#include "psx_lightrec.h"
#endif

#define SERVICE_INSTRUCTIONS 256u
#define FRAME_CYCLES 564480u
#define CODE_BUFFER_BYTES (8u << 20)
#ifndef SECONDS
#define SECONDS 60u
#endif

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES] __attribute__((aligned(4096)));
static uint16_t psx_vram[PSX_VRAM_PIXELS];
#if defined(PSX_DISC_LIGHTREC) && defined(__riscv)
static uint8_t code_buffer[CODE_BUFFER_BYTES] __attribute__((aligned(4096)));
#endif
static struct psx_machine machine;
#ifdef __riscv
static long disc_fd;

static long syscall5(long number, long a0, long a1, long a2, long a3, long a4)
{
	register long r0 __asm__("a0") = a0;
	register long r1 __asm__("a1") = a1;
	register long r2 __asm__("a2") = a2;
	register long r3 __asm__("a3") = a3;
	register long r4 __asm__("a4") = a4;
	register long r7 __asm__("a7") = number;
	__asm__ volatile ("ecall" : "+r"(r0)
		: "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r7) : "memory");
	return r0;
}

long tpx_runtime_write(int fd, const void *buffer, size_t length)
{
	return syscall5(64, fd, (long)buffer, (long)length, 0, 0);
}

void tpx_runtime_exit(int status)
{
	for (;;)
		syscall5(93, status, 0, 0, 0, 0);
}

/* openat 56, _llseek 62 (rv32 has no lseek), read 63. */
static int read_sector(void *opaque, uint32_t lba, uint8_t *sector)
{
	uint64_t offset = (uint64_t)lba * PSX_CD_SECTOR_BYTES;
	uint64_t result;
	size_t done = 0;
	(void)opaque;
	if (syscall5(62, disc_fd, (long)(offset >> 32), (long)offset,
		     (long)&result, 0) < 0)
		return -1;
	while (done < PSX_CD_SECTOR_BYTES) {
		long n = syscall5(63, disc_fd, (long)(sector + done),
			(long)(PSX_CD_SECTOR_BYTES - done), 0, 0);
		if (n <= 0)
			return -1;
		done += (size_t)n;
	}
	return 0;
}
#else
static FILE *disc_file;

static int read_sector(void *opaque, uint32_t lba, uint8_t *sector)
{
	(void)opaque;
	if (fseek(disc_file, (long)lba * (long)PSX_CD_SECTOR_BYTES, SEEK_SET) ||
	    fread(sector, 1, PSX_CD_SECTOR_BYTES, disc_file) !=
	    PSX_CD_SECTOR_BYTES)
		return -1;
	return 0;
}
#endif

#ifdef CD_TRACE
static void trace_command(const struct psx_cdrom *cd, uint8_t command,
	uint32_t cycles)
{
	uint32_t n;
	fprintf(stderr, "cd %10lu cmd %02x", (unsigned long)cycles, command);
	for (n = 0; n < cd->param_count; ++n)
		fprintf(stderr, " %02x", cd->params[n]);
	fprintf(stderr, " pc %08lx\n", (unsigned long)machine.cpu.pc);
}

static void trace_response(const struct psx_cdrom *cd, uint32_t cycles)
{
	uint32_t n;
	fprintf(stderr, "cd %10lu INT%u", (unsigned long)cycles, cd->irq_flag);
	for (n = 0; n < cd->result_count; ++n)
		fprintf(stderr, " %02x", cd->result[n]);
	if (cd->irq_flag == 1u)
		fprintf(stderr, " lba %lu", (unsigned long)cd->last_lba);
	fprintf(stderr, "\n");
}
#endif

int main(void)
{
	static const char path[] = DISC_PATH;
	struct psx_disc disc = {0};
	uint64_t size;
	uint32_t next_vblank = FRAME_CYCLES;
	uint32_t flags = 0;
	uint32_t second = 0;

#ifdef __riscv
	disc_fd = syscall5(56, -100 /* AT_FDCWD */, (long)path, 0, 0, 0);
	if (disc_fd < 0 ||
	    syscall5(62, disc_fd, 0, 0, (long)&size, 2 /* SEEK_END */) < 0) {
		fprintf(stderr, "cannot open %s\n", path);
		return 2;
	}
#else
	disc_file = fopen(path, "rb");
	if (!disc_file || fseek(disc_file, 0, SEEK_END)) {
		fprintf(stderr, "cannot open %s\n", path);
		return 2;
	}
	size = (uint64_t)ftell(disc_file);
#endif
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	disc.read = read_sector;
	disc.sectors = (uint32_t)(size / PSX_CD_SECTOR_BYTES);
	disc.region = 'A';
	psx_machine_insert_disc(&machine, &disc);
#ifdef CD_TRACE
	machine.cdrom.trace_command = trace_command;
	machine.cdrom.trace_response = trace_response;
#endif
#ifdef PSX_DISC_LIGHTREC
#ifndef __riscv
	uint8_t *code_buffer = mmap(NULL, CODE_BUFFER_BYTES,
		PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS,
		-1, 0);
	if (code_buffer == MAP_FAILED)
		return 3;
#endif
	if (psx_lightrec_init(&machine, code_buffer, CODE_BUFFER_BYTES)) {
		fprintf(stderr, "psx_lightrec_init failed\n");
		return 3;
	}
#endif
	while (machine.cpu.cycles / (FRAME_CYCLES * 60u) < SECONDS) {
		if (psx_machine_waiting_for_vblank(&machine))
			psx_machine_idle_to(&machine, next_vblank);
		if ((int32_t)(machine.cpu.cycles - next_vblank) >= 0) {
			psx_machine_vblank(&machine);
			next_vblank += FRAME_CYCLES;
		} else {
#ifdef PSX_DISC_LIGHTREC
#ifdef FINE_FROM
			/* One block per run from FINE_FROM, to find a divergence. */
			flags = psx_lightrec_run(&machine,
				machine.cpu.cycles >= FINE_FROM ? 1u :
				SERVICE_INSTRUCTIONS);
#else
			flags = psx_lightrec_run(&machine, SERVICE_INSTRUCTIONS);
#endif
#ifdef RUN_TRACE
			{
				uint32_t sum = machine.cpu.hi * 31u + machine.cpu.lo;
				uint32_t r;
				for (r = 0; r < 32u; ++r)
					sum = sum * 31u + machine.cpu.gpr[r];
				fprintf(stderr, "t %08lx %08lx %08lx lo %08lx sr %08lx "
					"cause %08lx epc %08lx istat %04lx\n",
					(unsigned long)machine.cpu.cycles,
					(unsigned long)machine.cpu.pc,
					(unsigned long)sum,
					(unsigned long)machine.cpu.lo,
					(unsigned long)machine.cpu.cp0[12],
					(unsigned long)machine.cpu.cp0[13],
					(unsigned long)machine.cpu.cp0[14],
					(unsigned long)machine.irq_status);
#ifdef DUMP_AT
				if (machine.cpu.cycles == DUMP_AT) {
					for (r = 0; r < 32u; ++r)
						fprintf(stderr, "r%lu %08lx\n",
							(unsigned long)r,
							(unsigned long)machine.cpu.gpr[r]);
					fprintf(stderr, "hi %08lx lo %08lx\n",
						(unsigned long)machine.cpu.hi,
						(unsigned long)machine.cpu.lo);
					fwrite(psx_ram, 1, sizeof(psx_ram), stdout);
					fflush(stdout);
				}
#endif
			}
#endif
			if (flags)
				break;
#else
			psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
#endif
		}
		if (machine.cpu.cycles / (FRAME_CYCLES * 60u) != second) {
			second = machine.cpu.cycles / (FRAME_CYCLES * 60u);
			fprintf(stderr, "s %3lu vblank %5lu sectors %6lu gpu_words "
				"%8lu pc %08lx flags %lx\n", (unsigned long)second,
				(unsigned long)machine.vblanks,
				(unsigned long)machine.cdrom.sectors_read,
				(unsigned long)machine.gpu.command_words,
				(unsigned long)machine.cpu.pc,
				(unsigned long)flags);
		}
	}
	fprintf(stderr, "end vblank %lu sectors %lu pc %08lx flags %lx\n",
		(unsigned long)machine.vblanks,
		(unsigned long)machine.cdrom.sectors_read,
		(unsigned long)machine.cpu.pc, (unsigned long)flags);
	for (second = 0; second < 32u; ++second)
		fprintf(stderr, "r%-2lu %08lx%s", (unsigned long)second,
			(unsigned long)machine.cpu.gpr[second],
			second % 8u == 7u ? "\n" : " ");
#ifdef RAM_DUMP
	/* Main RAM to stdout, for tools/mipsdis.py. */
	fwrite(psx_ram, 1, sizeof(psx_ram), stdout);
#endif
	return 0;
}

#ifdef __riscv
__asm__(
	"	.section .text.start,\"ax\",@progbits\n"
	"	.globl _start\n"
	"_start:\n"
	"	call main\n"
	"	call tpx_runtime_exit\n"
	"	.text\n");
#endif
