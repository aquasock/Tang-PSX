/* SPDX-License-Identifier: GPL-3.0-only */

OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
	all PT_LOAD FLAGS(7);
}

SECTIONS
{
	. = 0x10000;
	.text : { *(.text.start) *(.text*) } :all
	.rodata : { *(.rodata*) *(.srodata*) } :all
	.data : { *(.data*) *(.sdata*) } :all
	.bss : { *(.bss*) *(.sbss*) *(COMMON) } :all
}
