// SPDX-License-Identifier: GPL-3.0-only

#include <stddef.h>
#include <stdint.h>

#include "jit.h"

#define RV_ZERO 0u
#define RV_RA   1u
#define RV_T0   5u
#define RV_T1   6u
#define RV_T2   7u
#define RV_A0  10u
#define RV_T3  28u
#define RV_T4  29u
#define RV_T5  30u
#define RV_T6  31u

#define RV_OP       0x33u
#define RV_OP_IMM   0x13u
#define RV_LOAD     0x03u
#define RV_STORE    0x23u
#define RV_BRANCH   0x63u
#define RV_JALR     0x67u
#define RV_JAL      0x6fu
#define RV_LUI      0x37u

#define JIT_HOT_COUNT 4u
#define JIT_MIN_GUEST 8u

struct emitter {
	uint32_t *code;
	uint32_t words;
	uint32_t capacity;
};

static uint32_t rv_r(uint32_t funct7, uint32_t rs2, uint32_t rs1,
	uint32_t funct3, uint32_t rd, uint32_t opcode)
{
	return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) |
		(funct3 << 12) | (rd << 7) | opcode;
}

static uint32_t rv_i(int32_t immediate, uint32_t rs1, uint32_t funct3,
	uint32_t rd, uint32_t opcode)
{
	return (((uint32_t)immediate & 0xfffu) << 20) | (rs1 << 15) |
		(funct3 << 12) | (rd << 7) | opcode;
}

static uint32_t rv_s(int32_t immediate, uint32_t rs2, uint32_t rs1,
	uint32_t funct3)
{
	uint32_t imm = (uint32_t)immediate & 0xfffu;
	return ((imm >> 5) << 25) | (rs2 << 20) | (rs1 << 15) |
		(funct3 << 12) | ((imm & 31u) << 7) | RV_STORE;
}

static uint32_t rv_b(int32_t immediate, uint32_t rs2, uint32_t rs1,
	uint32_t funct3)
{
	uint32_t imm = (uint32_t)immediate & 0x1fffu;
	return (((imm >> 12) & 1u) << 31) | (((imm >> 5) & 63u) << 25) |
		(rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(((imm >> 1) & 15u) << 8) | (((imm >> 11) & 1u) << 7) |
		RV_BRANCH;
}

static uint32_t rv_j(int32_t immediate, uint32_t rd)
{
	uint32_t imm = (uint32_t)immediate & 0x1fffffu;
	return (((imm >> 20) & 1u) << 31) | (((imm >> 1) & 0x3ffu) << 21) |
		(((imm >> 11) & 1u) << 20) | (((imm >> 12) & 0xffu) << 12) |
		(rd << 7) | RV_JAL;
}

static int emit(struct emitter *emitter, uint32_t instruction)
{
	if (emitter->words >= emitter->capacity)
		return -1;
	emitter->code[emitter->words++] = instruction;
	return 0;
}

static int emit_lw(struct emitter *emitter, uint32_t rd, uint32_t base,
	uint32_t offset)
{
	return emit(emitter, rv_i((int32_t)offset, base, 2u, rd, RV_LOAD));
}

static int emit_sw(struct emitter *emitter, uint32_t source, uint32_t base,
	uint32_t offset)
{
	return emit(emitter, rv_s((int32_t)offset, source, base, 2u));
}

static int emit_sb(struct emitter *emitter, uint32_t source, uint32_t base,
	uint32_t offset)
{
	return emit(emitter, rv_s((int32_t)offset, source, base, 0u));
}

static int emit_const(struct emitter *emitter, uint32_t rd, uint32_t value)
{
	uint32_t upper = (value + 0x800u) & 0xfffff000u;
	int32_t lower = (int32_t)(value - upper);
	return emit(emitter, upper | (rd << 7) | RV_LUI) ||
		emit(emitter, rv_i(lower, rd, 0u, rd, RV_OP_IMM));
}

static int emit_load_gpr(struct emitter *emitter, uint32_t host,
	uint32_t guest)
{
	return emit_lw(emitter, host, RV_A0,
		(uint32_t)offsetof(struct psx_cpu, gpr) + guest * 4u);
}

static int emit_store_gpr(struct emitter *emitter, uint32_t host,
	uint32_t guest)
{
	if (guest == 0u)
		return 0;
	return emit_sw(emitter, host, RV_A0,
		(uint32_t)offsetof(struct psx_cpu, gpr) + guest * 4u);
}

static int finish_alu(struct emitter *emitter, uint32_t host, uint32_t guest)
{
	return emit_store_gpr(emitter, host, guest) ? -1 : 1;
}

static int emit_pending(struct emitter *emitter, int pending_reg)
{
	if (pending_reg < 0)
		return emit_sb(emitter, RV_ZERO, RV_A0,
			(uint32_t)offsetof(struct psx_cpu, load_pending));
	if (emit_sw(emitter, RV_T4, RV_A0,
	    (uint32_t)offsetof(struct psx_cpu, load_value)) ||
	    emit(emitter, rv_i(pending_reg, RV_ZERO, 0u, RV_T0, RV_OP_IMM)) ||
	    emit_sb(emitter, RV_T0, RV_A0,
	    (uint32_t)offsetof(struct psx_cpu, load_reg)) ||
	    emit(emitter, rv_i(1, RV_ZERO, 0u, RV_T0, RV_OP_IMM)) ||
	    emit_sb(emitter, RV_T0, RV_A0,
	    (uint32_t)offsetof(struct psx_cpu, load_pending)))
		return -1;
	return 0;
}

static int emit_exit(struct emitter *emitter, uint32_t guest_instructions,
	uint32_t next_pc, int dynamic_pc, uint32_t branch_pc, int pending_reg)
{
	uint32_t cycles = (uint32_t)offsetof(struct psx_cpu, cycles);
	uint32_t pc = (uint32_t)offsetof(struct psx_cpu, pc);
	uint32_t next = (uint32_t)offsetof(struct psx_cpu, next_pc);

	if (dynamic_pc) {
		if (emit_sw(emitter, RV_T3, RV_A0, pc) ||
		    emit(emitter, rv_i(4, RV_T3, 0u, RV_T0, RV_OP_IMM)) ||
		    emit_sw(emitter, RV_T0, RV_A0, next))
			return -1;
	} else if (emit_const(emitter, RV_T0, next_pc) ||
		   emit_sw(emitter, RV_T0, RV_A0, pc) ||
		   emit(emitter, rv_i(4, RV_T0, 0u, RV_T1, RV_OP_IMM)) ||
		   emit_sw(emitter, RV_T1, RV_A0, next)) {
		return -1;
	}
	if (branch_pc != 0u &&
	    (emit_const(emitter, RV_T0, branch_pc) ||
	     emit_sw(emitter, RV_T0, RV_A0,
		(uint32_t)offsetof(struct psx_cpu, branch_pc))))
		return -1;
	if (emit_sb(emitter, RV_ZERO, RV_A0,
	    (uint32_t)offsetof(struct psx_cpu, in_delay_slot)) ||
	    emit_sb(emitter, RV_ZERO, RV_A0,
	    (uint32_t)offsetof(struct psx_cpu, next_delay_slot)) ||
	    emit_pending(emitter, pending_reg) ||
	    emit_lw(emitter, RV_T0, RV_A0, cycles) ||
	    emit(emitter, rv_i((int32_t)guest_instructions, RV_T0, 0u,
		RV_T0, RV_OP_IMM)) || emit_sw(emitter, RV_T0, RV_A0, cycles) ||
	    emit(emitter, rv_i((int32_t)guest_instructions, RV_ZERO, 0u,
		RV_A0, RV_OP_IMM)) || emit(emitter,
		rv_i(0, RV_RA, 0u, RV_ZERO, RV_JALR)))
		return -1;
	return 0;
}

static int emit_memory(struct emitter *emitter, uint32_t instruction,
	uint32_t current_pc, uint32_t executed_before, int pending_reg)
{
	uint32_t op = instruction >> 26;
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	uint32_t guards[4];
	uint32_t guard_rs1[4];
	uint32_t guard_rs2[4];
	uint32_t guard_funct3[4];
	uint32_t guard_count = 0;
	uint32_t alignment = (op == 0x21u || op == 0x25u || op == 0x29u) ?
		1u : ((op == 0x23u || op == 0x2bu) ? 3u : 0u);
	uint32_t load_funct3;
	uint32_t store_funct3;
	uint32_t jump;
	uint32_t bailout;
	uint32_t after;
	uint32_t n;
	int is_load = op == 0x20u || op == 0x24u || op == 0x21u ||
		op == 0x25u || op == 0x23u;
	int is_store = op == 0x28u || op == 0x29u || op == 0x2bu;

	if (!is_load && !is_store)
		return 0;
	if (emit_load_gpr(emitter, RV_T0, rs) ||
	    emit_const(emitter, RV_T1,
		(uint32_t)(int32_t)(int16_t)instruction) ||
	    emit(emitter, rv_r(0u, RV_T1, RV_T0, 0u, RV_T0, RV_OP)))
		return -1;
	if (alignment) {
		if (emit(emitter, rv_i((int32_t)alignment, RV_T0, 7u,
		    RV_T2, RV_OP_IMM)))
			return -1;
		guards[guard_count] = emitter->words;
		guard_rs1[guard_count] = RV_T2;
		guard_rs2[guard_count] = RV_ZERO;
		guard_funct3[guard_count++] = 1u;
		if (emit(emitter, 0u))
			return -1;
	}
	if (emit_const(emitter, RV_T2, 0x1fffffffu) ||
	    emit(emitter, rv_r(0u, RV_T2, RV_T0, 7u, RV_T1, RV_OP)) ||
	    emit_lw(emitter, RV_T2, RV_A0,
		(uint32_t)offsetof(struct psx_cpu, ram_map_size)))
		return -1;
	guards[guard_count] = emitter->words;
	guard_rs1[guard_count] = RV_T1;
	guard_rs2[guard_count] = RV_T2;
	guard_funct3[guard_count++] = 7u;
	if (emit(emitter, 0u))
		return -1;
	if (is_store) {
		if (emit_lw(emitter, RV_T2, RV_A0,
		    (uint32_t)offsetof(struct psx_cpu, cp0) + 12u * 4u) ||
		    emit_const(emitter, RV_T6, 0x00010000u) ||
		    emit(emitter, rv_r(0u, RV_T6, RV_T2, 7u, RV_T2, RV_OP)))
			return -1;
		guards[guard_count] = emitter->words;
		guard_rs1[guard_count] = RV_T2;
		guard_rs2[guard_count] = RV_ZERO;
		guard_funct3[guard_count++] = 1u;
		if (emit(emitter, 0u))
			return -1;
	}
	if (emit_lw(emitter, RV_T2, RV_A0,
	    (uint32_t)offsetof(struct psx_cpu, ram_size)) ||
	    emit(emitter, rv_i(-1, RV_T2, 0u, RV_T2, RV_OP_IMM)) ||
	    emit(emitter, rv_r(0u, RV_T2, RV_T1, 7u, RV_T1, RV_OP)) ||
	    emit_lw(emitter, RV_T2, RV_A0,
		(uint32_t)offsetof(struct psx_cpu, ram)) ||
	    emit(emitter, rv_r(0u, RV_T2, RV_T1, 0u, RV_T1, RV_OP)))
		return -1;
	if (is_load) {
		load_funct3 = op == 0x20u ? 0u : (op == 0x24u ? 4u :
			(op == 0x21u ? 1u : (op == 0x25u ? 5u : 2u)));
		if (emit(emitter, rv_i(0, RV_T1, load_funct3, RV_T5, RV_LOAD)))
			return -1;
	} else {
		store_funct3 = op == 0x28u ? 0u : (op == 0x29u ? 1u : 2u);
		if (emit_load_gpr(emitter, RV_T5, rt) ||
		    emit(emitter, rv_s(0, RV_T5, RV_T1, store_funct3)))
			return -1;
	}
	jump = emitter->words;
	if (emit(emitter, 0u))
		return -1;
	bailout = emitter->words;
	if (emit_exit(emitter, executed_before, current_pc, 0, 0u,
	    pending_reg))
		return -1;
	after = emitter->words;
	for (n = 0; n < guard_count; ++n)
		emitter->code[guards[n]] = rv_b((int32_t)(bailout - guards[n]) * 4,
			guard_rs2[n], guard_rs1[n], guard_funct3[n]);
	emitter->code[jump] = rv_j((int32_t)(after - jump) * 4, RV_ZERO);
	return is_load ? 2 : 1;
}

static int fetch_word(const struct psx_cpu *cpu, uint32_t address,
	uint32_t *value)
{
	uint32_t physical = address & 0x1fffffffu;
	const uint8_t *p;
	uint32_t offset;

	if (cpu->ram && physical < cpu->ram_map_size) {
		offset = physical & (cpu->ram_size - 1u);
		if (offset > cpu->ram_size - 4u)
			return -1;
		p = &cpu->ram[offset];
	} else if (cpu->bios && physical >= cpu->bios_base) {
		offset = physical - cpu->bios_base;
		if (offset > cpu->bios_size - 4u)
			return -1;
		p = &cpu->bios[offset];
	} else {
		return -1;
	}
	*value = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		((uint32_t)p[3] << 24);
	return 0;
}

static int emit_alu(struct emitter *emitter, uint32_t instruction)
{
	uint32_t op = instruction >> 26;
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	uint32_t rd = (instruction >> 11) & 31u;
	uint32_t sa = (instruction >> 6) & 31u;
	uint32_t fn = instruction & 63u;
	uint32_t funct3;
	uint32_t funct7 = 0;
	uint32_t immediate;

	if (op == 0u) {
		if (fn == 0u || fn == 2u || fn == 3u) {
			if (emit_load_gpr(emitter, RV_T0, rt))
				return -1;
			funct3 = fn == 0u ? 1u : 5u;
			if (fn == 3u)
				sa |= 0x400u;
			if (emit(emitter, rv_i((int32_t)sa, RV_T0, funct3,
			    RV_T2, RV_OP_IMM)))
				return -1;
			return finish_alu(emitter, RV_T2, rd);
		}
		if (fn != 4u && fn != 6u && fn != 7u && fn != 0x21u &&
		    fn != 0x23u && fn != 0x24u && fn != 0x25u && fn != 0x26u &&
		    fn != 0x27u && fn != 0x2au && fn != 0x2bu)
			return 0;
		if (emit_load_gpr(emitter, RV_T0, rs) ||
		    emit_load_gpr(emitter, RV_T1, rt))
			return -1;
		switch (fn) {
		case 4u: funct3 = 1u; break;
		case 6u: funct3 = 5u; break;
		case 7u: funct3 = 5u; funct7 = 0x20u; break;
		case 0x21u: funct3 = 0u; break;
		case 0x23u: funct3 = 0u; funct7 = 0x20u; break;
		case 0x24u: funct3 = 7u; break;
		case 0x25u: funct3 = 6u; break;
		case 0x26u: funct3 = 4u; break;
		case 0x27u:
			if (emit(emitter, rv_r(0u, RV_T1, RV_T0, 6u,
			    RV_T2, RV_OP)) || emit(emitter,
			    rv_i(-1, RV_T2, 4u, RV_T2, RV_OP_IMM)))
				return -1;
			return finish_alu(emitter, RV_T2, rd);
		case 0x2au: funct3 = 2u; break;
		default: funct3 = 3u; break;
		}
		if (emit(emitter, rv_r(funct7,
		    (fn == 4u || fn == 6u || fn == 7u) ? RV_T0 : RV_T1,
		    (fn == 4u || fn == 6u || fn == 7u) ? RV_T1 : RV_T0, funct3,
		    RV_T2, RV_OP)))
			return -1;
		return finish_alu(emitter, RV_T2, rd);
	}
	if (op < 9u || op > 15u)
		return 0;
	if (op == 15u) {
		if (emit_const(emitter, RV_T2, instruction << 16))
			return -1;
		return finish_alu(emitter, RV_T2, rt);
	}
	immediate = op >= 12u ? instruction & 0xffffu :
		(uint32_t)(int32_t)(int16_t)instruction;
	if (emit_load_gpr(emitter, RV_T0, rs) ||
	    emit_const(emitter, RV_T1, immediate))
		return -1;
	switch (op) {
	case 9u: funct3 = 0u; break;
	case 10u: funct3 = 2u; break;
	case 11u: funct3 = 3u; break;
	case 12u: funct3 = 7u; break;
	case 13u: funct3 = 6u; break;
	default: funct3 = 4u; break;
	}
	if (emit(emitter, rv_r(0u, RV_T1, RV_T0, funct3, RV_T2, RV_OP)))
		return -1;
	return finish_alu(emitter, RV_T2, rt);
}

static int emit_condition(struct emitter *emitter, uint32_t instruction,
	uint32_t current_pc)
{
	uint32_t op = instruction >> 26;
	uint32_t rs = (instruction >> 21) & 31u;
	uint32_t rt = (instruction >> 16) & 31u;
	uint32_t funct3;
	uint32_t left = RV_T0;
	uint32_t right = RV_T1;
	uint32_t target = current_pc + 4u +
		((uint32_t)(int32_t)(int16_t)instruction << 2);
	uint32_t fallthrough = current_pc + 8u;

	if (emit_load_gpr(emitter, RV_T0, rs))
		return -1;
	if (op == 1u) {
		rt = (instruction >> 16) & 31u;
		if (rt != 0u && rt != 1u && rt != 16u && rt != 17u)
			return 0;
		funct3 = (rt & 1u) ? 5u : 4u;
		right = RV_ZERO;
		if ((rt & 16u) && (emit_const(emitter, RV_T2,
		    current_pc + 8u) || emit_store_gpr(emitter, RV_T2, 31u)))
			return -1;
	} else if (op == 4u || op == 5u) {
		if (emit_load_gpr(emitter, RV_T1, rt))
			return -1;
		funct3 = op == 4u ? 0u : 1u;
	} else if (op == 6u) {
		funct3 = 5u;
		left = RV_ZERO;
		right = RV_T0;
	} else if (op == 7u) {
		funct3 = 4u;
		left = RV_ZERO;
		right = RV_T0;
	} else {
		return 0;
	}
	if (emit(emitter, rv_b(16, right, left, funct3)) ||
	    emit_const(emitter, RV_T3, fallthrough) ||
	    emit(emitter, rv_j(12, RV_ZERO)) ||
	    emit_const(emitter, RV_T3, target))
		return -1;
	return 1;
}

static int alu_written_reg(uint32_t instruction)
{
	uint32_t op = instruction >> 26;
	if (op == 0u)
		return (int)((instruction >> 11) & 31u);
	if (op >= 9u && op <= 15u)
		return (int)((instruction >> 16) & 31u);
	return 0;
}

static int compile_block(struct psx_jit *jit, struct psx_cpu *cpu,
	struct psx_jit_block *block)
{
	struct emitter emitter;
	uint32_t pc = cpu->pc;
	uint32_t count = 0;
	uint32_t source_words = 0;
	uint32_t instruction;
	uint32_t op;
	int pending_reg = -1;
	int terminated = 0;

	if (jit->code_words + 1024u > PSX_JIT_CODE_WORDS)
		psx_jit_reset(jit);
	emitter.code = &jit->code[jit->code_words];
	emitter.words = 0;
	emitter.capacity = PSX_JIT_CODE_WORDS - jit->code_words;
	while (count < PSX_JIT_GUEST_WORDS && !terminated) {
		uint32_t before = emitter.words;
		if (fetch_word(cpu, pc + count * 4u, &instruction))
			break;
		op = instruction >> 26;
		if (op == 1u || (op >= 4u && op <= 7u)) {
			uint32_t delay;
			int old_pending = pending_reg;
			int branch_written = op == 1u &&
				(((instruction >> 16) & 16u) != 0u) ? 31 : 0;
			int condition;
			if (count + 2u > PSX_JIT_GUEST_WORDS ||
			    fetch_word(cpu, pc + (count + 1u) * 4u, &delay))
				break;
			condition = emit_condition(&emitter, instruction,
				pc + count * 4u);
			if (condition <= 0) {
				emitter.words = before;
				break;
			}
			if (pending_reg > 0 && pending_reg != branch_written &&
			    emit_store_gpr(&emitter, RV_T4, (uint32_t)pending_reg))
				return -1;
			pending_reg = -1;
			if (emit_alu(&emitter, delay) <= 0) {
				emitter.words = before;
				pending_reg = old_pending;
				break;
			}
			block->source[source_words++] = instruction;
			block->source[source_words++] = delay;
			count += 2u;
			if (emit_exit(&emitter, count, 0u, 1,
			    pc + (count - 2u) * 4u, -1))
				return -1;
			terminated = 1;
		} else if (op == 2u || op == 3u) {
			uint32_t delay;
			int old_pending = pending_reg;
			int branch_written = op == 3u ? 31 : 0;
			uint32_t target;
			if (count + 2u > PSX_JIT_GUEST_WORDS ||
			    fetch_word(cpu, pc + (count + 1u) * 4u, &delay))
				break;
			target = ((pc + count * 4u + 4u) & 0xf0000000u) |
				((instruction & 0x03ffffffu) << 2);
			if (op == 3u && (emit_const(&emitter, RV_T2,
			    pc + count * 4u + 8u) ||
			    emit_store_gpr(&emitter, RV_T2, 31u)))
				return -1;
			if (emit_const(&emitter, RV_T3, target))
				return -1;
			if (pending_reg > 0 && pending_reg != branch_written &&
			    emit_store_gpr(&emitter, RV_T4, (uint32_t)pending_reg))
				return -1;
			pending_reg = -1;
			if (emit_alu(&emitter, delay) <= 0) {
				emitter.words = before;
				pending_reg = old_pending;
				break;
			}
			block->source[source_words++] = instruction;
			block->source[source_words++] = delay;
			count += 2u;
			if (emit_exit(&emitter, count, 0u, 1,
			    pc + (count - 2u) * 4u, -1))
				return -1;
			terminated = 1;
		} else {
			int written = alu_written_reg(instruction);
			int emitted = emit_memory(&emitter, instruction,
				pc + count * 4u, count, pending_reg);
			if (emitted == 0)
				emitted = emit_alu(&emitter, instruction);
			if (emitted <= 0) {
				emitter.words = before;
				break;
			}
			if (emitted == 2)
				written = (int)((instruction >> 16) & 31u);
			if (pending_reg > 0 && pending_reg != written &&
			    emit_store_gpr(&emitter, RV_T4, (uint32_t)pending_reg))
				return -1;
			if (emitted == 2 && written != 0) {
				if (emit_sw(&emitter, RV_T5, RV_A0,
				    (uint32_t)offsetof(struct psx_cpu, load_value)) ||
				    emit(&emitter, rv_i(written, RV_ZERO, 0u,
				    RV_T0, RV_OP_IMM)) ||
				    emit_sb(&emitter, RV_T0, RV_A0,
				    (uint32_t)offsetof(struct psx_cpu, load_reg)) ||
				    emit(&emitter, rv_i(0, RV_T5, 0u, RV_T4,
				    RV_OP_IMM)))
					return -1;
				pending_reg = written;
			} else {
				pending_reg = -1;
			}
			block->source[source_words++] = instruction;
			++count;
		}
	}
	if (count < JIT_MIN_GUEST) {
		if (count == 0u && !fetch_word(cpu, pc, &instruction)) {
			block->source[0] = instruction;
			source_words = 1u;
		}
		block->pc = pc;
		block->source_words = (uint8_t)source_words;
		block->guest_instructions = 0u;
		block->valid = 2u;
		return 0;
	}
	if (!terminated && emit_exit(&emitter, count, pc + count * 4u, 0, 0u,
	    pending_reg))
		return -1;
	block->pc = pc;
	block->code_offset = jit->code_words;
	block->source_words = (uint8_t)source_words;
	block->guest_instructions = (uint8_t)count;
	block->valid = 1u;
	jit->code_words += emitter.words;
	++jit->compiled_blocks;
#if defined(__riscv) && __riscv_xlen == 32
	__asm__ volatile ("fence rw,rw\n\tfence.i" ::: "memory");
#endif
	return 1;
}

static int block_matches(const struct psx_jit_block *block,
	const struct psx_cpu *cpu)
{
	uint32_t n;
	if (block->valid != 1u || block->pc != cpu->pc)
		return 0;
	for (n = 0; n < block->source_words; ++n) {
		uint32_t word;
		if (fetch_word(cpu, block->pc + n * 4u, &word) ||
		    word != block->source[n])
			return 0;
	}
	return 1;
}

static int rejected_block_matches(const struct psx_jit_block *block,
	const struct psx_cpu *cpu)
{
	uint32_t n;
	if (block->valid != 2u || block->pc != cpu->pc)
		return 0;
	for (n = 0; n < block->source_words; ++n) {
		uint32_t word;
		if (fetch_word(cpu, block->pc + n * 4u, &word) ||
		    word != block->source[n])
			return 0;
	}
	return 1;
}

void psx_jit_reset(struct psx_jit *jit)
{
	uint32_t n;
	for (n = 0; n < PSX_JIT_BLOCK_COUNT; ++n)
		jit->blocks[n].valid = 0;
	jit->code_words = 0;
	jit->compiled_blocks = 0;
	jit->executed_blocks = 0;
	jit->executed_instructions = 0;
	jit->interpreter_instructions = 0;
	++jit->cache_flushes;
}

int psx_jit_run(struct psx_jit *jit, struct psx_cpu *cpu,
	uint32_t instruction_limit)
{
	uint32_t executed = 0;
	int result = 0;

	while (executed < instruction_limit) {
		struct psx_jit_block *block =
			&jit->blocks[(cpu->pc >> 2) & (PSX_JIT_BLOCK_COUNT - 1u)];
		uint32_t remaining = instruction_limit - executed;
		int compiled;
		if (cpu->load_pending || cpu->in_delay_slot ||
		    ((cpu->cp0[12] & 1u) &&
		    (cpu->cp0[12] & cpu->cp0[13] & 0x0000ff00u))) {
			int step = psx_cpu_run(cpu, 1u);
			if (step)
				result = step;
			++jit->interpreter_instructions;
			++executed;
			continue;
		}
		if (rejected_block_matches(block, cpu)) {
			int step = psx_cpu_run(cpu, 1u);
			if (step)
				result = step;
			++jit->interpreter_instructions;
			++executed;
			continue;
		}
		if (!block_matches(block, cpu)) {
			if (block->pc != cpu->pc || block->valid != 0u) {
				block->pc = cpu->pc;
				block->valid = 0u;
				block->hot_count = 1u;
			} else if (block->hot_count < JIT_HOT_COUNT) {
				++block->hot_count;
			}
			if (block->hot_count < JIT_HOT_COUNT) {
				int step = psx_cpu_run(cpu, 1u);
				if (step)
					result = step;
				++jit->interpreter_instructions;
				++executed;
				continue;
			}
			compiled = compile_block(jit, cpu, block);
			if (compiled < 0)
				block->valid = 0;
		}
		if (!block_matches(block, cpu) ||
		    block->guest_instructions > remaining) {
			int step = psx_cpu_run(cpu, 1u);
			if (step)
				result = step;
			++jit->interpreter_instructions;
			++executed;
			continue;
		}
#if defined(__riscv) && __riscv_xlen == 32
		{
			typedef uint32_t (*block_fn)(struct psx_cpu *);
			block_fn function = (block_fn)(uintptr_t)
				&jit->code[block->code_offset];
			uint32_t count = function(cpu);
			++jit->executed_blocks;
			/*
			 * A guarded memory operation can bail out before the first
			 * instruction in a block. Interpret that instruction here so
			 * the dispatcher always makes forward progress.
			 */
			if (count == 0u) {
				int step = psx_cpu_run(cpu, 1u);
				if (step)
					result = step;
				++jit->interpreter_instructions;
				++executed;
				continue;
			}
			jit->executed_instructions += count;
			executed += count;
		}
#else
		{
			int step = psx_cpu_run(cpu, 1u);
			if (step)
				result = step;
			++jit->interpreter_instructions;
			++executed;
		}
#endif
	}
	return result;
}
