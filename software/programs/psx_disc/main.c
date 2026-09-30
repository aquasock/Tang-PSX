// SPDX-License-Identifier: GPL-3.0-only
//
// Boot SCPH-1001 with the disc image Tang-Control serves from the SD card.
// Sectors are requested in 32-sector windows through the loader's disc
// mailbox and arrive through the stream FIFO; two windows allow read-ahead.
// VBlank runs at 60 Hz of emulated time, and while the CPU only waits for it
// the machine clock skips ahead so device latency matches hardware.

#include <stdint.h>

#include "machine.h"
#include "tpx_api.h"
#ifdef PSX_LIGHTREC
#include "psx_lightrec.h"
#include "tpx_platform.h"
#endif

#ifndef RUN_TIMEOUT_SECONDS
#define RUN_TIMEOUT_SECONDS  0u       /* zero runs until the core is reset */
#endif
#define FRAME_CYCLES         564480u    /* 33.8688 MHz / 60 */
#define REFRESH_FRAMES       3u
#define SERVICE_INSTRUCTIONS 256u
#define WINDOW_SECTORS       32u
#define WINDOW_BYTES         (WINDOW_SECTORS * PSX_CD_SECTOR_BYTES)
#define REQUEST_TIMEOUT_MS   2000u
#define RESULT_DONE          0xd15c0001u
#define PSX_VRAM_BASE        0x7fe00000u
#define CODE_BUFFER_BYTES    (8u << 20)

enum window_state { WINDOW_EMPTY, WINDOW_FILLING, WINDOW_READY };

struct window {
	uint32_t lba;
	uint32_t count;
	uint32_t state;
	uint8_t data[WINDOW_BYTES] __attribute__((aligned(4)));
};

extern const uint8_t psx_bios_image[];
extern const uint8_t psx_bios_image_end[];

static uint8_t psx_ram[PSX_MAIN_RAM_BYTES];
static uint16_t *const psx_vram =
	(uint16_t *)(uintptr_t)PSX_VRAM_BASE;
static struct psx_machine machine;
static struct window windows[2];
static const struct tpx_api *api;
static uint32_t disc_sectors;
static int filling = -1;
static uint32_t fill_bytes;
static uint64_t fill_started;
static uint64_t start_cycle;
static uint32_t requests;
static uint32_t retries;
static uint32_t misses;
static uint64_t profile_sync_cycles;
static uint64_t profile_display_cycles;
#ifdef PSX_LIGHTREC
static uint8_t code_buffer[CODE_BUFFER_BYTES] __attribute__((aligned(4096)));
#endif

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

static uint32_t elapsed_ms(void)
{
	return (uint32_t)((read_cycle() - start_cycle) / (api->cpu_hz / 1000u));
}

static uint32_t profile_ms(uint64_t cycles)
{
	return (uint32_t)(cycles / (api->cpu_hz / 1000u));
}

static void request_window(int slot, uint32_t lba)
{
	struct window *w = &windows[slot];
	w->lba = lba;
	w->count = disc_sectors - lba < WINDOW_SECTORS ?
		disc_sectors - lba : WINDOW_SECTORS;
	w->state = WINDOW_FILLING;
	filling = slot;
	fill_bytes = 0;
	fill_started = read_cycle();
	++requests;
	api->disc_request(lba * PSX_CD_SECTOR_BYTES,
		w->count * PSX_CD_SECTOR_BYTES);
}

/* Moves stream FIFO entries into the window being filled. */
static void pump_stream(void)
{
	uint32_t data;
	int32_t tag;
	while ((tag = api->stream_read(&data)) >= 0) {
		struct window *w;
		if (filling < 0)
			continue;               /* stale session */
		w = &windows[filling];
		if (tag == TPX_STREAM_START) {
			fill_bytes = 0;
		} else if (tag == TPX_STREAM_DATA) {
			if (fill_bytes + 4u <= WINDOW_BYTES) {
				w->data[fill_bytes] = (uint8_t)data;
				w->data[fill_bytes + 1u] = (uint8_t)(data >> 8);
				w->data[fill_bytes + 2u] = (uint8_t)(data >> 16);
				w->data[fill_bytes + 3u] = (uint8_t)(data >> 24);
			}
			fill_bytes += 4u;
		} else {
			w->state = (tag == TPX_STREAM_END &&
				data == w->count * PSX_CD_SECTOR_BYTES) ?
				WINDOW_READY : WINDOW_EMPTY;
			filling = -1;
		}
	}
	/* Tang-Control never answered: ask again. */
	if (filling >= 0 && (read_cycle() - fill_started) / (api->cpu_hz / 1000u) >
	    REQUEST_TIMEOUT_MS) {
		++retries;
		request_window(filling, windows[filling].lba);
	}
}

static int read_sector(void *opaque, uint32_t lba, uint8_t *sector)
{
	int slot;
	(void)opaque;
	pump_stream();
	for (slot = 0; slot < 2; ++slot) {
		const struct window *w = &windows[slot];
		uint32_t n;
		const uint8_t *source;
		if (w->state != WINDOW_READY || lba < w->lba ||
		    lba >= w->lba + w->count)
			continue;
		source = &w->data[(lba - w->lba) * PSX_CD_SECTOR_BYTES];
		for (n = 0; n < PSX_CD_SECTOR_BYTES; ++n)
			sector[n] = source[n];
		/* Past the middle of a window: fetch the next one ahead. */
		if (filling < 0 && lba >= w->lba + w->count / 2u &&
		    w->lba + w->count < disc_sectors) {
			const struct window *other = &windows[slot ^ 1];
			uint32_t next = w->lba + w->count;
			if (other->state != WINDOW_READY || other->lba != next)
				request_window(slot ^ 1, next);
		}
		return 0;
	}
	++misses;
	if (filling < 0) {
		/* Replace the window that did not just serve a sector. */
		slot = windows[0].state == WINDOW_READY &&
			windows[1].state != WINDOW_READY ? 1 : 0;
		request_window(slot, lba);
	}
	return -1;
}

static void log_text(const char *text)
{
	while (*text)
		api->putc(*text++);
}

static void log_decimal(const char *label, uint32_t value)
{
	char digits[10];
	int n = 0;
	log_text(label);
	do {
		digits[n++] = (char)('0' + value % 10u);
		value /= 10u;
	} while (value);
	while (n)
		api->putc(digits[--n]);
}

/* Returns Lightrec's unexpected exit flags, which end the run; 0 otherwise. */
static uint32_t run_cpu(void)
{
#ifdef PSX_LIGHTREC
	uint64_t start = read_cycle();
	uint32_t flags = psx_lightrec_run(&machine, SERVICE_INSTRUCTIONS);
	machine.profile_cpu_cycles += read_cycle() - start;
	return flags;
#else
	psx_machine_run(&machine, SERVICE_INSTRUCTIONS);
	return 0;
#endif
}

static void publish(void)
{
	api->set_reg(TPX_REG_WORDS, machine.cpu.cycles);
	api->set_reg(TPX_REG_CHECKSUM, machine.gpu.command_words);
	api->set_reg(TPX_REG_JIT, machine.cdrom.sectors_read);
	api->set_reg(TPX_REG_CYCLES, elapsed_ms());
	api->set_reg(TPX_REG_FEATURES,
		(machine.cdrom.commands << 16) | (machine.vblanks & 0xffffu));
	api->set_reg(TPX_REG_FAIL_ADDRESS, machine.cpu.pc);
	api->set_reg(TPX_REG_FAIL_EXPECTED, (requests << 16) | (misses & 0xffffu));
	api->set_reg(TPX_REG_FAIL_OBSERVED, machine.cpu.cp0[13]);
	api->set_reg(TPX_REG_PROFILE_CPU, profile_ms(machine.profile_cpu_cycles));
	api->set_reg(TPX_REG_PROFILE_GPU, profile_ms(machine.profile_gpu_cycles));
	api->set_reg(TPX_REG_PROFILE_ACCEL,
		profile_ms(machine.profile_accel_cycles));
	api->set_reg(TPX_REG_PROFILE_SYNC, profile_ms(profile_sync_cycles));
	api->set_reg(TPX_REG_PROFILE_DISPLAY, profile_ms(profile_display_cycles));
}

uint32_t main(const struct tpx_api *loader)
{
	volatile uint16_t *framebuffer =
		(volatile uint16_t *)(uintptr_t)TPX_FRAMEBUFFER_BASE;
	struct psx_disc disc = {0};
	uint32_t next_vblank = FRAME_CYCLES;
	uint32_t next_publish = 1000u;
	uint32_t frames = 0;
	uint32_t flags = 0;

	api = loader;
#ifdef PSX_LIGHTREC
	tpx_platform_attach(api);
#endif
	api->set_reg(TPX_REG_STAGE, 0x00020001u);
	api->set_reg(TPX_REG_FAILURE, 0);
	log_text("PSX disc boot\n");
	if (api->version < 2u ||
	    (uint32_t)(psx_bios_image_end - psx_bios_image) != PSX_BIOS_BYTES) {
		api->set_reg(TPX_REG_FAILURE, 1);
		api->set_reg(TPX_REG_STAGE, 0x8002bad1u);
		log_text("needs loader API 2 and the BIOS\n");
		return 0xdead2001u;
	}
	start_cycle = read_cycle();
	/* Tang-Control publishes the disc once it sees the core. */
	while ((disc_sectors = api->disc_sectors()) == 0u && elapsed_ms() < 3000u)
		;
	log_decimal("disc sectors ", disc_sectors);
	log_text("\n");

	psx_machine_reset(&machine, psx_ram, psx_vram, psx_bios_image);
	if (disc_sectors) {
		disc.read = read_sector;
		disc.sectors = disc_sectors;
		disc.region = 'A';
		psx_machine_insert_disc(&machine, &disc);
	}
#ifdef PSX_LIGHTREC
	if (psx_lightrec_init(&machine, code_buffer, sizeof(code_buffer))) {
		api->set_reg(TPX_REG_FAILURE, 3u);
		api->set_reg(TPX_REG_STAGE, 0x8002bad3u);
		log_text("lightrec init failed\n");
		return 0xdead2003u;
	}
#endif
	start_cycle = read_cycle();
#if RUN_TIMEOUT_SECONDS
	while (elapsed_ms() < RUN_TIMEOUT_SECONDS * 1000u) {
#else
	for (;;) {
#endif
		pump_stream();
		if (psx_machine_waiting_for_vblank(&machine))
			psx_machine_idle_to(&machine, next_vblank);
		if ((int32_t)(machine.cpu.cycles - next_vblank) >= 0) {
			psx_machine_vblank(&machine);
			next_vblank += FRAME_CYCLES;
			if (++frames % REFRESH_FRAMES == 0u) {
				uint64_t profile_start = read_cycle();
				psx_gpu_sync(&machine.gpu);
				profile_sync_cycles += read_cycle() - profile_start;
				profile_start = read_cycle();
				psx_machine_copy_display(&machine, framebuffer,
					TPX_FRAMEBUFFER_WIDTH, TPX_FRAMEBUFFER_HEIGHT);
				api->flush_dcache();
				profile_display_cycles += read_cycle() - profile_start;
			}
		} else if ((flags = run_cpu()) != 0) {
			break;
		}
		if (elapsed_ms() >= next_publish) {
			publish();
			api->set_reg(TPX_REG_STAGE,
				0x00020000u | (machine.vblanks & 0xffffu));
			next_publish += 1000u;
		}
	}
	publish();
	if (flags) {
		api->set_reg(TPX_REG_FAILURE, 4u);
		api->set_reg(TPX_REG_STAGE, 0x8002bad4u);
		log_decimal("lightrec exit ", flags);
		log_text("\n");
		return 0xdead2004u;
	}
	api->set_reg(TPX_REG_STAGE, 0x80020001u);
	log_decimal("vblanks ", machine.vblanks);
	log_decimal(" sectors ", machine.cdrom.sectors_read);
	log_decimal(" requests ", requests);
	log_decimal(" retries ", retries);
	log_decimal(" misses ", misses);
	log_text("\n");
	return RESULT_DONE;
}
