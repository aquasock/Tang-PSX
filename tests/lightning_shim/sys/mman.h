/* SPDX-License-Identifier: GPL-3.0-only */
/* Linux <sys/mman.h> subset for running GNU Lightning under qemu user mode. */
#ifndef TANG_PSX_LIGHTNING_SYS_MMAN_H
#define TANG_PSX_LIGHTNING_SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>

#define PROT_READ     1
#define PROT_WRITE    2
#define PROT_EXEC     4
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

void *mmap(void *address, size_t length, int protection, int flags, int fd,
	off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);

#endif
