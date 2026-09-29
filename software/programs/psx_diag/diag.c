// SPDX-License-Identifier: GPL-3.0-only

#include "diag.h"
#include "psx.h"
#include "psx_vectors.h"

static uint8_t psx_ram[PSX_RAM_BYTES];

static void clear_bytes(uint8_t *memory, uint32_t size)
{
	uint32_t i;
	for (i = 0; i < size; ++i)
		memory[i] = 0;
}

static uint32_t load_word(uint32_t address)
{
	return psx_ram[address] | ((uint32_t)psx_ram[address + 1u] << 8) |
		((uint32_t)psx_ram[address + 2u] << 16) |
		((uint32_t)psx_ram[address + 3u] << 24);
}

static void store_word(uint32_t address, uint32_t value)
{
	psx_ram[address] = (uint8_t)value;
	psx_ram[address + 1u] = (uint8_t)(value >> 8);
	psx_ram[address + 2u] = (uint8_t)(value >> 16);
	psx_ram[address + 3u] = (uint8_t)(value >> 24);
}

static uint32_t cpu_value(const struct psx_cpu *cpu, uint16_t selector)
{
	if (selector < 32u)
		return cpu->gpr[selector];
	switch (selector) {
	case 0x40: return cpu->pc;
	case 0x41: return cpu->next_pc;
	case 0x42: return cpu->exception_count;
	case 0x48: return cpu->cp0[8];
	case 0x4c: return cpu->cp0[12];
	case 0x4d: return cpu->cp0[13];
	case 0x4e: return cpu->cp0[14];
	default: break;
	}
	if (selector >= 0x80u && selector < 0xa0u)
		return cpu->gte.data[selector - 0x80u];
	if (selector >= 0xa0u && selector < 0xc0u)
		return cpu->gte.control[selector - 0xa0u];
	if (selector >= 0x1000u) {
		uint32_t address = (uint32_t)(selector - 0x1000u) * 4u;
		return load_word(address);
	}
	return 0xdeadc0deu;
}

static uint32_t gte_value(const struct psx_gte *gte, uint16_t selector)
{
	if (selector >= 0x80u && selector < 0xa0u)
		return gte->data[selector - 0x80u];
	if (selector >= 0xa0u && selector < 0xc0u)
		return gte->control[selector - 0xa0u];
	return 0xdeadc0deu;
}

static void mix(struct psx_diag_result *result, uint32_t value)
{
	result->checksum = (result->checksum << 7) |
		(result->checksum >> 25);
	result->checksum ^= value + 0x9e3779b9u;
}

static int check_value(struct psx_diag_result *result, uint32_t group,
	uint32_t vector, uint32_t check, uint32_t expected, uint32_t observed)
{
	mix(result, observed);
	if (expected == observed) {
		++result->passed;
		return 0;
	}
	if (result->failure == 0u) {
		result->failure = (group << 24) | (vector << 12) | check;
		result->expected = expected;
		result->observed = observed;
	}
	return -1;
}

int psx_diag_run(struct psx_diag_result *result)
{
	uint32_t vector;

	result->passed = 0;
	result->total = 0;
	result->vector_crc32 = PSX_VECTOR_CRC32;
	result->checksum = PSX_VECTOR_CRC32;
	result->failure = 0;
	result->expected = 0;
	result->observed = 0;

	for (vector = 0; vector < PSX_CPU_VECTOR_COUNT; ++vector) {
		const struct psx_cpu_vector *test = &psx_cpu_vectors[vector];
		struct psx_cpu cpu;
		uint32_t n;

		clear_bytes(psx_ram, sizeof(psx_ram));
		for (n = 0; n < test->words; ++n)
			store_word(4u * n, test->program[n]);
		psx_cpu_reset(&cpu, psx_ram, sizeof(psx_ram), 0);
		for (n = 0; n < test->steps; ++n)
			(void)psx_cpu_step(&cpu);
		for (n = 0; n < test->check_count; ++n) {
			const struct psx_vector_check *check = &test->checks[n];
			++result->total;
			check_value(result, 1, vector, n, check->expected,
				cpu_value(&cpu, check->selector));
		}
	}

	for (vector = 0; vector < PSX_GTE_VECTOR_COUNT; ++vector) {
		const struct psx_gte_vector *test = &psx_gte_vectors[vector];
		struct psx_gte gte;
		uint32_t n;

		psx_gte_reset(&gte);
		for (n = 0; n < test->write_count; ++n) {
			const struct psx_gte_write *write = &test->writes[n];
			if (write->control)
				psx_gte_write_control(&gte, write->reg, write->value);
			else
				psx_gte_write_data(&gte, write->reg, write->value);
		}
		if (psx_gte_command(&gte, test->command) && result->failure == 0u) {
			result->failure = (2u << 24) | (vector << 12) | 0xfffu;
			result->expected = 0;
			result->observed = 0xffffffffu;
		}
		for (n = 0; n < test->check_count; ++n) {
			const struct psx_vector_check *check = &test->checks[n];
			++result->total;
			check_value(result, 2, vector, n, check->expected,
				gte_value(&gte, check->selector));
		}
	}
	return result->failure == 0u ? 0 : -1;
}
