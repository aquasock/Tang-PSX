/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * Programs run from DDR3 at 0x42000000, clear of the regions the Gate 1
 * self-checks use. The image payload is the flat binary from _start to
 * _edata; .bss is cleared by crt0.
 */

OUTPUT_FORMAT("elf32-littleriscv")
ENTRY(_start)

MEMORY
{
	ddr (rwx) : ORIGIN = 0x42000000, LENGTH = 64M
}

SECTIONS
{
	.text : {
		KEEP(*(.text.start))
		*(.text .text.*)
	} > ddr

	.rodata : ALIGN(8) {
		*(.rodata .rodata.* .srodata .srodata.*)
	} > ddr

	.data : ALIGN(8) {
		*(.data .data.* .sdata .sdata.*)
		_edata = .;
	} > ddr

	.bss (NOLOAD) : ALIGN(8) {
		_fbss = .;
		*(.sbss .sbss.* .bss .bss.* COMMON)
		. = ALIGN(4);
		_ebss = .;
	} > ddr

	/DISCARD/ : { *(.eh_frame) *(.comment) *(.riscv.attributes) }
}
