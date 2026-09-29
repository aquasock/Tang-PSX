// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include <stdio.h>

#include <generated/csr.h>
#include <generated/mem.h>

#include "tpx_api.h"

#if VIDEO_FRAMEBUFFER_BASE != TPX_FRAMEBUFFER_BASE || \
	VIDEO_FRAMEBUFFER_HRES != TPX_FRAMEBUFFER_WIDTH || \
	VIDEO_FRAMEBUFFER_VRES != TPX_FRAMEBUFFER_HEIGHT
#error "Gate 1 framebuffer constants do not match the TPX drawing API"
#endif

/* Test layout inside main RAM (1 GiB at MAIN_RAM_BASE). */
#define DDR_TEST_WORDS        (1u << 18)        /* 1 MiB fixed-pattern region */
#define CACHED_TEST_OFFSET    0x01000000u       /* cached fixed-pattern region */
#define BYTE_TEST_OFFSET      0x00200000u
#define BYTE_TEST_BYTES       256u              /* eight 256-bit controller words */
#define INTERLEAVE_OFFSET     0x00300000u
#define INTERLEAVE_WORDS      4096u
#define CODE_OFFSET           0x00400000u
#define ADDRESS_BITS_FIRST    2u
#define ADDRESS_BITS_LAST     29u               /* 1 GiB */

#define CALIB_TIMEOUT_CYCLES  1500000000u       /* 2 s at the 750 MHz core clock */

#define FEATURE_DDR_INIT       (1u << 0)
#define FEATURE_DDR_RW         (1u << 1)
#define FEATURE_JIT_EXEC       (1u << 2)
#define FEATURE_DDR_ADDRESS    (1u << 3)
#define FEATURE_DDR_BYTE       (1u << 4)
#define FEATURE_DDR_INTERLEAVE (1u << 5)
#define FEATURE_CACHES         (1u << 6)
#define FEATURE_FRAMEBUFFER    (1u << 7)

#define FAIL_DDR_INIT          0x00010001u
#define FAIL_DDR_DATA          0x00010002u
#define FAIL_DDR_ADDRESS       0x00010003u
#define FAIL_DDR_BYTE          0x00010004u
#define FAIL_DDR_INTERLEAVE    0x00010005u
#define FAIL_DDR_OVERFLOW      0x00010006u
#define FAIL_JIT_FIRST         0x00020001u
#define FAIL_JIT_REPLACE       0x00020002u
#define FAIL_CACHE_ENABLE      0x00030001u
#define FAIL_CACHE_CCTL        0x00030002u

/* AndeStar V5 cache CSRs (see .ai/core-reference.md AE350-002). */
#define CSR_MCACHE_CTL         0x7ca
#define CSR_MCCTLCOMMAND       0x7cc
#define CSR_MICM_CFG           0xfc0
#define CSR_MDCM_CFG           0xfc1
#define CSR_MMSC_CFG           0xfc2
#define MCACHE_CTL_IC_EN       (1u << 0)
#define MCACHE_CTL_DC_EN       (1u << 1)
#define MMSC_CFG_CCTLCSR       (1u << 16)
#define CCTL_L1D_WBINVAL_ALL   6u

#define CSR_STR_(x) #x
#define CSR_STR(x) CSR_STR_(x)
#define CSR_READ(csr) ({ uint32_t v_; \
	__asm__ volatile ("csrr %0, " CSR_STR(csr) : "=r" (v_)); v_; })
#define CSR_WRITE(csr, value) \
	__asm__ volatile ("csrw " CSR_STR(csr) ", %0" :: "r" ((uint32_t)(value)) : "memory")
#define CSR_SET(csr, value) \
	__asm__ volatile ("csrs " CSR_STR(csr) ", %0" :: "r" ((uint32_t)(value)) : "memory")

#define LOG_WORDS 32u

static uint32_t log_head;
static uint32_t log_word;

static int gate1_putc(char c, FILE *file)
{
	uint32_t byte = log_head & 3u;
	uint32_t slot = (log_head >> 2) & (LOG_WORDS - 1u);

	(void)file;
	if (byte == 0)
		log_word = 0;
	log_word &= ~(0xffu << (8u * byte));
	log_word |= (uint32_t)(uint8_t)c << (8u * byte);
	csr_write_simple(log_word, CSR_GATE1_LOG0_ADDR + 4u * slot);
	++log_head;
	gate1_log_head_write(log_head);
	return (unsigned char)c;
}

static int gate1_getc(FILE *file)
{
	(void)file;
	return -1;
}

static FILE gate1_stdio = FDEV_SETUP_STREAM(
	gate1_putc, gate1_getc, NULL, _FDEV_SETUP_RW);

FILE *const stdout = &gate1_stdio;
FILE *const stderr = &gate1_stdio;
FILE *const stdin = &gate1_stdio;

static inline uint32_t read_cycle(void)
{
	uint32_t value;
	__asm__ volatile ("rdcycle %0" : "=r" (value));
	return value;
}

static uint32_t features;
static int caches_on;

static void feature_passed(uint32_t feature, uint32_t stage)
{
	features |= feature;
	gate1_features_write(features);
	gate1_stage_write(stage);
}

static __attribute__((noreturn)) void fail(uint32_t code, uintptr_t address,
	uint32_t expected, uint32_t observed)
{
	gate1_fail_address_write((uint32_t)address);
	gate1_fail_expected_write(expected);
	gate1_fail_observed_write(observed);
	gate1_failure_write(code);
	gate1_stage_write(0x80000000u | code);
	for (;;)
		__asm__ volatile ("wfi");
}

static uint32_t pattern(uint32_t index)
{
	uint32_t value = index + 0x9e3779b9u;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	return value;
}

static void install_return_constant(volatile uint32_t *code, uint32_t value)
{
	/* addi a0, zero, value; jalr zero, 0(ra) */
	code[0] = ((value & 0xfffu) << 20) | 0x00000513u;
	code[1] = 0x00008067u;
	__asm__ volatile ("fence rw, rw\n\tfence.i" ::: "memory");
}

static void wait_for_calibration(void)
{
	uint32_t start = read_cycle();

	while (!(ddr3_status_read() & (1u << CSR_DDR3_STATUS_CALIB_DONE_OFFSET))) {
		if (read_cycle() - start > CALIB_TIMEOUT_CYCLES)
			fail(FAIL_DDR_INIT, 0, 1, ddr3_status_read());
	}
}

/*
 * With the write-back D-cache on, write back and invalidate it before a
 * verify pass so every checked word is read from DDR3 rather than the cache.
 */
static void l1d_flush(void)
{
	__asm__ volatile ("fence rw, rw" ::: "memory");
	if (caches_on)
		CSR_WRITE(CSR_MCCTLCOMMAND, CCTL_L1D_WBINVAL_ALL);
	__asm__ volatile ("fence rw, rw" ::: "memory");
}

static void enable_caches(void)
{
	uint32_t ctl;

	if (!(CSR_READ(CSR_MMSC_CFG) & MMSC_CFG_CCTLCSR))
		fail(FAIL_CACHE_CCTL, 0, MMSC_CFG_CCTLCSR, CSR_READ(CSR_MMSC_CFG));
	CSR_SET(CSR_MCACHE_CTL, MCACHE_CTL_IC_EN | MCACHE_CTL_DC_EN);
	ctl = CSR_READ(CSR_MCACHE_CTL);
	if ((ctl & (MCACHE_CTL_IC_EN | MCACHE_CTL_DC_EN)) != (MCACHE_CTL_IC_EN | MCACHE_CTL_DC_EN))
		fail(FAIL_CACHE_ENABLE, 0, MCACHE_CTL_IC_EN | MCACHE_CTL_DC_EN, ctl);
	caches_on = 1;
	__asm__ volatile ("fence.i" ::: "memory");
	printf("ic %08lx dc %08lx ms %08lx mc %08lx\n",
		(unsigned long)CSR_READ(CSR_MICM_CFG), (unsigned long)CSR_READ(CSR_MDCM_CFG),
		(unsigned long)CSR_READ(CSR_MMSC_CFG), (unsigned long)ctl);
}

/* Sequential fixed-pattern write then verify; returns write and read cycles. */
static void test_fixed_pattern(volatile uint32_t *ram, uint32_t *write_cycles,
	uint32_t *read_cycles)
{
	uint32_t checksum = 0;
	uint32_t start;
	uint32_t i;

	start = read_cycle();
	for (i = 0; i < DDR_TEST_WORDS; ++i)
		ram[i] = pattern(i);
	l1d_flush();
	*write_cycles = read_cycle() - start;

	start = read_cycle();
	for (i = 0; i < DDR_TEST_WORDS; ++i) {
		uint32_t expected = pattern(i);
		uint32_t actual = ram[i];
		if (actual != expected) {
			gate1_ddr_words_write(i);
			fail(FAIL_DDR_DATA, (uintptr_t)&ram[i], expected, actual);
		}
		checksum = (checksum << 5) | (checksum >> 27);
		checksum ^= actual;
	}
	*read_cycles = read_cycle() - start;
	gate1_ddr_words_write(DDR_TEST_WORDS);
	gate1_ddr_checksum_write(checksum);
}

/*
 * Walking address bits over the full 1 GiB: every power-of-two offset gets a
 * distinct value, so a stuck or shorted address/bank line aliases two offsets.
 */
static void test_address_lines(uintptr_t base)
{
	volatile uint32_t *const origin = (volatile uint32_t *)base;
	uint32_t bit;

	*origin = 0xa5a5a5a5u;
	for (bit = ADDRESS_BITS_FIRST; bit <= ADDRESS_BITS_LAST; ++bit)
		*(volatile uint32_t *)(base + (1u << bit)) = pattern(0x1000u + bit);
	l1d_flush();

	if (*origin != 0xa5a5a5a5u)
		fail(FAIL_DDR_ADDRESS, base, 0xa5a5a5a5u, *origin);
	for (bit = ADDRESS_BITS_FIRST; bit <= ADDRESS_BITS_LAST; ++bit) {
		volatile uint32_t *word = (volatile uint32_t *)(base + (1u << bit));
		uint32_t expected = pattern(0x1000u + bit);
		uint32_t actual = *word;
		if (actual != expected)
			fail(FAIL_DDR_ADDRESS, (uintptr_t)word, expected, actual);
	}
}

/*
 * Byte and halfword stores into every byte lane of several controller words,
 * checked against a shadow copy.  Each narrow store must leave its neighbours
 * intact, which exercises the controller's byte write masks.
 */
static void test_byte_lanes(uintptr_t base)
{
	static uint8_t shadow[BYTE_TEST_BYTES];
	volatile uint32_t *const words = (volatile uint32_t *)base;
	volatile uint8_t *const bytes = (volatile uint8_t *)base;
	volatile uint16_t *const halves = (volatile uint16_t *)base;
	uint32_t i;

	for (i = 0; i < BYTE_TEST_BYTES / 4u; ++i) {
		uint32_t value = pattern(0x2000u + i);
		words[i] = value;
		shadow[4u * i + 0u] = (uint8_t)value;
		shadow[4u * i + 1u] = (uint8_t)(value >> 8);
		shadow[4u * i + 2u] = (uint8_t)(value >> 16);
		shadow[4u * i + 3u] = (uint8_t)(value >> 24);
	}
	for (i = 0; i < BYTE_TEST_BYTES; i += 3u) {
		uint8_t value = (uint8_t)(0x5bu ^ (i * 29u));
		bytes[i] = value;
		shadow[i] = value;
	}
	for (i = 2u; i < BYTE_TEST_BYTES; i += 10u) {
		uint16_t value = (uint16_t)(0xc3a5u ^ (i * 0x0101u));
		halves[i / 2u] = value;
		shadow[i] = (uint8_t)value;
		shadow[i + 1u] = (uint8_t)(value >> 8);
	}
	l1d_flush();

	for (i = 0; i < BYTE_TEST_BYTES / 4u; ++i) {
		uint32_t expected = shadow[4u * i] |
			((uint32_t)shadow[4u * i + 1u] << 8) |
			((uint32_t)shadow[4u * i + 2u] << 16) |
			((uint32_t)shadow[4u * i + 3u] << 24);
		uint32_t actual = words[i];
		if (actual != expected)
			fail(FAIL_DDR_BYTE, (uintptr_t)&words[i], expected, actual);
	}
}

/* Reads immediately following writes to the same and neighbouring words. */
static void test_interleave(volatile uint32_t *ram)
{
	uint32_t i;

	for (i = 0; i < INTERLEAVE_WORDS; ++i) {
		uint32_t value = pattern(0x40000u + i);
		uint32_t actual;

		ram[i] = value;
		actual = ram[i];
		if (actual != value)
			fail(FAIL_DDR_INTERLEAVE, (uintptr_t)&ram[i], value, actual);
		if (i != 0) {
			uint32_t previous = pattern(0x40000u + i - 1u);
			actual = ram[i - 1u];
			if (actual != previous)
				fail(FAIL_DDR_INTERLEAVE, (uintptr_t)&ram[i - 1u], previous, actual);
		}
	}
	l1d_flush();
	for (i = INTERLEAVE_WORDS; i-- != 0;) {
		uint32_t expected = pattern(0x40000u + i);
		uint32_t actual = ram[i];
		if (actual != expected)
			fail(FAIL_DDR_INTERLEAVE, (uintptr_t)&ram[i], expected, actual);
	}
}

/*
 * Returns the value observed when the code was rewritten without fence.i.
 * That result is architecturally unspecified; it is logged only as evidence
 * of whether the I-cache still held the previous instructions.
 */
static uint32_t test_code_execution(volatile uint32_t *code)
{
	uint32_t first;
	uint32_t stale;
	uint32_t second;

	install_return_constant(code, 42);
	first = ((uint32_t (*)(void))(uintptr_t)code)();
	if (first != 42) {
		gate1_jit_result_write(first << 16);
		fail(FAIL_JIT_FIRST, (uintptr_t)code, 42, first);
	}

	code[0] = ((77u & 0xfffu) << 20) | 0x00000513u;
	__asm__ volatile ("fence rw, rw" ::: "memory");
	stale = ((uint32_t (*)(void))(uintptr_t)code)();

	install_return_constant(code, 99);
	second = ((uint32_t (*)(void))(uintptr_t)code)();
	gate1_jit_result_write((first << 16) | second);
	if (second != 99)
		fail(FAIL_JIT_REPLACE, (uintptr_t)code, 99, second);
	return stale;
}

/*
 * Draw a pattern that is visibly distinct from the gateware color bars. The
 * framebuffer is static after this write, so one whole-cache writeback before
 * enabling DMA is sufficient for coherent scanout.
 */
static void start_framebuffer(void)
{
	volatile uint16_t *const pixels = (volatile uint16_t *)TPX_FRAMEBUFFER_BASE;
	uint32_t y;

	for (y = 0; y < TPX_FRAMEBUFFER_HEIGHT; ++y) {
		uint32_t x;
		for (x = 0; x < TPX_FRAMEBUFFER_WIDTH; ++x) {
			uint32_t red = (x * 31u) / 639u;
			uint32_t green = (y * 63u) / 479u;
			uint32_t blue = ((x >> 4) ^ (y >> 4)) & 31u;
			uint16_t value;

			if (((x >> 5) ^ (y >> 5)) & 1u) {
				red ^= 31u;
				blue ^= 31u;
			}
			value = (uint16_t)((red << 11) | (green << 5) | blue);
			if (x < 8u || x >= 632u || y < 8u || y >= 472u)
				value = 0xffffu;
			else if ((x >= 316u && x < 324u) || (y >= 236u && y < 244u))
				value = 0x0000u;
			pixels[y * TPX_FRAMEBUFFER_WIDTH + x] = value;
		}
	}
	l1d_flush();
	video_framebuffer_dma_enable_write(1);
}

/* ---- Program loader ------------------------------------------------------ */

#define CPU_HZ            750000000u
#define STREAM_TAG_DATA   0u
#define STREAM_TAG_START  1u
#define STREAM_TAG_END    2u
#define STREAM_TAG_CANCEL 3u

static const uint32_t crc_nibble[16] = {
	0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
	0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
	0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
	0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

static uint32_t crc32_byte(uint32_t crc, uint8_t byte)
{
	crc ^= byte;
	crc = (crc >> 4) ^ crc_nibble[crc & 15u];
	return (crc >> 4) ^ crc_nibble[crc & 15u];
}

static void api_putc(char c)
{
	gate1_putc(c, stdout);
}

static void api_set_reg(uint32_t reg, uint32_t value)
{
	switch (reg) {
	case TPX_REG_STAGE:         gate1_stage_write(value); break;
	case TPX_REG_FAILURE:       gate1_failure_write(value); break;
	case TPX_REG_WORDS:         gate1_ddr_words_write(value); break;
	case TPX_REG_CHECKSUM:      gate1_ddr_checksum_write(value); break;
	case TPX_REG_JIT:           gate1_jit_result_write(value); break;
	case TPX_REG_CYCLES:        gate1_cycles_write(value); break;
	case TPX_REG_FEATURES:      gate1_features_write(value); break;
	case TPX_REG_FAIL_ADDRESS:  gate1_fail_address_write(value); break;
	case TPX_REG_FAIL_EXPECTED: gate1_fail_expected_write(value); break;
	case TPX_REG_FAIL_OBSERVED: gate1_fail_observed_write(value); break;
	default: break;
	}
}

static int32_t api_stream_read(uint32_t *data)
{
	uint32_t status = loader_status_read();

	if (!(status & (1u << CSR_LOADER_STATUS_VALID_OFFSET)))
		return -1;
	*data = loader_data_read();
	loader_pop_write(1);
	return (int32_t)((status >> CSR_LOADER_STATUS_TAG_OFFSET) &
		((1u << CSR_LOADER_STATUS_TAG_SIZE) - 1u));
}

/* Offset and length first: Tang-Control acts when the sequence changes. */
static void api_disc_request(uint32_t offset, uint32_t length)
{
	gate1_disc_offset_write(offset);
	gate1_disc_length_write(length);
	gate1_disc_sequence_write(gate1_disc_sequence_read() + 1u);
}

static uint32_t api_disc_sectors(void)
{
	return gate1_disc_sectors_read();
}

static const struct tpx_api loader_api = {
	.version      = TPX_API_VERSION,
	.cpu_hz       = CPU_HZ,
	.putc         = api_putc,
	.set_reg      = api_set_reg,
	.flush_dcache = l1d_flush,
	.stream_read  = api_stream_read,
	.disc_request = api_disc_request,
	.disc_sectors = api_disc_sectors,
};

/* Blocks until the stream FIFO has an entry, then returns and discards it. */
static uint32_t stream_pop(uint32_t *data)
{
	uint32_t status;

	do
		status = loader_status_read();
	while (!(status & (1u << CSR_LOADER_STATUS_VALID_OFFSET)));
	*data = loader_data_read();
	loader_pop_write(1);
	return (status >> CSR_LOADER_STATUS_TAG_OFFSET) &
		((1u << CSR_LOADER_STATUS_TAG_SIZE) - 1u);
}

static int stream_overflowed(void)
{
	return (loader_status_read() >> CSR_LOADER_STATUS_OVERFLOW_OFFSET) & 1u;
}

/*
 * Receives one image after its START. Returns TPX_LOADER_RUN with the header
 * filled in when the payload is in place and verified, TPX_LOADER_RECEIVE when
 * a new START arrived mid-image (receive again), or an error state.
 */
static uint32_t receive_image(struct tpx_image_header *header)
{
	uint32_t *const words = (uint32_t *)header;
	uintptr_t const ram_end = (uintptr_t)MAIN_RAM_BASE + (uintptr_t)MAIN_RAM_SIZE;
	volatile uint32_t *destination;
	uint32_t crc = 0xffffffffu;
	uint32_t count;
	uint32_t data;
	uint32_t tag;
	uint32_t i;

	for (i = 0; i < TPX_IMAGE_HEADER / 4u; ++i) {
		tag = stream_pop(&data);
		if (tag == STREAM_TAG_START)
			return TPX_LOADER_RECEIVE;
		if (tag == STREAM_TAG_CANCEL)
			return TPX_LOADER_ERR_CANCELLED;
		if (tag != STREAM_TAG_DATA)
			return TPX_LOADER_ERR_TRUNCATED;
		words[i] = data;
	}
	if (header->magic != TPX_IMAGE_MAGIC || header->header_size != TPX_IMAGE_HEADER ||
	    header->flags != 0 || header->payload_size == 0)
		return TPX_LOADER_ERR_HEADER;
	if (header->load_address < MAIN_RAM_BASE || (header->load_address & 3u) ||
	    header->payload_size > ram_end - header->load_address ||
	    header->entry < header->load_address ||
	    header->entry - header->load_address >= header->payload_size ||
	    (header->entry & 1u))
		return TPX_LOADER_ERR_RANGE;

	destination = (volatile uint32_t *)header->load_address;
	for (i = 0; i < (header->payload_size + 3u) / 4u; ++i) {
		uint32_t remaining = header->payload_size - 4u * i;
		uint32_t byte;

		tag = stream_pop(&data);
		if (tag == STREAM_TAG_START)
			return TPX_LOADER_RECEIVE;
		if (tag == STREAM_TAG_CANCEL)
			return TPX_LOADER_ERR_CANCELLED;
		if (tag != STREAM_TAG_DATA)
			return TPX_LOADER_ERR_TRUNCATED;
		destination[i] = data;
		for (byte = 0; byte < 4u && byte < remaining; ++byte)
			crc = crc32_byte(crc, (uint8_t)(data >> (8u * byte)));
	}

	tag = stream_pop(&count);
	if (tag == STREAM_TAG_START)
		return TPX_LOADER_RECEIVE;
	if (tag != STREAM_TAG_END || count != TPX_IMAGE_HEADER + header->payload_size)
		return TPX_LOADER_ERR_LENGTH;

	crc = ~crc;
	gate1_loader_bytes_write(header->payload_size);
	gate1_loader_crc_write(crc);
	if (crc != header->payload_crc32)
		return TPX_LOADER_ERR_CRC;
	if (stream_overflowed())
		return TPX_LOADER_ERR_OVERFLOW;
	return TPX_LOADER_RUN;
}

static __attribute__((noreturn)) void run_loader(void)
{
	struct tpx_image_header header;
	uint32_t runs = 0;
	uint32_t data;
	uint32_t state;

	gate1_loader_state_write(TPX_LOADER_WAIT);
	for (;;) {
		/* Discard anything up to the next image. */
		while (stream_pop(&data) != STREAM_TAG_START)
			;
		do {
			gate1_loader_state_write((runs << 16) | TPX_LOADER_RECEIVE);
			state = receive_image(&header);
		} while (state == TPX_LOADER_RECEIVE);

		if (state != TPX_LOADER_RUN) {
			gate1_loader_state_write((runs << 16) | state);
			continue;
		}

		l1d_flush();
		__asm__ volatile ("fence.i" ::: "memory");
		gate1_loader_state_write((runs << 16) | TPX_LOADER_RUN);
		data = ((tpx_entry)(uintptr_t)header.entry)(&loader_api);
		++runs;
		gate1_loader_result_write(data);
		gate1_loader_state_write((runs << 16) | TPX_LOADER_RETURNED);
	}
}

int main(void)
{
	uintptr_t const base = MAIN_RAM_BASE;
	uint32_t start_cycles = read_cycle();
	uint32_t uncached_write, uncached_read, cached_write, cached_read;
	uint32_t stale;

	gate1_loader_state_write(TPX_LOADER_BOOT);
	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);

	wait_for_calibration();
	feature_passed(FEATURE_DDR_INIT, 2);

	/* Baseline with both caches off (their reset state). */
	test_fixed_pattern((volatile uint32_t *)base, &uncached_write, &uncached_read);
	feature_passed(FEATURE_DDR_RW, 3);

	enable_caches();
	feature_passed(FEATURE_CACHES, 4);

	test_fixed_pattern((volatile uint32_t *)(base + CACHED_TEST_OFFSET),
		&cached_write, &cached_read);
	gate1_stage_write(5);

	test_address_lines(base);
	feature_passed(FEATURE_DDR_ADDRESS, 6);

	test_byte_lanes(base + BYTE_TEST_OFFSET);
	feature_passed(FEATURE_DDR_BYTE, 7);

	test_interleave((volatile uint32_t *)(base + INTERLEAVE_OFFSET));
	feature_passed(FEATURE_DDR_INTERLEAVE, 8);

	stale = test_code_execution((volatile uint32_t *)(base + CODE_OFFSET));
	feature_passed(FEATURE_JIT_EXEC, 9);

	start_framebuffer();
	feature_passed(FEATURE_FRAMEBUFFER, 10);

	if (ddr3_status_read() & (1u << CSR_DDR3_STATUS_OVERFLOW_OFFSET))
		fail(FAIL_DDR_OVERFLOW, 0, 0, ddr3_status_read());

	gate1_cycles_write(read_cycle() - start_cycles);
	printf("u %lu %lu c %lu %lu\n", (unsigned long)uncached_write,
		(unsigned long)uncached_read, (unsigned long)cached_write,
		(unsigned long)cached_read);
	printf("stale %lu\n", (unsigned long)stale);
	printf("gate1 pass\n");
	gate1_stage_write(0x80000001u);

	run_loader();
}
