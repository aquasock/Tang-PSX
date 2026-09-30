// SPDX-License-Identifier: GPL-3.0-only
//
// AE350 data-memory cost in DDR3, in core cycles per access, with the loader's
// cache settings. Each region holds one 32-byte node per cache line.
//
//   L16K ... L4M      dependent loads along a random single cycle of nodes
//                     (Sattolo), so every load waits for the previous one;
//                     16 KiB fits the 32 KiB D-cache, 64 and 96 KiB fit the
//                     128 KiB fabric L2 (gateware/l2_cache.py) but not the
//                     D-cache, and 256 KiB and 4 MiB fit neither
//   Q4M               loads of consecutive lines, independent of each other
//   S4M               one store per line, including write-back of the dirty
//                     lines each store evicts
//
// The D-cache is written back and invalidated before each measurement.
// Results go to the log and to the result registers (WORDS L4M, CHECKSUM
// L256K, JIT L16K, FEATURES Q4M, FAIL_ADDRESS S4M, FAIL_EXPECTED L64K,
// FAIL_OBSERVED L96K).

#include <stdint.h>

#include "tpx_api.h"

#define LINE_BYTES   32u
#define REGION_BYTES (4u << 20)
#define NODES        (REGION_BYTES / LINE_BYTES)
#define CHASE_LOADS  (1u << 20)

struct node {
	struct node *next;
	uint32_t pad[LINE_BYTES / 4u - 1u];
};

static struct node nodes[NODES] __attribute__((aligned(LINE_BYTES)));
static uint32_t order[NODES];

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

/* A random single cycle through the first count nodes (Sattolo). */
static void build_chain(uint32_t count)
{
	uint32_t seed = 0x12345678u;
	uint32_t i;
	for (i = 0; i < count; ++i)
		order[i] = i;
	for (i = count - 1u; i > 0u; --i) {
		uint32_t j;
		uint32_t t;
		seed = seed * 1664525u + 1013904223u;
		j = (uint32_t)(((uint64_t)seed * i) >> 32);   /* 0 .. i-1 */
		t = order[i];
		order[i] = order[j];
		order[j] = t;
	}
	for (i = 0; i < count; ++i)
		nodes[order[i]].next = &nodes[order[(i + 1u) % count]];
}

/* Cycles per dependent load, times ten. */
static uint32_t chase(const struct tpx_api *api, uint32_t bytes)
{
	struct node *p = &nodes[0];
	uint64_t start;
	uint32_t i;
	build_chain(bytes / LINE_BYTES);
	api->flush_dcache();
	start = read_cycle();
	for (i = 0; i < CHASE_LOADS; ++i)
		p = p->next;
	start = read_cycle() - start;
	__asm__ volatile ("" : : "r"(p));
	return (uint32_t)(start * 10u / CHASE_LOADS);
}

/* Cycles per line for consecutive independent loads, times ten. */
static uint32_t stream_read(const struct tpx_api *api)
{
	uint64_t start;
	uint32_t sum = 0;
	uint32_t i;
	api->flush_dcache();
	start = read_cycle();
	for (i = 0; i < NODES; ++i)
		sum += nodes[i].pad[0];
	start = read_cycle() - start;
	__asm__ volatile ("" : : "r"(sum));
	return (uint32_t)(start * 10u / NODES);
}

/* Cycles per line for one store per line, including dirty evictions. */
static uint32_t stream_write(const struct tpx_api *api)
{
	volatile struct node *region = nodes;
	uint64_t start;
	uint32_t pass;
	uint32_t i;
	api->flush_dcache();
	start = read_cycle();
	for (pass = 0; pass < 2u; ++pass)
		for (i = 0; i < NODES; ++i)
			region[i].pad[0] = i + pass;
	start = read_cycle() - start;
	return (uint32_t)(start * 10u / (2u * NODES));
}

static void report(const struct tpx_api *api, const char *label,
	uint32_t tenths)
{
	log_decimal(api, label, tenths / 10u);
	log_decimal(api, ".", tenths % 10u);
}

uint32_t main(const struct tpx_api *api)
{
	uint32_t l16k;
	uint32_t l64k;
	uint32_t l96k;
	uint32_t l256k;
	uint32_t l4m;
	uint32_t q4m;
	uint32_t s4m;

	api->set_reg(TPX_REG_STAGE, 0x00050001u);
	api->set_reg(TPX_REG_FAILURE, 0);
	l16k = chase(api, 16u << 10);
	l64k = chase(api, 64u << 10);
	l96k = chase(api, 96u << 10);
	l256k = chase(api, 256u << 10);
	l4m = chase(api, REGION_BYTES);
	q4m = stream_read(api);
	s4m = stream_write(api);
	report(api, "L16K ", l16k);
	report(api, " L64K ", l64k);
	report(api, " L96K ", l96k);
	report(api, "\nL256K ", l256k);
	report(api, " L4M ", l4m);
	report(api, "\nQ4M ", q4m);
	report(api, " S4M ", s4m);
	log_text(api, "\n");
	api->set_reg(TPX_REG_WORDS, l4m);
	api->set_reg(TPX_REG_CHECKSUM, l256k);
	api->set_reg(TPX_REG_JIT, l16k);
	api->set_reg(TPX_REG_FEATURES, q4m);
	api->set_reg(TPX_REG_FAIL_ADDRESS, s4m);
	api->set_reg(TPX_REG_FAIL_EXPECTED, l64k);
	api->set_reg(TPX_REG_FAIL_OBSERVED, l96k);
	api->set_reg(TPX_REG_STAGE, 0x80050001u);
	return 0x3e3a7001u;
}
