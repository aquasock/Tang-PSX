// SPDX-License-Identifier: GPL-3.0-only
//
// Counter-rate check: run a dependent addi chain while continuously
// publishing the 64-bit cycle and retired-instruction counters. The host
// samples the registers with wall-clock timestamps to derive the rdcycle rate;
// instructions per counted cycle on the chain shows whether rdcycle counts
// core cycles. Runs for 2^30 counted cycles (1.4 s at 750 MHz), then returns;
// tools/ae350_run.py reports the wall-clock time from stream start.

#include <stdint.h>

#include "tpx_api.h"

#define CLOCK_RESULT 0x600dc10cu

static uint64_t read_cycle(void)
{
	uint32_t high;
	uint32_t low;
	uint32_t check;
	do {
		__asm__ volatile ("rdcycleh %0" : "=r"(high));
		__asm__ volatile ("rdcycle %0" : "=r"(low));
		__asm__ volatile ("rdcycleh %0" : "=r"(check));
	} while (high != check);
	return ((uint64_t)high << 32) | low;
}

static uint64_t read_instret(void)
{
	uint32_t high;
	uint32_t low;
	uint32_t check;
	do {
		__asm__ volatile ("rdinstreth %0" : "=r"(high));
		__asm__ volatile ("rdinstret %0" : "=r"(low));
		__asm__ volatile ("rdinstreth %0" : "=r"(check));
	} while (high != check);
	return ((uint64_t)high << 32) | low;
}

uint32_t main(const struct tpx_api *api)
{
	uint64_t start_cycle = read_cycle();
	uint64_t start_instret = read_instret();
	uint64_t cycles;
	uint32_t chain = 0;

	api->set_reg(TPX_REG_STAGE, 0x00c10001u);
	api->set_reg(TPX_REG_FEATURES, api->cpu_hz);
	do {
		uint64_t instret;
		uint32_t n;
		/* 4096 dependent adds: one per core cycle on a scalar pipeline. */
		for (n = 0; n < 256u; ++n)
			__asm__ volatile (
				".rept 16\n\taddi %0, %0, 1\n\t.endr"
				: "+r"(chain));
		cycles = read_cycle() - start_cycle;
		instret = read_instret() - start_instret;
		api->set_reg(TPX_REG_CYCLES, (uint32_t)cycles);
		api->set_reg(TPX_REG_WORDS, (uint32_t)(cycles >> 32));
		api->set_reg(TPX_REG_CHECKSUM, (uint32_t)instret);
		api->set_reg(TPX_REG_JIT, (uint32_t)(instret >> 32));
	} while (cycles < (1ull << 30));
	api->set_reg(TPX_REG_FAIL_OBSERVED, chain);
	api->set_reg(TPX_REG_STAGE, 0x80c10001u);
	return CLOCK_RESULT;
}
