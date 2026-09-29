// SPDX-License-Identifier: GPL-3.0-only

#ifndef TANG_PSX_JIT_H
#define TANG_PSX_JIT_H

#include <stdint.h>

#include "psx.h"

#define PSX_JIT_BLOCK_COUNT 512u
#define PSX_JIT_CODE_WORDS  32768u
#define PSX_JIT_GUEST_WORDS 16u

struct psx_jit_block {
	uint32_t pc;
	uint32_t code_offset;
	uint32_t source[PSX_JIT_GUEST_WORDS];
	uint8_t source_words;
	uint8_t guest_instructions;
	uint8_t valid;
	uint8_t hot_count;
};

struct psx_jit {
	_Alignas(16) uint32_t code[PSX_JIT_CODE_WORDS];
	struct psx_jit_block blocks[PSX_JIT_BLOCK_COUNT];
	uint32_t code_words;
	uint32_t compiled_blocks;
	uint32_t executed_blocks;
	uint32_t executed_instructions;
	uint32_t interpreter_instructions;
	uint32_t cache_flushes;
};

void psx_jit_reset(struct psx_jit *jit);
int psx_jit_run(struct psx_jit *jit, struct psx_cpu *cpu,
	uint32_t instruction_limit);

#endif
