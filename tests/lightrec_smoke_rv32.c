// SPDX-License-Identifier: GPL-3.0-only
//
// Bare-metal smoke test of the Lightrec library under qemu-riscv32: no libc
// startup, newlib-nano over software/lightrec/runtime.c, code emitted only
// into caller-supplied buffers. It checks that GNU Lightning without mmap
// emits runnable code and rejects a buffer that is too small, then runs a
// small MIPS program through Lightrec's compiler and its interpreter and
// compares the results. Without the threaded compiler Lightrec interprets a
// block once and compiles it at once, so the first round compiles every block
// and the later rounds run only native code. The report goes to stderr.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <lightning.h>

#include "lightrec.h"
#include "memmanager.h"
#include "runtime.h"

#define RAM_BYTES 0x200000u
#define PROGRAM_PC 0x80010000u
#define RESULT_PC 0x80020000u
#define HW_MAGIC 0x5a5aa5a5u
#define ROUNDS 3u

enum {
	ZERO = 0, V0 = 2, A0 = 4, T0 = 8, T1, T2, T3, T4, T5, T6,
	S0 = 16, RA = 31,
};

#define R_TYPE(rs, rt, rd, shamt, funct) \
	(((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | \
	 ((uint32_t)(rd) << 11) | ((uint32_t)(shamt) << 6) | (funct))
#define I_TYPE(op, rs, rt, imm) \
	(((uint32_t)(op) << 26) | ((uint32_t)(rs) << 21) | \
	 ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define ADDU(rd, rs, rt) R_TYPE(rs, rt, rd, 0, 0x21)
#define MULTU(rs, rt) R_TYPE(rs, rt, 0, 0, 0x19)
#define MFLO(rd) R_TYPE(0, 0, rd, 0, 0x12)
#define JR(rs) R_TYPE(rs, 0, 0, 0, 0x08)
#define SYSCALL R_TYPE(0, 0, 0, 0, 0x0c)
#define NOP 0u
#define ADDIU(rt, rs, imm) I_TYPE(9, rs, rt, imm)
#define LUI(rt, imm) I_TYPE(15, 0, rt, imm)
#define BNE(rs, rt, words) I_TYPE(5, rs, rt, words)
#define LW(rt, offset, base) I_TYPE(35, base, rt, offset)
#define SW(rt, offset, base) I_TYPE(43, base, rt, offset)
#define JAL(target) ((3u << 26) | (((target) >> 2) & 0x3ffffffu))

// sum = 1000 + 999 + ... + 1, stored to RAM and reloaded; then ten calls of
// a function accumulating the square of the (already decremented) counter;
// one store to and one load from I/O registers; then a syscall.
static const uint32_t program[] = {
	ADDIU(T0, ZERO, 0),		// 0x00
	ADDIU(T1, ZERO, 1000),		// 0x04
	ADDU(T0, T0, T1),		// 0x08 loop:
	ADDIU(T1, T1, -1),		// 0x0c
	BNE(T1, ZERO, -3),		// 0x10 -> loop
	NOP,				// 0x14
	LUI(T2, RESULT_PC >> 16),	// 0x18
	SW(T0, 0, T2),			// 0x1c
	LW(T3, 0, T2),			// 0x20
	ADDIU(S0, ZERO, 10),		// 0x24
	ADDIU(A0, ZERO, 0),		// 0x28
	JAL(PROGRAM_PC + 0x50),		// 0x2c call:
	ADDIU(S0, S0, -1),		// 0x30
	BNE(S0, ZERO, -3),		// 0x34 -> call
	NOP,				// 0x38
	LUI(T4, 0x1f80),		// 0x3c
	SW(T3, 0x1070, T4),		// 0x40 I_STAT
	LW(T5, 0x1074, T4),		// 0x44 I_MASK
	SYSCALL,			// 0x48
	NOP,				// 0x4c
	MULTU(S0, S0),			// 0x50 function:
	MFLO(T6),			// 0x54
	ADDU(A0, A0, T6),		// 0x58
	JR(RA),				// 0x5c
	ADDU(V0, A0, T3),		// 0x60
};

static const struct {
	unsigned int reg;
	uint32_t value;
} expected[] = {
	{ T0, 500500u }, { T1, 0u }, { T2, RESULT_PC }, { T3, 500500u },
	{ S0, 0u }, { A0, 285u }, { V0, 500785u }, { T6, 0u },
	{ T4, 0x1f800000u }, { T5, HW_MAGIC }, { RA, PROGRAM_PC + 0x34u },
};

static uint8_t ram[RAM_BYTES] __attribute__((aligned(4096)));
static uint8_t bios[0x80000] __attribute__((aligned(4096)));
static uint8_t scratch_pad[0x400] __attribute__((aligned(64)));
static uint8_t parallel_port[0x10000];
static uint8_t hw_registers[0x8000];
static uint8_t code_buffer[1u << 20] __attribute__((aligned(4096)));
static uint8_t lightning_buffer[4096] __attribute__((aligned(16)));

static uint32_t hw_store_addr, hw_store_data, hw_stores, hw_loads;
static uint32_t code_invalidations;

static void hw_sb(struct lightrec_state *state, u32 opcode, void *host,
		  u32 addr, u32 data)
{
	(void)state; (void)opcode; (void)host;
	hw_store_addr = addr;
	hw_store_data = data;
	++hw_stores;
}

static void hw_sh(struct lightrec_state *state, u32 opcode, void *host,
		  u32 addr, u32 data)
{
	(void)state; (void)opcode; (void)host;
	hw_store_addr = addr;
	hw_store_data = data;
	++hw_stores;
}

static void hw_sw(struct lightrec_state *state, u32 opcode, void *host,
		  u32 addr, u32 data)
{
	(void)state; (void)opcode; (void)host;
	hw_store_addr = addr;
	hw_store_data = data;
	++hw_stores;
}

static u8 hw_lb(struct lightrec_state *state, u32 opcode, void *host, u32 addr)
{
	(void)state; (void)opcode; (void)host; (void)addr;
	++hw_loads;
	return (u8)HW_MAGIC;
}

static u16 hw_lh(struct lightrec_state *state, u32 opcode, void *host, u32 addr)
{
	(void)state; (void)opcode; (void)host; (void)addr;
	++hw_loads;
	return (u16)HW_MAGIC;
}

static u32 hw_lw(struct lightrec_state *state, u32 opcode, void *host, u32 addr)
{
	(void)state; (void)opcode; (void)host; (void)addr;
	++hw_loads;
	return HW_MAGIC;
}

static const struct lightrec_mem_map_ops hw_ops = {
	.sb = hw_sb, .sh = hw_sh, .sw = hw_sw,
	.lb = hw_lb, .lh = hw_lh, .lw = hw_lw,
};

static const struct lightrec_mem_map maps[] = {
	[PSX_MAP_KERNEL_USER_RAM] = {
		.pc = 0x00000000, .length = RAM_BYTES, .address = ram },
	[PSX_MAP_BIOS] = {
		.pc = 0x1fc00000, .length = sizeof(bios), .address = bios },
	[PSX_MAP_SCRATCH_PAD] = {
		.pc = 0x1f800000, .length = sizeof(scratch_pad),
		.address = scratch_pad },
	[PSX_MAP_PARALLEL_PORT] = {
		.pc = 0x1f000000, .length = sizeof(parallel_port),
		.address = parallel_port },
	[PSX_MAP_HW_REGISTERS] = {
		.pc = 0x1f801000, .length = sizeof(hw_registers),
		.address = hw_registers, .ops = &hw_ops },
	[PSX_MAP_MIRROR1] = {
		.pc = 0x00200000, .length = RAM_BYTES,
		.mirror_of = &maps[PSX_MAP_KERNEL_USER_RAM] },
	[PSX_MAP_MIRROR2] = {
		.pc = 0x00400000, .length = RAM_BYTES,
		.mirror_of = &maps[PSX_MAP_KERNEL_USER_RAM] },
	[PSX_MAP_MIRROR3] = {
		.pc = 0x00600000, .length = RAM_BYTES,
		.mirror_of = &maps[PSX_MAP_KERNEL_USER_RAM] },
	[PSX_MAP_CODE_BUFFER] = {
		.length = sizeof(code_buffer), .address = code_buffer },
};

static void cop2_op(struct lightrec_state *state, u32 op)
{
	(void)state; (void)op;
}

static void enable_ram(struct lightrec_state *state, _Bool enable)
{
	(void)state; (void)enable;
}

static void code_inv(void *addr, u32 length)
{
	(void)addr; (void)length;
	tpx_runtime_sync_icache();
	++code_invalidations;
}

static const struct lightrec_ops ops = {
	.cop2_op = cop2_op,
	.enable_ram = enable_ram,
	.code_inv = code_inv,
};

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

// Emit "return 0x12345678" into lightning_buffer with a code buffer of
// `length` bytes; return the result of calling it, or 0 if jit_emit failed.
static uint32_t lightning_emit_and_call(size_t length)
{
	jit_state_t *_jit = jit_new_state();
	uint32_t (*function)(void);
	uint32_t result = 0;

	jit_prolog();
	jit_movi(JIT_R0, 0x12345678);
	jit_retr(JIT_R0);
	jit_epilog();
	jit_realize();
	jit_set_data(NULL, 0, JIT_DISABLE_DATA | JIT_DISABLE_NOTE);
	jit_set_code(lightning_buffer, length);
	function = (uint32_t (*)(void))jit_emit();
	if (function) {
		tpx_runtime_sync_icache();
		result = function();
	}
	jit_destroy_state();
	return result;
}

static int lightning_compare_check(void)
{
	jit_state_t *_jit = jit_new_state();
	jit_node_t *arg;
	uint32_t (*compare)(uint32_t);
	int ok = 0;

	jit_prolog();
	arg = jit_arg();
	jit_getarg(JIT_R0, arg);
	jit_lti(JIT_R1, JIT_R0, 0xccd);
	jit_lti_u(JIT_R0, JIT_R0, 0x1000);
	jit_lshi(JIT_R0, JIT_R0, 1);
	jit_orr(JIT_R0, JIT_R0, JIT_R1);
	jit_retr(JIT_R0);
	jit_epilog();
	jit_realize();
	jit_set_data(NULL, 0, JIT_DISABLE_DATA | JIT_DISABLE_NOTE);
	jit_set_code(lightning_buffer, sizeof(lightning_buffer));
	compare = (uint32_t (*)(uint32_t))jit_emit();
	if (compare) {
		tpx_runtime_sync_icache();
		ok = compare(0xcccu) == 3u && compare(0xfffu) == 2u &&
			compare(0x1000u) == 0u && compare(0xffffffffu) == 1u;
	}
	jit_destroy_state();
	return ok;
}

static int lightning_check(void)
{
	uint32_t small, full;

	init_jit(NULL);
	small = lightning_emit_and_call(16);
	full = lightning_emit_and_call(sizeof(lightning_buffer));
	finish_jit();
	fprintf(stderr, "lightning: small buffer %s, full buffer returned "
		"0x%08lx\n", small ? "emitted" : "rejected",
		(unsigned long)full);
	return small == 0 && full == 0x12345678u;
}

static int run_program(struct lightrec_state *state, int interpreter,
		       uint32_t *exit_pc)
{
	struct lightrec_registers *regs = lightrec_get_registers(state);
	uint32_t pc = PROGRAM_PC, flags = 0;
	unsigned int i;

	memset(regs->gpr, 0, sizeof(regs->gpr));
	memset(&ram[RESULT_PC & (RAM_BYTES - 1)], 0, 4);
	for (i = 0; i < 64 && !(flags & LIGHTREC_EXIT_SYSCALL); ++i) {
		uint32_t target = lightrec_current_cycle_count(state) + 100000u;
		pc = interpreter ? lightrec_run_interpreter(state, pc, target) :
			lightrec_execute(state, pc, target);
		flags = lightrec_exit_flags(state);
		if (flags & ~LIGHTREC_EXIT_SYSCALL) {
			fprintf(stderr, "unexpected exit flags 0x%lx at 0x%08lx\n",
				(unsigned long)flags, (unsigned long)pc);
			return 0;
		}
	}
	*exit_pc = pc;
	if (!(flags & LIGHTREC_EXIT_SYSCALL))
		return 0;
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
		if (regs->gpr[expected[i].reg] != expected[i].value) {
			fprintf(stderr, "r%u = 0x%08lx, expected 0x%08lx\n",
				expected[i].reg,
				(unsigned long)regs->gpr[expected[i].reg],
				(unsigned long)expected[i].value);
			return 0;
		}
	}
	return 1;
}

int main(void)
{
	static char name[] = "lightrec";
	struct lightrec_state *state;
	uint32_t stored, exit_pc = 0;
	unsigned int round;
	int ok;

	ok = lightning_check();

	memcpy(&ram[PROGRAM_PC & (RAM_BYTES - 1)], program, sizeof(program));
	state = lightrec_init(name, maps, sizeof(maps) / sizeof(maps[0]), &ops);
	if (!state) {
		fprintf(stderr, "lightrec_init failed\n");
		return 1;
	}
	for (round = 0; round < ROUNDS; ++round) {
		int passed = run_program(state, 0, &exit_pc);
		memcpy(&stored, &ram[RESULT_PC & (RAM_BYTES - 1)], 4);
		passed = passed && stored == 500500u &&
			hw_store_data == 500500u &&
			(hw_store_addr & 0x1fffffffu) == 0x1f801070u;
		fprintf(stderr, "lightrec round %u: %s, exit pc 0x%08lx, "
			"native code %u bytes in %lu emissions\n", round,
			passed ? "ok" : "FAIL", (unsigned long)exit_pc,
			lightrec_get_mem_usage(MEM_FOR_CODE),
			(unsigned long)code_invalidations);
		ok = ok && passed;
	}
	ok = ok && lightrec_get_mem_usage(MEM_FOR_CODE) > 0 &&
		code_invalidations > 0;
	lightrec_invalidate_all(state);
	round = run_program(state, 1, &exit_pc);
	fprintf(stderr, "interpreter: %s, exit pc 0x%08lx\n",
		round ? "ok" : "FAIL", (unsigned long)exit_pc);
	ok = ok && round;
	fprintf(stderr, "hw_stores=%lu hw_loads=%lu code_invalidations=%lu "
		"heap_bytes=%lu\n", (unsigned long)hw_stores,
		(unsigned long)hw_loads, (unsigned long)code_invalidations,
		(unsigned long)tpx_runtime_heap_used());
	lightrec_destroy(state);
	init_jit(NULL);
	round = lightning_compare_check();
	finish_jit();
	fprintf(stderr, "large signed/unsigned compares: %s\n",
		round ? "ok" : "FAIL");
	ok = ok && round;
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
