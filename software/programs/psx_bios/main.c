// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>

#include "machine.h"
#include "tpx_api.h"

#define SERVICE_INSTRUCTIONS 256u
#define REFRESH_VBLANKS 60u
#define RUN_TIMEOUT_SECONDS 30u
#define PROFILE_VBLANK 13u
#define RESULT_COMPLETE 0xb1051001u
#define PSX_VRAM_BASE 0x7fe00000u

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES];
static uint16_t *const psx_vram =
	(uint16_t *)(uintptr_t)PSX_VRAM_BASE;
static struct psx_machine machine;
static uint64_t start_cycle;
static uint64_t elapsed_cycles;
static uint64_t display_cycles;
static uint64_t profile_vblank_cycles;
static uint32_t counter_hz;

/*
 * Full 64-bit cycle count. A single emulated operation can outlast the 5.7 s
 * wrap of the low word, so wrap-accumulated 32-bit deltas lose time.
 */
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

static uint64_t elapsed(void)
{
	elapsed_cycles = read_cycle() - start_cycle;
	return elapsed_cycles;
}

static void log_text(const struct tpx_api *api, const char *text)
{
	while (*text)
		api->putc(*text++);
}

static void log_decimal(const struct tpx_api *api, const char *label,
	uint32_t value)
{
	char digits[10];
	int n = 0;
	log_text(api, label);
	do {
		digits[n++] = (char)('0' + value % 10u);
		value /= 10u;
	} while (value);
	while (n)
		api->putc(digits[--n]);
}

static uint32_t milliseconds(uint64_t cycles)
{
	return (uint32_t)(cycles / (counter_hz / 1000u));
}

/*
 * The log ring holds 128 bytes, so the summary is compact: elapsed time and
 * its attribution in milliseconds, then the JIT counts that tie this run to
 * the deterministic qemu-riscv32 profile.
 */
static void log_profile(const struct tpx_api *api)
{
	log_decimal(api, "ms t", milliseconds(elapsed_cycles));
	log_decimal(api, " v13 ", milliseconds(profile_vblank_cycles));
	log_decimal(api, " cpu ", milliseconds(machine.profile_cpu_cycles));
	log_decimal(api, " gpu ", milliseconds(machine.profile_gpu_cycles));
	log_decimal(api, " acc ", milliseconds(machine.profile_accel_cycles));
	log_decimal(api, " dsp ", milliseconds(display_cycles));
	log_decimal(api, "\njit ", machine.jit.executed_instructions);
	log_decimal(api, " fb ", machine.jit.interpreter_instructions);
	log_decimal(api, " fl ", machine.jit.cache_flushes);
	log_text(api, "\n");
}

static void publish(const struct tpx_api *api)
{
	api->set_reg(TPX_REG_WORDS, machine.cpu.cycles);
	api->set_reg(TPX_REG_CHECKSUM, machine.gpu.command_words);
	api->set_reg(TPX_REG_JIT, machine.gpu.primitives);
	api->set_reg(TPX_REG_CYCLES, (uint32_t)elapsed());
	api->set_reg(TPX_REG_FEATURES,
		(machine.gpu.uploads << 16) | (machine.vblanks & 0xffffu));
	api->set_reg(TPX_REG_FAIL_ADDRESS, machine.cpu.pc);
	api->set_reg(TPX_REG_FAIL_EXPECTED,
		(machine.irq_mask << 16) | (machine.irq_status & 0xffffu));
	api->set_reg(TPX_REG_FAIL_OBSERVED, machine.dma_words);
}

static void copy_display(const struct tpx_api *api,
	volatile uint16_t *framebuffer)
{
	uint64_t start = read_cycle();
	psx_gpu_sync(&machine.gpu);
	psx_machine_copy_display(&machine, framebuffer,
		TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
	api->flush_dcache();
	display_cycles += read_cycle() - start;
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
	uint64_t timeout = (uint64_t)RUN_TIMEOUT_SECONDS * api->cpu_hz;
	uint64_t next_publish = api->cpu_hz;
	uint32_t next_refresh = REFRESH_VBLANKS;

	counter_hz = api->cpu_hz;
	api->set_reg(TPX_REG_STAGE, 0x00010001u);
	api->set_reg(TPX_REG_FAILURE, 0);
	log_text(api, "SCPH-1001 BIOS start\n");
	if ((uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES) {
		api->set_reg(TPX_REG_FAILURE, 1);
		api->set_reg(TPX_REG_STAGE, 0x8001badu);
		return 0xdead1001u;
	}
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	elapsed_cycles = 0;
	start_cycle = read_cycle();
	while (!logo_complete() && elapsed() < timeout) {
		if (psx_machine_waiting_for_vblank(&machine)) {
			psx_machine_vblank(&machine);
			if (machine.vblanks == PROFILE_VBLANK)
				profile_vblank_cycles = elapsed();
		} else {
			psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
		}
		if ((int32_t)(machine.vblanks - next_refresh) >= 0) {
			copy_display(api, framebuffer);
			publish(api);
			api->set_reg(TPX_REG_STAGE,
				0x00010000u | (machine.vblanks & 0xffffu));
			next_refresh = machine.vblanks + REFRESH_VBLANKS;
		}
		/* Once-per-second heartbeat, so a stall is visible between refreshes. */
		if (elapsed_cycles >= next_publish) {
			publish(api);
			api->set_reg(TPX_REG_STAGE,
				0x00010000u | (machine.vblanks & 0xffffu));
			next_publish += api->cpu_hz;
		}
	}
	elapsed();
	copy_display(api, framebuffer);
	publish(api);
	if (!logo_complete()) {
		api->set_reg(TPX_REG_FAILURE, 2u);
		api->set_reg(TPX_REG_STAGE, 0x8001bad2u);
		log_text(api, "logo timed out\n");
		log_profile(api);
		return 0xdead1002u;
	}
	api->set_reg(TPX_REG_STAGE, 0x80011001u);
	log_text(api, "logo complete\n");
	log_profile(api);
	return RESULT_COMPLETE;
}
