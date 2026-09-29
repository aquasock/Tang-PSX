/* SPDX-License-Identifier: GPL-3.0-only */

	.section .rodata.bios,"a",@progbits
	.balign 4
	.global psx_bios_image
	.global psx_bios_image_end
psx_bios_image:
	.incbin "scph1001.bin"
psx_bios_image_end:
