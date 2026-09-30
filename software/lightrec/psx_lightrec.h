// SPDX-License-Identifier: GPL-3.0-only
//
// Lightrec as the R3000A core of a struct psx_machine. Lightrec owns the CPU
// registers while it runs; the machine keeps the bus, devices, DMA, GPU and
// interrupt controller. After every run the machine's cpu mirrors Lightrec's
// registers (pc, gpr, hi/lo, cp0, cycles), so the machine's VBlank-wait
// detection and statistics work unchanged. One instance at a time.
//
// Differences from the interpreter (r3000.c); the SCPH-1001 logo checkpoint
// is still reproduced exactly:
// - Interrupts are taken on Lightrec block boundaries: between runs, and at
//   the end of a block in which an I/O write raised one (before a SYSCALL or
//   BREAK that ends that block), not on the instruction after the write.
// - Lightrec raises no overflow, address-error or coprocessor-unusable
//   exceptions (ADD behaves as ADDU); an unknown opcode ends the run with
//   LIGHTREC_EXIT_UNKNOWN_OP instead of a reserved-instruction exception.
// - A SYSCALL or BREAK in a branch delay slot is entered with EPC at the
//   instruction itself (an upstream Lightrec limitation).
// - A GTE command at an interrupt's EPC is not executed on entry: handlers,
//   like the BIOS, return past it, and Lightrec's JR/RFE handling moves the
//   return back onto it so it runs once then.
// - Cache isolation (SR bit 16) is honoured when Status is written from
//   uncached code (kseg1 or the BIOS), as the BIOS cache flush does; Lightrec
//   assumes code in cached RAM never isolates the cache. Upstream Lightrec
//   also turns a JR to a known kseg1 address in RAM into a J, which stays in
//   the caller's segment, so such a jump from kseg0 code stays cached.
// - The machine's pattern accelerators (accelerate_delay_loop) are not used;
//   the guest code they replace runs compiled instead.

#ifndef TPX_PSX_LIGHTREC_H
#define TPX_PSX_LIGHTREC_H

#include <stddef.h>
#include <stdint.h>

#include "machine.h"

struct psx_lightrec_stats {
	uint32_t runs;
	uint32_t interrupts;
	uint32_t syscalls;
	uint32_t breaks;
	uint32_t gte_commands;
	uint32_t io_reads;
	uint32_t io_writes;
	uint32_t code_emissions;
	uint32_t dma_invalidations;
	uint32_t cache_isolations;
	uint32_t exit_flags;
	/*
	 * Host cycles (rdcycle on RISC-V, zero elsewhere), cumulative. execute
	 * covers lightrec_execute, including the callbacks below it (io, gte,
	 * and invalidate, which the RAM-write hook reaches from DMA); service
	 * is psx_machine_service after each execute; run is all of
	 * psx_lightrec_run.
	 */
	uint64_t run_cycles;
	uint64_t execute_cycles;
	uint64_t io_read_cycles;
	uint64_t io_write_cycles;
	uint64_t gte_cycles;
	uint64_t service_cycles;
	uint64_t invalidate_cycles;
};

/*
 * Attach Lightrec to a machine just reset with psx_machine_reset; the machine
 * state becomes Lightrec's starting state. code_buffer receives the generated
 * RV32 code. Returns 0, or -1 if Lightrec could not start.
 */
int psx_lightrec_init(struct psx_machine *machine, void *code_buffer,
	size_t code_bytes);
/*
 * Run about `cycles` guest instructions, taking interrupts and exceptions.
 * Returns 0, or the unexpected Lightrec exit flags (the run stops).
 */
uint32_t psx_lightrec_run(struct psx_machine *machine, uint32_t cycles);
const struct psx_lightrec_stats *psx_lightrec_stats(void);
void psx_lightrec_destroy(void);

#endif
