// SPDX-License-Identifier: GPL-3.0-only

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime.h"

#ifndef TPX_RUNTIME_HEAP_BYTES
#define TPX_RUNTIME_HEAP_BYTES (16u << 20)
#endif

static uint8_t heap[TPX_RUNTIME_HEAP_BYTES] __attribute__((aligned(16)));
static size_t heap_top;

size_t tpx_runtime_heap_used(void)
{
	return heap_top;
}

void *_sbrk(ptrdiff_t increment)
{
	void *previous = &heap[heap_top];

	if (increment < 0 ? (size_t)-increment > heap_top :
	    (size_t)increment > sizeof(heap) - heap_top) {
		errno = ENOMEM;
		return (void *)-1;
	}
	heap_top += (size_t)increment;
	return previous;
}

int _write(int fd, const void *buffer, size_t length)
{
	return (int)tpx_runtime_write(fd, buffer, length);
}

int _read(int fd, void *buffer, size_t length)
{
	(void)fd;
	(void)buffer;
	(void)length;
	return 0;
}

int _close(int fd)
{
	(void)fd;
	errno = EBADF;
	return -1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
	(void)fd;
	(void)offset;
	(void)whence;
	errno = ESPIPE;
	return -1;
}

int _fstat(int fd, struct stat *status)
{
	(void)fd;
	status->st_mode = S_IFCHR;
	return 0;
}

int _isatty(int fd)
{
	(void)fd;
	return 0;
}

int _getpid(void)
{
	return 1;
}

int _kill(int pid, int signal)
{
	(void)pid;
	tpx_runtime_exit(128 + signal);
}

void _exit(int status)
{
	tpx_runtime_exit(status);
}

// GNU Lightning's jit_flush rounds its __clear_cache range to pages.
long sysconf(int name)
{
	if (name == _SC_PAGESIZE)
		return 4096;
	errno = EINVAL;
	return -1;
}
