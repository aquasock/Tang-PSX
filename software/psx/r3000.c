// SPDX-License-Identifier: GPL-3.0-only

#include "psx.h"

static uint32_t sign16(uint32_t value)
{
	return (uint32_t)(int32_t)(int16_t)value;
}

static int address_ptr(struct psx_cpu *cpu, uint32_t address, uint32_t bytes,
	uint8_t **pointer)
{
	uint32_t physical = address & 0x1fffffffu;

	if (physical > cpu->ram_size || bytes > cpu->ram_size - physical)
		return -1;
	*pointer = &cpu->ram[physical];
	return 0;
}

static int read8(struct psx_cpu *cpu, uint32_t address, uint32_t *value)
{
	uint8_t *p;
	if (cpu->bus_read)
		return cpu->bus_read(cpu->bus_opaque, address, 1, value);
	if (address_ptr(cpu, address, 1, &p))
		return -1;
	*value = p[0];
	return 0;
}

static int read16(struct psx_cpu *cpu, uint32_t address, uint32_t *value)
{
	uint8_t *p;
	if (cpu->bus_read)
		return cpu->bus_read(cpu->bus_opaque, address, 2, value);
	if (address_ptr(cpu, address, 2, &p))
		return -1;
	*value = p[0] | ((uint32_t)p[1] << 8);
	return 0;
}

static int read32(struct psx_cpu *cpu, uint32_t address, uint32_t *value)
{
	uint8_t *p;
	if (cpu->bus_read)
		return cpu->bus_read(cpu->bus_opaque, address, 4, value);
	if (address_ptr(cpu, address, 4, &p))
		return -1;
	*value = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		((uint32_t)p[3] << 24);
	return 0;
}

static int write8(struct psx_cpu *cpu, uint32_t address, uint32_t value)
{
	uint8_t *p;
	if (cpu->bus_write)
		return cpu->bus_write(cpu->bus_opaque, address, 1, value);
	if (address_ptr(cpu, address, 1, &p))
		return -1;
	p[0] = (uint8_t)value;
	return 0;
}

static int write16(struct psx_cpu *cpu, uint32_t address, uint32_t value)
{
	uint8_t *p;
	if (cpu->bus_write)
		return cpu->bus_write(cpu->bus_opaque, address, 2, value);
	if (address_ptr(cpu, address, 2, &p))
		return -1;
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	return 0;
}

static int write32(struct psx_cpu *cpu, uint32_t address, uint32_t value)
{
	uint8_t *p;
	if (cpu->bus_write)
		return cpu->bus_write(cpu->bus_opaque, address, 4, value);
	if (address_ptr(cpu, address, 4, &p))
		return -1;
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
	return 0;
}

static void write_reg(struct psx_cpu *cpu, uint32_t reg, uint32_t value,
	uint32_t *written)
{
	if (reg != 0u) {
		cpu->gpr[reg] = value;
		*written |= 1u << reg;
	}
}

static void delayed_load(struct psx_cpu *cpu, uint32_t reg, uint32_t value)
{
	if (reg != 0u) {
		cpu->load_pending = 1;
		cpu->load_reg = (uint8_t)reg;
		cpu->load_value = value;
	}
}

static int exception(struct psx_cpu *cpu, enum psx_exception code,
	uint32_t current_pc, int in_delay_slot, uint32_t bad_address,
	int set_bad_address, uint32_t coprocessor)
{
	uint32_t cause = (cpu->cp0[13] & 0x0000ff00u) |
		((uint32_t)code << 2) | (coprocessor << 28);
	uint32_t status = cpu->cp0[12];

	if (in_delay_slot) {
		cause |= 0x80000000u;
		cpu->cp0[14] = cpu->branch_pc;
	} else {
		cpu->cp0[14] = current_pc;
	}
	if (set_bad_address)
		cpu->cp0[8] = bad_address;
	cpu->cp0[13] = cause;
	cpu->cp0[12] = (status & ~0x3fu) | ((status << 2) & 0x3fu);
	cpu->pc = (status & (1u << 22)) ? 0xbfc00180u : 0x80000080u;
	cpu->next_pc = cpu->pc + 4u;
	cpu->in_delay_slot = 0;
	cpu->next_delay_slot = 0;
	++cpu->exception_count;
	return (int)code + 1;
}

static void branch(struct psx_cpu *cpu, uint32_t current_pc, uint32_t target,
	int taken)
{
	cpu->branch_pc = current_pc;
	cpu->in_delay_slot = 1;
	if (taken)
		cpu->next_pc = target;
}

static int overflow_add(uint32_t left, uint32_t right, uint32_t *result)
{
	int64_t value = (int64_t)(int32_t)left + (int64_t)(int32_t)right;
	*result = (uint32_t)value;
	return value > 0x7fffffffll || value < -0x80000000ll;
}

static int overflow_sub(uint32_t left, uint32_t right, uint32_t *result)
{
	int64_t value = (int64_t)(int32_t)left - (int64_t)(int32_t)right;
	*result = (uint32_t)value;
	return value > 0x7fffffffll || value < -0x80000000ll;
}

static int execute_special(struct psx_cpu *cpu, uint32_t instruction,
	uint32_t current_pc, int in_delay, uint32_t *written)
{
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	uint32_t rd = (instruction >> 11) & 31u;
	uint32_t sa = (instruction >> 6) & 31u;
	uint32_t value;
	uint64_t product;

	switch (instruction & 63u) {
	case 0x00: write_reg(cpu, rd, cpu->gpr[rt] << sa, written); break;
	case 0x02: write_reg(cpu, rd, cpu->gpr[rt] >> sa, written); break;
	case 0x03:
		write_reg(cpu, rd, (uint32_t)((int32_t)cpu->gpr[rt] >> sa), written);
		break;
	case 0x04:
		write_reg(cpu, rd, cpu->gpr[rt] << (cpu->gpr[rs] & 31u), written);
		break;
	case 0x06:
		write_reg(cpu, rd, cpu->gpr[rt] >> (cpu->gpr[rs] & 31u), written);
		break;
	case 0x07:
		write_reg(cpu, rd, (uint32_t)((int32_t)cpu->gpr[rt] >>
			(cpu->gpr[rs] & 31u)), written);
		break;
	case 0x08:
		if (cpu->gpr[rs] & 3u)
			return exception(cpu, PSX_EXC_ADEL, current_pc, in_delay,
				cpu->gpr[rs], 1, 0);
		branch(cpu, current_pc, cpu->gpr[rs], 1);
		break;
	case 0x09:
		value = cpu->gpr[rs];
		write_reg(cpu, rd, current_pc + 8u, written);
		if (value & 3u)
			return exception(cpu, PSX_EXC_ADEL, current_pc, in_delay,
				value, 1, 0);
		branch(cpu, current_pc, value, 1);
		break;
	case 0x0c:
		return exception(cpu, PSX_EXC_SYSCALL, current_pc, in_delay,
			0, 0, 0);
	case 0x0d:
		return exception(cpu, PSX_EXC_BREAK, current_pc, in_delay,
			0, 0, 0);
	case 0x10: write_reg(cpu, rd, cpu->hi, written); break;
	case 0x11: cpu->hi = cpu->gpr[rs]; break;
	case 0x12: write_reg(cpu, rd, cpu->lo, written); break;
	case 0x13: cpu->lo = cpu->gpr[rs]; break;
	case 0x18:
		product = (uint64_t)((int64_t)(int32_t)cpu->gpr[rs] *
			(int64_t)(int32_t)cpu->gpr[rt]);
		cpu->lo = (uint32_t)product;
		cpu->hi = (uint32_t)(product >> 32);
		break;
	case 0x19:
		product = (uint64_t)cpu->gpr[rs] * cpu->gpr[rt];
		cpu->lo = (uint32_t)product;
		cpu->hi = (uint32_t)(product >> 32);
		break;
	case 0x1a:
		if (cpu->gpr[rt] == 0u) {
			cpu->hi = cpu->gpr[rs];
			cpu->lo = (int32_t)cpu->gpr[rs] < 0 ? 1u : 0xffffffffu;
		} else if (cpu->gpr[rs] == 0x80000000u &&
			   cpu->gpr[rt] == 0xffffffffu) {
			cpu->lo = 0x80000000u;
			cpu->hi = 0;
		} else {
			cpu->lo = (uint32_t)((int32_t)cpu->gpr[rs] /
				(int32_t)cpu->gpr[rt]);
			cpu->hi = (uint32_t)((int32_t)cpu->gpr[rs] %
				(int32_t)cpu->gpr[rt]);
		}
		break;
	case 0x1b:
		if (cpu->gpr[rt] == 0u) {
			cpu->lo = 0xffffffffu;
			cpu->hi = cpu->gpr[rs];
		} else {
			cpu->lo = cpu->gpr[rs] / cpu->gpr[rt];
			cpu->hi = cpu->gpr[rs] % cpu->gpr[rt];
		}
		break;
	case 0x20:
		if (overflow_add(cpu->gpr[rs], cpu->gpr[rt], &value))
			return exception(cpu, PSX_EXC_OVERFLOW, current_pc, in_delay,
				0, 0, 0);
		write_reg(cpu, rd, value, written);
		break;
	case 0x21: write_reg(cpu, rd, cpu->gpr[rs] + cpu->gpr[rt], written); break;
	case 0x22:
		if (overflow_sub(cpu->gpr[rs], cpu->gpr[rt], &value))
			return exception(cpu, PSX_EXC_OVERFLOW, current_pc, in_delay,
				0, 0, 0);
		write_reg(cpu, rd, value, written);
		break;
	case 0x23: write_reg(cpu, rd, cpu->gpr[rs] - cpu->gpr[rt], written); break;
	case 0x24: write_reg(cpu, rd, cpu->gpr[rs] & cpu->gpr[rt], written); break;
	case 0x25: write_reg(cpu, rd, cpu->gpr[rs] | cpu->gpr[rt], written); break;
	case 0x26: write_reg(cpu, rd, cpu->gpr[rs] ^ cpu->gpr[rt], written); break;
	case 0x27: write_reg(cpu, rd, ~(cpu->gpr[rs] | cpu->gpr[rt]), written); break;
	case 0x2a:
		write_reg(cpu, rd, (int32_t)cpu->gpr[rs] < (int32_t)cpu->gpr[rt],
			written);
		break;
	case 0x2b: write_reg(cpu, rd, cpu->gpr[rs] < cpu->gpr[rt], written); break;
	default:
		return exception(cpu, PSX_EXC_RESERVED, current_pc, in_delay,
			0, 0, 0);
	}
	return 0;
}

static int execute_regimm(struct psx_cpu *cpu, uint32_t instruction,
	uint32_t current_pc, int in_delay, uint32_t *written)
{
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	int condition;
	int link = 0;

	switch (rt) {
	case 0x00: condition = (int32_t)cpu->gpr[rs] < 0; break;
	case 0x01: condition = (int32_t)cpu->gpr[rs] >= 0; break;
	case 0x10: condition = (int32_t)cpu->gpr[rs] < 0; link = 1; break;
	case 0x11: condition = (int32_t)cpu->gpr[rs] >= 0; link = 1; break;
	default:
		return exception(cpu, PSX_EXC_RESERVED, current_pc, in_delay,
			0, 0, 0);
	}
	if (link)
		write_reg(cpu, 31, current_pc + 8u, written);
	branch(cpu, current_pc, current_pc + 4u + (sign16(instruction) << 2),
		condition);
	return 0;
}

static int execute_cop0(struct psx_cpu *cpu, uint32_t instruction,
	uint32_t current_pc, int in_delay, uint32_t *written)
{
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	uint32_t rd = (instruction >> 11) & 31u;

	(void)written;
	if (rs == 0u) {
		delayed_load(cpu, rt, cpu->cp0[rd]);
		return 0;
	}
	if (rs == 4u) {
		cpu->cp0[rd] = cpu->gpr[rt];
		return 0;
	}
	if (rs == 16u && (instruction & 63u) == 0x10u) {
		cpu->cp0[12] = (cpu->cp0[12] & ~0xfu) |
			((cpu->cp0[12] >> 2) & 0xfu);
		return 0;
	}
	return exception(cpu, PSX_EXC_RESERVED, current_pc, in_delay, 0, 0, 0);
}

static int execute_cop2(struct psx_cpu *cpu, uint32_t instruction,
	uint32_t current_pc, int in_delay)
{
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	uint32_t rd = (instruction >> 11) & 31u;

	if (rs & 0x10u) {
		if (psx_gte_command(&cpu->gte, instruction))
			return exception(cpu, PSX_EXC_RESERVED, current_pc, in_delay,
				0, 0, 0);
		return 0;
	}
	switch (rs) {
	case 0: delayed_load(cpu, rt, psx_gte_read_data(&cpu->gte, rd)); break;
	case 2: delayed_load(cpu, rt, psx_gte_read_control(&cpu->gte, rd)); break;
	case 4: psx_gte_write_data(&cpu->gte, rd, cpu->gpr[rt]); break;
	case 6: psx_gte_write_control(&cpu->gte, rd, cpu->gpr[rt]); break;
	default:
		return exception(cpu, PSX_EXC_RESERVED, current_pc, in_delay,
			0, 0, 0);
	}
	return 0;
}

void psx_cpu_reset(struct psx_cpu *cpu, uint8_t *ram, uint32_t ram_size,
	uint32_t pc)
{
	uint32_t i;

	for (i = 0; i < 32u; ++i) {
		cpu->gpr[i] = 0;
		cpu->cp0[i] = 0;
	}
	cpu->hi = 0;
	cpu->lo = 0;
	cpu->pc = pc;
	cpu->next_pc = pc + 4u;
	cpu->ram = ram;
	cpu->ram_size = ram_size;
	cpu->bus_read = 0;
	cpu->bus_write = 0;
	cpu->bus_opaque = 0;
	cpu->cycles = 0;
	cpu->exception_count = 0;
	cpu->branch_pc = 0;
	cpu->load_value = 0;
	cpu->load_reg = 0;
	cpu->load_pending = 0;
	cpu->in_delay_slot = 0;
	cpu->next_delay_slot = 0;
	psx_gte_reset(&cpu->gte);
}

void psx_cpu_reset_bus(struct psx_cpu *cpu, psx_bus_read_fn read_fn,
	psx_bus_write_fn write_fn, void *opaque, uint32_t pc)
{
	psx_cpu_reset(cpu, 0, 0, pc);
	cpu->bus_read = read_fn;
	cpu->bus_write = write_fn;
	cpu->bus_opaque = opaque;
}

int psx_cpu_step(struct psx_cpu *cpu)
{
	uint32_t instruction;
	uint32_t current_pc = cpu->pc;
	uint32_t written = 0;
	uint32_t old_load_value = cpu->load_value;
	uint32_t old_load_reg = cpu->load_reg;
	int old_load_pending = cpu->load_pending;
	int in_delay = cpu->in_delay_slot;
	uint32_t op;
	uint32_t rs;
	uint32_t rt;
	uint32_t address;
	uint32_t value;
	uint32_t memory;
	int result = 0;

	cpu->load_pending = 0;
	cpu->in_delay_slot = 0;
	if ((cpu->cp0[12] & 1u) &&
	    (cpu->cp0[12] & cpu->cp0[13] & 0x0000ff00u)) {
		result = exception(cpu, PSX_EXC_INTERRUPT, current_pc, in_delay,
			0, 0, 0);
		goto finish;
	}
	if (current_pc & 3u) {
		result = exception(cpu, PSX_EXC_ADEL, current_pc, in_delay,
			current_pc, 1, 0);
		goto finish;
	}
	if (read32(cpu, current_pc, &instruction)) {
		result = exception(cpu, PSX_EXC_IBE, current_pc, in_delay,
			current_pc, 0, 0);
		goto finish;
	}
	cpu->pc = cpu->next_pc;
	cpu->next_pc += 4u;
	++cpu->cycles;
	op = instruction >> 26;
	rs = (instruction >> 21) & 31u;
	rt = (instruction >> 16) & 31u;

	switch (op) {
	case 0x00:
		result = execute_special(cpu, instruction, current_pc, in_delay,
			&written);
		break;
	case 0x01:
		result = execute_regimm(cpu, instruction, current_pc, in_delay,
			&written);
		break;
	case 0x02:
		branch(cpu, current_pc, (cpu->pc & 0xf0000000u) |
			((instruction & 0x03ffffffu) << 2), 1);
		break;
	case 0x03:
		write_reg(cpu, 31, current_pc + 8u, &written);
		branch(cpu, current_pc, (cpu->pc & 0xf0000000u) |
			((instruction & 0x03ffffffu) << 2), 1);
		break;
	case 0x04:
		branch(cpu, current_pc, current_pc + 4u +
			(sign16(instruction) << 2), cpu->gpr[rs] == cpu->gpr[rt]);
		break;
	case 0x05:
		branch(cpu, current_pc, current_pc + 4u +
			(sign16(instruction) << 2), cpu->gpr[rs] != cpu->gpr[rt]);
		break;
	case 0x06:
		branch(cpu, current_pc, current_pc + 4u +
			(sign16(instruction) << 2), (int32_t)cpu->gpr[rs] <= 0);
		break;
	case 0x07:
		branch(cpu, current_pc, current_pc + 4u +
			(sign16(instruction) << 2), (int32_t)cpu->gpr[rs] > 0);
		break;
	case 0x08:
		if (overflow_add(cpu->gpr[rs], sign16(instruction), &value))
			result = exception(cpu, PSX_EXC_OVERFLOW, current_pc,
				in_delay, 0, 0, 0);
		else
			write_reg(cpu, rt, value, &written);
		break;
	case 0x09: write_reg(cpu, rt, cpu->gpr[rs] + sign16(instruction), &written); break;
	case 0x0a:
		write_reg(cpu, rt, (int32_t)cpu->gpr[rs] <
			(int32_t)sign16(instruction), &written);
		break;
	case 0x0b: write_reg(cpu, rt, cpu->gpr[rs] < sign16(instruction), &written); break;
	case 0x0c: write_reg(cpu, rt, cpu->gpr[rs] & (instruction & 0xffffu), &written); break;
	case 0x0d: write_reg(cpu, rt, cpu->gpr[rs] | (instruction & 0xffffu), &written); break;
	case 0x0e: write_reg(cpu, rt, cpu->gpr[rs] ^ (instruction & 0xffffu), &written); break;
	case 0x0f: write_reg(cpu, rt, instruction << 16, &written); break;
	case 0x10:
		result = execute_cop0(cpu, instruction, current_pc, in_delay,
			&written);
		break;
	case 0x11:
	case 0x13:
		result = exception(cpu, PSX_EXC_COP_UNUSABLE, current_pc,
			in_delay, 0, 0, op - 0x10u);
		break;
	case 0x12:
		result = execute_cop2(cpu, instruction, current_pc, in_delay);
		break;
	case 0x20: /* LB */
	case 0x24: /* LBU */
		address = cpu->gpr[rs] + sign16(instruction);
		if (read8(cpu, address, &value))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		else
			delayed_load(cpu, rt, op == 0x20u ?
				(uint32_t)(int32_t)(int8_t)value : value);
		break;
	case 0x21: /* LH */
	case 0x25: /* LHU */
		address = cpu->gpr[rs] + sign16(instruction);
		if (address & 1u)
			result = exception(cpu, PSX_EXC_ADEL, current_pc, in_delay,
				address, 1, 0);
		else if (read16(cpu, address, &value))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		else
			delayed_load(cpu, rt, op == 0x21u ?
				(uint32_t)(int32_t)(int16_t)value : value);
		break;
	case 0x22: /* LWL */
		address = cpu->gpr[rs] + sign16(instruction);
		if (read32(cpu, address & ~3u, &memory))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		else {
			static const uint32_t mask[4] = {
				0x00ffffffu, 0x0000ffffu, 0x000000ffu, 0u};
			static const uint8_t shift[4] = {24, 16, 8, 0};
			value = cpu->gpr[rt];
			delayed_load(cpu, rt, (value & mask[address & 3u]) |
				(memory << shift[address & 3u]));
		}
		break;
	case 0x23: /* LW */
		address = cpu->gpr[rs] + sign16(instruction);
		if (address & 3u)
			result = exception(cpu, PSX_EXC_ADEL, current_pc, in_delay,
				address, 1, 0);
		else if (read32(cpu, address, &value))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		else
			delayed_load(cpu, rt, value);
		break;
	case 0x26: /* LWR */
		address = cpu->gpr[rs] + sign16(instruction);
		if (read32(cpu, address & ~3u, &memory))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		else {
			static const uint32_t mask[4] = {
				0u, 0xff000000u, 0xffff0000u, 0xffffff00u};
			static const uint8_t shift[4] = {0, 8, 16, 24};
			value = cpu->gpr[rt];
			delayed_load(cpu, rt, (value & mask[address & 3u]) |
				(memory >> shift[address & 3u]));
		}
		break;
	case 0x28: /* SB */
	case 0x29: /* SH */
	case 0x2b: /* SW */
		address = cpu->gpr[rs] + sign16(instruction);
		if ((op == 0x29u && (address & 1u)) ||
		    (op == 0x2bu && (address & 3u)))
			result = exception(cpu, PSX_EXC_ADES, current_pc, in_delay,
				address, 1, 0);
		else if ((op == 0x28u && write8(cpu, address, cpu->gpr[rt])) ||
			 (op == 0x29u && write16(cpu, address, cpu->gpr[rt])) ||
			 (op == 0x2bu && write32(cpu, address, cpu->gpr[rt])))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		break;
	case 0x2a: /* SWL */
	case 0x2e: /* SWR */
		address = cpu->gpr[rs] + sign16(instruction);
		if (read32(cpu, address & ~3u, &memory)) {
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
			break;
		}
		if (op == 0x2au) {
			static const uint32_t mask[4] = {
				0xffffff00u, 0xffff0000u, 0xff000000u, 0u};
			static const uint8_t shift[4] = {24, 16, 8, 0};
			value = (memory & mask[address & 3u]) |
				(cpu->gpr[rt] >> shift[address & 3u]);
		} else {
			static const uint32_t mask[4] = {
				0u, 0x000000ffu, 0x0000ffffu, 0x00ffffffu};
			static const uint8_t shift[4] = {0, 8, 16, 24};
			value = (memory & mask[address & 3u]) |
				(cpu->gpr[rt] << shift[address & 3u]);
		}
		if (write32(cpu, address & ~3u, value))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		break;
	case 0x32: /* LWC2 */
		address = cpu->gpr[rs] + sign16(instruction);
		if (address & 3u)
			result = exception(cpu, PSX_EXC_ADEL, current_pc, in_delay,
				address, 1, 0);
		else if (read32(cpu, address, &value))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		else
			psx_gte_write_data(&cpu->gte, rt, value);
		break;
	case 0x3a: /* SWC2 */
		address = cpu->gpr[rs] + sign16(instruction);
		if (address & 3u)
			result = exception(cpu, PSX_EXC_ADES, current_pc, in_delay,
				address, 1, 0);
		else if (write32(cpu, address,
			psx_gte_read_data(&cpu->gte, rt)))
			result = exception(cpu, PSX_EXC_DBE, current_pc, in_delay,
				address, 0, 0);
		break;
	default:
		result = exception(cpu, PSX_EXC_RESERVED, current_pc, in_delay,
			0, 0, 0);
		break;
	}

finish:
	if (old_load_pending && old_load_reg != 0u &&
	    !(written & (1u << old_load_reg)))
		cpu->gpr[old_load_reg] = old_load_value;
	cpu->gpr[0] = 0;
	return result;
}

int psx_cpu_run(struct psx_cpu *cpu, uint32_t instruction_limit)
{
	uint32_t i;
	int result;

	for (i = 0; i < instruction_limit; ++i) {
		result = psx_cpu_step(cpu);
		if (result)
			return result;
	}
	return 0;
}
