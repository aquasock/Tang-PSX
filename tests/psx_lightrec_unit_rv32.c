// SPDX-License-Identifier: GPL-3.0-only
//
// Directed test of software/lightrec/psx_lightrec.c under qemu-riscv32, for
// the paths the SCPH-1001 logo run does not reach. A hand-assembled guest
// program with its own exception handler:
// - calls a function until Lightrec has compiled it, then overwrites the
//   function's first instruction by OTC DMA and calls it again: the new code
//   must run (DMA invalidation);
// - unmasks an already pending interrupt with an I/O write; the block ends
//   and the next one starts with a GTE command: the interrupt is taken there
//   and the command must run exactly once (the handler, like the BIOS, steps
//   over it, and Lightrec's JR/RFE handling moves the return back onto it);
// - completes a DMA with its interrupt enabled right before a SYSCALL in the
//   same block: the interrupt must come first, then the system call;
// - continues uncached through kseg1, as the BIOS cache flush does, and
//   stores to RAM with the cache isolated, which must not reach RAM. The
//   kseg1 address is loaded from memory: upstream Lightrec turns a JR to a
//   known address into a J, which stays in the caller's segment (kseg0).
// Results go to stderr.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "machine.h"
#include "psx_lightrec.h"
#include "runtime.h"

enum {
	ZERO = 0, AT = 1, V0 = 2, V1 = 3, T0 = 8, T1, T2, T3, T4, T5, T6, T7,
	S0 = 16, T9 = 25, K0 = 26, K1 = 27, RA = 31,
};

#define R_TYPE(rs, rt, rd, shamt, funct) \
	(((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | \
	 ((uint32_t)(rd) << 11) | ((uint32_t)(shamt) << 6) | (funct))
#define I_TYPE(op, rs, rt, imm) \
	(((uint32_t)(op) << 26) | ((uint32_t)(rs) << 21) | \
	 ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define NOP 0u
#define SLL(rd, rt, sa) R_TYPE(0, rt, rd, sa, 0x00)
#define SRL(rd, rt, sa) R_TYPE(0, rt, rd, sa, 0x02)
#define JR(rs) R_TYPE(rs, 0, 0, 0, 0x08)
#define SYSCALL R_TYPE(0, 0, 0, 0, 0x0c)
#define ADD(rd, rs, rt) R_TYPE(rs, rt, rd, 0, 0x20)
#define ADDIU(rt, rs, imm) I_TYPE(9, rs, rt, imm)
#define ANDI(rt, rs, imm) I_TYPE(12, rs, rt, imm)
#define ORI(rt, rs, imm) I_TYPE(13, rs, rt, imm)
#define LUI(rt, imm) I_TYPE(15, 0, rt, imm)
#define BEQ(rs, rt, words) I_TYPE(4, rs, rt, words)
#define BNE(rs, rt, words) I_TYPE(5, rs, rt, words)
#define LW(rt, offset, base) I_TYPE(35, base, rt, offset)
#define SW(rt, offset, base) I_TYPE(43, base, rt, offset)
#define J(target) ((2u << 26) | (((target) >> 2) & 0x3ffffffu))
#define JAL(target) ((3u << 26) | (((target) >> 2) & 0x3ffffffu))
#define MFC0(rt, rd) (0x40000000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MTC0(rt, rd) (0x40800000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MFC2(rt, rd) (0x48000000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MTC2(rt, rd) (0x48800000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define RFE 0x42000010u
#define GTE_SQR 0x4a000028u

#define MAIN_PC 0x80010000u
#define FUNCTION 0x00031024u
#define GTE_PC (MAIN_PC + 0x70u)
#define SYSCALL_PC (MAIN_PC + 0xb0u)
#define UNCACHED_PC (0xa0000000u | (MAIN_PC + 0xc8u))
#define DONE_PC (0xa0000000u | (MAIN_PC + 0xecu))
#define RESULTS 0x210u
#define EPC_LOG 0x230u
#define CAUSE_LOG 0x240u
#define ISOLATED_WORD 0x400u
#define JUMP_SLOT 0x300u

static const uint32_t handler[] = {
	MFC0(K1, 14),			// 0x80
	SLL(K0, T9, 2),			// 0x84
	SW(K1, EPC_LOG, K0),		// 0x88 EPC log
	MFC0(K0, 13),			// 0x8c
	SLL(AT, T9, 2),			// 0x90
	SW(K0, CAUSE_LOG, AT),		// 0x94 cause log
	ADDIU(T9, T9, 1),		// 0x98 exception count
	ANDI(K0, K0, 0x7c),		// 0x9c
	BNE(K0, ZERO, 10),		// 0xa0 not an interrupt: 0xcc
	NOP,				// 0xa4
	LUI(AT, 0x1f80),		// 0xa8
	SW(ZERO, 0x1070, AT),		// 0xac acknowledge every IRQ
	LW(K0, 0, K1),			// 0xb0
	NOP,				// 0xb4
	SRL(K0, K0, 25),		// 0xb8
	ADDIU(AT, ZERO, 0x25),		// 0xbc GTE command at EPC?
	BNE(K0, AT, 3),			// 0xc0 no: 0xd0
	NOP,				// 0xc4
	BEQ(ZERO, ZERO, 1),		// 0xc8 yes, it issued: skip it
	ADDIU(K1, K1, 4),		// 0xcc (syscall: resume after it)
	JR(K1),				// 0xd0
	RFE,				// 0xd4
};

static const uint32_t program[] = {
	LUI(T0, 0x4000),		// 0x00
	ORI(T0, T0, 0x0401),		// 0x04 CU2, IM2, IEc
	MTC0(T0, 12),			// 0x08
	ADDIU(V1, ZERO, 7),		// 0x0c
	ADDIU(S0, ZERO, 3),		// 0x10
	JAL(0x80000000u | FUNCTION),	// 0x14 three calls compile it
	ADDIU(S0, S0, -1),		// 0x18
	BNE(S0, ZERO, -3),		// 0x1c
	NOP,				// 0x20
	SW(V0, RESULTS + 0x0, ZERO),	// 0x24 = 1
	LUI(T1, 0x1f80),		// 0x28
	LUI(T2, FUNCTION >> 16),	// 0x2c
	ORI(T2, T2, FUNCTION & 0xffffu),// 0x30
	SW(T2, 0x10e0, T1),		// 0x34 DMA6 base
	ADDIU(T3, ZERO, 2),		// 0x38
	SW(T3, 0x10e4, T1),		// 0x3c DMA6 two words
	LUI(T3, 0x1100),		// 0x40
	ORI(T3, T3, 0x0002),		// 0x44
	SW(T3, 0x10e8, T1),		// 0x48 DMA6 start
	JAL(0x80000000u | FUNCTION),	// 0x4c
	NOP,				// 0x50
	SW(V0, RESULTS + 0x4, ZERO),	// 0x54 = 7
	ADDIU(T3, ZERO, 3),		// 0x58
	MTC2(T3, 9),			// 0x5c IR1 = 3
	ADDIU(T4, ZERO, 1),		// 0x60
	SW(T4, 0x1074, T1),		// 0x64 I_MASK: VBlank IRQ is pending
	J(GTE_PC),			// 0x68 end the block
	NOP,				// 0x6c
	GTE_SQR,			// 0x70 MAC1 = IR1 * IR1
	MFC2(T5, 25),			// 0x74
	NOP,				// 0x78
	SW(T5, RESULTS + 0x8, ZERO),	// 0x7c = 9
	SW(T9, RESULTS + 0xc, ZERO),	// 0x80 = 1 exception
	LUI(T3, 0x00c0),		// 0x84
	SW(T3, 0x10f4, T1),		// 0x88 DICR: master and DMA6 enable
	ADDIU(T4, ZERO, 9),		// 0x8c
	SW(T4, 0x1074, T1),		// 0x90 I_MASK: VBlank, DMA
	ADDIU(T3, ZERO, 0x1000),	// 0x94
	SW(T3, 0x10e0, T1),		// 0x98 DMA6 base
	ADDIU(T3, ZERO, 1),		// 0x9c
	SW(T3, 0x10e4, T1),		// 0xa0 DMA6 one word
	LUI(T3, 0x1100),		// 0xa4
	ORI(T3, T3, 0x0002),		// 0xa8
	SW(T3, 0x10e8, T1),		// 0xac DMA6 start: completes, DMA IRQ
	SYSCALL,			// 0xb0
	SW(T9, RESULTS + 0x10, ZERO),	// 0xb4 = 3 exceptions
	LW(T0, JUMP_SLOT, ZERO),	// 0xb8
	NOP,				// 0xbc
	JR(T0),				// 0xc0 continue through kseg1
	NOP,				// 0xc4
	LUI(T0, 0x4001),		// 0xc8
	MTC0(T0, 12),			// 0xcc isolate the cache
	ADDIU(T6, ZERO, -1),		// 0xd0
	SW(T6, ISOLATED_WORD, ZERO),	// 0xd4 must not reach RAM
	LUI(T0, 0x4000),		// 0xd8
	MTC0(T0, 12),			// 0xdc
	LW(T7, ISOLATED_WORD, ZERO),	// 0xe0
	NOP,				// 0xe4
	SW(T7, RESULTS + 0x14, ZERO),	// 0xe8 = 0x11111111
	J(DONE_PC),			// 0xec done
	NOP,				// 0xf0
};

/*
 * OTC DMA of two words at FUNCTION stores FUNCTION - 4 there, which decodes
 * as ADD v0, zero, v1, and its terminator 0x00ffffff at FUNCTION - 4.
 */
static const uint32_t function[] = {
	ADDIU(V0, ZERO, 1),
	JR(RA),
	NOP,
};

static const struct {
	uint32_t address;
	uint32_t value;
	const char *what;
} expected[] = {
	{ RESULTS + 0x0, 1u, "compiled function" },
	{ RESULTS + 0x4, 7u, "function after DMA overwrote it" },
	{ RESULTS + 0x8, 9u, "GTE command at the interrupt" },
	{ RESULTS + 0xc, 1u, "exceptions after the GTE command" },
	{ RESULTS + 0x10, 3u, "exceptions after the syscall" },
	{ RESULTS + 0x14, 0x11111111u, "RAM word stored while isolated" },
	{ EPC_LOG + 0x0, GTE_PC, "interrupt EPC" },
	{ CAUSE_LOG + 0x0, 0x400u, "interrupt cause" },
	{ EPC_LOG + 0x4, SYSCALL_PC, "DMA interrupt EPC" },
	{ CAUSE_LOG + 0x4, 0x400u, "DMA interrupt cause" },
	{ EPC_LOG + 0x8, SYSCALL_PC, "syscall EPC" },
	{ CAUSE_LOG + 0x8, 0x20u, "syscall cause" },
};

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES] __attribute__((aligned(4096)));
static uint8_t psx_bios[PSX_BIOS_BYTES];
static uint16_t psx_vram[PSX_VRAM_PIXELS];
static uint8_t code_buffer[1u << 20] __attribute__((aligned(4096)));
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

static uint32_t ram_word(uint32_t address)
{
	uint32_t value;
	memcpy(&value, &psx_ram[address], 4);
	return value;
}

int main(void)
{
	const struct psx_lightrec_stats *stats;
	uint32_t isolated = 0x11111111u;
	uint32_t uncached = UNCACHED_PC;
	uint32_t flags = 0;
	unsigned int i;
	int ok = 1;

	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios);
	memcpy(&psx_ram[0x80], handler, sizeof(handler));
	memcpy(&psx_ram[MAIN_PC & 0x1fffffu], program, sizeof(program));
	memcpy(&psx_ram[FUNCTION], function, sizeof(function));
	memcpy(&psx_ram[ISOLATED_WORD], &isolated, 4);
	memcpy(&psx_ram[JUMP_SLOT], &uncached, 4);
	machine.cpu.pc = MAIN_PC;
	machine.cpu.cp0[12] = 0x40000000u;
	machine.irq_status = 1u;
	if (psx_lightrec_init(&machine, code_buffer, sizeof(code_buffer))) {
		fprintf(stderr, "psx_lightrec_init failed\n");
		return 1;
	}
	for (i = 0; i < 64u && !flags; ++i)
		flags = psx_lightrec_run(&machine, 1000u);
	stats = psx_lightrec_stats();
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
		uint32_t value = ram_word(expected[i].address);
		int match = value == expected[i].value;
		fprintf(stderr, "%-32s 0x%08lx %s\n", expected[i].what,
			(unsigned long)value, match ? "ok" : "FAIL");
		ok = ok && match;
	}
	fprintf(stderr, "pc=0x%08lx exit_flags=0x%lx interrupts=%lu "
		"syscalls=%lu gte=%lu dma_invalidations=%lu\n",
		(unsigned long)machine.cpu.pc, (unsigned long)flags,
		(unsigned long)stats->interrupts,
		(unsigned long)stats->syscalls,
		(unsigned long)stats->gte_commands,
		(unsigned long)stats->dma_invalidations);
	ok = ok && !flags && machine.cpu.pc == DONE_PC &&
		stats->interrupts == 2u && stats->syscalls == 1u &&
		stats->gte_commands == 1u && stats->dma_invalidations == 3u;
	psx_lightrec_destroy();
	fprintf(stderr, "%s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}

__asm__(
	"	.section .text.start,\"ax\",@progbits\n"
	"	.globl _start\n"
	"_start:\n"
	"	call main\n"
	"	call tpx_runtime_exit\n"
	"	.text\n");
