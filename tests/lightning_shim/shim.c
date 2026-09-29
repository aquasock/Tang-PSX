// SPDX-License-Identifier: GPL-3.0-only
// Host services GNU Lightning's check driver takes from a hosted C library.
// The .tst file is preprocessed on the build host and arrives on stdin, and
// the C functions the tests call through @name resolve from a fixed table.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "dlfcn.h"

// Linux RISC-V system calls, which qemu user mode services directly.
static long linux_syscall(long number, long a0, long a1, long a2, long a3,
	long a4, long a5)
{
	register long r0 __asm__("a0") = a0;
	register long r1 __asm__("a1") = a1;
	register long r2 __asm__("a2") = a2;
	register long r3 __asm__("a3") = a3;
	register long r4 __asm__("a4") = a4;
	register long r5 __asm__("a5") = a5;
	register long r7 __asm__("a7") = number;
	__asm__ volatile ("ecall" : "+r"(r0)
		: "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
		: "memory");
	return r0;
}

void *mmap(void *address, size_t length, int protection, int flags, int fd,
	off_t offset)
{
	long result = linux_syscall(222, (long)address, (long)length,
		protection, flags, fd, (long)offset);
	return result < 0 && result > -4096 ? MAP_FAILED : (void *)result;
}

int munmap(void *address, size_t length)
{
	return linux_syscall(215, (long)address, (long)length, 0, 0, 0, 0)
		? -1 : 0;
}

int mprotect(void *address, size_t length, int protection)
{
	return linux_syscall(226, (long)address, (long)length, protection,
		0, 0, 0) ? -1 : 0;
}

// GET_JIT_SIZE builds write their measurements through this "file".
FILE *shim_size_fopen(const char *path, const char *mode)
{
	(void)path;
	(void)mode;
	return stderr;
}

// Lightning rounds its instruction-cache flush range to the page size.
long sysconf(int name)
{
	return name == _SC_PAGESIZE ? 4096 : -1;
}

FILE *popen(const char *command, const char *mode)
{
	(void)command;
	(void)mode;
	return stdin;
}

int pclose(FILE *stream)
{
	(void)stream;
	return 0;
}

static const struct {
	const char *name;
	void *address;
} symbols[] = {
	{ "abort", (void *)abort },     { "atoi", (void *)atoi },
	{ "free", (void *)free },       { "malloc", (void *)malloc },
	{ "memcmp", (void *)memcmp },   { "memcpy", (void *)memcpy },
	{ "memset", (void *)memset },   { "printf", (void *)printf },
	{ "puts", (void *)puts },       { "sprintf", (void *)sprintf },
	{ "sscanf", (void *)sscanf },   { "strtoul", (void *)strtoul },
};

static char error[96];
static int error_pending;

void *dlopen(const char *file, int mode)
{
	(void)file;
	(void)mode;
	return RTLD_DEFAULT;
}

void *dlsym(void *handle, const char *name)
{
	(void)handle;
	for (size_t n = 0; n < sizeof(symbols) / sizeof(symbols[0]); ++n)
		if (strcmp(symbols[n].name, name) == 0)
			return symbols[n].address;
	snprintf(error, sizeof(error), "shim dlsym: %s is not in the table", name);
	error_pending = 1;
	return NULL;
}

char *dlerror(void)
{
	if (!error_pending)
		return NULL;
	error_pending = 0;
	return error;
}
