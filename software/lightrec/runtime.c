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

/*
 * newlib-nano's memset and memcpy move one byte per iteration, about 4 and
 * 6 RV32 instructions per byte, and GNU Lightning clears its node pools and
 * liveness sets with memset on every compile. These move aligned words, eight
 * per iteration. The optimize attribute stops GCC from turning the loops back
 * into memset and memcpy calls.
 */
typedef uint32_t __attribute__((may_alias)) word_t;

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *destination, int value, size_t length)
{
	uint8_t *d = destination;
	uint32_t pattern = (uint8_t)value * 0x01010101u;
	word_t *w;

	while (((uintptr_t)d & 3u) && length) {
		*d++ = (uint8_t)value;
		--length;
	}
	w = (word_t *)d;
	for (; length >= 32u; length -= 32u, w += 8) {
		w[0] = pattern; w[1] = pattern; w[2] = pattern; w[3] = pattern;
		w[4] = pattern; w[5] = pattern; w[6] = pattern; w[7] = pattern;
	}
	for (; length >= 4u; length -= 4u)
		*w++ = pattern;
	d = (uint8_t *)w;
	while (length--)
		*d++ = (uint8_t)value;
	return destination;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *restrict destination, const void *restrict source,
	size_t length)
{
	uint8_t *d = destination;
	const uint8_t *s = source;

	if ((((uintptr_t)d ^ (uintptr_t)s) & 3u) == 0) {
		word_t *w;
		const word_t *r;
		while (((uintptr_t)d & 3u) && length) {
			*d++ = *s++;
			--length;
		}
		w = (word_t *)d;
		r = (const word_t *)s;
		for (; length >= 32u; length -= 32u, w += 8, r += 8) {
			w[0] = r[0]; w[1] = r[1]; w[2] = r[2]; w[3] = r[3];
			w[4] = r[4]; w[5] = r[5]; w[6] = r[6]; w[7] = r[7];
		}
		for (; length >= 4u; length -= 4u)
			*w++ = *r++;
		d = (uint8_t *)w;
		s = (const uint8_t *)r;
	}
	while (length--)
		*d++ = *s++;
	return destination;
}
