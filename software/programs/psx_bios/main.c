// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>

#include "machine.h"
#include "tpx_api.h"

#define BIOS_BATCH_INSTRUCTIONS 1000000u
#define BIOS_BATCHES 100u
#define BIOS_REFRESH_BATCHES 5u
#define RESULT_COMPLETE 0xb1051001u

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES];
static uint16_t psx_vram[PSX_VRAM_PIXELS];
static struct psx_machine machine;

static uint32_t read_cycle(void)
{
	uint32_t value;
	__asm__ volatile ("rdcycle %0" : "=r"(value));
	return value;
}

static void log_text(const struct tpx_api *api, const char *text)
{
	while (*text)
		api->putc(*text++);
}

static void publish(const struct tpx_api *api, uint32_t start)
{
	api->set_reg(TPX_REG_WORDS, machine.cpu.cycles);
	api->set_reg(TPX_REG_CHECKSUM, machine.gpu.command_words);
	api->set_reg(TPX_REG_JIT, machine.gpu.primitives);
	api->set_reg(TPX_REG_CYCLES, read_cycle() - start);
	api->set_reg(TPX_REG_FEATURES,
		(machine.gpu.uploads << 16) | (machine.vblanks & 0xffffu));
	api->set_reg(TPX_REG_FAIL_ADDRESS, machine.cpu.pc);
	api->set_reg(TPX_REG_FAIL_EXPECTED,
		(machine.irq_mask << 16) | (machine.irq_status & 0xffffu));
	api->set_reg(TPX_REG_FAIL_OBSERVED, machine.dma_words);
}

uint32_t main(const struct tpx_api *api)
{
	volatile uint16_t *framebuffer =
		(volatile uint16_t *)(uintptr_t)TPX_FRAMEBUFFER_BASE;
	uint32_t start = read_cycle();
	uint32_t batch;

	api->set_reg(TPX_REG_STAGE, 0x00010001u);
	api->set_reg(TPX_REG_FAILURE, 0);
	log_text(api, "SCPH-1001 BIOS start\n");
	if ((uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES) {
		api->set_reg(TPX_REG_FAILURE, 1);
		api->set_reg(TPX_REG_STAGE, 0x8001badu);
		return 0xdead1001u;
	}
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	for (batch = 0; batch < BIOS_BATCHES; ++batch) {
		psx_machine_run(&machine, BIOS_BATCH_INSTRUCTIONS);
		if ((batch + 1u) % BIOS_REFRESH_BATCHES == 0u) {
			psx_machine_copy_display(&machine, framebuffer,
				TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
			api->flush_dcache();
			publish(api, start);
			api->set_reg(TPX_REG_STAGE, 0x00010000u | (batch + 1u));
		}
	}
	publish(api, start);
	api->set_reg(TPX_REG_STAGE, 0x80011001u);
	log_text(api, "SCPH-1001 logo checkpoint complete\n");
	return RESULT_COMPLETE;
}
