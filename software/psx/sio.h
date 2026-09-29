// SPDX-License-Identifier: GPL-3.0-only
//
// Controller and memory-card serial port (SIO0, 1F801040h-1F80104Eh) with a
// digital pad in slot 1 and no memory cards, following PSX-SPX "Controllers
// and Memory Cards". Time is the machine's CPU cycle count.

#ifndef TANG_PSX_SIO_H
#define TANG_PSX_SIO_H

#include <stdint.h>

struct psx_sio {
	uint16_t mode;
	uint16_t control;
	uint16_t baud;
	uint8_t rx;
	uint8_t rx_full;
	uint8_t ack_level;              /* /ACK input asserted */
	uint8_t irq;                    /* JOY_STAT bit 9 */
	uint8_t device;                 /* 0 none, 1 pad, 2 memory card */
	uint8_t step;                   /* byte index within the exchange */
	uint8_t ack_pending;
	uint32_t ack_due;
	uint16_t buttons;               /* active-low, 0xFFFF = none pressed */
};

void psx_sio_reset(struct psx_sio *sio);
uint32_t psx_sio_read(struct psx_sio *sio, uint32_t address, uint32_t bytes);
void psx_sio_write(struct psx_sio *sio, uint32_t address, uint32_t bytes,
	uint32_t value, uint32_t cycles);
/* Advances time; returns nonzero when IRQ7 should be raised. */
int psx_sio_service(struct psx_sio *sio, uint32_t cycles);

#endif
