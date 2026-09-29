// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>

#include "machine.h"
#include "tpx_api.h"

#define PERF_INSTRUCTIONS 1000000u
#define RESULT_PASS 0x600d5001u

static uint8_t flat_ram[PSX_RAM_BYTES];
static uint8_t psx_ram[PSX_MAIN_RAM_BYTES];
static uint16_t psx_vram[PSX_VRAM_PIXELS];
static uint8_t psx_bios[PSX_BIOS_BYTES];
static struct psx_cpu flat_cpu;
static struct psx_machine machine;

static uint32_t read_cycle(void)
{
	uint32_t value;
	__asm__ volatile ("rdcycle %0" : "=r"(value));
	return value;
}

static uint32_t read_cache_control(void)
{
	uint32_t value;
	__asm__ volatile ("csrr %0, 0x7ca" : "=r"(value));
	return value;
}

static void store32(uint8_t *ram, uint32_t address, uint32_t value)
{
	ram[address] = (uint8_t)value;
	ram[address + 1u] = (uint8_t)(value >> 8);
	ram[address + 2u] = (uint8_t)(value >> 16);
	ram[address + 3u] = (uint8_t)(value >> 24);
}

static void memory_forever(uint8_t *ram)
{
	static const uint32_t program[16] = {
		0x24080100u, /* addiu t0,zero,0x100 */
		0x8d090000u, /* lw    t1,0(t0) */
		0x252a0001u, /* addiu t2,t1,1 (load delay) */
		0x012a5821u, /* addu  t3,t1,t2 */
		0xad0b0004u, /* sw    t3,4(t0) */
		0x910c0004u, /* lbu   t4,4(t0) */
		0x258d0002u, /* addiu t5,t4,2 (load delay) */
		0xa10d0008u, /* sb    t5,8(t0) */
		0x850e0004u, /* lh    t6,4(t0) */
		0x25cf0003u, /* addiu t7,t6,3 (load delay) */
		0xa50f000au, /* sh    t7,10(t0) */
		0x8d100008u, /* lw    s0,8(t0) */
		0x020b8826u, /* xor   s1,s0,t3 (load delay) */
		0x26320007u, /* addiu s2,s1,7 */
		0x1000fff1u, /* beq   zero,zero,0 */
		0x26940001u, /* addiu s4,s4,1 */
	};
	uint32_t n;
	for (n = 0; n < 16u; ++n)
		store32(ram, n * 4u, program[n]);
	store32(ram, 0x100u, 0x12345678u);
}

static void alu_forever(uint8_t *ram)
{
	static const uint32_t program[16] = {
		0x25080001u, /* addiu t0,t0,1 */
		0x25290003u, /* addiu t1,t1,3 */
		0x01095021u, /* addu  t2,t0,t1 */
		0x01495826u, /* xor   t3,t2,t1 */
		0x01686025u, /* or    t4,t3,t0 */
		0x01896824u, /* and   t5,t4,t1 */
		0x01a8702au, /* slt   t6,t5,t0 */
		0x01c9782bu, /* sltu  t7,t6,t1 */
		0x000f8080u, /* sll   s0,t7,2 */
		0x00108842u, /* srl   s1,s0,1 */
		0x00119043u, /* sra   s2,s1,1 */
		0x02509827u, /* nor   s3,s2,zero */
		0x3274ffffu, /* andi  s4,s3,0xffff */
		0x3a95a5a5u, /* xori  s5,s4,0xa5a5 */
		0x1000fff1u, /* beq   zero,zero,0 */
		0x26d60001u, /* addiu s6,s6,1 */
	};
	uint32_t n;
	for (n = 0; n < 16u; ++n)
		store32(ram, n * 4u, program[n]);
}

uint32_t main(const struct tpx_api *api)
{
	volatile uint16_t *framebuffer =
		(volatile uint16_t *)(uintptr_t)TPX_FRAMEBUFFER_BASE;
	uint32_t start;
	uint32_t interpreter_alu_cycles;
	uint32_t jit_alu_cycles;
	uint32_t interpreter_memory_cycles;
	uint32_t jit_memory_cycles;

	api->set_reg(TPX_REG_STAGE, 0x00005000u);
	api->set_reg(TPX_REG_FAILURE, 0);
	alu_forever(flat_ram);
	psx_cpu_reset(&flat_cpu, flat_ram, sizeof(flat_ram), 0);
	start = read_cycle();
	psx_cpu_run(&flat_cpu, PERF_INSTRUCTIONS);
	interpreter_alu_cycles = read_cycle() - start;

	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios);
	alu_forever(psx_ram);
	machine.cpu.pc = 0;
	machine.cpu.next_pc = 4;
	machine.cpu.cp0[12] = 0;
	start = read_cycle();
	psx_machine_run(&machine, PERF_INSTRUCTIONS);
	jit_alu_cycles = read_cycle() - start;

	memory_forever(flat_ram);
	psx_cpu_reset(&flat_cpu, flat_ram, sizeof(flat_ram), 0);
	start = read_cycle();
	psx_cpu_run(&flat_cpu, PERF_INSTRUCTIONS);
	interpreter_memory_cycles = read_cycle() - start;

	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios);
	memory_forever(psx_ram);
	machine.cpu.pc = 0;
	machine.cpu.next_pc = 4;
	machine.cpu.cp0[12] = 0;
	start = read_cycle();
	psx_machine_run(&machine, PERF_INSTRUCTIONS);
	jit_memory_cycles = read_cycle() - start;

	/* Keep the display coherent while the benchmark result is inspected. */
	psx_machine_copy_display(&machine, framebuffer,
		TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
	api->flush_dcache();

	api->set_reg(TPX_REG_WORDS, interpreter_alu_cycles);
	api->set_reg(TPX_REG_CHECKSUM, jit_alu_cycles);
	api->set_reg(TPX_REG_JIT, interpreter_memory_cycles);
	api->set_reg(TPX_REG_CYCLES, jit_memory_cycles);
	api->set_reg(TPX_REG_FEATURES, api->cpu_hz);
	api->set_reg(TPX_REG_FAIL_ADDRESS, read_cache_control());
	api->set_reg(TPX_REG_FAIL_EXPECTED, machine.jit.executed_blocks);
	api->set_reg(TPX_REG_FAIL_OBSERVED,
		(machine.jit.compiled_blocks << 16) |
		(machine.jit.interpreter_instructions & 0xffffu));
	api->set_reg(TPX_REG_STAGE, 0x80005001u);
	return RESULT_PASS;
}
