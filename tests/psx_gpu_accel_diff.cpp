// SPDX-License-Identifier: GPL-3.0-only
//
// Verilated differential for the fabric rasterizer. The current software GPU
// produces descriptors through its test hook, a second copy draws the same
// GP0 stream in software, and this harness requires identical VRAM after every
// primitive.
//
// With arguments TRACE FIRST LAST it replays a GPU trace recorded by
// tests/psx_disc_cores_rv32.c (GPU_TRACE) instead: both copies draw in
// software until VBlank FIRST, the fabric draws from then on, VRAM must match
// at every VBlank through LAST, and the fabric's clocks per pixel over that
// window are reported.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Vgpu_rasterizer.h"
#ifdef STATE_HISTOGRAM
// Clocks spent in each rasterizer state, from Verilator's internal signal.
#include "Vgpu_rasterizer___024root.h"
static unsigned long long state_clocks[64];
#endif
#include "gpu.h"

void ref_gpu_reset(struct psx_gpu *, uint16_t *);
void ref_gpu_write_gp0(struct psx_gpu *, uint32_t);
void ref_gpu_write_gp1(struct psx_gpu *, uint32_t);
uint32_t ref_gpu_read_data(struct psx_gpu *);

int psx_gpu_accel_test_available = 1;
static std::vector<uint32_t> descriptors;
static std::vector<uint32_t> last_descriptors;
static uint32_t last_command;

void psx_gpu_accel_test_push(uint32_t value)
{
	descriptors.push_back(value);
}

static constexpr uint32_t VRAM_NATIVE_BASE = 0x1ff0000u;
static struct psx_gpu accelerated, reference;
static uint16_t accelerated_vram[PSX_VRAM_PIXELS];
static uint16_t reference_vram[PSX_VRAM_PIXELS];
// Clocks from a read command to its data; the board's DDR3 path is about 30.
#ifndef READ_LATENCY
#define READ_LATENCY 1
#endif

static uint32_t random_state = 0x6157a11du;

static uint32_t random32()
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static uint32_t below(uint32_t limit)
{
	return random32() % limit;
}

class RasterModel {
public:
	Vgpu_rasterizer dut;
	bool read_valid = false;
	uint32_t read_delay = 0;
	uint32_t read_words[8]{};
	bool write_pending = false;
	uint32_t write_address = 0;
	uint64_t cycles = 0;

	RasterModel()
	{
		dut.clk = 0;
		dut.rst = 1;
		dut.cmd_valid = 0;
		dut.mem_cmd_ready = 1;
		dut.mem_wdata_ready = 1;
		dut.mem_rdata_valid = 0;
		for (unsigned n = 0; n < 8; ++n)
			dut.mem_rdata_data[n] = 0;
		tick();
		tick();
		dut.rst = 0;
		tick();
	}

	void load_line(uint32_t address, uint32_t *words)
	{
		if (address < VRAM_NATIVE_BASE ||
		    address >= VRAM_NATIVE_BASE + PSX_VRAM_PIXELS / 16u) {
			std::fprintf(stderr, "bad raster read address %08x\n", address);
			std::exit(2);
		}
		const uint32_t offset = (address - VRAM_NATIVE_BASE) * 16u;
		for (unsigned n = 0; n < 8; ++n)
			words[n] = (uint32_t)accelerated_vram[offset + n * 2u] |
				((uint32_t)accelerated_vram[offset + n * 2u + 1u] << 16);
	}

	void store_line(uint32_t address, const uint32_t *words, uint32_t enables)
	{
		if (address < VRAM_NATIVE_BASE ||
		    address >= VRAM_NATIVE_BASE + PSX_VRAM_PIXELS / 16u) {
			std::fprintf(stderr, "bad raster write address %08x\n", address);
			std::exit(2);
		}
		uint8_t *memory = reinterpret_cast<uint8_t *>(accelerated_vram);
		const uint32_t offset = (address - VRAM_NATIVE_BASE) * 32u;
		for (unsigned byte = 0; byte < 32; ++byte)
			if (enables & (1u << byte))
				memory[offset + byte] =
					(uint8_t)(words[byte / 4u] >> (8u * (byte & 3u)));
	}

	void tick()
	{
		dut.clk = 0;
		dut.mem_cmd_ready = 1;
		dut.mem_wdata_ready = 1;
		dut.mem_rdata_valid = read_valid;
		for (unsigned n = 0; n < 8; ++n)
			dut.mem_rdata_data[n] = read_words[n];
		dut.eval();

		const bool command = dut.mem_cmd_valid && dut.mem_cmd_ready;
		const bool command_write = dut.mem_cmd_we;
		const uint32_t command_address = dut.mem_cmd_addr;
		const bool write_data = dut.mem_wdata_valid && dut.mem_wdata_ready;
		uint32_t data[8];
		for (unsigned n = 0; n < 8; ++n)
			data[n] = dut.mem_wdata_data[n];
		const uint32_t enables = dut.mem_wdata_we;
		const bool read_consumed = read_valid && dut.mem_rdata_ready;

#ifdef STATE_HISTOGRAM
		++state_clocks[dut.rootp->gpu_rasterizer__DOT__state & 63];
#endif
		dut.clk = 1;
		dut.eval();
		++cycles;

		if (read_consumed)
			read_valid = false;
		if (read_delay && --read_delay == 0)
			read_valid = true;
		if (command) {
			if (command_write) {
				if (write_pending) {
					std::fprintf(stderr, "overlapping raster writes\n");
					std::exit(2);
				}
				write_pending = true;
				write_address = command_address;
			} else {
				if (read_valid || read_delay) {
					std::fprintf(stderr, "overlapping raster reads\n");
					std::exit(2);
				}
				load_line(command_address, read_words);
				if (READ_LATENCY <= 1)
					read_valid = true;
				else
					read_delay = READ_LATENCY - 1;
			}
		}
		if (write_data) {
			if (!write_pending) {
				std::fprintf(stderr, "raster write data without command\n");
				std::exit(2);
			}
			store_line(write_address, data, enables);
			write_pending = false;
		}
	}

	void run_descriptors()
	{
		last_descriptors = descriptors;
		size_t index = 0;
		uint64_t watchdog = 0;
		while (index != descriptors.size() || !dut.idle) {
			dut.cmd_valid = index != descriptors.size();
			dut.cmd_data = index != descriptors.size() ? descriptors[index] : 0;
			dut.clk = 0;
			dut.eval();
			const bool accepted = dut.cmd_valid && dut.cmd_ready;
			tick();
			if (accepted)
				++index;
			if (++watchdog > 100000000u) {
				std::fprintf(stderr, "raster timeout with %zu/%zu descriptor words\n",
					index, descriptors.size());
				std::exit(2);
			}
		}
		dut.cmd_valid = 0;
		descriptors.clear();
		if (dut.error) {
			std::fprintf(stderr, "raster descriptor error\n");
			std::exit(2);
		}
	}
};

static RasterModel raster;

static void both(uint32_t word)
{
	psx_gpu_write_gp0(&accelerated, word);
	ref_gpu_write_gp0(&reference, word);
}

static uint32_t coordinate(uint32_t base_x, uint32_t base_y)
{
	int32_t x = (int32_t)base_x + (int32_t)below(17) - 8;
	int32_t y = (int32_t)base_y + (int32_t)below(17) - 8;
	return ((uint32_t)x & 0x7ffu) | (((uint32_t)y & 0x7ffu) << 16);
}

static void random_gpu_state(uint32_t iteration)
{
	uint32_t page = below(0x200u);
	both(0xe1000000u | page);
	both(0xe3000000u);                         // full drawing area
	both(0xe4000000u | 1023u | (511u << 10));
	both(0xe5000000u);                         // zero drawing offset
	both(0xe6000000u | ((iteration / 97u) & 3u));
}

static void random_polygon(uint32_t iteration)
{
	uint32_t command = 0x20u | (random32() & 0x1du);
	uint32_t vertices = (command & 8u) ? 4u : 3u;
	uint32_t base_x = 4u + below(1016u);
	uint32_t base_y = 4u + below(504u);
	last_command = command << 24;

	both((command << 24) | (random32() & 0xffffffu));
	for (uint32_t n = 0; n < vertices; ++n) {
		if (n && (command & 16u))
			both(random32() & 0xffffffu);
		both(coordinate(base_x, base_y));
		if (command & 4u) {
			uint32_t attribute = n == 0 ? below(0x8000u) :
				(n == 1 ? below(0x200u) : 0u);
			both((attribute << 16) | (random32() & 0xffffu));
		}
	}
	(void)iteration;
}

static void random_rectangle()
{
	uint32_t textured = below(2u);
	uint32_t raw = below(2u);
	uint32_t command = 0x60u | (textured << 2) | raw;
	uint32_t x = below(1016u);
	uint32_t y = below(504u);
	uint32_t width = 1u + below(8u);
	uint32_t height = 1u + below(8u);
	last_command = command << 24;
	both((command << 24) | (random32() & 0xffffffu));
	both((x & 0x7ffu) | ((y & 0x7ffu) << 16));
	if (textured)
		both((below(0x8000u) << 16) | (random32() & 0xffffu));
	both(width | (height << 16));
}

static void print_states()
{
#ifdef STATE_HISTOGRAM
	static const char *const names[] = {
		"IDLE", "LOAD", "TRI_INIT", "RECT_INIT", "PIXEL", "LOOKUP",
		"READ_CMD", "READ_WAIT", "TEX_PACKED", "TEX_CLUT", "TEX_READY",
		"MASK_READY", "PIXEL_COMMIT", "FLUSH_CMD", "FLUSH_DATA", "FINISH",
		"DONE", "TEX_MULT", "TEX_SHADE"};
	unsigned long long total = 0;
	for (unsigned n = 0; n < 64; ++n)
		total += state_clocks[n];
	for (unsigned n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
		std::printf("  %-12s %12llu %5.1f%%\n", names[n], state_clocks[n],
			total ? 100.0 * state_clocks[n] / total : 0.0);
#endif
}

static int replay(const char *path, uint32_t first, uint32_t last)
{
	FILE *file = std::fopen(path, "rb");
	uint32_t record[2];
	uint64_t cycles = 0;
	uint32_t pixels = 0;
	uint32_t frames = 0;
	if (!file) {
		std::fprintf(stderr, "cannot open %s\n", path);
		return 2;
	}
	accelerated.hardware_accel = 0;
	while (std::fread(record, sizeof(record), 1, file) == 1) {
		if (record[0] == 0) {
			last_command = record[1];
			both(record[1]);
			if (!descriptors.empty())
				raster.run_descriptors();
		} else if (record[0] == 1) {
			psx_gpu_write_gp1(&accelerated, record[1]);
			ref_gpu_write_gp1(&reference, record[1]);
		} else if (record[0] == 2) {
			uint32_t a = psx_gpu_read_data(&accelerated);
			uint32_t b = ref_gpu_read_data(&reference);
			if (a != b || a != record[1]) {
				std::fprintf(stderr, "GPUREAD %08x/%08x, recorded %08x\n",
					a, b, record[1]);
				return 1;
			}
		} else if (record[1] == first) {
			accelerated.hardware_accel = 1;
#ifdef STATE_HISTOGRAM
			std::memset(state_clocks, 0, sizeof(state_clocks));
#endif
			cycles = raster.cycles;
			pixels = raster.dut.pixels;
		} else if (record[1] > first) {
			++frames;
			if (std::memcmp(accelerated_vram, reference_vram,
			    sizeof(accelerated_vram)) != 0) {
				std::fprintf(stderr, "fabric mismatch at VBlank %u\n", record[1]);
				return 1;
			}
			if (record[1] == last)
				break;
		}
	}
	std::fclose(file);
	cycles = raster.cycles - cycles;
	pixels = raster.dut.pixels - pixels;
	std::printf("replay VBlank %u-%u: %u frames match, %u pixels in %llu clocks, "
		"%.2f clocks per pixel, %.0f clocks per VBlank (read latency %u)\n",
		first, last, frames, pixels, (unsigned long long)cycles,
		pixels ? (double)cycles / pixels : 0.0,
		frames ? (double)cycles / frames : 0.0, (unsigned)READ_LATENCY);
	print_states();
	return frames ? 0 : 1;
}

int main(int argc, char **argv)
{
	ref_gpu_reset(&reference, reference_vram);
	psx_gpu_reset(&accelerated, accelerated_vram);
	if (argc == 4)
		return replay(argv[1], (uint32_t)std::strtoul(argv[2], 0, 0),
			(uint32_t)std::strtoul(argv[3], 0, 0));
	for (uint32_t n = 0; n < PSX_VRAM_PIXELS; ++n) {
		uint16_t value = (uint16_t)random32();
		accelerated_vram[n] = value;
		reference_vram[n] = value;
	}

	for (uint32_t n = 0; n < 40000u; ++n) {
		if (n % 97u == 0)
			random_gpu_state(n);
		if (n % 20u == 0)
			random_rectangle();
		else
			random_polygon(n);
		raster.run_descriptors();
		if (std::memcmp(accelerated_vram, reference_vram,
		    sizeof(accelerated_vram)) != 0) {
			uint32_t pixel = 0;
			while (accelerated_vram[pixel] == reference_vram[pixel])
				++pixel;
			std::fprintf(stderr,
				"fabric mismatch after primitive %u command %08x at %u,%u: %04x expected %04x\n",
				n, last_command, pixel % 1024u, pixel / 1024u,
				accelerated_vram[pixel], reference_vram[pixel]);
			for (size_t word = 0; word < last_descriptors.size(); ++word)
				std::fprintf(stderr, "d[%zu]=%08x\n", word, last_descriptors[word]);
			return 1;
		}
	}
	std::printf("PSX fabric GPU: 40000 random polygons/rectangles match in %llu cycles\n",
		(unsigned long long)raster.cycles);
	print_states();
	std::printf("read latency %u: %u pixels, %.2f clocks per pixel\n",
		(unsigned)READ_LATENCY, (unsigned)raster.dut.pixels,
		(double)raster.cycles / raster.dut.pixels);
	return 0;
}
