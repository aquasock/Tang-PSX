// SPDX-License-Identifier: GPL-3.0-only
//
// PlayStation CD-ROM controller (registers 1F801800h-1F801803h, DMA channel 3)
// following PSX-SPX "CDROM Drive". Discs are reached through callbacks so the
// same controller runs against a host .bin file or sectors streamed on the
// Tang Console. Time is measured in the machine's CPU cycle count.

#ifndef TANG_PSX_CDROM_H
#define TANG_PSX_CDROM_H

#include <stdint.h>

#define PSX_CD_SECTOR_BYTES 2352u
#define PSX_CD_FIFO_BYTES   16u
#define PSX_CD_QUEUE        4u

/*
 * A single-session disc. read() fills one raw 2352-byte sector at logical
 * block address lba (LBA 0 is MSF 00:02:00) and returns 0, or nonzero when
 * the sector is not available yet or at all.
 */
struct psx_disc {
	void *opaque;
	int (*read)(void *opaque, uint32_t lba, uint8_t *sector);
	uint32_t sectors;               /* data track length; 0 = no disc */
	uint8_t region;                 /* 'A', 'E' or 'I' for SCEA/SCEE/SCEI */
};

struct psx_cd_response {
	uint8_t irq;                    /* INT1-INT5 */
	uint8_t count;
	uint8_t bytes[PSX_CD_FIFO_BYTES];
	uint32_t due;                   /* cycle at which it may be delivered */
};

struct psx_cdrom {
	struct psx_disc disc;
	uint8_t index;
	uint8_t irq_enable;
	uint8_t irq_flag;               /* INT type currently latched, 0 = none */
	uint8_t stat;
	uint8_t mode;
	uint8_t filter_file;
	uint8_t filter_channel;
	uint8_t params[PSX_CD_FIFO_BYTES];
	uint8_t param_count;
	uint8_t result[PSX_CD_FIFO_BYTES];
	uint8_t result_count;
	uint8_t result_read;
	uint8_t command;
	uint8_t busy;                   /* command accepted, INT3 not yet sent */
	struct psx_cd_response queue[PSX_CD_QUEUE];
	uint8_t queue_count;
	uint32_t setloc;
	uint8_t setloc_pending;
	uint8_t reading;
	uint8_t seeking;
	uint32_t position;              /* next LBA to read */
	uint32_t next_sector;           /* cycle of the next sector */
	uint8_t sector[PSX_CD_SECTOR_BYTES];
	uint8_t sector_ready;           /* sector holds the INT1 sector */
	uint8_t data[PSX_CD_SECTOR_BYTES];
	uint32_t data_count;
	uint32_t data_read;
	uint8_t last_header[8];         /* GetlocL: header and subheader */
	uint32_t last_lba;
	uint8_t irq_line;               /* output: level for I_STAT bit 2 */
	/* Diagnostics. */
	uint32_t commands;
	uint32_t sectors_read;
	uint32_t sectors_skipped;
	uint32_t read_errors;
	uint8_t history[32];
	/* Optional diagnostic hooks: command with its parameters, and each
	 * response as it is delivered. */
	void (*trace_command)(const struct psx_cdrom *cd, uint8_t command,
		uint32_t cycles);
	void (*trace_response)(const struct psx_cdrom *cd, uint32_t cycles);
};

void psx_cdrom_reset(struct psx_cdrom *cd, const struct psx_disc *disc);
uint8_t psx_cdrom_read(struct psx_cdrom *cd, uint32_t port, uint32_t cycles);
void psx_cdrom_write(struct psx_cdrom *cd, uint32_t port, uint8_t value,
	uint32_t cycles);
/* Advances time; returns nonzero when a new interrupt was raised. */
int psx_cdrom_service(struct psx_cdrom *cd, uint32_t cycles);
/* DMA channel 3: reads one little-endian word from the data FIFO. */
uint32_t psx_cdrom_dma_word(struct psx_cdrom *cd);

#endif
