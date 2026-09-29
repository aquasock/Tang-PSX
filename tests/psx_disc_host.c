// SPDX-License-Identifier: GPL-3.0-only
//
// Boot the BIOS with a disc image on the host: psx_disc_host BIOS BIN SECONDS
// [PPM]. SECONDS is emulated time. VBlank arrives every NTSC frame of CPU
// cycles; while the BIOS only waits for it, the clock skips ahead (as in the
// hardware psx_disc program). Prints CD-ROM and GPU progress once per emulated
// second. Diagnostics, selected by environment variable:
//   PSX_STOP_CYCLES=n  stop at cycle n instead
//   PSX_CD_TRACE=1     log CD commands, responses, and sector headers
//   PSX_TTY=1          print kernel console output (B0:3Dh)
//   PSX_CALLS=n        from cycle n, log calls with arguments and returns
//   PSX_RAM_DUMP=file  write main RAM at the end (see tools/mipsdis.py)
//   PSX_WATCH=addr     then step until that RAM word changes; print the path
//   PSX_PC_PROFILE=1   then histogram the next 4M PCs

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "machine.h"

#define FRAME_CYCLES 564480u        /* 33.8688 MHz / 60 */

static int read_sector(void *opaque, uint32_t lba, uint8_t *sector)
{
	FILE *file = opaque;
	if (fseek(file, (long)lba * (long)PSX_CD_SECTOR_BYTES, SEEK_SET) != 0 ||
	    fread(sector, 1, PSX_CD_SECTOR_BYTES, file) != PSX_CD_SECTOR_BYTES)
		return -1;
	return 0;
}

static void trace_command(const struct psx_cdrom *cd, uint8_t command,
	uint32_t cycles)
{
	uint32_t n;
	printf("cd %10u cmd %02x", cycles, command);
	for (n = 0; n < cd->param_count; ++n)
		printf(" %02x", cd->params[n]);
	printf("\n");
}

static void trace_response(const struct psx_cdrom *cd, uint32_t cycles)
{
	uint32_t n;
	printf("cd %10u INT%u", cycles, cd->irq_flag);
	for (n = 0; n < cd->result_count; ++n)
		printf(" %02x", cd->result[n]);
	if (cd->irq_flag == 1u) {
		printf("  lba %u data:", cd->last_lba);
		for (n = 24; n < 40u; ++n)
			printf(" %02x", cd->sector[n]);
	}
	printf("\n");
}

static int write_display(const char *path, const struct psx_machine *machine)
{
	static uint16_t pixels[640u * 480u];
	FILE *file = fopen(path, "wb");
	uint32_t n;
	if (!file)
		return -1;
	psx_machine_copy_display(machine, pixels, 640, 480);
	fprintf(file, "P6\n640 480\n255\n");
	for (n = 0; n < 640u * 480u; ++n) {
		fputc((pixels[n] >> 8) & 0xf8u, file);
		fputc((pixels[n] >> 3) & 0xfcu, file);
		fputc((pixels[n] << 3) & 0xf8u, file);
	}
	return fclose(file);
}

static void report(const struct psx_machine *machine, uint32_t second)
{
	const struct psx_cdrom *cd = &machine->cdrom;
	uint32_t n;
	printf("t=%us cycles=%u vblank=%u pc=%08x gpu=%u/%u/%u cd: cmds=%u "
		"read=%u skip=%u stat=%02x mode=%02x lba=%u last:",
		second, machine->cpu.cycles, machine->vblanks, machine->cpu.pc,
		machine->gpu.command_words, machine->gpu.primitives,
		machine->gpu.uploads, cd->commands, cd->sectors_read,
		cd->sectors_skipped, cd->stat, cd->mode, cd->last_lba);
	for (n = cd->commands > 8u ? cd->commands - 8u : 0u; n < cd->commands; ++n)
		printf(" %02x", cd->history[n & 31u]);
	printf(" irq=%04x/%04x cdirq=%u/%02x q=%u busy=%u sio=%u/%u",
		machine->irq_status, machine->irq_mask, cd->irq_flag,
		cd->irq_enable, cd->queue_count, cd->busy,
		machine->sio.step, machine->sio.control);
	printf(" dma=%u/%u/%u/%u/%u/%u/%u mdec=%u", machine->dma_starts[0],
		machine->dma_starts[1], machine->dma_starts[2],
		machine->dma_starts[3], machine->dma_starts[4],
		machine->dma_starts[5], machine->dma_starts[6],
		machine->mdec_accesses);
	printf(" epc=%08x cause=%08x badv=%08x sr=%08x",
		machine->cpu.cp0[14], machine->cpu.cp0[13], machine->cpu.cp0[8],
		machine->cpu.cp0[12]);
	printf(" unknown=%u/%u (%08x/%08x)\n", machine->unknown_reads,
		machine->unknown_writes, machine->last_unknown_read,
		machine->last_unknown_write);
}

int main(int argc, char **argv)
{
	struct psx_machine *machine = calloc(1, sizeof(*machine));
	uint8_t *bios = malloc(PSX_BIOS_BYTES);
	uint8_t *ram = malloc(PSX_MAIN_RAM_BYTES);
	uint16_t *vram = malloc(PSX_VRAM_PIXELS * sizeof(*vram));
	struct psx_disc disc = {0};
	FILE *file;
	uint32_t seconds;
	uint32_t stop_cycles = 0xffffffffu;
	uint32_t last_vblank = 0;
	uint32_t second = 0;
	long size;

	if (argc < 4 || !machine || !bios || !ram || !vram) {
		fprintf(stderr, "usage: %s BIOS BIN SECONDS [PPM]\n", argv[0]);
		return 2;
	}
	file = fopen(argv[1], "rb");
	if (!file || fread(bios, 1, PSX_BIOS_BYTES, file) != PSX_BIOS_BYTES)
		return 2;
	fclose(file);
	file = fopen(argv[2], "rb");
	if (!file || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0)
		return 2;
	disc.opaque = file;
	disc.read = read_sector;
	disc.sectors = (uint32_t)(size / (long)PSX_CD_SECTOR_BYTES);
	disc.region = 'A';
	seconds = (uint32_t)strtoul(argv[3], 0, 0);
	if (getenv("PSX_STOP_CYCLES"))
		stop_cycles = (uint32_t)strtoul(getenv("PSX_STOP_CYCLES"), 0, 0);

	psx_machine_reset(machine, ram, vram, bios);
	psx_machine_insert_disc(machine, &disc);
	if (getenv("PSX_CD_TRACE")) {
		machine->cdrom.trace_command = trace_command;
		machine->cdrom.trace_response = trace_response;
	}
	while (machine->cpu.cycles / (FRAME_CYCLES * 60u) < seconds &&
	       machine->cpu.cycles < stop_cycles) {
		if (psx_machine_waiting_for_vblank(machine))
			psx_machine_idle_to(machine, last_vblank + FRAME_CYCLES);
		if (machine->cpu.cycles - last_vblank >= FRAME_CYCLES) {
			psx_machine_vblank(machine);
			last_vblank += FRAME_CYCLES;
		} else if (getenv("PSX_CALLS") && machine->cpu.cycles >=
			   (uint32_t)strtoul(getenv("PSX_CALLS"), 0, 0)) {
			/* Call trace: jal/jalr targets with arguments, and v0 at jr ra. */
			uint32_t n;
			for (n = 0; n < 256u; ++n) {
				uint32_t pc = machine->cpu.pc;
				uint32_t word = 0;
				uint32_t op;
				if ((pc & 0x1fffffffu) < PSX_MAIN_RAM_BYTES)
					word = *(uint32_t *)&ram[pc & 0x1ffffcu];
				else if ((pc & 0x1fffffffu) >= 0x1fc00000u)
					word = *(uint32_t *)&bios[(pc & 0x7fffcu)];
				op = word >> 26;
				if (op == 3u)
					printf("%10u %08x jal %08x a0=%08x a1=%08x a2=%08x\n",
						machine->cpu.cycles, pc,
						(pc & 0xf0000000u) | ((word & 0x3ffffffu) << 2),
						machine->cpu.gpr[4], machine->cpu.gpr[5],
						machine->cpu.gpr[6]);
				else if (op == 0u && (word & 63u) == 9u)
					printf("%10u %08x jalr %08x a0=%08x a1=%08x\n",
						machine->cpu.cycles, pc,
						machine->cpu.gpr[(word >> 21) & 31u],
						machine->cpu.gpr[4], machine->cpu.gpr[5]);
				else if (word == 0x03e00008u)
					printf("%10u %08x ret v0=%08x\n",
						machine->cpu.cycles, pc, machine->cpu.gpr[2]);
				psx_machine_step(machine);
			}
		} else if (getenv("PSX_TTY")) {
			/* Single-step so kernel console output (B0:3Dh) is seen. */
			uint32_t n;
			for (n = 0; n < 256u; ++n) {
				if ((machine->cpu.pc & 0x1fffffffu) == 0xb0u &&
				    machine->cpu.gpr[9] == 0x3du)
					putchar((int)(machine->cpu.gpr[4] & 0xffu));
				psx_machine_step(machine);
			}
		} else {
			psx_machine_run(machine, 256u);
		}
		if (machine->cpu.cycles / (FRAME_CYCLES * 60u) != second) {
			second = machine->cpu.cycles / (FRAME_CYCLES * 60u);
			report(machine, second);
		}
	}
	report(machine, second);
	if (getenv("PSX_WATCH")) {
		/* Step until the RAM word at PSX_WATCH changes, keeping the path. */
		uint32_t address = (uint32_t)strtoul(getenv("PSX_WATCH"), 0, 0) &
			(PSX_MAIN_RAM_BYTES - 4u);
		static uint32_t ring_pc[256];
		static uint32_t ring_ra[256];
		uint32_t count = 0;
		uint32_t watch = *(uint32_t *)&ram[address];
		uint32_t n;
		for (n = 0; n < 400000000u; ++n) {
			ring_pc[count & 255u] = machine->cpu.pc;
			ring_ra[count & 255u] = machine->cpu.gpr[31];
			++count;
			if (machine->cpu.cycles - last_vblank >= FRAME_CYCLES) {
				psx_machine_vblank(machine);
				last_vblank += FRAME_CYCLES;
			}
			psx_machine_step(machine);
			if (*(uint32_t *)&ram[address] != watch) {
				printf("ram[%06x] %08x -> %08x at step %u\n", address,
					watch, *(uint32_t *)&ram[address], n);
				break;
			}
		}
		printf("stopped after %u steps, cycles=%u\n", n, machine->cpu.cycles);
		for (n = count > 256u ? count - 256u : 0u; n < count; ++n)
			printf("  %08x ra=%08x\n", ring_pc[n & 255u], ring_ra[n & 255u]);
	}
	if (getenv("PSX_PC_PROFILE")) {
		/* Where the next 4M instructions go, one step at a time. */
		static uint32_t pcs[4096];
		static uint32_t hits[4096];
		uint32_t used = 0;
		uint32_t n;
		for (n = 0; n < 4000000u; ++n) {
			uint32_t pc = machine->cpu.pc;
			uint32_t k;
			for (k = 0; k < used && pcs[k] != pc; ++k)
				;
			if (k == used && used < 4096u)
				pcs[used++] = pc;
			if (k < used)
				++hits[k];
			if (machine->cpu.cycles - last_vblank >= FRAME_CYCLES) {
				psx_machine_vblank(machine);
				last_vblank = machine->cpu.cycles;
			}
			psx_machine_step(machine);
		}
		for (n = 0; n < used; ++n)
			printf("  %08x %u\n", pcs[n], hits[n]);
	}
	if (argc > 4 && write_display(argv[4], machine))
		return 2;
	if (getenv("PSX_RAM_DUMP")) {
		FILE *dump = fopen(getenv("PSX_RAM_DUMP"), "wb");
		if (!dump || fwrite(ram, 1, PSX_MAIN_RAM_BYTES, dump) !=
		    PSX_MAIN_RAM_BYTES || fclose(dump) != 0)
			return 2;
	}
	return 0;
}
