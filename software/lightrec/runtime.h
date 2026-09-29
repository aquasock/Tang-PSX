// SPDX-License-Identifier: GPL-3.0-only
//
// Bare-metal C runtime for Lightrec and GNU Lightning, which need malloc and
// stdio. runtime.c supplies newlib-nano's system calls over a static heap;
// the program supplies the two platform hooks below (qemu-user system calls
// in the tests, the tpx_api console on the AE350).

#ifndef TPX_LIGHTREC_RUNTIME_H
#define TPX_LIGHTREC_RUNTIME_H

#include <stddef.h>

// Platform hooks.
long tpx_runtime_write(int fd, const void *buffer, size_t length);
__attribute__((noreturn)) void tpx_runtime_exit(int status);

// Bytes of the static heap handed out so far.
size_t tpx_runtime_heap_used(void);

// Make stores to freshly emitted code visible to instruction fetch.
static inline void tpx_runtime_sync_icache(void)
{
#if defined(__riscv)
	__asm__ volatile ("fence rw,rw\n\tfence.i" ::: "memory");
#endif
}

#endif
