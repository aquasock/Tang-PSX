// SPDX-License-Identifier: GPL-3.0-only
//
// Large-image loader check: the image embeds BLOB_BYTES of deterministic data
// (blob.S) and returns their zlib CRC-32, which the host recomputes from the
// same generator in tools/ae350_run.py.

#include "tpx_api.h"

extern const uint8_t blob_start[];
extern const uint8_t blob_end[];

static const uint32_t crc_nibble[16] = {
	0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
	0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
	0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
	0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

uint32_t main(const struct tpx_api *api)
{
	uint32_t crc = 0xffffffffu;
	const uint8_t *byte;

	for (byte = blob_start; byte != blob_end; ++byte) {
		crc ^= *byte;
		crc = (crc >> 4) ^ crc_nibble[crc & 15u];
		crc = (crc >> 4) ^ crc_nibble[crc & 15u];
	}
	api->set_reg(TPX_REG_WORDS, (uint32_t)(blob_end - blob_start));
	return ~crc;
}
