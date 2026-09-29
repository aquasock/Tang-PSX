// SPDX-License-Identifier: GPL-3.0-only
//
// Small, portable PlayStation R3000A/GTE interpreter used by Gate 1 tests and
// as the starting point for the runtime emulator.

#ifndef TANG_PSX_H
#define TANG_PSX_H

#include <stdint.h>

#define PSX_RAM_BYTES 65536u

enum psx_exception {
	PSX_EXC_INTERRUPT = 0,
	PSX_EXC_ADEL = 4,
	PSX_EXC_ADES = 5,
	PSX_EXC_IBE = 6,
	PSX_EXC_DBE = 7,
	PSX_EXC_SYSCALL = 8,
	PSX_EXC_BREAK = 9,
	PSX_EXC_RESERVED = 10,
	PSX_EXC_COP_UNUSABLE = 11,
	PSX_EXC_OVERFLOW = 12,
};

struct psx_gte {
	uint32_t data[32];
	uint32_t control[32];
};

struct psx_cpu {
	uint32_t gpr[32];
	uint32_t hi;
	uint32_t lo;
	uint32_t pc;
	uint32_t next_pc;
	uint32_t cp0[32];
	struct psx_gte gte;
	uint8_t *ram;
	uint32_t ram_size;
	uint32_t cycles;
	uint32_t exception_count;
	uint32_t branch_pc;
	uint32_t load_value;
	uint8_t load_reg;
	uint8_t load_pending;
	uint8_t in_delay_slot;
	uint8_t next_delay_slot;
};

void psx_gte_reset(struct psx_gte *gte);
uint32_t psx_gte_read_data(struct psx_gte *gte, uint32_t reg);
void psx_gte_write_data(struct psx_gte *gte, uint32_t reg, uint32_t value);
uint32_t psx_gte_read_control(const struct psx_gte *gte, uint32_t reg);
void psx_gte_write_control(struct psx_gte *gte, uint32_t reg, uint32_t value);
int psx_gte_command(struct psx_gte *gte, uint32_t instruction);

void psx_cpu_reset(struct psx_cpu *cpu, uint8_t *ram, uint32_t ram_size,
	uint32_t pc);
int psx_cpu_step(struct psx_cpu *cpu);
int psx_cpu_run(struct psx_cpu *cpu, uint32_t instruction_limit);

#endif
