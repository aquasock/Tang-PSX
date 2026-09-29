// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>

#include "jit.h"

static uint8_t ram[PSX_RAM_BYTES];
static uint8_t reference_ram[PSX_RAM_BYTES];
static struct psx_cpu cpu;
static struct psx_cpu reference_cpu;
static struct psx_jit jit;

static void store32(uint8_t *memory, uint32_t address, uint32_t value)
{
	memory[address] = (uint8_t)value;
	memory[address + 1u] = (uint8_t)(value >> 8);
	memory[address + 2u] = (uint8_t)(value >> 16);
	memory[address + 3u] = (uint8_t)(value >> 24);
}

static int compare_cpu(void)
{
	uint32_t n;
	for (n = 0; n < 32u; ++n) {
		if (cpu.gpr[n] != reference_cpu.gpr[n])
			return (int)n + 1;
	}
	if (cpu.hi != reference_cpu.hi || cpu.lo != reference_cpu.lo) return 33;
	if (cpu.pc != reference_cpu.pc) return 35;
	if (cpu.next_pc != reference_cpu.next_pc) return 36;
	if (cpu.cycles != reference_cpu.cycles) return 37;
	if (cpu.exception_count != reference_cpu.exception_count) return 38;
	if (cpu.branch_pc != reference_cpu.branch_pc) return 39;
	if (cpu.load_value != reference_cpu.load_value) return 40;
	if (cpu.load_reg != reference_cpu.load_reg) return 41;
	if (cpu.load_pending != reference_cpu.load_pending) return 42;
	if (cpu.in_delay_slot != reference_cpu.in_delay_slot) return 43;
	if (cpu.next_delay_slot != reference_cpu.next_delay_slot) return 44;
	return 0;
}

static int run_case(const uint32_t *program, uint32_t words, uint32_t steps,
	uint32_t seed, uint32_t repeats)
{
	uint32_t n;
	psx_jit_reset(&jit);
	while (repeats-- != 0u) {
		for (n = 0; n < PSX_RAM_BYTES; ++n) {
			ram[n] = 0;
			reference_ram[n] = 0;
		}
		for (n = 0; n < words; ++n) {
			store32(ram, n * 4u, program[n]);
			store32(reference_ram, n * 4u, program[n]);
		}
		store32(ram, 0x100u, 0x89abcdefu);
		store32(reference_ram, 0x100u, 0x89abcdefu);
		psx_cpu_reset(&cpu, ram, sizeof(ram), 0u);
		psx_cpu_reset(&reference_cpu, reference_ram,
			sizeof(reference_ram), 0u);
		for (n = 1; n < 32u; ++n) {
			cpu.gpr[n] = seed * (n * 0x10201u + 0x1234567u);
			reference_cpu.gpr[n] = cpu.gpr[n];
		}
		if (psx_cpu_run(&reference_cpu, steps) !=
		    psx_jit_run(&jit, &cpu, steps))
			return -1;
		if (compare_cpu())
			return compare_cpu();
		for (n = 0; n < PSX_RAM_BYTES; ++n) {
			if (ram[n] != reference_ram[n])
				return 34;
		}
	}
	return 0;
}

#define MIPS_I(op, rs, rt, imm) (((op) << 26) | ((rs) << 21) | \
	((rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define MIPS_R(rs, rt, rd, sa, fn) (((rs) << 21) | ((rt) << 16) | \
	((rd) << 11) | ((sa) << 6) | (fn))

int psx_jit_rv32_test(void)
{
	/*
	 * Increment t0 one hundred times. Each four-instruction loop body is one
	 * compiled block containing the branch and its delay slot.
	 */
	static const uint32_t alu_program[] = {
		MIPS_I(15u, 0u, 1u, 0x9234u),
		MIPS_I(13u, 1u, 1u, 0xfedcu),
		MIPS_I(9u, 1u, 2u, -32768),
		MIPS_I(12u, 1u, 3u, 0xf0f0u),
		MIPS_I(14u, 3u, 4u, 0xffffu),
		MIPS_I(10u, 2u, 5u, -1),
		MIPS_I(11u, 2u, 6u, -1),
		MIPS_R(0u, 6u, 7u, 3u, 0u),
		MIPS_R(0u, 2u, 8u, 4u, 3u),
		MIPS_R(0u, 2u, 9u, 5u, 2u),
		MIPS_R(8u, 9u, 10u, 0u, 0x21u),
		MIPS_R(8u, 9u, 11u, 0u, 0x23u),
		MIPS_R(10u, 11u, 12u, 0u, 0x27u),
		MIPS_R(11u, 10u, 13u, 0u, 0x2au),
		MIPS_R(11u, 10u, 14u, 0u, 0x2bu),
		MIPS_R(7u, 6u, 15u, 0u, 4u),
	};
	static const uint32_t load_program[] = {
		MIPS_I(9u, 0u, 1u, 0x100u),
		MIPS_I(43u, 1u, 2u, 0u),
		MIPS_I(35u, 1u, 3u, 0u),
		MIPS_I(9u, 3u, 4u, 1u),
		MIPS_I(9u, 3u, 5u, 2u),
		MIPS_I(40u, 1u, 5u, 4u),
		MIPS_I(36u, 1u, 6u, 4u),
		MIPS_I(9u, 6u, 7u, 0u),
		0x0000000cu,
	};
	static const uint32_t first_bailout_program[] = {
		MIPS_I(43u, 1u, 2u, 0u),
		MIPS_I(9u, 8u, 8u, 1u), MIPS_I(9u, 9u, 9u, 2u),
		MIPS_I(9u, 10u, 10u, 3u), MIPS_I(9u, 11u, 11u, 4u),
		MIPS_I(9u, 12u, 12u, 5u), MIPS_I(9u, 13u, 13u, 6u),
		MIPS_I(9u, 14u, 14u, 7u),
	};
	static const uint32_t branch_programs[][2] = {
		{MIPS_I(4u, 1u, 1u, 3u), MIPS_I(9u, 10u, 10u, 1u)},
		{MIPS_I(5u, 1u, 2u, 3u), MIPS_I(9u, 10u, 10u, 1u)},
		{MIPS_I(6u, 0u, 0u, 3u), MIPS_I(9u, 10u, 10u, 1u)},
		{MIPS_I(7u, 1u, 0u, 3u), MIPS_I(9u, 10u, 10u, 1u)},
		{MIPS_I(1u, 1u, 0u, 3u), MIPS_I(9u, 10u, 10u, 1u)},
		{MIPS_I(1u, 1u, 17u, 3u), MIPS_I(9u, 10u, 10u, 1u)},
	};
	static const uint32_t misaligned_program[] = {
		MIPS_I(9u, 0u, 1u, 0x101u),
		MIPS_I(9u, 0u, 8u, 1u), MIPS_I(9u, 0u, 9u, 2u),
		MIPS_I(9u, 0u, 10u, 3u), MIPS_I(9u, 0u, 11u, 4u),
		MIPS_I(9u, 0u, 12u, 5u), MIPS_I(9u, 0u, 13u, 6u),
		MIPS_I(35u, 1u, 2u, 0u),
	};
	static const uint32_t mmio_program[] = {
		MIPS_I(15u, 0u, 1u, 0x1f80u),
		MIPS_I(9u, 0u, 8u, 1u), MIPS_I(9u, 0u, 9u, 2u),
		MIPS_I(9u, 0u, 10u, 3u), MIPS_I(9u, 0u, 11u, 4u),
		MIPS_I(9u, 0u, 12u, 5u), MIPS_I(9u, 0u, 13u, 6u),
		MIPS_I(35u, 1u, 2u, 0x1000u),
	};
	uint32_t n;

	store32(ram, 0x00u, 0x25080001u); /* addiu t0,t0,1 */
	store32(ram, 0x04u, 0x254a0001u); /* addiu t2,t2,1 */
	store32(ram, 0x08u, 0x01485826u); /* xor   t3,t2,t0 */
	store32(ram, 0x0cu, 0x016a6025u); /* or    t4,t3,t2 */
	store32(ram, 0x10u, 0x018b6824u); /* and   t5,t4,t3 */
	store32(ram, 0x14u, 0x2d090064u); /* sltiu t1,t0,100 */
	store32(ram, 0x18u, 0x1520fff9u); /* bne   t1,zero,0 */
	store32(ram, 0x1cu, 0x00000000u); /* nop */
	store32(ram, 0x20u, 0x25020000u); /* addiu v0,t0,0 */
	store32(ram, 0x24u, 0x08000009u); /* j 0x24 */
	store32(ram, 0x28u, 0x00000000u); /* nop */
	psx_cpu_reset(&cpu, ram, sizeof(ram), 0u);
	psx_jit_reset(&jit);
	if (psx_jit_run(&jit, &cpu, 801u) != 0)
		return 1;
	if (cpu.gpr[8] != 100u || cpu.gpr[2] != 100u || cpu.pc != 0x24u)
		return 2;
	if (cpu.cycles != 801u)
		return 3;
	if (jit.executed_blocks == 0u)
		return 5;
	if (jit.executed_instructions + jit.interpreter_instructions != 801u)
		return 6;
	if (jit.compiled_blocks == 0u)
		return 4;
	{
		int mismatch = run_case(alu_program,
			sizeof(alu_program) / sizeof(alu_program[0]), 16u, 7u, 5u);
		if (mismatch)
			return 40 + mismatch;
	}
	for (n = 0; n < sizeof(branch_programs) / sizeof(branch_programs[0]);
	     ++n) {
		if (run_case(branch_programs[n], 2u, 2u, n + 1u, 1u))
			return 9 + (int)n;
	}
	{
		int mismatch = run_case(load_program,
			sizeof(load_program) / sizeof(load_program[0]), 8u, 11u, 5u);
		if (mismatch)
			return 100 + mismatch;
	}
	/* A guard bailout on instruction zero must fall back and make progress. */
	for (n = 0; n < PSX_RAM_BYTES; ++n)
		ram[n] = 0;
	for (n = 0; n < sizeof(first_bailout_program) /
	    sizeof(first_bailout_program[0]); ++n)
		store32(ram, n * 4u, first_bailout_program[n]);
	psx_jit_reset(&jit);
	for (n = 0; n < 4u; ++n) {
		psx_cpu_reset(&cpu, ram, sizeof(ram), 0u);
		cpu.gpr[1] = 0x100u;
		cpu.gpr[2] = 0x12345678u;
		if (psx_jit_run(&jit, &cpu, 8u))
			return 21;
	}
	if (jit.compiled_blocks == 0u)
		return 22;
	for (n = 0; n < PSX_RAM_BYTES; ++n)
		reference_ram[n] = ram[n];
	store32(ram, 0x100u, 0x89abcdefu);
	store32(reference_ram, 0x100u, 0x89abcdefu);
	psx_cpu_reset(&cpu, ram, sizeof(ram), 0u);
	psx_cpu_reset(&reference_cpu, reference_ram,
		sizeof(reference_ram), 0u);
	cpu.gpr[1] = reference_cpu.gpr[1] = 0x100u;
	cpu.gpr[2] = reference_cpu.gpr[2] = 0x76543210u;
	cpu.cp0[12] = reference_cpu.cp0[12] = 0x00010000u;
	{
		uint32_t fallback_before = jit.interpreter_instructions;
		if (psx_cpu_run(&reference_cpu, 8u) !=
		    psx_jit_run(&jit, &cpu, 8u))
			return 23;
		if (jit.interpreter_instructions == fallback_before)
			return 24;
		if (compare_cpu())
			return 25;
		for (n = 0; n < PSX_RAM_BYTES; ++n) {
			if (ram[n] != reference_ram[n])
				return 26;
		}
	}
	if (run_case(misaligned_program,
	    sizeof(misaligned_program) / sizeof(misaligned_program[0]),
	    8u, 13u, 5u))
		return 19;
	if (run_case(mmio_program,
	    sizeof(mmio_program) / sizeof(mmio_program[0]), 8u, 17u, 5u))
		return 20;
	/* A source change must reject the cached block before executing it. */
	for (n = 0; n < PSX_RAM_BYTES; ++n)
		ram[n] = 0;
	store32(ram, 0u, MIPS_I(9u, 0u, 2u, 1u));
	store32(ram, 4u, MIPS_I(9u, 0u, 3u, 3u));
	store32(ram, 8u, MIPS_I(9u, 0u, 4u, 4u));
	store32(ram, 12u, MIPS_I(9u, 0u, 5u, 5u));
	store32(ram, 16u, MIPS_I(9u, 0u, 6u, 6u));
	store32(ram, 20u, MIPS_I(9u, 0u, 7u, 7u));
	store32(ram, 24u, MIPS_I(9u, 0u, 8u, 8u));
	store32(ram, 28u, MIPS_I(9u, 0u, 9u, 9u));
	store32(ram, 32u, 0x0000000cu);
	psx_jit_reset(&jit);
	for (n = 0; n < 4u; ++n) {
		psx_cpu_reset(&cpu, ram, sizeof(ram), 0u);
		if (psx_jit_run(&jit, &cpu, 8u) || cpu.gpr[2] != 1u)
			return 16;
	}
	store32(ram, 0u, MIPS_I(9u, 0u, 2u, 2u));
	for (n = 0; n < 4u; ++n) {
		psx_cpu_reset(&cpu, ram, sizeof(ram), 0u);
		if (psx_jit_run(&jit, &cpu, 8u) || cpu.gpr[2] != 2u)
			return 17;
	}
	if (jit.compiled_blocks != 2u)
		return 18;
	return 0;
}
