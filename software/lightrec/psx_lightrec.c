// SPDX-License-Identifier: GPL-3.0-only

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lightrec.h"
#include "psx_lightrec.h"
#include "runtime.h"

#if defined(__riscv) && __riscv_xlen == 32
static inline uint64_t host_cycle(void)
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
#else
static inline uint64_t host_cycle(void)
{
	return 0;
}
#endif

#define CP0_SR    12
#define CP0_CAUSE 13
#define CP0_EPC   14
#define SR_IEC    0x00000001u
#define SR_BEV    0x00400000u
#define CAUSE_IP2 0x00000400u

/*
 * While the cache is isolated the BIOS clears the cache tags by storing to
 * low RAM; the interpreter drops those stores. Lightrec's code writes RAM
 * directly, so the start of RAM is saved on isolation and restored after.
 * SCPH-1001's flushes store only within the first 4 KiB (the I-cache size);
 * 64 KiB leaves a margin for other flush routines.
 */
#define ISOLATED_RAM_BYTES 0x10000u

/* COP2 data then control registers, as struct psx_gte lays them out. */
_Static_assert(offsetof(struct lightrec_registers, cp2c) ==
	offsetof(struct lightrec_registers, cp2d) +
	offsetof(struct psx_gte, control), "GTE register layout");
_Static_assert(sizeof(struct psx_gte) == 64u * sizeof(uint32_t),
	"GTE register layout");

static struct {
	struct psx_machine *machine;
	struct lightrec_state *state;
	struct psx_lightrec_stats stats;
	uint32_t pc;
	/*
	 * Lightrec's counter holds the low 31 bits of cpu.cycles, so a run's
	 * target never wraps; cycle_base holds bit 31.
	 */
	uint32_t cycle_base;
	struct lightrec_mem_map maps[PSX_MAP_CODE_BUFFER + 1];
	uint8_t isolated_ram[ISOLATED_RAM_BYTES];
} lr;

static struct psx_gte *gte_registers(struct lightrec_state *state)
{
	return (struct psx_gte *)lightrec_get_registers(state)->cp2d;
}

static uint32_t irq_cause(const struct psx_machine *machine,
	const uint32_t *cp0)
{
	uint32_t cause = cp0[CP0_CAUSE] & ~CAUSE_IP2;
	if (machine->irq_status & machine->irq_mask)
		cause |= CAUSE_IP2;
	return cause;
}

static int interrupt_pending(const uint32_t *cp0)
{
	return (cp0[CP0_SR] & SR_IEC) &&
		(cp0[CP0_SR] & cp0[CP0_CAUSE] & 0x0000ff00u);
}

/* Enter the exception vector with EPC = pc (never a delay slot here). */
static uint32_t exception(uint32_t *cp0, enum psx_exception code, uint32_t pc)
{
	uint32_t status = cp0[CP0_SR];

	cp0[CP0_CAUSE] = (cp0[CP0_CAUSE] & 0x0000ff00u) | ((uint32_t)code << 2);
	cp0[CP0_EPC] = pc;
	cp0[CP0_SR] = (status & ~0x3fu) | ((status << 2) & 0x3fu);
	return (status & SR_BEV) ? 0xbfc00180u : 0x80000080u;
}

static uint32_t machine_cycles(const struct lightrec_state *state)
{
	return lr.cycle_base + lightrec_current_cycle_count(state);
}

static uint32_t io_read(struct lightrec_state *state, uint32_t address,
	uint32_t bytes)
{
	uint32_t value = 0;
	uint64_t start = host_cycle();

	lr.machine->cpu.cycles = machine_cycles(state);
	++lr.stats.io_reads;
	psx_machine_read(lr.machine, address, bytes, &value);
	lr.stats.io_read_cycles += host_cycle() - start;
	return value;
}

static void io_write(struct lightrec_state *state, uint32_t address,
	uint32_t bytes, uint32_t value)
{
	uint32_t *cp0 = lightrec_get_registers(state)->cp0;
	uint64_t start = host_cycle();

	lr.machine->cpu.cycles = machine_cycles(state);
	++lr.stats.io_writes;
	psx_machine_write(lr.machine, address, bytes, value);
	lr.stats.io_write_cycles += host_cycle() - start;
	/* A write that raises an enabled interrupt ends the run early. */
	cp0[CP0_CAUSE] = irq_cause(lr.machine, cp0);
	if (interrupt_pending(cp0))
		lightrec_set_exit_flags(state, LIGHTREC_EXIT_CHECK_INTERRUPT);
}

static void io_sb(struct lightrec_state *state, u32 opcode, void *host,
	u32 address, u32 data)
{
	(void)opcode;
	(void)host;
	io_write(state, address, 1u, data);
}

static void io_sh(struct lightrec_state *state, u32 opcode, void *host,
	u32 address, u32 data)
{
	(void)opcode;
	(void)host;
	io_write(state, address, 2u, data);
}

static void io_sw(struct lightrec_state *state, u32 opcode, void *host,
	u32 address, u32 data)
{
	(void)opcode;
	(void)host;
	io_write(state, address, 4u, data);
}

static u8 io_lb(struct lightrec_state *state, u32 opcode, void *host,
	u32 address)
{
	(void)opcode;
	(void)host;
	return (u8)io_read(state, address, 1u);
}

static u16 io_lh(struct lightrec_state *state, u32 opcode, void *host,
	u32 address)
{
	(void)opcode;
	(void)host;
	return (u16)io_read(state, address, 2u);
}

static u32 io_lw(struct lightrec_state *state, u32 opcode, void *host,
	u32 address)
{
	(void)opcode;
	(void)host;
	return io_read(state, address, 4u);
}

static const struct lightrec_mem_map_ops io_ops = {
	.sb = io_sb, .sh = io_sh, .sw = io_sw,
	.lb = io_lb, .lh = io_lh, .lw = io_lw,
	/* Unaligned word pairs (LWL/LWR, SWL/SWR) merged by the optimizer. */
	.lwu = io_lw, .swu = io_sw,
};

static void cop2_op(struct lightrec_state *state, u32 op)
{
	uint64_t start = host_cycle();

	++lr.stats.gte_commands;
	psx_gte_command(gte_registers(state), op);
	lr.stats.gte_cycles += host_cycle() - start;
}

static void enable_ram(struct lightrec_state *state, _Bool enable)
{
	(void)state;
	if (enable) {
		memcpy(lr.machine->ram, lr.isolated_ram, ISOLATED_RAM_BYTES);
	} else {
		++lr.stats.cache_isolations;
		memcpy(lr.isolated_ram, lr.machine->ram, ISOLATED_RAM_BYTES);
	}
}

static void code_inv(void *address, u32 length)
{
	(void)address;
	(void)length;
	tpx_runtime_sync_icache();
	++lr.stats.code_emissions;
}

static const struct lightrec_ops ops = {
	.cop2_op = cop2_op,
	.enable_ram = enable_ram,
	.code_inv = code_inv,
};

static void ram_written(void *opaque, uint32_t offset)
{
	uint64_t start = host_cycle();

	(void)opaque;
	++lr.stats.dma_invalidations;
	lightrec_invalidate(lr.state, offset, 4u);
	lr.stats.invalidate_cycles += host_cycle() - start;
}

static void set_map(enum psx_map index, uint32_t pc, uint32_t length,
	void *address, const struct lightrec_mem_map_ops *map_ops,
	enum psx_map mirror_of)
{
	struct lightrec_mem_map *map = &lr.maps[index];

	map->pc = pc;
	map->length = length;
	map->address = address;
	map->ops = map_ops;
	map->mirror_of = mirror_of == PSX_MAP_UNKNOWN ? NULL :
		&lr.maps[mirror_of];
}

int psx_lightrec_init(struct psx_machine *machine, void *code_buffer,
	size_t code_bytes)
{
	static char name[] = "tang-psx";
	struct lightrec_registers *regs;
	const enum psx_map none = PSX_MAP_UNKNOWN;

	memset(&lr.stats, 0, sizeof(lr.stats));
	memset(lr.maps, 0, sizeof(lr.maps));
	lr.machine = machine;
	/* Physical (kunseg) addresses, as the machine's bus decodes them. */
	set_map(PSX_MAP_KERNEL_USER_RAM, 0x00000000u, PSX_MAIN_RAM_BYTES,
		machine->ram, NULL, none);
	set_map(PSX_MAP_BIOS, 0x1fc00000u, PSX_BIOS_BYTES,
		(void *)(uintptr_t)machine->bios, NULL, none);
	set_map(PSX_MAP_SCRATCH_PAD, 0x1f800000u, PSX_SCRATCH_BYTES,
		machine->scratch, NULL, none);
	set_map(PSX_MAP_PARALLEL_PORT, 0x1f000000u, 0x10000u, NULL, &io_ops,
		none);
	set_map(PSX_MAP_HW_REGISTERS, 0x1f801000u, 0x8000u, NULL, &io_ops,
		none);
	set_map(PSX_MAP_CACHE_CONTROL, 0x5ffe0130u, 4u, NULL, &io_ops, none);
	set_map(PSX_MAP_MIRROR1, 0x00200000u, PSX_MAIN_RAM_BYTES, NULL, NULL,
		PSX_MAP_KERNEL_USER_RAM);
	set_map(PSX_MAP_MIRROR2, 0x00400000u, PSX_MAIN_RAM_BYTES, NULL, NULL,
		PSX_MAP_KERNEL_USER_RAM);
	set_map(PSX_MAP_MIRROR3, 0x00600000u, PSX_MAIN_RAM_BYTES, NULL, NULL,
		PSX_MAP_KERNEL_USER_RAM);
	set_map(PSX_MAP_CODE_BUFFER, 0, (uint32_t)code_bytes, code_buffer,
		NULL, none);

	lr.state = lightrec_init(name, lr.maps, PSX_MAP_CODE_BUFFER + 1, &ops);
	if (!lr.state)
		return -1;
	/* One cycle per instruction, as the machine's cpu.cycles counts. */
	lightrec_set_cycles_per_opcode(lr.state, 1u);
	regs = lightrec_get_registers(lr.state);
	memcpy(regs->gpr, machine->cpu.gpr, sizeof(machine->cpu.gpr));
	regs->gpr[32] = machine->cpu.lo;
	regs->gpr[33] = machine->cpu.hi;
	memcpy(regs->cp0, machine->cpu.cp0, sizeof(regs->cp0));
	memcpy(gte_registers(lr.state), &machine->cpu.gte,
		sizeof(machine->cpu.gte));
	lr.pc = machine->cpu.pc;
	machine->ram_write_hook = ram_written;
	machine->ram_write_opaque = NULL;
	return 0;
}

uint32_t psx_lightrec_run(struct psx_machine *machine, uint32_t cycles)
{
	struct lightrec_state *state = lr.state;
	struct lightrec_registers *regs = lightrec_get_registers(state);
	uint32_t *cp0 = regs->cp0;
	uint32_t start = machine->cpu.cycles & 0x7fffffffu;
	uint32_t end;
	uint32_t pc = lr.pc;
	uint32_t flags = 0;
	uint64_t run_start = host_cycle();
	uint64_t host_start;

	if (cycles > 0x7fffffffu)
		cycles = 0x7fffffffu;
	end = start + cycles;
	++lr.stats.runs;
	lr.cycle_base = machine->cpu.cycles - start;
	lightrec_reset_cycle_count(state, start);
	while (lightrec_current_cycle_count(state) < end) {
		cp0[CP0_CAUSE] = irq_cause(machine, cp0);
		if (interrupt_pending(cp0)) {
			pc = exception(cp0, PSX_EXC_INTERRUPT, pc);
			++lr.stats.interrupts;
		}
		host_start = host_cycle();
		pc = lightrec_execute(state, pc, end);
		lr.stats.execute_cycles += host_cycle() - host_start;
		machine->cpu.cycles = machine_cycles(state);
		host_start = host_cycle();
		psx_machine_service(machine);
		lr.stats.service_cycles += host_cycle() - host_start;
		flags = lightrec_exit_flags(state);
		cp0[CP0_CAUSE] = irq_cause(machine, cp0);
		if ((flags & (LIGHTREC_EXIT_SYSCALL | LIGHTREC_EXIT_BREAK)) &&
		    interrupt_pending(cp0)) {
			/*
			 * An I/O write earlier in the block raised it: take the
			 * interrupt first; the SYSCALL or BREAK (which has not
			 * executed) runs again when the handler returns.
			 */
		} else if (flags & LIGHTREC_EXIT_SYSCALL) {
			pc = exception(cp0, PSX_EXC_SYSCALL, pc);
			++lr.stats.syscalls;
		} else if (flags & LIGHTREC_EXIT_BREAK) {
			pc = exception(cp0, PSX_EXC_BREAK, pc);
			++lr.stats.breaks;
		}
		flags &= ~(uint32_t)(LIGHTREC_EXIT_SYSCALL | LIGHTREC_EXIT_BREAK |
			LIGHTREC_EXIT_CHECK_INTERRUPT);
		if (flags) {
			lr.stats.exit_flags |= flags;
			break;
		}
	}
	lr.pc = pc;
	cp0[CP0_CAUSE] = irq_cause(machine, cp0);
	machine->cpu.pc = pc;
	machine->cpu.next_pc = pc + 4u;
	memcpy(machine->cpu.gpr, regs->gpr, sizeof(machine->cpu.gpr));
	machine->cpu.lo = regs->gpr[32];
	machine->cpu.hi = regs->gpr[33];
	memcpy(machine->cpu.cp0, cp0, sizeof(machine->cpu.cp0));
	lr.stats.run_cycles += host_cycle() - run_start;
	return flags;
}

const struct psx_lightrec_stats *psx_lightrec_stats(void)
{
	return &lr.stats;
}

void psx_lightrec_destroy(void)
{
	if (lr.machine)
		lr.machine->ram_write_hook = NULL;
	if (lr.state)
		lightrec_destroy(lr.state);
	lr.state = NULL;
	lr.machine = NULL;
}
