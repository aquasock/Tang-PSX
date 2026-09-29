// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>

#include "machine.h"
#include "psx_display.h"
#include "tpx_api.h"
#ifdef PSX_BIOS_LIGHTREC
#include "memmanager.h"
#include "psx_lightrec.h"
#include "runtime.h"
#include "tpx_runtime.h"
#endif

#define SERVICE_INSTRUCTIONS 256u
#define REFRESH_VBLANKS 60u
#ifdef PSX_BIOS_LIGHTREC
#define RUN_TIMEOUT_SECONDS 120u
#define CODE_BUFFER_BYTES (8u << 20)
#else
#define RUN_TIMEOUT_SECONDS 30u
#endif
#define PROFILE_VBLANK 13u
#define RESULT_COMPLETE 0xb1051001u
#define PSX_VRAM_BASE 0x7fe00000u

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES];
static uint16_t *const psx_vram =
	(uint16_t *)(uintptr_t)PSX_VRAM_BASE;
static struct psx_machine machine;
#ifdef PSX_BIOS_LIGHTREC
static uint8_t code_buffer[CODE_BUFFER_BYTES] __attribute__((aligned(4096)));
#endif
static uint64_t start_cycle;
static uint64_t elapsed_cycles;
static uint64_t display_cycles;
static uint64_t profile_vblank_cycles;
static uint32_t counter_hz;
static uint32_t fabric_display;

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
#ifdef PSX_BIOS_LIGHTREC
	log_decimal(api, "\nlr ", psx_lightrec_stats()->code_emissions);
	log_decimal(api, " code ", lightrec_get_mem_usage(MEM_FOR_CODE));
	log_decimal(api, " heap ", (uint32_t)tpx_runtime_heap_used());
#else
	log_decimal(api, "\njit ", machine.jit.executed_instructions);
	log_decimal(api, " fb ", machine.jit.interpreter_instructions);
	log_decimal(api, " fl ", machine.jit.cache_flushes);
#endif
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
	api->set_reg(TPX_REG_PROFILE_CPU, milliseconds(machine.profile_cpu_cycles));
	api->set_reg(TPX_REG_PROFILE_GPU, milliseconds(machine.profile_gpu_cycles));
	api->set_reg(TPX_REG_PROFILE_ACCEL, milliseconds(machine.profile_accel_cycles));
	api->set_reg(TPX_REG_PROFILE_DISPLAY, milliseconds(display_cycles));
}

static void copy_display(const struct tpx_api *api,
	volatile uint16_t *framebuffer)
{
	uint64_t start = read_cycle();
	psx_gpu_sync(&machine.gpu);
	if (fabric_display) {
		psx_display_blit(&machine.gpu);
	} else {
		psx_machine_copy_display(&machine, framebuffer,
			TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
		api->flush_dcache();
	}
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
#ifdef PSX_BIOS_LIGHTREC
	uint32_t flags = 0;
	tpx_runtime_bind(api, 0x8001bad4u);
#endif

	counter_hz = api->cpu_hz;
	fabric_display = (uint32_t)psx_display_available();
	api->set_reg(TPX_REG_STAGE, 0x00010001u);
	api->set_reg(TPX_REG_FAILURE, 0);
	log_text(api, "SCPH-1001 BIOS start\n");
	if ((uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES) {
		api->set_reg(TPX_REG_FAILURE, 1);
		api->set_reg(TPX_REG_STAGE, 0x8001badu);
		return 0xdead1001u;
	}
	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
#ifdef PSX_BIOS_LIGHTREC
	if (psx_lightrec_init(&machine, code_buffer, sizeof(code_buffer))) {
		api->set_reg(TPX_REG_FAILURE, 3u);
		api->set_reg(TPX_REG_STAGE, 0x8001bad3u);
		log_text(api, "Lightrec init failed\n");
		return 0xdead1003u;
	}
#endif
	elapsed_cycles = 0;
	start_cycle = read_cycle();
	while (!logo_complete() && elapsed() < timeout) {
		if (psx_machine_waiting_for_vblank(&machine)) {
			psx_machine_vblank(&machine);
			if (machine.vblanks == PROFILE_VBLANK)
				profile_vblank_cycles = elapsed();
		} else {
#ifdef PSX_BIOS_LIGHTREC
			uint64_t cpu_start = read_cycle();
			flags = psx_lightrec_run(&machine, SERVICE_INSTRUCTIONS);
			machine.profile_cpu_cycles += read_cycle() - cpu_start;
			if (flags)
				break;
#else
			psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
#endif
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
#ifdef PSX_BIOS_LIGHTREC
	if (flags) {
		api->set_reg(TPX_REG_FAILURE, flags);
		api->set_reg(TPX_REG_STAGE, 0x8001bad5u);
		log_text(api, "Lightrec exit\n");
		log_profile(api);
		return 0xdead1005u;
	}
#endif
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
