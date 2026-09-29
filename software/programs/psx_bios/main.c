// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>

#include "machine.h"
#include "tpx_api.h"

#define BOARD_TIMER_HZ 75000000u
#define SERVICE_INSTRUCTIONS 256u
#define REFRESH_VBLANKS 60u
#define RUN_TIMEOUT_TICKS (30u * BOARD_TIMER_HZ)
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

static void log_hex(const struct tpx_api *api, const char *label,
	uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	int shift;
	log_text(api, label);
	for (shift = 28; shift >= 0; shift -= 4)
		api->putc(digits[(value >> (uint32_t)shift) & 15u]);
	api->putc('\n');
}

static void log_jit(const struct tpx_api *api)
{
	log_hex(api, "jit compiled ", machine.jit.compiled_blocks);
	log_hex(api, "jit blocks ", machine.jit.executed_blocks);
	log_hex(api, "jit instructions ", machine.jit.executed_instructions);
	log_hex(api, "jit fallback ", machine.jit.interpreter_instructions);
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

static int logo_complete(void)
{
	return machine.gpu.command_words >= 10768u &&
		machine.gpu.primitives >= 414u && machine.gpu.uploads >= 63u &&
		machine.dma_words >= 158497u;
}

uint32_t main(const struct tpx_api *api)
{
	volatile uint16_t *framebuffer =
		(volatile uint16_t *)(uintptr_t)TPX_FRAMEBUFFER_BASE;
	uint32_t start;
	uint32_t next_refresh = REFRESH_VBLANKS;

	api->set_reg(TPX_REG_STAGE, 0x00010001u);
	api->set_reg(TPX_REG_FAILURE, 0);
	log_text(api, "SCPH-1001 BIOS start\n");
	if ((uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES) {
		api->set_reg(TPX_REG_FAILURE, 1);
		api->set_reg(TPX_REG_STAGE, 0x8001badu);
		return 0xdead1001u;
	}
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	start = read_cycle();
	while (!logo_complete() && read_cycle() - start < RUN_TIMEOUT_TICKS) {
		if (psx_machine_waiting_for_vblank(&machine))
			psx_machine_vblank(&machine);
		else
			psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
		if ((int32_t)(machine.vblanks - next_refresh) >= 0) {
			psx_machine_copy_display(&machine, framebuffer,
				TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
			api->flush_dcache();
			publish(api, start);
			api->set_reg(TPX_REG_STAGE,
				0x00010000u | (machine.vblanks & 0xffffu));
			next_refresh = machine.vblanks + REFRESH_VBLANKS;
		}
	}
	psx_machine_copy_display(&machine, framebuffer,
		TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
	api->flush_dcache();
	publish(api, start);
	log_jit(api);
	if (!logo_complete()) {
		api->set_reg(TPX_REG_FAILURE, 2u);
		api->set_reg(TPX_REG_STAGE, 0x8001bad2u);
		log_text(api, "SCPH-1001 logo checkpoint timed out\n");
		return 0xdead1002u;
	}
	api->set_reg(TPX_REG_STAGE, 0x80011001u);
	log_text(api, "SCPH-1001 logo checkpoint complete\n");
	return RESULT_COMPLETE;
}
