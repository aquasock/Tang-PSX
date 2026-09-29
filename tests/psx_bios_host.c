// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"

#define HOST_VBLANK_INSTRUCTIONS 500000u
#define BOARD_DISPATCHES_PER_VBLANK 6900u
#define BOARD_TICKS_PER_DISPATCH 181u
#define BOARD_TICKS_PER_VBLANK 1251250u

static void step_scheduled(struct psx_machine *machine, uint32_t *next_vblank)
{
	psx_machine_step(machine);
	while ((int32_t)(machine->cpu.cycles - *next_vblank) >= 0) {
		psx_machine_vblank(machine);
		*next_vblank += HOST_VBLANK_INSTRUCTIONS;
	}
}

static int write_display(const char *path, const struct psx_machine *machine)
{
	uint16_t *pixels = malloc(640u * 480u * sizeof(*pixels));
	FILE *file;
	uint32_t n;
	if (!pixels)
		return -1;
	file = fopen(path, "wb");
	if (!file) {
		free(pixels);
		return -1;
	}
	psx_machine_copy_display(machine, pixels, 640, 480);
	fprintf(file, "P6\n640 480\n255\n");
	for (n = 0; n < 640u * 480u; ++n) {
		uint16_t pixel = pixels[n];
		fputc((pixel >> 8) & 0xf8u, file);
		fputc((pixel >> 3) & 0xfcu, file);
		fputc((pixel << 3) & 0xf8u, file);
	}
	fclose(file);
	free(pixels);
	return 0;
}

static void print_hot_pcs(const char *name, uint32_t base,
	uint32_t *hits, const uint8_t *code, uint32_t words)
{
	uint32_t rank;
	printf("%s hot PCs:\n", name);
	for (rank = 0; rank < 128u; ++rank) {
		uint32_t best = 0;
		uint32_t best_hits = 0;
		uint32_t n;
		for (n = 0; n < words; ++n) {
			if (hits[n] > best_hits) {
				best = n;
				best_hits = hits[n];
			}
		}
		if (best_hits == 0u)
			break;
		printf("  %08x %08x %u\n", base + best * 4u,
			*(const uint32_t *)&code[best * 4u], best_hits);
		hits[best] = 0;
	}
}

static int logo_complete(const struct psx_machine *machine)
{
	return machine->gpu.command_words >= 10768u &&
		machine->gpu.primitives >= 414u && machine->gpu.uploads >= 63u &&
		machine->dma_words >= 158497u;
}

static uint32_t code_hash(const uint8_t *ram, uint32_t start, uint32_t end)
{
	uint32_t hash = 2166136261u;
	uint32_t n;
	for (n = start; n < end; ++n) {
		hash ^= ram[n];
		hash *= 16777619u;
	}
	return hash;
}

int main(int argc, char **argv)
{
	struct psx_machine *machine;
	uint8_t *bios;
	uint8_t *ram;
	uint16_t *vram;
	FILE *file;
	uint32_t batches;
	uint32_t n;
	uint32_t next_vblank = HOST_VBLANK_INSTRUCTIONS;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s BIOS BATCHES [trace]\n", argv[0]);
		return 2;
	}
	batches = (uint32_t)strtoul(argv[2], 0, 0);
	bios = malloc(PSX_BIOS_BYTES);
	ram = malloc(PSX_MAIN_RAM_BYTES);
	vram = malloc(PSX_VRAM_PIXELS * sizeof(*vram));
	machine = malloc(sizeof(*machine));
	if (!bios || !ram || !vram || !machine)
		return 2;
	file = fopen(argv[1], "rb");
	if (!file || fread(bios, 1, PSX_BIOS_BYTES, file) != PSX_BIOS_BYTES ||
	    fgetc(file) != EOF) {
		fprintf(stderr, "BIOS must be exactly %u bytes\n", PSX_BIOS_BYTES);
		return 2;
	}
	fclose(file);
	psx_machine_reset(machine, ram, vram, bios);
	if (argc == 4 && strcmp(argv[3], "wall") == 0) {
		for (n = 0; n < batches * 1000000u && !logo_complete(machine); ++n) {
			psx_machine_step(machine);
			if ((n + 1u) % BOARD_DISPATCHES_PER_VBLANK == 0u)
				psx_machine_vblank(machine);
		}
		printf("dispatches=%u instructions=%u accelerated=%u vblank=%u "
			"gpu_words=%u primitives=%u uploads=%u dma_words=%u "
			"complete=%u\n", n, machine->cpu.cycles,
			machine->accelerated_instructions, machine->vblanks,
			machine->gpu.command_words, machine->gpu.primitives,
			machine->gpu.uploads, machine->dma_words,
			(unsigned)logo_complete(machine));
		return logo_complete(machine) ? 0 : 1;
	}
	if (argc == 4 && strcmp(argv[3], "wallfast") == 0) {
		uint64_t ticks = 0;
		uint64_t next_event = BOARD_TICKS_PER_VBLANK;
		uint32_t dispatches = 0;
		for (n = 0; n < batches * 1000000u && !logo_complete(machine); ++n) {
			if (psx_machine_waiting_for_vblank(machine) && ticks < next_event)
				ticks = next_event;
			if (ticks >= next_event) {
				psx_machine_vblank(machine);
				next_event += BOARD_TICKS_PER_VBLANK;
			} else {
				psx_machine_step(machine);
				ticks += BOARD_TICKS_PER_DISPATCH;
				++dispatches;
			}
		}
		printf("dispatches=%u ticks=%llu instructions=%u accelerated=%u "
			"vblank=%u gpu_words=%u primitives=%u uploads=%u "
			"dma_words=%u pc=%08x complete=%u\n", dispatches,
			(unsigned long long)ticks, machine->cpu.cycles,
			machine->accelerated_instructions, machine->vblanks,
			machine->gpu.command_words, machine->gpu.primitives,
			machine->gpu.uploads, machine->dma_words, machine->cpu.pc,
			(unsigned)logo_complete(machine));
		return logo_complete(machine) ? 0 : 1;
	}
	if (argc == 4 && (strcmp(argv[3], "fast") == 0 ||
	    strcmp(argv[3], "timeline") == 0)) {
		int timeline = strcmp(argv[3], "timeline") == 0;
		uint32_t idle_events = 0;
		uint32_t previous_dispatch = 0;
		for (n = 0; n < batches * 1000000u && !logo_complete(machine); ++n) {
			if (psx_machine_waiting_for_vblank(machine)) {
				if (timeline)
					printf("vblank=%u dispatches=%u delta=%u cycles=%u "
						"gpu=%u/%u/%u dma=%u pc=%08x\n",
						idle_events + 1u, n, n - previous_dispatch,
						machine->cpu.cycles, machine->gpu.command_words,
						machine->gpu.primitives, machine->gpu.uploads,
						machine->dma_words, machine->cpu.pc);
				previous_dispatch = n;
				psx_machine_vblank(machine);
				++idle_events;
			} else {
				psx_machine_step(machine);
			}
		}
		printf("dispatches=%u instructions=%u accelerated=%u vblank=%u "
			"idle_events=%u gpu_words=%u primitives=%u uploads=%u "
			"dma_words=%u pc=%08x irq=%08x/%08x complete=%u\n", n,
			machine->cpu.cycles,
			machine->accelerated_instructions, machine->vblanks,
			idle_events, machine->gpu.command_words,
			machine->gpu.primitives, machine->gpu.uploads,
			machine->dma_words, machine->cpu.pc, machine->irq_status,
			machine->irq_mask, (unsigned)logo_complete(machine));
		return logo_complete(machine) ? 0 : 1;
	}
	if (argc == 4 && strcmp(argv[3], "slicedfast") == 0) {
		const char *output = getenv("PSX_FRAMEBUFFER");
		uint32_t idle_events = 0;
		if (!output)
			output = "/tmp/tang-psx-slicedfast.ppm";
		for (n = 0; n < batches * 1000000u && !logo_complete(machine); ++n) {
			if (psx_machine_waiting_for_vblank(machine)) {
				psx_machine_vblank(machine);
				++idle_events;
			} else {
				psx_machine_run(machine, 256u);
			}
		}
		printf("batches=%u instructions=%u accelerated=%u vblank=%u "
			"idle_events=%u gpu_words=%u primitives=%u uploads=%u "
			"dma_words=%u pc=%08x irq=%08x/%08x complete=%u\n", n,
			machine->cpu.cycles,
			machine->accelerated_instructions, machine->vblanks,
			idle_events, machine->gpu.command_words,
			machine->gpu.primitives, machine->gpu.uploads,
			machine->dma_words, machine->cpu.pc, machine->irq_status,
			machine->irq_mask, (unsigned)logo_complete(machine));
		if (write_display(output, machine))
			return 2;
		return logo_complete(machine) ? 0 : 1;
	}
	if (argc == 4 && strcmp(argv[3], "partial13") == 0) {
		while (machine->vblanks < 13u) {
			if (psx_machine_waiting_for_vblank(machine))
				psx_machine_vblank(machine);
			else
				psx_machine_run(machine, 256u);
		}
		printf("instructions=%u vblank=%u gpu_words=%u primitives=%u "
			"uploads=%u dma_words=%u pc=%08x\n", machine->cpu.cycles,
			machine->vblanks, machine->gpu.command_words,
			machine->gpu.primitives, machine->gpu.uploads,
			machine->dma_words, machine->cpu.pc);
		return write_display("/tmp/tang-psx-partial13.ppm", machine) ? 2 : 0;
	}
	if (argc == 4 && strcmp(argv[3], "trace") == 0) {
		uint32_t previous = machine->cpu.pc;
		uint32_t previous_ram0 = *(uint32_t *)&ram[0];
		uint32_t previous_exceptions = 0;
		uint16_t previous_spu_control = machine->spu[0x1aau >> 1];
		for (n = 0; machine->cpu.cycles < batches * 1000000u; ++n) {
			uint32_t pc = machine->cpu.pc;
			uint32_t ram0 = *(uint32_t *)&ram[0];
			if ((previous & 0xff000000u) != (pc & 0xff000000u) ||
			    (pc < 0x2000u && previous >= 0x2000u) ||
			    ram0 != previous_ram0)
				printf("step=%u pc=%08x from=%08x status=%08x "
					"ra=%08x sp=%08x ram0=%08x ram70=%08x\n",
					n, pc, previous, machine->cpu.cp0[12],
					machine->cpu.gpr[31], machine->cpu.gpr[29],
					ram0, *(uint32_t *)&ram[0x70]);
			previous = pc;
			previous_ram0 = ram0;
			step_scheduled(machine, &next_vblank);
			if (machine->spu[0x1aau >> 1] != previous_spu_control) {
				printf("SPUCNT step=%u pc=%08x value=%04x\n", n,
					pc, machine->spu[0x1aau >> 1]);
				previous_spu_control = machine->spu[0x1aau >> 1];
			}
			if (machine->cpu.exception_count != previous_exceptions) {
				printf("EXCEPTION step=%u epc=%08x insn=%08x cause=%08x "
					"status=%08x a0=%08x a1=%08x t1=%08x\n", n,
					machine->cpu.cp0[14],
					*(uint32_t *)&ram[machine->cpu.cp0[14] & 0x1fffffu],
					machine->cpu.cp0[13],
					machine->cpu.cp0[12], machine->cpu.gpr[4],
					machine->cpu.gpr[5], machine->cpu.gpr[9]);
				previous_exceptions = machine->cpu.exception_count;
			}
		}
		return 0;
	}
	if (argc == 4 && (strcmp(argv[3], "profile") == 0 ||
	    strcmp(argv[3], "profilefast") == 0)) {
		int fast = strcmp(argv[3], "profilefast") == 0;
		uint32_t search_start = 0;
		uint32_t search_return = 0;
		uint32_t search_output_x = 0;
		uint32_t search_output_y = 0;
		uint32_t *ram_hits = calloc(PSX_MAIN_RAM_BYTES / 4u,
			sizeof(*ram_hits));
		uint32_t *bios_hits = calloc(PSX_BIOS_BYTES / 4u,
			sizeof(*bios_hits));
		if (!ram_hits || !bios_hits)
			return 2;
		for (n = 0; machine->cpu.cycles < batches * 1000000u &&
		     (!fast || !logo_complete(machine)); ++n) {
			uint32_t physical = machine->cpu.pc & 0x1fffffffu;
			if (fast && physical == 0x0004a450u && search_start == 0u) {
				uint32_t stack = machine->cpu.gpr[29] & 0x1fffffu;
				search_start = machine->cpu.cycles;
				search_return = machine->cpu.gpr[31];
				search_output_x = *(uint32_t *)&ram[(stack + 16u) & 0x1fffffu];
				search_output_y = *(uint32_t *)&ram[(stack + 20u) & 0x1fffffu];
				printf("graphics-search-input: %08x %08x %08x %08x "
					"match=%08x bitmap=%d,%d,%d,%d bounds=%d,%d,%d,%d\n",
					machine->cpu.gpr[4],
					machine->cpu.gpr[5], machine->cpu.gpr[6],
					machine->cpu.gpr[7],
					*(uint32_t *)&ram[(stack + 24u) & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[machine->cpu.gpr[4] & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[(machine->cpu.gpr[4] + 2u) & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[(machine->cpu.gpr[4] + 4u) & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[(machine->cpu.gpr[4] + 6u) & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[machine->cpu.gpr[5] & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[(machine->cpu.gpr[5] + 2u) & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[(machine->cpu.gpr[5] + 4u) & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[(machine->cpu.gpr[5] + 6u) & 0x1fffffu]);
			}
			if (fast && search_start != 0u &&
			    machine->cpu.pc == search_return) {
				printf("graphics-search-output: cycles=%u return=%08x "
					"result=%08x xy=%d,%d\n",
					machine->cpu.cycles - search_start, search_return,
					machine->cpu.gpr[2],
					(int16_t)*(uint16_t *)&ram[search_output_x & 0x1fffffu],
					(int16_t)*(uint16_t *)&ram[search_output_y & 0x1fffffu]);
				search_start = 0u;
			}
			if (fast && psx_machine_waiting_for_vblank(machine)) {
				psx_machine_vblank(machine);
				continue;
			}
			if (physical < PSX_MAIN_RAM_BYTES)
				++ram_hits[physical >> 2];
			else if (physical >= 0x1fc00000u &&
				 physical < 0x1fc00000u + PSX_BIOS_BYTES)
				++bios_hits[(physical - 0x1fc00000u) >> 2];
			step_scheduled(machine, &next_vblank);
		}
		print_hot_pcs("RAM", 0x80000000u, ram_hits, ram,
			PSX_MAIN_RAM_BYTES / 4u);
		print_hot_pcs("BIOS", 0xbfc00000u, bios_hits, bios,
			PSX_BIOS_BYTES / 4u);
		printf("wait-code:");
		for (n = 0x59d80u; n < 0x59e40u; n += 4u)
			printf(" %08x", *(uint32_t *)&ram[n]);
		putchar('\n');
		printf("graphics-hot-code:\n");
		printf("graphics-hashes: %08x %08x %08x\n",
			code_hash(ram, 0x4a100u, 0x4a1f0u),
			code_hash(ram, 0x4a450u, 0x4a55cu),
			code_hash(ram, 0x4a55cu, 0x4a710u));
		printf("graphics-callers:\n");
		for (n = 0x49a60u; n < 0x49bd0u; n += 4u)
			printf("%08x: %08x\n", 0x80000000u + n,
				*(uint32_t *)&ram[n]);
		for (n = 0x4a0f0u; n < 0x4a740u; n += 4u)
			printf("%08x: %08x\n", 0x80000000u + n,
				*(uint32_t *)&ram[n]);
		putchar('\n');
		free(bios_hits);
		free(ram_hits);
		return 0;
	}
	for (n = 0; n < batches; ++n) {
		uint32_t target = (n + 1u) * 1000000u;
		while ((int32_t)(machine->cpu.cycles - target) < 0)
			step_scheduled(machine, &next_vblank);
		printf("instructions=%u pc=%08x ra=%08x sp=%08x exceptions=%u status=%08x "
			"cause=%08x irq=%08x/%08x vblank=%u gpu_words=%u primitives=%u "
			"uploads=%u unknown_gpu=%u dma_words=%u unknown=%u/%u "
			"last=%08x/%08x\n",
			machine->cpu.cycles, machine->cpu.pc, machine->cpu.gpr[31],
			machine->cpu.gpr[29],
			machine->cpu.exception_count, machine->cpu.cp0[12],
			machine->cpu.cp0[13], machine->irq_status, machine->irq_mask,
			machine->vblanks,
			machine->gpu.command_words, machine->gpu.primitives,
			machine->gpu.uploads, machine->gpu.unknown_commands,
			machine->dma_words, machine->unknown_reads,
			machine->unknown_writes, machine->last_unknown_read,
			machine->last_unknown_write);
	}
	if (batches != 0u) {
		uint32_t address = ((machine->cpu.gpr[31] & 0x1fffffu) - 128u) & ~31u;
		uint32_t first = machine->bios_trace_count > 32u ?
			machine->bios_trace_count - 32u : 0u;
		printf("CD commands:");
		for (n = machine->cdrom.commands > 32u ?
		     machine->cdrom.commands - 32u : 0u;
		     n < machine->cdrom.commands; ++n)
			printf(" %02x", machine->cdrom.history[n & 31u]);
		putchar('\n');
		printf("BIOS trace:");
		for (n = first; n < machine->bios_trace_count; ++n)
			printf(" %02x@%08x/%08x", machine->bios_trace[n & 31u],
				machine->bios_trace_pc[n & 31u],
				machine->bios_trace_ra[n & 31u]);
		putchar('\n');
		printf("code@%06x:", address);
		for (n = 0; n < 64u; ++n)
			printf(" %08x", *(uint32_t *)&ram[address + n * 4u]);
		putchar('\n');
	}
	if (argc == 4 && write_display(argv[3], machine)) {
		fprintf(stderr, "failed to write %s\n", argv[3]);
		return 2;
	}
	free(machine);
	free(vram);
	free(ram);
	free(bios);
	return 0;
}
