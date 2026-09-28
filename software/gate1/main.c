// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include <stdio.h>

#include <generated/csr.h>
#include <generated/mem.h>

/* Test layout inside main RAM (1 GiB at MAIN_RAM_BASE). */
#define DDR_TEST_WORDS        (1u << 18)        /* 1 MiB fixed-pattern region */
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

#define FAIL_DDR_INIT          0x00010001u
#define FAIL_DDR_DATA          0x00010002u
#define FAIL_DDR_ADDRESS       0x00010003u
#define FAIL_DDR_BYTE          0x00010004u
#define FAIL_DDR_INTERLEAVE    0x00010005u
#define FAIL_DDR_OVERFLOW      0x00010006u
#define FAIL_JIT_FIRST         0x00020001u
#define FAIL_JIT_REPLACE       0x00020002u

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
	printf("ddr3 calibrated\n");
}

/* Sequential fixed-pattern write then verify. */
static void test_fixed_pattern(volatile uint32_t *ram)
{
	uint32_t checksum = 0;
	uint32_t i;

	for (i = 0; i < DDR_TEST_WORDS; ++i)
		ram[i] = pattern(i);
	__asm__ volatile ("fence rw, rw" ::: "memory");

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
	__asm__ volatile ("fence rw, rw" ::: "memory");

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
	__asm__ volatile ("fence rw, rw" ::: "memory");

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
	for (i = INTERLEAVE_WORDS; i-- != 0;) {
		uint32_t expected = pattern(0x40000u + i);
		uint32_t actual = ram[i];
		if (actual != expected)
			fail(FAIL_DDR_INTERLEAVE, (uintptr_t)&ram[i], expected, actual);
	}
}

static void test_code_execution(volatile uint32_t *code)
{
	uint32_t first;
	uint32_t second;

	install_return_constant(code, 42);
	first = ((uint32_t (*)(void))(uintptr_t)code)();
	if (first != 42) {
		gate1_jit_result_write(first << 16);
		fail(FAIL_JIT_FIRST, (uintptr_t)code, 42, first);
	}

	install_return_constant(code, 99);
	second = ((uint32_t (*)(void))(uintptr_t)code)();
	gate1_jit_result_write((first << 16) | second);
	if (second != 99)
		fail(FAIL_JIT_REPLACE, (uintptr_t)code, 99, second);
}

int main(void)
{
	uintptr_t const base = MAIN_RAM_BASE;
	uint32_t start_cycles = read_cycle();

	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);

	wait_for_calibration();
	feature_passed(FEATURE_DDR_INIT, 2);

	test_fixed_pattern((volatile uint32_t *)base);
	feature_passed(FEATURE_DDR_RW, 3);

	test_address_lines(base);
	feature_passed(FEATURE_DDR_ADDRESS, 4);

	test_byte_lanes(base + BYTE_TEST_OFFSET);
	feature_passed(FEATURE_DDR_BYTE, 5);

	test_interleave((volatile uint32_t *)(base + INTERLEAVE_OFFSET));
	feature_passed(FEATURE_DDR_INTERLEAVE, 6);

	test_code_execution((volatile uint32_t *)(base + CODE_OFFSET));
	feature_passed(FEATURE_JIT_EXEC, 7);

	if (ddr3_status_read() & (1u << CSR_DDR3_STATUS_OVERFLOW_OFFSET))
		fail(FAIL_DDR_OVERFLOW, 0, 0, ddr3_status_read());

	gate1_cycles_write(read_cycle() - start_cycles);
	printf("gate1 pass\n");
	gate1_stage_write(0x80000001u);

	for (;;)
		__asm__ volatile ("wfi");
}
