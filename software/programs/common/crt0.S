/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * Entry point of a program loaded by the Gate 1 ROM loader. The loader calls
 * _start as `uint32_t _start(const struct tpx_api *api)` on its own stack with
 * the caches enabled. Clear .bss, call main(api), and return its value.
 */

	.section .text.start
	.global _start
	.type _start, @function
_start:
	addi	sp, sp, -16
	sw	ra, 12(sp)
	sw	a0, 8(sp)

	la	t0, _fbss
	la	t1, _ebss
1:
	bgeu	t0, t1, 2f
	sw	zero, 0(t0)
	addi	t0, t0, 4
	j	1b
2:
	lw	a0, 8(sp)
	call	main
	lw	ra, 12(sp)
	addi	sp, sp, 16
	ret

	.size _start, .-_start
