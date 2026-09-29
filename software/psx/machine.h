// SPDX-License-Identifier: GPL-3.0-only

#ifndef TANG_PSX_MACHINE_H
#define TANG_PSX_MACHINE_H

#include <stdint.h>

#include "gpu.h"
#include "jit.h"
#include "psx.h"

#define PSX_MAIN_RAM_BYTES (2u * 1024u * 1024u)
#define PSX_BIOS_BYTES     (512u * 1024u)
#define PSX_SCRATCH_BYTES  1024u

struct psx_dma_channel {
	uint32_t base;
	uint32_t block;
	uint32_t control;
};

struct psx_machine {
	struct psx_cpu cpu;
	struct psx_jit jit;
	struct psx_gpu gpu;
	uint8_t *ram;
	uint16_t *vram;
	const uint8_t *bios;
	uint8_t scratch[PSX_SCRATCH_BYTES];
	uint16_t spu[512];
	uint32_t memory_control[9];
	struct psx_dma_channel dma[7];
	uint32_t ram_size;
	uint32_t cache_control;
	uint32_t irq_status;
	uint32_t irq_mask;
	uint32_t dma_control;
	uint32_t dma_interrupt;
	uint32_t timer_mode[3];
	uint32_t timer_target[3];
	uint8_t cd_index;
	uint8_t cd_irq_enable;
	uint8_t cd_irq_flag;
	uint8_t cd_drive_status;
	uint8_t cd_parameters[16];
	uint8_t cd_parameter_count;
	uint8_t cd_response[16];
	uint8_t cd_response_read;
	uint8_t cd_response_count;
	uint8_t cd_pending_command;
	uint8_t cd_pending_stage;
	uint8_t cd_mode;
	uint32_t cd_deadline;
	uint8_t cd_commands[32];
	uint32_t cd_command_count;
	uint32_t vblanks;
	uint32_t accelerated_instructions;
	uint32_t dma_words;
	uint32_t unknown_reads;
	uint32_t unknown_writes;
	uint32_t last_unknown_read;
	uint32_t last_unknown_write;
	uint8_t bios_trace[32];
	uint32_t bios_trace_pc[32];
	uint32_t bios_trace_ra[32];
	uint32_t bios_trace_count;
	/*
	 * AE350 cycle attribution; zero on hosts without rdcycle. CPU cycles
	 * include the GPU cycles spent in GP0 writes and GPU DMA.
	 */
	uint64_t profile_cpu_cycles;
	uint64_t profile_accel_cycles;
	uint64_t profile_gpu_cycles;
};

void psx_machine_reset(struct psx_machine *machine, uint8_t *ram,
	uint16_t *vram, const uint8_t *bios);
int psx_machine_step(struct psx_machine *machine);
int psx_machine_run(struct psx_machine *machine, uint32_t instruction_limit);
void psx_machine_vblank(struct psx_machine *machine);
int psx_machine_waiting_for_vblank(const struct psx_machine *machine);
void psx_machine_copy_display(const struct psx_machine *machine,
	volatile uint16_t *output, uint32_t output_width, uint32_t output_height);

#endif
