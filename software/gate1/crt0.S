/* SPDX-License-Identifier: GPL-3.0-only */

#define MSTATUS_FS_INITIAL (1 << 13)

	.section .text
	.global _start
	.type _start, @function
_start:
	j reset_vector

reset_vector:
	la sp, _fstack
	la t0, trap_vector
	csrw mtvec, t0

	/* The LiteX ABI enables rv32imafdc, so establish clean FPU state. */
	li t0, MSTATUS_FS_INITIAL
	csrs mstatus, t0
	csrwi fcsr, 0

	/* Copy initialized data from the boot ROM image into SRAM. */
	la t0, _fdata
	la t1, _edata
	la t2, _fdata_rom
1:
	beq t0, t1, 2f
	lw t3, 0(t2)
	sw t3, 0(t0)
	addi t0, t0, 4
	addi t2, t2, 4
	j 1b
2:

	/* Clear BSS. */
	la t0, _fbss
	la t1, _ebss
3:
	beq t0, t1, 4f
	sw zero, 0(t0)
	addi t0, t0, 4
	j 3b
4:
	call main
5:
	wfi
	j 5b

	/* Gate 1 uses no interrupts. Any trap is a hard stop, preserving status. */
	.balign 4
trap_vector:
	j trap_vector

	.size _start, .-_start
