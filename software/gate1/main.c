// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include <stdio.h>

#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/sdram_phy.h>

#include <liblitedram/accessors.h>
#include <liblitedram/sdram.h>

#define DDR_TEST_WORDS 16384u
#define FEATURE_DDR_INIT  (1u << 0)
#define FEATURE_DDR_RW    (1u << 1)
#define FEATURE_JIT_EXEC  (1u << 2)

#define FAIL_DDR_INIT     0x00010001u
#define FAIL_DDR_DATA     0x00010002u
#define FAIL_JIT_FIRST    0x00020001u
#define FAIL_JIT_REPLACE  0x00020002u
#define FAIL_PHY_NO_BURST 0x00030000u
#define FAIL_PHY_NO_MATCH 0x00040000u
#define FAIL_PHY_STATIC_NO_MATCH 0x00050000u
#define FAIL_PHY_TEMPORAL_MAP 0x00060000u
#define FAIL_PHY_WRITE_DELAY_SCAN 0x00070000u

#define FEATURE_JEDEC_INIT    (1u << 0)
#define FEATURE_LANE0_BURST   (1u << 1)
#define FEATURE_LANE1_BURST   (1u << 2)
#define FEATURE_LANE0_MATCH   (1u << 3)
#define FEATURE_LANE1_MATCH   (1u << 4)

#define PHY_STATIC_PATTERN_A 0xa55aa55au
#define PHY_STATIC_PATTERN_B 0x5aa55aa5u

static const uint32_t phy_test_pattern[SDRAM_PHY_PHASES] = {
	0x3cc3a55au,
	0xc33c5aa5u,
	0x96690ff0u,
	0x6996f00fu,
};

#define LOG_WORDS 32u

static uint32_t log_head;
static uint32_t log_word;

static inline void diagnostic_result_write(uint32_t index, uint32_t value)
{
	csr_write_simple(value, CSR_GATE1_LOG0_ADDR + 4u * index);
}

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

static inline void publish_failure(uint32_t code)
{
	gate1_failure_write(code);
	gate1_stage_write(0x80000000u | code);
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

struct phy_scan_result {
	uint32_t burst_mask[SDRAM_PHY_MODULES];
	uint32_t burst_count[SDRAM_PHY_MODULES];
	uint32_t match_mask[SDRAM_PHY_MODULES];
	uint32_t match_count[SDRAM_PHY_MODULES];
	uint32_t first[SDRAM_PHY_MODULES];
	uint32_t best_error[SDRAM_PHY_MODULES];
	uint32_t best[SDRAM_PHY_MODULES];
	uint32_t best_raw[SDRAM_PHY_MODULES][SDRAM_PHY_PHASES];
};

static uint32_t popcount32(uint32_t value)
{
	value -= (value >> 1) & 0x55555555u;
	value = (value & 0x33333333u) + ((value >> 2) & 0x33333333u);
	value = (value + (value >> 4)) & 0x0f0f0f0fu;
	value += value >> 8;
	value += value >> 16;
	return value & 0x3fu;
}

static void activate_test_row(void)
{
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_RAS | DFII_COMMAND_CS);
	cdelay(15);
}

static void precharge_test_row(void)
{
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_RAS | DFII_COMMAND_WE | DFII_COMMAND_CS);
	cdelay(15);
}

static void issue_test_read(void)
{
#if SDRAM_PHY_RDPHASE == 0
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_CAS | DFII_COMMAND_CS | DFII_COMMAND_RDDATA);
#elif SDRAM_PHY_RDPHASE == 1
	sdram_dfii_pi1_address_write(0);
	sdram_dfii_pi1_baddress_write(0);
	command_p1(DFII_COMMAND_CAS | DFII_COMMAND_CS | DFII_COMMAND_RDDATA);
#elif SDRAM_PHY_RDPHASE == 2
	sdram_dfii_pi2_address_write(0);
	sdram_dfii_pi2_baddress_write(0);
	command_p2(DFII_COMMAND_CAS | DFII_COMMAND_CS | DFII_COMMAND_RDDATA);
#elif SDRAM_PHY_RDPHASE == 3
	sdram_dfii_pi3_address_write(0);
	sdram_dfii_pi3_baddress_write(0);
	command_p3(DFII_COMMAND_CAS | DFII_COMMAND_CS | DFII_COMMAND_RDDATA);
#else
#error Unsupported SDRAM read phase
#endif
	cdelay(15);
}

static void issue_test_write_pattern(const uint32_t pattern[SDRAM_PHY_PHASES])
{
	sdram_dfii_pi0_wrdata_write(pattern[0]);
	sdram_dfii_pi1_wrdata_write(pattern[1]);
	sdram_dfii_pi2_wrdata_write(pattern[2]);
	sdram_dfii_pi3_wrdata_write(pattern[3]);

#if SDRAM_PHY_WRPHASE == 0
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_CAS | DFII_COMMAND_WE | DFII_COMMAND_CS |
		DFII_COMMAND_WRDATA);
#elif SDRAM_PHY_WRPHASE == 1
	sdram_dfii_pi1_address_write(0);
	sdram_dfii_pi1_baddress_write(0);
	command_p1(DFII_COMMAND_CAS | DFII_COMMAND_WE | DFII_COMMAND_CS |
		DFII_COMMAND_WRDATA);
#elif SDRAM_PHY_WRPHASE == 2
	sdram_dfii_pi2_address_write(0);
	sdram_dfii_pi2_baddress_write(0);
	command_p2(DFII_COMMAND_CAS | DFII_COMMAND_WE | DFII_COMMAND_CS |
		DFII_COMMAND_WRDATA);
#elif SDRAM_PHY_WRPHASE == 3
	sdram_dfii_pi3_address_write(0);
	sdram_dfii_pi3_baddress_write(0);
	command_p3(DFII_COMMAND_CAS | DFII_COMMAND_WE | DFII_COMMAND_CS |
		DFII_COMMAND_WRDATA);
#else
#error Unsupported SDRAM write phase
#endif
	cdelay(15);
}

static void capture_raw_read(uint32_t raw[SDRAM_PHY_PHASES])
{
	raw[0] = sdram_dfii_pi0_rddata_read();
	raw[1] = sdram_dfii_pi1_rddata_read();
	raw[2] = sdram_dfii_pi2_rddata_read();
	raw[3] = sdram_dfii_pi3_rddata_read();
}

static uint32_t lane_errors(uint32_t module,
	const uint32_t raw[SDRAM_PHY_PHASES],
	const uint32_t expected[SDRAM_PHY_PHASES])
{
	uint32_t lane_mask = module == 0 ? 0x00ff00ffu : 0xff00ff00u;
	uint32_t errors = 0;
	uint32_t phase;

	for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
		errors += popcount32((raw[phase] ^ expected[phase]) & lane_mask);
	return errors;
}

static void scan_read_data(struct phy_scan_result *result,
	const uint32_t expected[SDRAM_PHY_PHASES])
{
	uint32_t module;

	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		uint32_t bitslip;

		result->first[module] = 0xffffffffu;
		result->best[module] = 0xffffffffu;
		result->best_error[module] = 0xffffffffu;
		sdram_leveling_action(module, 0, read_rst_dq_bitslip);
		for (bitslip = 0; bitslip < SDRAM_PHY_BITSLIPS; ++bitslip) {
			uint32_t delay;

			sdram_leveling_action(module, 0, read_rst_dq_delay);
			for (delay = 0; delay < SDRAM_PHY_DELAYS; ++delay) {
				uint32_t raw[SDRAM_PHY_PHASES];
				uint32_t coordinate = (bitslip << 16) | delay;
				uint32_t seen;

				ddrphy_burstdet_clr_write(1);
				issue_test_read();
				seen = ddrphy_burstdet_seen_read();
				if (seen & (1u << module)) {
					uint32_t errors;
					uint32_t phase;

					result->burst_mask[module] |= 1u << bitslip;
					++result->burst_count[module];
					capture_raw_read(raw);
					errors = lane_errors(module, raw, expected);
					if (errors < result->best_error[module]) {
						result->best_error[module] = errors;
						result->best[module] = coordinate;
						for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
							result->best_raw[module][phase] = raw[phase];
					}
					if (errors == 0) {
						result->match_mask[module] |= 1u << bitslip;
						++result->match_count[module];
						if (result->first[module] == 0xffffffffu)
							result->first[module] = coordinate;
					}
				}
				if (delay != SDRAM_PHY_DELAYS - 1)
					sdram_leveling_action(module, 0, read_inc_dq_delay);
			}
			if (bitslip != SDRAM_PHY_BITSLIPS - 1)
				sdram_leveling_action(module, 0, read_inc_dq_bitslip);
		}
	}
}

__attribute__((unused, noreturn)) static void run_phy_write_read_diagnostic(void)
{
	struct phy_scan_result result = {0};
	uint32_t features = 0;
	uint32_t missing = 0;
	uint32_t unmatched = 0;
	uint32_t start_cycles = read_cycle();

	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);
	printf("PHY direct write/read scan wp=%u\n", SDRAM_PHY_WRPHASE);

	sdram_software_control_on();
	init_sequence();
	features |= FEATURE_JEDEC_INIT;
	gate1_features_write(features);
	gate1_stage_write(2);

	activate_test_row();
	issue_test_write_pattern(phy_test_pattern);
	scan_read_data(&result, phy_test_pattern);
	precharge_test_row();

	if (result.burst_mask[0])
		features |= FEATURE_LANE0_BURST;
	else
		missing |= 1u;
	if (result.burst_mask[1])
		features |= FEATURE_LANE1_BURST;
	else
		missing |= 2u;
	if (result.match_mask[0])
		features |= FEATURE_LANE0_MATCH;
	else
		unmatched |= 1u;
	if (result.match_mask[1])
		features |= FEATURE_LANE1_MATCH;
	else
		unmatched |= 2u;

	gate1_phy_burst_masks_write(result.burst_mask[0] | (result.burst_mask[1] << 8));
	gate1_phy_burst_counts_write(result.burst_count[0] | (result.burst_count[1] << 16));
	gate1_phy_first0_write(result.first[0]);
	gate1_phy_first1_write(result.first[1]);
	gate1_phy_raw0_write(result.best_raw[0][0]);
	gate1_phy_raw1_write(result.best_raw[0][1]);
	gate1_phy_raw2_write(result.best_raw[0][2]);
	gate1_phy_raw3_write(result.best_raw[0][3]);
	gate1_features_write(features);
	gate1_cycles_write(read_cycle() - start_cycles);

	printf("m0 burst=%02lx/%lu match=%02lx/%lu best=%lu@%08lx\n",
		(unsigned long)result.burst_mask[0], (unsigned long)result.burst_count[0],
		(unsigned long)result.match_mask[0], (unsigned long)result.match_count[0],
		(unsigned long)result.best_error[0], (unsigned long)result.best[0]);
	printf("m1 burst=%02lx/%lu match=%02lx/%lu best=%lu@%08lx\n",
		(unsigned long)result.burst_mask[1], (unsigned long)result.burst_count[1],
		(unsigned long)result.match_mask[1], (unsigned long)result.match_count[1],
		(unsigned long)result.best_error[1], (unsigned long)result.best[1]);

	/* Publish after printf(), which uses the same physical log-ring CSRs. */
	diagnostic_result_write(0, result.match_mask[0] | (result.match_mask[1] << 8));
	diagnostic_result_write(1, result.match_count[0] | (result.match_count[1] << 16));
	diagnostic_result_write(2, result.best_error[0] | (result.best_error[1] << 8));
	diagnostic_result_write(3, result.best[0]);
	diagnostic_result_write(4, result.best[1]);
	diagnostic_result_write(5, (uint32_t)SDRAM_PHY_WRPHASE | (1u << 8) |
		((uint32_t)SDRAM_PHY_PHASES << 16));
	diagnostic_result_write(6, result.best_raw[1][0]);
	diagnostic_result_write(7, result.best_raw[1][1]);
	diagnostic_result_write(8, result.best_raw[1][2]);
	diagnostic_result_write(9, result.best_raw[1][3]);

	if (missing)
		gate1_failure_write(FAIL_PHY_NO_BURST | missing);
	else if (unmatched)
		gate1_failure_write(FAIL_PHY_NO_MATCH | unmatched);
	gate1_stage_write(0x800000d2u);
	for (;;)
		__asm__ volatile ("wfi");
}

__attribute__((unused, noreturn)) static void run_phy_static_pattern_diagnostic(void)
{
	static const uint32_t patterns[2][SDRAM_PHY_PHASES] = {
		{PHY_STATIC_PATTERN_A, PHY_STATIC_PATTERN_A,
		 PHY_STATIC_PATTERN_A, PHY_STATIC_PATTERN_A},
		{PHY_STATIC_PATTERN_B, PHY_STATIC_PATTERN_B,
		 PHY_STATIC_PATTERN_B, PHY_STATIC_PATTERN_B},
	};
	struct phy_scan_result result[2] = {0};
	uint32_t features = 0;
	uint32_t missing = 0;
	uint32_t response[SDRAM_PHY_MODULES] = {0};
	uint32_t start_cycles = read_cycle();
	uint32_t pattern_index;
	uint32_t module;

	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);
	printf("PHY static-pattern scan\n");

	sdram_software_control_on();
	init_sequence();
	features |= FEATURE_JEDEC_INIT;
	gate1_features_write(features);
	gate1_stage_write(2);

	activate_test_row();
	for (pattern_index = 0; pattern_index < 2; ++pattern_index) {
		issue_test_write_pattern(patterns[pattern_index]);
		scan_read_data(&result[pattern_index], patterns[pattern_index]);
	}
	precharge_test_row();

	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		uint32_t phase;
		uint32_t lane_mask = module == 0 ? 0x00ff00ffu : 0xff00ff00u;

		if (result[0].match_mask[module])
			features |= FEATURE_LANE0_BURST << module;
		else
			missing |= 1u << module;
		if (result[1].match_mask[module])
			features |= FEATURE_LANE0_MATCH << module;
		else
			missing |= 1u << (module + 4u);
		for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
			response[module] += popcount32(
				(result[0].best_raw[module][phase] ^
				 result[1].best_raw[module][phase]) & lane_mask);
	}

	gate1_phy_burst_masks_write(
		result[0].burst_mask[0] | (result[0].burst_mask[1] << 8));
	gate1_phy_burst_counts_write(
		result[1].burst_mask[0] | (result[1].burst_mask[1] << 8));
	gate1_phy_first0_write(PHY_STATIC_PATTERN_A);
	gate1_phy_first1_write(PHY_STATIC_PATTERN_B);
	gate1_phy_raw0_write(result[0].best_raw[0][0]);
	gate1_phy_raw1_write(result[0].best_raw[0][1]);
	gate1_phy_raw2_write(result[0].best_raw[0][2]);
	gate1_phy_raw3_write(response[0] | (response[1] << 8));
	gate1_features_write(features);
	gate1_cycles_write(read_cycle() - start_cycles);

	printf("A: m0=%lu@%08lx m1=%lu@%08lx\n",
		(unsigned long)result[0].best_error[0], (unsigned long)result[0].best[0],
		(unsigned long)result[0].best_error[1], (unsigned long)result[0].best[1]);
	printf("B: m0=%lu@%08lx m1=%lu@%08lx\n",
		(unsigned long)result[1].best_error[0], (unsigned long)result[1].best[0],
		(unsigned long)result[1].best_error[1], (unsigned long)result[1].best[1]);

	diagnostic_result_write(0,
		result[0].match_mask[0] | (result[0].match_mask[1] << 8));
	diagnostic_result_write(1,
		result[0].match_count[0] | (result[0].match_count[1] << 16));
	diagnostic_result_write(2,
		result[0].best_error[0] | (result[0].best_error[1] << 8));
	diagnostic_result_write(3, result[0].best[0]);
	diagnostic_result_write(4, result[0].best[1]);
	diagnostic_result_write(5,
		result[1].match_mask[0] | (result[1].match_mask[1] << 8));
	diagnostic_result_write(6,
		result[1].match_count[0] | (result[1].match_count[1] << 16));
	diagnostic_result_write(7,
		result[1].best_error[0] | (result[1].best_error[1] << 8));
	diagnostic_result_write(8, result[1].best[0]);
	diagnostic_result_write(9, result[1].best[1]);

	if (missing)
		gate1_failure_write(FAIL_PHY_STATIC_NO_MATCH | missing);
	gate1_stage_write(0x800000d3u);
	for (;;)
		__asm__ volatile ("wfi");
}

static uint32_t temporal_lane_byte(
	const uint32_t raw[SDRAM_PHY_PHASES], uint32_t module, uint32_t beat)
{
	uint32_t shift = ((beat & 1u) ? 16u : 0u) + 8u * module;

	return (raw[beat >> 1] >> shift) & 0xffu;
}

__attribute__((unused, noreturn)) static void run_phy_temporal_map_diagnostic(void)
{
	uint32_t raw_map[8][SDRAM_PHY_PHASES] = {{0}};
	uint32_t mapping[SDRAM_PHY_MODULES][2] = {{0}};
	uint32_t burst_slots[SDRAM_PHY_MODULES] = {0};
	uint32_t exact_slots[SDRAM_PHY_MODULES] = {0};
	uint32_t total_errors[SDRAM_PHY_MODULES] = {0};
	uint32_t features = 0;
	uint32_t bad = 0;
	uint32_t start_cycles = read_cycle();
	uint32_t module;
	uint32_t write_slot;

	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);

	sdram_software_control_on();
	init_sequence();
	features |= FEATURE_JEDEC_INIT;
	gate1_features_write(features);
	gate1_stage_write(2);

	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		sdram_leveling_action(module, 0, read_rst_dq_bitslip);
		sdram_leveling_action(module, 0, read_rst_dq_delay);
	}

	activate_test_row();
	for (write_slot = 0; write_slot < 8; ++write_slot) {
		uint32_t expected[SDRAM_PHY_PHASES] = {0};
		uint32_t seen;
		uint32_t phase;

		expected[write_slot >> 1] =
			(write_slot & 1u) ? 0xffff0000u : 0x0000ffffu;
		issue_test_write_pattern(expected);
		ddrphy_burstdet_clr_write(1);
		issue_test_read();
		seen = ddrphy_burstdet_seen_read();
		capture_raw_read(raw_map[write_slot]);

		for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
			uint32_t observed = 0;
			uint32_t read_slot;
			uint32_t errors = lane_errors(module,
				raw_map[write_slot], expected);

			if (seen & (1u << module))
				burst_slots[module] |= 1u << write_slot;
			if (errors == 0)
				exact_slots[module] |= 1u << write_slot;
			total_errors[module] += errors;
			for (read_slot = 0; read_slot < 8; ++read_slot) {
				if (temporal_lane_byte(raw_map[write_slot],
					module, read_slot) == 0xffu)
					observed |= 1u << read_slot;
			}
			mapping[module][write_slot >> 2] |=
				observed << (8u * (write_slot & 3u));
		}

		for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
			diagnostic_result_write(4u * write_slot + phase,
				raw_map[write_slot][phase]);
	}
	precharge_test_row();

	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		if (burst_slots[module] == 0xffu)
			features |= FEATURE_LANE0_BURST << module;
		if (exact_slots[module] == 0xffu)
			features |= FEATURE_LANE0_MATCH << module;
		else
			bad |= 1u << module;
	}

	gate1_phy_burst_masks_write(
		burst_slots[0] | (burst_slots[1] << 8));
	gate1_phy_burst_counts_write(
		exact_slots[0] | (exact_slots[1] << 8));
	gate1_phy_first0_write(mapping[0][0]);
	gate1_phy_first1_write(mapping[0][1]);
	gate1_phy_raw0_write(mapping[1][0]);
	gate1_phy_raw1_write(mapping[1][1]);
	gate1_phy_raw2_write(total_errors[0] | (total_errors[1] << 16));
	gate1_phy_raw3_write(0x0008ffffu);
	gate1_features_write(features);
	gate1_cycles_write(read_cycle() - start_cycles);

	if (bad)
		gate1_failure_write(FAIL_PHY_TEMPORAL_MAP | bad);
	gate1_stage_write(0x800000d4u);
	for (;;)
		__asm__ volatile ("wfi");
}

struct write_delay_scan_result {
	uint32_t burst_count[SDRAM_PHY_MODULES];
	uint32_t exact_count[SDRAM_PHY_MODULES];
	uint32_t first_exact[SDRAM_PHY_MODULES];
	uint32_t best_error[SDRAM_PHY_MODULES];
	uint32_t best_delay[SDRAM_PHY_MODULES];
	uint32_t best_raw[SDRAM_PHY_MODULES][2][SDRAM_PHY_PHASES];
};

static void make_phy_pattern(uint32_t output[SDRAM_PHY_PHASES],
	uint32_t invert)
{
	uint32_t phase;

	for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
		output[phase] = phy_test_pattern[phase] ^ invert;
}

__attribute__((unused, noreturn)) static void run_phy_write_delay_scan_diagnostic(void)
{
	struct write_delay_scan_result result = {0};
	uint32_t expected[2][SDRAM_PHY_PHASES];
	uint32_t features = 0;
	uint32_t bad = 0;
	uint32_t start_cycles = read_cycle();
	uint32_t module;

	make_phy_pattern(expected[0], 0);
	make_phy_pattern(expected[1], 0xffffffffu);
	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);

	sdram_software_control_on();
	init_sequence();
	features |= FEATURE_JEDEC_INIT;
	gate1_features_write(features);
	gate1_stage_write(2);

	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		result.first_exact[module] = 0xffffffffu;
		result.best_delay[module] = 0xffffffffu;
		result.best_error[module] = 0xffffffffu;
		sdram_leveling_action(module, 0, read_rst_dq_bitslip);
		sdram_leveling_action(module, 0, read_rst_dq_delay);
	}

	activate_test_row();
	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		uint32_t delay;

		sdram_leveling_action(module, 0, write_rst_dq_delay);
		for (delay = 0; delay < SDRAM_PHY_DELAYS; ++delay) {
			uint32_t raw[2][SDRAM_PHY_PHASES];
			uint32_t seen[2];
			uint32_t combined_error = 0;
			uint32_t pattern_index;

			/* Requiring opposite consecutive writes prevents stale DDR data
			 * from turning a failed write into a false passing tap. */
			for (pattern_index = 0; pattern_index < 2; ++pattern_index) {
				issue_test_write_pattern(expected[pattern_index]);
				ddrphy_burstdet_clr_write(1);
				issue_test_read();
				seen[pattern_index] = ddrphy_burstdet_seen_read();
				capture_raw_read(raw[pattern_index]);
				combined_error += lane_errors(module,
					raw[pattern_index], expected[pattern_index]);
			}

			if ((seen[0] & seen[1]) & (1u << module))
				++result.burst_count[module];
			if (combined_error < result.best_error[module]) {
				uint32_t phase;

				result.best_error[module] = combined_error;
				result.best_delay[module] = delay;
				for (pattern_index = 0; pattern_index < 2; ++pattern_index)
					for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
						result.best_raw[module][pattern_index][phase] =
							raw[pattern_index][phase];
			}
			if (combined_error == 0 &&
			    ((seen[0] & seen[1]) & (1u << module))) {
				++result.exact_count[module];
				if (result.first_exact[module] == 0xffffffffu)
					result.first_exact[module] = delay;
			}
			if (delay != SDRAM_PHY_DELAYS - 1)
				sdram_leveling_action(module, 0, write_inc_dq_delay);
		}
	}
	precharge_test_row();

	for (module = 0; module < SDRAM_PHY_MODULES; ++module) {
		uint32_t pattern_index;
		uint32_t phase;

		if (result.burst_count[module])
			features |= FEATURE_LANE0_BURST << module;
		if (result.exact_count[module])
			features |= FEATURE_LANE0_MATCH << module;
		else
			bad |= 1u << module;
		for (pattern_index = 0; pattern_index < 2; ++pattern_index)
			for (phase = 0; phase < SDRAM_PHY_PHASES; ++phase)
				diagnostic_result_write(
					(module * 2u + pattern_index) * SDRAM_PHY_PHASES + phase,
					result.best_raw[module][pattern_index][phase]);
	}

	gate1_phy_burst_masks_write(result.burst_count[0] |
		(result.burst_count[1] << 16));
	gate1_phy_burst_counts_write(result.exact_count[0] |
		(result.exact_count[1] << 16));
	gate1_phy_first0_write(result.first_exact[0]);
	gate1_phy_first1_write(result.first_exact[1]);
	gate1_phy_raw0_write(result.best_error[0] |
		(result.best_error[1] << 16));
	gate1_phy_raw1_write(result.best_delay[0] |
		(result.best_delay[1] << 16));
	gate1_phy_raw2_write((uint32_t)SDRAM_PHY_DELAYS);
	gate1_phy_raw3_write(0x0002ffffu);
	gate1_features_write(features);
	gate1_cycles_write(read_cycle() - start_cycles);

	if (bad)
		gate1_failure_write(FAIL_PHY_WRITE_DELAY_SCAN | bad);
	gate1_stage_write(0x800000d5u);
	for (;;)
		__asm__ volatile ("wfi");
}

__attribute__((unused, noreturn)) static void run_gate1_full(void)
{
	volatile uint32_t *const ram = (volatile uint32_t *)MAIN_RAM_BASE;
	volatile uint32_t *const code = ram + DDR_TEST_WORDS + 64u;
	uint32_t checksum = 0;
	uint32_t features = 0;
	uint32_t start_cycles = read_cycle();
	uint32_t first;
	uint32_t second;
	uint32_t i;

	gate1_failure_write(0);
	gate1_features_write(0);
	gate1_stage_write(1);

	if (sdram_init() != 1) {
		publish_failure(FAIL_DDR_INIT);
		for (;;) { }
	}
	features |= FEATURE_DDR_INIT;
	gate1_features_write(features);
	gate1_stage_write(2);

	for (i = 0; i < DDR_TEST_WORDS; ++i)
		ram[i] = pattern(i);
	__asm__ volatile ("fence rw, rw" ::: "memory");

	for (i = 0; i < DDR_TEST_WORDS; ++i) {
		uint32_t expected = pattern(i);
		uint32_t actual = ram[i];
		if (actual != expected) {
			gate1_ddr_words_write(i);
			gate1_ddr_checksum_write(actual);
			publish_failure(FAIL_DDR_DATA);
			for (;;) { }
		}
		checksum = (checksum << 5) | (checksum >> 27);
		checksum ^= actual;
	}
	gate1_ddr_words_write(DDR_TEST_WORDS);
	gate1_ddr_checksum_write(checksum);
	features |= FEATURE_DDR_RW;
	gate1_features_write(features);
	gate1_stage_write(3);

	install_return_constant(code, 42);
	first = ((uint32_t (*)(void))(uintptr_t)code)();
	if (first != 42) {
		gate1_jit_result_write(first << 16);
		publish_failure(FAIL_JIT_FIRST);
		for (;;) { }
	}

	install_return_constant(code, 99);
	second = ((uint32_t (*)(void))(uintptr_t)code)();
	gate1_jit_result_write((first << 16) | second);
	if (second != 99) {
		publish_failure(FAIL_JIT_REPLACE);
		for (;;) { }
	}

	features |= FEATURE_JIT_EXEC;
	gate1_features_write(features);
	gate1_cycles_write(read_cycle() - start_cycles);
	gate1_stage_write(0x80000001u);

	for (;;) {
		__asm__ volatile ("wfi");
	}
}

int main(void)
{
	run_phy_temporal_map_diagnostic();
}
