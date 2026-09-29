// SPDX-License-Identifier: GPL-3.0-only

	.section .text.start
	.globl _start
_start:
	call psx_jit_rv32_test
	li a7, 93
	ecall
