// SPDX-License-Identifier: GPL-3.0-only

#include "sio.h"

/* One byte at the BIOS's 250 kHz serial clock plus the device's /ACK delay. */
#define SIO_ACK_CYCLES 1500u

#define CTRL_SELECT     0x0002u
#define CTRL_ACK        0x0010u
#define CTRL_RESET      0x0040u
#define CTRL_ACK_IRQ    0x1000u
#define CTRL_PORT2      0x2000u

void psx_sio_reset(struct psx_sio *sio)
{
	uint8_t *bytes = (uint8_t *)sio;
	uint32_t n;
	for (n = 0; n < sizeof(*sio); ++n)
		bytes[n] = 0;
	sio->buttons = 0xffffu;
}

/* Returns the device's reply to one byte and whether it acknowledges. */
static uint8_t exchange(struct psx_sio *sio, uint8_t tx, int *ack)
{
	uint8_t step = sio->step++;
	*ack = 0;
	if (step == 0u) {
		/* Only a digital pad is connected, in slot 1. */
		sio->device = (tx == 0x01u && !(sio->control & CTRL_PORT2)) ? 1u : 0u;
		*ack = sio->device != 0u;
		return 0xffu;
	}
	if (sio->device != 1u)
		return 0xffu;
	switch (step) {
	case 1: *ack = 1; return 0x41u;           /* digital pad ID */
	case 2: *ack = 1; return 0x5au;
	case 3: *ack = 1; return (uint8_t)sio->buttons;
	case 4: return (uint8_t)(sio->buttons >> 8);
	default: return 0xffu;
	}
}

uint32_t psx_sio_read(struct psx_sio *sio, uint32_t address, uint32_t bytes)
{
	uint32_t value = 0;
	(void)bytes;
	switch (address & 0xfu) {
	case 0x0:
		value = sio->rx_full ? sio->rx : 0xffu;
		sio->rx_full = 0;
		break;
	case 0x4:
		value = 0x5u;                   /* TX ready, TX finished */
		if (sio->rx_full)
			value |= 0x2u;
		if (sio->ack_level)
			value |= 0x80u;
		if (sio->irq)
			value |= 0x200u;
		break;
	case 0x8:
		value = sio->mode;
		break;
	case 0xa:
		value = sio->control;
		break;
	case 0xe:
		value = sio->baud;
		break;
	default:
		break;
	}
	return value;
}

void psx_sio_write(struct psx_sio *sio, uint32_t address, uint32_t bytes,
	uint32_t value, uint32_t cycles)
{
	int ack;
	(void)bytes;
	switch (address & 0xfu) {
	case 0x0:
		if (!(sio->control & CTRL_SELECT))
			break;
		sio->rx = exchange(sio, (uint8_t)value, &ack);
		sio->rx_full = 1;
		sio->ack_level = 0;
		sio->ack_pending = (uint8_t)ack;
		sio->ack_due = cycles + SIO_ACK_CYCLES;
		break;
	case 0x8:
		sio->mode = (uint16_t)value;
		break;
	case 0xa:
		if (value & CTRL_RESET) {
			psx_sio_reset(sio);
			break;
		}
		if (value & CTRL_ACK)
			sio->irq = 0;
		if (!(value & CTRL_SELECT)) {
			sio->step = 0;
			sio->device = 0;
			sio->ack_pending = 0;
		}
		sio->control = (uint16_t)(value & ~(CTRL_ACK | CTRL_RESET));
		break;
	case 0xe:
		sio->baud = (uint16_t)value;
		break;
	default:
		break;
	}
}

int psx_sio_service(struct psx_sio *sio, uint32_t cycles)
{
	if (!sio->ack_pending || (int32_t)(cycles - sio->ack_due) < 0)
		return 0;
	sio->ack_pending = 0;
	sio->ack_level = 1;
	if (!(sio->control & CTRL_ACK_IRQ) || sio->irq)
		return 0;
	sio->irq = 1;
	return 1;
}
