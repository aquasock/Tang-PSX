// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cdrom.h"

#define MODE_RAW_SPEED 0xa0u
#define SECTOR_CYCLES  225792u

struct fake_disc {
	uint8_t sectors[2][PSX_CD_SECTOR_BYTES];
	uint32_t reads;
};

static int read_sector(void *opaque, uint32_t lba, uint8_t *sector)
{
	struct fake_disc *disc = opaque;
	if (lba >= 2u)
		return -1;
	memcpy(sector, disc->sectors[lba], PSX_CD_SECTOR_BYTES);
	++disc->reads;
	return 0;
}

static void select_bank(struct psx_cdrom *cd, uint8_t bank, uint32_t cycles)
{
	psx_cdrom_write(cd, 0u, bank, cycles);
}

static void acknowledge(struct psx_cdrom *cd, uint32_t cycles)
{
	select_bank(cd, 1u, cycles);
	psx_cdrom_write(cd, 3u, 7u, cycles);
}

static int verify_sector(struct psx_cdrom *cd, const uint8_t *raw,
	uint32_t cycles)
{
	uint32_t word;
	uint32_t n;

	select_bank(cd, 0u, cycles);
	psx_cdrom_write(cd, 3u, 0x80u, cycles);
	for (n = 0; n < 3u; ++n) {
		word = psx_cdrom_dma_word(cd);
		if (memcmp(&word, raw + 12u + n * 4u, 4u) != 0)
			return -1;
	}

	/* PsyQ LibCD reasserts BFRD here.  The cursor must remain after the
	 * header rather than jumping back to raw-sector byte 12. */
	psx_cdrom_write(cd, 3u, 0x80u, cycles);
	for (n = 0; n < 512u; ++n) {
		word = psx_cdrom_dma_word(cd);
		if (memcmp(&word, raw + 24u + n * 4u, 4u) != 0)
			return -1;
	}
	return 0;
}

int main(void)
{
	struct fake_disc fake = {0};
	struct psx_disc disc = {&fake, read_sector, 2u, 'A'};
	struct psx_cdrom cd = {0};
	uint32_t cycles = 0;
	uint32_t sector;
	uint32_t n;

	for (sector = 0; sector < 2u; ++sector) {
		for (n = 0; n < PSX_CD_SECTOR_BYTES; ++n)
			fake.sectors[sector][n] = (uint8_t)(n * 29u + sector * 113u);
		fake.sectors[sector][15] = 2u;
	}

	psx_cdrom_reset(&cd, &disc);
	select_bank(&cd, 0u, cycles);
	psx_cdrom_write(&cd, 2u, MODE_RAW_SPEED, cycles);
	psx_cdrom_write(&cd, 1u, 0x0eu, cycles);
	cycles += 20000u;
	psx_cdrom_service(&cd, cycles);
	acknowledge(&cd, cycles);

	select_bank(&cd, 0u, cycles);
	psx_cdrom_write(&cd, 1u, 0x06u, cycles);
	cycles += 20000u;
	psx_cdrom_service(&cd, cycles);
	acknowledge(&cd, cycles);

	cycles += SECTOR_CYCLES;
	psx_cdrom_service(&cd, cycles);
	if (cd.irq_flag != 1u || verify_sector(&cd, fake.sectors[0], cycles) != 0)
		return 1;
	acknowledge(&cd, cycles);

	cycles += SECTOR_CYCLES;
	psx_cdrom_service(&cd, cycles);
	if (cd.irq_flag != 1u || verify_sector(&cd, fake.sectors[1], cycles) != 0)
		return 1;
	if (fake.reads != 2u)
		return 1;

	puts("CD-ROM: PsyQ raw header/data FIFO cursor passed across 2 sectors");
	return 0;
}
