// SPDX-License-Identifier: GPL-3.0-only

#include "psx.h"

#define FLAG_ERROR    0x80000000u
#define FLAG_IR1_SAT  0x81000000u
#define FLAG_IR2_SAT  0x80800000u
#define FLAG_IR3_SAT  0x00400000u
#define FLAG_SZ_SAT   0x80040000u
#define FLAG_DIV_OVF  0x80020000u
#define FLAG_MAC0_POS 0x80010000u
#define FLAG_MAC0_NEG 0x80008000u
#define FLAG_SX_SAT   0x80004000u
#define FLAG_SY_SAT   0x80002000u
#define FLAG_IR0_SAT  0x00001000u

static int16_t lo16(uint32_t value)
{
	return (int16_t)value;
}

static int16_t hi16(uint32_t value)
{
	return (int16_t)(value >> 16);
}

static uint32_t pack16(int32_t low, int32_t high)
{
	return (uint16_t)low | ((uint32_t)(uint16_t)high << 16);
}

static int32_t clamp_flag(struct psx_gte *gte, int64_t value, int32_t low,
	int32_t high, uint32_t flag)
{
	if (value < low) {
		gte->control[31] |= flag;
		return low;
	}
	if (value > high) {
		gte->control[31] |= flag;
		return high;
	}
	return (int32_t)value;
}

static void set_mac0(struct psx_gte *gte, int64_t value)
{
	if (value > 0x7fffffffll)
		gte->control[31] |= FLAG_MAC0_POS;
	if (value < -0x80000000ll)
		gte->control[31] |= FLAG_MAC0_NEG;
	gte->data[24] = (uint32_t)value;
}

/* The hardware reciprocal algorithm. It intentionally is not plain divide. */
static uint32_t divide(struct psx_gte *gte, uint16_t numerator,
	uint16_t denominator)
{
	uint32_t shift = 0;
	uint32_t r1;
	uint32_t index;
	uint32_t r2;
	uint32_t r3;
	uint32_t reciprocal;
	uint64_t result;

	if ((uint32_t)numerator >= (uint32_t)denominator * 2u) {
		gte->control[31] |= FLAG_DIV_OVF;
		return 0x1ffffu;
	}
	while (shift < 16u && !(denominator & (0x8000u >> shift)))
		++shift;
	r1 = ((uint32_t)denominator << shift) & 0x7fffu;
	index = (r1 + 0x40u) >> 7;
	r2 = (((0x40000u / (index + 0x100u)) + 1u) / 2u);
	if (r2 > 0x101u)
		r2 -= 0x101u;
	else
		r2 = 0;
	r2 += 0x101u;
	r3 = ((0x80u - r2 * (r1 + 0x8000u)) >> 8) & 0x1ffffu;
	reciprocal = (r2 * r3 + 0x80u) >> 8;
	result = ((uint64_t)reciprocal * ((uint32_t)numerator << shift) +
		0x8000u) >> 16;
	return result > 0x1ffffu ? 0x1ffffu : (uint32_t)result;
}

void psx_gte_reset(struct psx_gte *gte)
{
	uint32_t i;

	for (i = 0; i < 32u; ++i) {
		gte->data[i] = 0;
		gte->control[i] = 0;
	}
}

uint32_t psx_gte_read_data(struct psx_gte *gte, uint32_t reg)
{
	uint32_t value;

	reg &= 31u;
	if (reg == 15u)
		return gte->data[14];
	if (reg == 28u || reg == 29u) {
		value = (uint32_t)clamp_flag(gte, lo16(gte->data[9]) >> 7,
			0, 31, 0);
		value |= (uint32_t)clamp_flag(gte, lo16(gte->data[10]) >> 7,
			0, 31, 0) << 5;
		value |= (uint32_t)clamp_flag(gte, lo16(gte->data[11]) >> 7,
			0, 31, 0) << 10;
		return value;
	}
	value = gte->data[reg];
	if (reg == 1u || reg == 3u || reg == 5u ||
	    (reg >= 8u && reg <= 11u))
		return (uint32_t)(int32_t)lo16(value);
	if (reg == 7u || (reg >= 16u && reg <= 19u))
		return (uint16_t)value;
	return value;
}

void psx_gte_write_data(struct psx_gte *gte, uint32_t reg, uint32_t value)
{
	reg &= 31u;
	if (reg == 15u) {
		gte->data[12] = gte->data[13];
		gte->data[13] = gte->data[14];
		gte->data[14] = value;
		return;
	}
	if (reg == 28u) {
		gte->data[9] = (value & 0x1fu) << 7;
		gte->data[10] = (value & 0x3e0u) << 2;
		gte->data[11] = (value & 0x7c00u) >> 3;
	}
	if (reg == 30u) {
		uint32_t scan = value;
		uint32_t count = 0;
		if (scan & 0x80000000u)
			scan = ~scan;
		while (count < 32u && !(scan & (0x80000000u >> count)))
			++count;
		gte->data[31] = count;
	}
	if (reg != 31u)
		gte->data[reg] = value;
}

uint32_t psx_gte_read_control(const struct psx_gte *gte, uint32_t reg)
{
	return gte->control[reg & 31u];
}

void psx_gte_write_control(struct psx_gte *gte, uint32_t reg, uint32_t value)
{
	reg &= 31u;
	if (reg == 4u || reg == 12u || reg == 20u || reg == 26u ||
	    reg == 27u || reg == 29u || reg == 30u)
		value = (uint32_t)(int32_t)(int16_t)value;
	if (reg == 31u) {
		value &= 0x7ffff000u;
		if (value & 0x7f87e000u)
			value |= FLAG_ERROR;
	}
	gte->control[reg] = value;
}

static int32_t matrix(const struct psx_gte *gte, uint32_t mx, uint32_t row,
	uint32_t column)
{
	uint32_t linear = row * 3u + column;
	uint32_t reg = mx * 8u + linear / 2u;
	uint32_t value;

	if (mx >= 3u)
		return 0;
	value = gte->control[reg];
	return (linear & 1u) ? hi16(value) : lo16(value);
}

static int32_t vector(const struct psx_gte *gte, uint32_t v, uint32_t n)
{
	if (v < 3u) {
		uint32_t value = gte->data[v * 2u + (n == 2u)];
		return n == 1u ? hi16(value) : lo16(value);
	}
	return lo16(gte->data[9u + n]);
}

static int64_t control_vector(const struct psx_gte *gte, uint32_t cv,
	uint32_t n)
{
	if (cv >= 3u)
		return 0;
	return (int32_t)gte->control[cv * 8u + 5u + n];
}

static int command_mvmva(struct psx_gte *gte, uint32_t instruction)
{
	uint32_t sf = (instruction >> 19) & 1u;
	uint32_t mx = (instruction >> 17) & 3u;
	uint32_t v = (instruction >> 15) & 3u;
	uint32_t cv = (instruction >> 13) & 3u;
	uint32_t lm = (instruction >> 10) & 1u;
	uint32_t row;

	for (row = 0; row < 3u; ++row) {
		int64_t sum = control_vector(gte, cv, row) * 4096;
		int32_t mac;
		int32_t ir;
		uint32_t flag = row == 0u ? FLAG_IR1_SAT :
			(row == 1u ? FLAG_IR2_SAT : FLAG_IR3_SAT);
		uint32_t col;

		for (col = 0; col < 3u; ++col)
			sum += (int64_t)matrix(gte, mx, row, col) *
				vector(gte, v, col);
		mac = (int32_t)(sf ? (sum >> 12) : sum);
		gte->data[25u + row] = (uint32_t)mac;
		ir = clamp_flag(gte, mac, lm ? 0 : -0x8000, 0x7fff, flag);
		gte->data[9u + row] = (uint32_t)(int32_t)(int16_t)ir;
	}
	return 0;
}

static int command_rtps(struct psx_gte *gte, uint32_t instruction,
	uint32_t vertex_index, int last)
{
	uint32_t sf = (instruction >> 19) & 1u;
	uint32_t lm = (instruction >> 10) & 1u;
	int64_t raw[3];
	int32_t mac[3];
	uint32_t row;
	uint16_t sz3;
	uint32_t quotient;
	int64_t sx;
	int64_t sy;

	for (row = 0; row < 3u; ++row) {
		uint32_t col;
		raw[row] = (int64_t)(int32_t)gte->control[5u + row] * 4096;
		for (col = 0; col < 3u; ++col)
			raw[row] += (int64_t)matrix(gte, 0, row, col) *
				vector(gte, vertex_index, col);
		mac[row] = (int32_t)(sf ? (raw[row] >> 12) : raw[row]);
		gte->data[25u + row] = (uint32_t)mac[row];
	}
	gte->data[9] = (uint32_t)(int32_t)(int16_t)clamp_flag(gte, mac[0],
		lm ? 0 : -0x8000, 0x7fff, FLAG_IR1_SAT);
	gte->data[10] = (uint32_t)(int32_t)(int16_t)clamp_flag(gte, mac[1],
		lm ? 0 : -0x8000, 0x7fff, FLAG_IR2_SAT);
	gte->data[11] = (uint32_t)(int32_t)(int16_t)clamp_flag(gte,
		raw[2] >> 12, lm ? 0 : -0x8000, 0x7fff, FLAG_IR3_SAT);

	sz3 = (uint16_t)clamp_flag(gte, raw[2] >> 12, 0, 0xffff,
		FLAG_SZ_SAT);
	gte->data[16] = gte->data[17];
	gte->data[17] = gte->data[18];
	gte->data[18] = gte->data[19];
	gte->data[19] = sz3;
	quotient = divide(gte, (uint16_t)gte->control[26], sz3);
	gte->data[12] = gte->data[13];
	gte->data[13] = gte->data[14];
	sx = ((int64_t)(int32_t)gte->control[24] +
		(int64_t)lo16(gte->data[9]) * quotient) >> 16;
	sy = ((int64_t)(int32_t)gte->control[25] +
		(int64_t)lo16(gte->data[10]) * quotient) >> 16;
	gte->data[14] = pack16(clamp_flag(gte, sx, -0x400, 0x3ff,
		FLAG_SX_SAT), clamp_flag(gte, sy, -0x400, 0x3ff,
		FLAG_SY_SAT));
	if (last) {
		int64_t depth = (int32_t)gte->control[28] +
			(int64_t)lo16(gte->control[27]) * quotient;
		set_mac0(gte, depth);
		gte->data[8] = (uint32_t)(int32_t)(int16_t)clamp_flag(gte,
			depth >> 12, 0, 0x1000, FLAG_IR0_SAT);
	}
	return 0;
}

int psx_gte_command(struct psx_gte *gte, uint32_t instruction)
{
	uint32_t command = instruction & 0x3fu;
	int64_t value;

	gte->control[31] = 0;
	switch (command) {
	case 0x01: /* RTPS */
		return command_rtps(gte, instruction, 0, 1);
	case 0x06: /* NCLIP */
		value = (int64_t)lo16(gte->data[12]) * hi16(gte->data[13]) +
			(int64_t)lo16(gte->data[13]) * hi16(gte->data[14]) +
			(int64_t)lo16(gte->data[14]) * hi16(gte->data[12]) -
			(int64_t)lo16(gte->data[12]) * hi16(gte->data[14]) -
			(int64_t)lo16(gte->data[13]) * hi16(gte->data[12]) -
			(int64_t)lo16(gte->data[14]) * hi16(gte->data[13]);
		set_mac0(gte, value);
		return 0;
	case 0x12: /* MVMVA */
		return command_mvmva(gte, instruction);
	case 0x2d: /* AVSZ3 */
		value = (int64_t)lo16(gte->control[29]) *
			((uint16_t)gte->data[17] + (uint16_t)gte->data[18] +
			 (uint16_t)gte->data[19]);
		set_mac0(gte, value);
		gte->data[7] = (uint16_t)clamp_flag(gte, value >> 12, 0,
			0xffff, FLAG_SZ_SAT);
		return 0;
	case 0x2e: /* AVSZ4 */
		value = (int64_t)lo16(gte->control[30]) *
			((uint16_t)gte->data[16] + (uint16_t)gte->data[17] +
			 (uint16_t)gte->data[18] + (uint16_t)gte->data[19]);
		set_mac0(gte, value);
		gte->data[7] = (uint16_t)clamp_flag(gte, value >> 12, 0,
			0xffff, FLAG_SZ_SAT);
		return 0;
	case 0x30: /* RTPT */
		command_rtps(gte, instruction, 0, 0);
		command_rtps(gte, instruction, 1, 0);
		return command_rtps(gte, instruction, 2, 1);
	default:
		return -1;
	}
}
