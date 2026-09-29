// SPDX-License-Identifier: GPL-3.0-only

#include "cdrom.h"

/*
 * Response timing in CPU cycles (33.8688 MHz). A sector arrives every
 * 1/75 s at single speed and 1/150 s at double speed.
 */
#define CD_ACK_CYCLES      20000u
#define CD_SECOND_CYCLES   50000u
#define CD_SEEK_CYCLES    100000u
#define CD_TOC_CYCLES     500000u
#define CD_SECTOR_1X      451584u
#define CD_SECTOR_2X      225792u
#define CD_RETRY_CYCLES     2000u

#define STAT_ERROR   0x01u
#define STAT_MOTOR   0x02u
#define STAT_SHELL   0x10u
#define STAT_READ    0x20u
#define STAT_SEEK    0x40u
#define STAT_PLAY    0x80u

#define MODE_ADPCM   0x40u
#define MODE_SIZE    0x20u
#define MODE_FILTER  0x08u
#define MODE_SPEED   0x80u

static uint8_t to_bcd(uint32_t value)
{
	return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static uint32_t from_bcd(uint8_t value)
{
	return (uint32_t)(value >> 4) * 10u + (value & 15u);
}

static void msf(uint32_t lba, uint8_t *out)
{
	uint32_t frames = lba + 150u;          /* two-second lead-in */
	out[0] = to_bcd(frames / (60u * 75u));
	out[1] = to_bcd((frames / 75u) % 60u);
	out[2] = to_bcd(frames % 75u);
}

static void update_irq(struct psx_cdrom *cd)
{
	cd->irq_line = cd->irq_flag != 0u &&
		(cd->irq_enable & (1u << (cd->irq_flag - 1u))) != 0u;
}

static void queue_response(struct psx_cdrom *cd, uint8_t irq,
	const uint8_t *bytes, uint32_t count, uint32_t due)
{
	struct psx_cd_response *response;
	uint32_t n;
	if (cd->queue_count >= PSX_CD_QUEUE)
		return;
	response = &cd->queue[cd->queue_count++];
	response->irq = irq;
	response->count = (uint8_t)count;
	for (n = 0; n < count && n < PSX_CD_FIFO_BYTES; ++n)
		response->bytes[n] = bytes[n];
	response->due = due;
}

static void queue_stat(struct psx_cdrom *cd, uint8_t irq, uint32_t due)
{
	queue_response(cd, irq, &cd->stat, 1u, due);
}

static void queue_error(struct psx_cdrom *cd, uint8_t code, uint32_t due)
{
	uint8_t bytes[2];
	bytes[0] = (uint8_t)(cd->stat | STAT_ERROR);
	bytes[1] = code;
	queue_response(cd, 5u, bytes, 2u, due);
}

static int queued_irq(const struct psx_cdrom *cd, uint8_t irq)
{
	uint32_t n;
	for (n = 0; n < cd->queue_count; ++n) {
		if (cd->queue[n].irq == irq)
			return 1;
	}
	return 0;
}

static void drop_queued_irq(struct psx_cdrom *cd, uint8_t irq)
{
	uint32_t in;
	uint32_t out = 0;
	for (in = 0; in < cd->queue_count; ++in) {
		if (cd->queue[in].irq != irq)
			cd->queue[out++] = cd->queue[in];
	}
	cd->queue_count = (uint8_t)out;
}

static uint32_t sector_cycles(const struct psx_cdrom *cd)
{
	return (cd->mode & MODE_SPEED) ? CD_SECTOR_2X : CD_SECTOR_1X;
}

static void stop_reading(struct psx_cdrom *cd)
{
	cd->reading = 0;
	cd->seeking = 0;
	cd->stat &= (uint8_t)~(STAT_READ | STAT_SEEK | STAT_PLAY);
	drop_queued_irq(cd, 1u);
}

static void start_reading(struct psx_cdrom *cd, uint32_t cycles)
{
	uint32_t delay = sector_cycles(cd);
	if (cd->setloc_pending) {
		cd->position = cd->setloc;
		cd->setloc_pending = 0;
		delay += CD_SEEK_CYCLES;
	}
	cd->reading = 1;
	cd->stat |= STAT_READ;
	cd->next_sector = cycles + delay;
}

void psx_cdrom_reset(struct psx_cdrom *cd, const struct psx_disc *disc)
{
	uint8_t *bytes = (uint8_t *)cd;
	uint32_t n;
	void (*trace_command)(const struct psx_cdrom *, uint8_t, uint32_t) =
		cd->trace_command;
	void (*trace_response)(const struct psx_cdrom *, uint32_t) =
		cd->trace_response;
	for (n = 0; n < sizeof(*cd); ++n)
		bytes[n] = 0;
	cd->trace_command = trace_command;
	cd->trace_response = trace_response;
	if (disc)
		cd->disc = *disc;
	/* Without a disc the BIOS sees an open lid. */
	cd->stat = cd->disc.sectors ? STAT_MOTOR : STAT_SHELL;
}

static void execute(struct psx_cdrom *cd, uint8_t command, uint32_t cycles)
{
	uint32_t ack = cycles + CD_ACK_CYCLES;
	uint8_t bytes[8];
	uint32_t n;

	if (cd->trace_command)
		cd->trace_command(cd, command, cycles);
	cd->history[cd->commands & 31u] = command;
	++cd->commands;
	cd->command = command;
	cd->busy = 1;
	switch (command) {
	case 0x01: /* GetStat: reports and clears the latched shell-open bit */
		queue_stat(cd, 3u, ack);
		if (cd->disc.sectors)
			cd->stat &= (uint8_t)~STAT_SHELL;
		break;
	case 0x02: /* Setloc amm, ass, asect */
		if (cd->param_count >= 3u) {
			uint32_t frames = (from_bcd(cd->params[0]) * 60u +
				from_bcd(cd->params[1])) * 75u +
				from_bcd(cd->params[2]);
			cd->setloc = frames >= 150u ? frames - 150u : 0u;
			cd->setloc_pending = 1;
		}
		queue_stat(cd, 3u, ack);
		break;
	case 0x03: /* Play: CD-DA is not emulated yet */
		queue_stat(cd, 3u, ack);
		break;
	case 0x06: /* ReadN */
	case 0x1b: /* ReadS */
		if (!cd->disc.sectors) {
			queue_error(cd, 0x80u, ack);
			break;
		}
		stop_reading(cd);
		start_reading(cd, cycles);
		queue_stat(cd, 3u, ack);
		break;
	case 0x07: /* MotorOn */
		queue_stat(cd, 3u, ack);
		cd->stat |= STAT_MOTOR;
		queue_stat(cd, 2u, ack + CD_SECOND_CYCLES);
		break;
	case 0x08: /* Stop */
		queue_stat(cd, 3u, ack);
		stop_reading(cd);
		cd->stat &= (uint8_t)~STAT_MOTOR;
		queue_stat(cd, 2u, ack + CD_SECOND_CYCLES);
		break;
	case 0x09: /* Pause: acknowledges with the reading state */
		queue_stat(cd, 3u, ack);
		stop_reading(cd);
		queue_stat(cd, 2u, ack + CD_SECOND_CYCLES);
		break;
	case 0x0a: /* Init: mode 20h, motor on, abort reads */
		queue_stat(cd, 3u, ack);
		stop_reading(cd);
		cd->mode = MODE_SIZE;
		if (cd->disc.sectors)
			cd->stat = STAT_MOTOR;
		queue_stat(cd, 2u, ack + CD_SECOND_CYCLES);
		break;
	case 0x0b: /* Mute */
	case 0x0c: /* Demute */
		queue_stat(cd, 3u, ack);
		break;
	case 0x0d: /* Setfilter file, channel */
		if (cd->param_count >= 2u) {
			cd->filter_file = cd->params[0];
			cd->filter_channel = cd->params[1];
		}
		queue_stat(cd, 3u, ack);
		break;
	case 0x0e: /* Setmode */
		if (cd->param_count >= 1u)
			cd->mode = cd->params[0];
		queue_stat(cd, 3u, ack);
		break;
	case 0x0f: /* Getparam */
		bytes[0] = cd->stat;
		bytes[1] = cd->mode;
		bytes[2] = 0;
		bytes[3] = cd->filter_file;
		bytes[4] = cd->filter_channel;
		queue_response(cd, 3u, bytes, 5u, ack);
		break;
	case 0x10: /* GetlocL: header and subheader of the newest sector */
		queue_response(cd, 3u, cd->last_header, 8u, ack);
		break;
	case 0x11: /* GetlocP: track, index, relative and absolute MSF */
		/* Track 1 starts at LBA 0, so relative time is the LBA. */
		bytes[0] = 0x01u;
		bytes[1] = 0x01u;
		bytes[2] = to_bcd(cd->last_lba / (60u * 75u));
		bytes[3] = to_bcd((cd->last_lba / 75u) % 60u);
		bytes[4] = to_bcd(cd->last_lba % 75u);
		msf(cd->last_lba, &bytes[5]);
		queue_response(cd, 3u, bytes, 8u, ack);
		break;
	case 0x12: /* SetSession */
		queue_stat(cd, 3u, ack);
		queue_stat(cd, 2u, ack + CD_SECOND_CYCLES);
		break;
	case 0x13: /* GetTN: one data track */
		bytes[0] = cd->stat;
		bytes[1] = 0x01u;
		bytes[2] = 0x01u;
		queue_response(cd, 3u, bytes, 3u, ack);
		break;
	case 0x14: /* GetTD track: 0 = lead-out */
		bytes[0] = cd->stat;
		if (cd->param_count >= 1u && cd->params[0] == 0u) {
			msf(cd->disc.sectors, &bytes[1]);
		} else {
			bytes[1] = 0x00u;
			bytes[2] = 0x02u;
		}
		queue_response(cd, 3u, bytes, 3u, ack);
		break;
	case 0x15: /* SeekL */
	case 0x16: /* SeekP */
		stop_reading(cd);
		cd->position = cd->setloc;
		cd->setloc_pending = 0;
		cd->stat |= STAT_SEEK;
		queue_stat(cd, 3u, ack);
		cd->stat &= (uint8_t)~STAT_SEEK;
		queue_stat(cd, 2u, ack + CD_SEEK_CYCLES);
		break;
	case 0x19: /* Test */
		if (cd->param_count >= 1u && cd->params[0] == 0x20u) {
			bytes[0] = 0x94u;       /* 1994-09-19, version C0 */
			bytes[1] = 0x09u;
			bytes[2] = 0x19u;
			bytes[3] = 0xc0u;
			queue_response(cd, 3u, bytes, 4u, ack);
		} else {
			queue_stat(cd, 3u, ack);
		}
		break;
	case 0x1a: /* GetID */
		queue_stat(cd, 3u, ack);
		if (!cd->disc.sectors) {
			bytes[0] = 0x08u;
			bytes[1] = 0x40u;
			for (n = 2; n < 8u; ++n)
				bytes[n] = 0;
			queue_response(cd, 5u, bytes, 8u, ack + CD_SECOND_CYCLES);
		} else {
			bytes[0] = cd->stat;
			bytes[1] = 0x00u;
			bytes[2] = 0x20u;       /* Mode 2 data disc */
			bytes[3] = 0x00u;
			bytes[4] = 'S';
			bytes[5] = 'C';
			bytes[6] = 'E';
			bytes[7] = cd->disc.region ? cd->disc.region : 'A';
			queue_response(cd, 2u, bytes, 8u, ack + CD_SECOND_CYCLES);
		}
		break;
	case 0x1e: /* ReadTOC */
		queue_stat(cd, 3u, ack);
		queue_stat(cd, 2u, ack + CD_TOC_CYCLES);
		break;
	default:
		queue_error(cd, 0x40u, ack);
		break;
	}
	cd->param_count = 0;
}

uint8_t psx_cdrom_read(struct psx_cdrom *cd, uint32_t port, uint32_t cycles)
{
	uint8_t value = 0;
	(void)cycles;
	switch (port & 3u) {
	case 0:
		value = cd->index;
		if (cd->param_count == 0u)
			value |= 0x08u;
		if (cd->param_count < PSX_CD_FIFO_BYTES)
			value |= 0x10u;
		if (cd->result_read < cd->result_count)
			value |= 0x20u;
		if (cd->data_read < cd->data_count)
			value |= 0x40u;
		if (cd->busy)
			value |= 0x80u;
		break;
	case 1:
		if (cd->result_read < cd->result_count)
			value = cd->result[cd->result_read++];
		break;
	case 2:
		if (cd->data_read < cd->data_count)
			value = cd->data[cd->data_read++];
		break;
	default:
		value = (uint8_t)(((cd->index & 1u) ? cd->irq_flag :
			cd->irq_enable) | 0xe0u);
		break;
	}
	return value;
}

static void load_data_fifo(struct psx_cdrom *cd)
{
	uint32_t offset = (cd->mode & MODE_SIZE) ? 12u : 24u;
	uint32_t count = (cd->mode & MODE_SIZE) ? 2340u : 2048u;
	uint32_t n;
	if (!cd->sector_ready) {
		cd->data_count = 0;
		cd->data_read = 0;
		return;
	}
	for (n = 0; n < count; ++n)
		cd->data[n] = cd->sector[offset + n];
	cd->data_count = count;
	cd->data_read = 0;
}

void psx_cdrom_write(struct psx_cdrom *cd, uint32_t port, uint8_t value,
	uint32_t cycles)
{
	port &= 3u;
	if (port == 0u) {
		cd->index = value & 3u;
		return;
	}
	/* Key: port in bits 3:2, bank index in bits 1:0. */
	switch ((port << 2) | cd->index) {
	case (1u << 2) | 0u: /* 1F801801h bank 0: command */
		execute(cd, value, cycles);
		break;
	case (2u << 2) | 0u: /* 1F801802h bank 0: parameter */
		if (cd->param_count < PSX_CD_FIFO_BYTES)
			cd->params[cd->param_count++] = value;
		break;
	case (2u << 2) | 1u: /* 1F801802h bank 1: interrupt enable */
		cd->irq_enable = value & 0x1fu;
		update_irq(cd);
		break;
	case (3u << 2) | 0u: /* 1F801803h bank 0: request register */
		if (value & 0x80u) {
			load_data_fifo(cd);
		} else {
			cd->data_count = 0;
			cd->data_read = 0;
		}
		break;
	case (3u << 2) | 1u: /* 1F801803h bank 1: interrupt acknowledge */
		if ((value & 0x07u) == 0x07u || (cd->irq_flag & value & 0x07u))
			cd->irq_flag = 0;
		if (value & 0x40u)
			cd->param_count = 0;
		update_irq(cd);
		break;
	default: /* audio volume and XA controls are accepted */
		break;
	}
}

static int xa_audio(const struct psx_cdrom *cd, const uint8_t *sector)
{
	uint8_t submode = sector[18];
	if (!(cd->mode & MODE_ADPCM) || sector[15] != 2u)
		return 0;
	/* Real-time audio sectors go to the ADPCM decoder, not the CPU. */
	return (submode & 0x44u) == 0x44u;
}

static void read_sector(struct psx_cdrom *cd, uint32_t cycles)
{
	uint32_t n;
	if (cd->position >= cd->disc.sectors) {
		stop_reading(cd);
		queue_error(cd, 0x04u, cycles);
		return;
	}
	/* The CPU has not collected the last sector yet: wait for it. */
	if (cd->irq_flag == 1u || queued_irq(cd, 1u)) {
		cd->next_sector = cycles + CD_RETRY_CYCLES;
		return;
	}
	if (cd->disc.read(cd->disc.opaque, cd->position, cd->sector) != 0) {
		/* Streamed sectors may not have arrived yet. */
		++cd->read_errors;
		cd->next_sector = cycles + CD_RETRY_CYCLES;
		return;
	}
	for (n = 0; n < 8u; ++n)
		cd->last_header[n] = cd->sector[12u + n];
	cd->last_lba = cd->position;
	++cd->position;
	cd->next_sector += sector_cycles(cd);
	if ((int32_t)(cd->next_sector - cycles) < 0)
		cd->next_sector = cycles + sector_cycles(cd);
	if (xa_audio(cd, cd->sector)) {
		++cd->sectors_skipped;
		return;
	}
	cd->sector_ready = 1;
	++cd->sectors_read;
	queue_stat(cd, 1u, cycles);
}

int psx_cdrom_service(struct psx_cdrom *cd, uint32_t cycles)
{
	uint8_t before = cd->irq_line;
	if (cd->reading && (int32_t)(cycles - cd->next_sector) >= 0)
		read_sector(cd, cycles);
	if (cd->irq_flag == 0u && cd->queue_count != 0u &&
	    (int32_t)(cycles - cd->queue[0].due) >= 0) {
		const struct psx_cd_response *response = &cd->queue[0];
		uint32_t n;
		for (n = 0; n < response->count; ++n)
			cd->result[n] = response->bytes[n];
		cd->result_count = response->count;
		cd->result_read = 0;
		cd->irq_flag = response->irq;
		if (response->irq == 3u || response->irq == 5u)
			cd->busy = 0;
		if (cd->trace_response)
			cd->trace_response(cd, cycles);
		for (n = 1; n < cd->queue_count; ++n)
			cd->queue[n - 1u] = cd->queue[n];
		--cd->queue_count;
		update_irq(cd);
	}
	return !before && cd->irq_line;
}

uint32_t psx_cdrom_dma_word(struct psx_cdrom *cd)
{
	uint32_t value = 0;
	uint32_t n;
	for (n = 0; n < 4u; ++n) {
		uint32_t byte = 0;
		if (cd->data_read < cd->data_count)
			byte = cd->data[cd->data_read++];
		value |= byte << (8u * n);
	}
	return value;
}
