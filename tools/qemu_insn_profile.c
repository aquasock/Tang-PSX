// SPDX-License-Identifier: GPL-3.0-only
//
// QEMU TCG plugin: count the RV32 instructions a program executes, per code
// address. Each translation block gets an execution callback; at exit every
// instruction address is written as "address executions loads stores" in
// hexadecimal and decimal, where loads and stores count the executions that
// were memory reads and writes:
//
//     qemu-riscv32 -plugin libqemu_insn_profile.so,out=PATH program
//
// Code rewritten at the same address (a JIT's buffer) accumulates into the
// same lines. tools/psx_core_compare.py attributes the addresses to functions.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

struct instruction {
	uint32_t vaddr;
	int kind;
};

struct block {
	uint64_t executions;
	uint32_t count;
	struct instruction instructions[];
};

struct total {
	uint64_t executions;
	uint64_t accesses[3];
};

static GPtrArray *blocks;
static char *output;

static int access_kind(const uint8_t *data, size_t size)
{
	uint32_t word = data[0] | (uint32_t)data[1] << 8;
	uint32_t funct3;
	if (size == 4) {
		switch (word & 0x7fu) {
		case 0x03: case 0x07: return 1;
		case 0x23: case 0x27: case 0x2f: return 2;
		default: return 0;
		}
	}
	funct3 = (word >> 13) & 7u;
	if ((word & 3u) == 1u || funct3 == 0u || funct3 == 4u)
		return 0;
	return funct3 < 4u ? 1 : 2;
}

static void block_executed(unsigned int vcpu, void *userdata)
{
	(void)vcpu;
	++((struct block *)userdata)->executions;
}

static void block_translated(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
	size_t count = qemu_plugin_tb_n_insns(tb);
	struct block *block = g_malloc0(sizeof(*block) +
		count * sizeof(block->instructions[0]));
	size_t n;
	(void)id;
	block->count = (uint32_t)count;
	for (n = 0; n < count; ++n) {
		struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, n);
		uint8_t data[4] = { 0 };
		size_t size = qemu_plugin_insn_data(insn, data, sizeof(data));
		block->instructions[n].vaddr =
			(uint32_t)qemu_plugin_insn_vaddr(insn);
		block->instructions[n].kind = access_kind(data, size);
	}
	g_ptr_array_add(blocks, block);
	qemu_plugin_register_vcpu_tb_exec_cb(tb, block_executed,
		QEMU_PLUGIN_CB_NO_REGS, block);
}

static gint compare_addresses(gconstpointer a, gconstpointer b)
{
	uint32_t left = GPOINTER_TO_UINT(*(void *const *)a);
	uint32_t right = GPOINTER_TO_UINT(*(void *const *)b);
	return left < right ? -1 : left > right;
}

static void finished(qemu_plugin_id_t id, void *userdata)
{
	GHashTable *totals = g_hash_table_new(NULL, NULL);
	GPtrArray *keys;
	FILE *file;
	guint n;
	(void)id;
	(void)userdata;
	for (n = 0; n < blocks->len; ++n) {
		struct block *block = g_ptr_array_index(blocks, n);
		uint32_t i;
		if (!block->executions)
			continue;
		for (i = 0; i < block->count; ++i) {
			const struct instruction *insn = &block->instructions[i];
			gpointer key = GUINT_TO_POINTER(insn->vaddr);
			struct total *total = g_hash_table_lookup(totals, key);
			if (!total) {
				total = g_new0(struct total, 1);
				g_hash_table_insert(totals, key, total);
			}
			total->executions += block->executions;
			total->accesses[insn->kind] += block->executions;
		}
	}
	keys = g_hash_table_get_keys_as_ptr_array(totals);
	g_ptr_array_sort(keys, compare_addresses);
	file = fopen(output, "w");
	if (!file) {
		perror(output);
		return;
	}
	for (n = 0; n < keys->len; ++n) {
		gpointer key = g_ptr_array_index(keys, n);
		const struct total *total = g_hash_table_lookup(totals, key);
		fprintf(file, "%08x %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			GPOINTER_TO_UINT(key), total->executions,
			total->accesses[1], total->accesses[2]);
	}
	fclose(file);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
	const qemu_info_t *info, int argc, char **argv)
{
	int n;
	(void)info;
	for (n = 0; n < argc; ++n) {
		if (strncmp(argv[n], "out=", 4) == 0)
			output = g_strdup(argv[n] + 4);
	}
	if (!output) {
		fprintf(stderr, "qemu_insn_profile: out=PATH is required\n");
		return -1;
	}
	blocks = g_ptr_array_new();
	qemu_plugin_register_vcpu_tb_trans_cb(id, block_translated);
	qemu_plugin_register_atexit_cb(id, finished, NULL);
	return 0;
}
