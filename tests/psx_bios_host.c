// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"

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

int main(int argc, char **argv)
{
	struct psx_machine *machine;
	uint8_t *bios;
	uint8_t *ram;
	uint16_t *vram;
	FILE *file;
	uint32_t batches;
	uint32_t n;

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
	if (argc == 4 && strcmp(argv[3], "trace") == 0) {
		uint32_t previous = machine->cpu.pc;
		uint32_t previous_ram0 = *(uint32_t *)&ram[0];
		uint32_t previous_exceptions = 0;
		for (n = 0; n < batches * 1000000u; ++n) {
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
			psx_machine_step(machine);
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
	for (n = 0; n < batches; ++n) {
		psx_machine_run(machine, 1000000u);
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
		for (n = machine->cd_command_count > 32u ?
		     machine->cd_command_count - 32u : 0u;
		     n < machine->cd_command_count; ++n)
			printf(" %02x", machine->cd_commands[n & 31u]);
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
