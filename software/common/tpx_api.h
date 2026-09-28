// SPDX-License-Identifier: GPL-3.0-only
//
// Interface between the Gate 1 ROM loader and programs it loads into DDR3.
//
// An image is a 32-byte little-endian header followed by the payload. The
// loader copies the payload to load_address, checks its length and CRC-32
// (the zlib/IEEE polynomial), writes back the D-cache, executes fence.i, and
// calls entry as `uint32_t entry(const struct tpx_api *api)` on the loader's
// stack with both caches enabled. When the program returns, its value is
// published and the loader waits for the next image.

#ifndef TPX_API_H
#define TPX_API_H

#include <stdint.h>

#define TPX_API_VERSION     1u
#define TPX_IMAGE_MAGIC     0x31495054u     /* "TPI1" */
#define TPX_IMAGE_HEADER    32u

/* Static Gate 1 drawing surface. Call flush_dcache after changing pixels. */
#define TPX_FRAMEBUFFER_BASE   0x7ff00000u
#define TPX_FRAMEBUFFER_WIDTH  640u
#define TPX_FRAMEBUFFER_HEIGHT 480u
#define TPX_FRAMEBUFFER_STRIDE (TPX_FRAMEBUFFER_WIDTH * 2u)

struct tpx_image_header {
	uint32_t magic;
	uint32_t header_size;       /* TPX_IMAGE_HEADER */
	uint32_t load_address;      /* in main RAM, 4-byte aligned */
	uint32_t entry;             /* inside the payload */
	uint32_t payload_size;
	uint32_t payload_crc32;
	uint32_t flags;             /* 0 */
	uint32_t reserved;          /* 0 */
};

/* Result registers readable through Tang-Control (debug addresses in README). */
enum tpx_reg {
	TPX_REG_STAGE,          /* 0x08 */
	TPX_REG_FAILURE,        /* 0x0c */
	TPX_REG_WORDS,          /* 0x10 */
	TPX_REG_CHECKSUM,       /* 0x14 */
	TPX_REG_JIT,            /* 0x18 */
	TPX_REG_CYCLES,         /* 0x1c */
	TPX_REG_FEATURES,       /* 0x20 */
	TPX_REG_FAIL_ADDRESS,   /* 0xc0 */
	TPX_REG_FAIL_EXPECTED,  /* 0xc4 */
	TPX_REG_FAIL_OBSERVED,  /* 0xc8 */
	TPX_REG_COUNT
};

struct tpx_api {
	uint32_t version;                               /* TPX_API_VERSION */
	uint32_t cpu_hz;                                /* rdcycle rate */
	void (*putc)(char c);                           /* append to the log ring */
	void (*set_reg)(uint32_t reg, uint32_t value);  /* enum tpx_reg */
	void (*flush_dcache)(void);                     /* write back + invalidate L1D */
};

typedef uint32_t (*tpx_entry)(const struct tpx_api *api);

/* Loader state, bits 7:0 of debug address 0xd0; bits 31:16 count completed runs. */
enum tpx_loader_state {
	TPX_LOADER_BOOT      = 0x00,
	TPX_LOADER_WAIT      = 0x01,
	TPX_LOADER_RECEIVE   = 0x02,
	TPX_LOADER_RUN       = 0x03,
	TPX_LOADER_RETURNED  = 0x04,
	TPX_LOADER_ERR_HEADER    = 0x81,
	TPX_LOADER_ERR_RANGE     = 0x82,
	TPX_LOADER_ERR_TRUNCATED = 0x83,
	TPX_LOADER_ERR_LENGTH    = 0x84,
	TPX_LOADER_ERR_CRC       = 0x85,
	TPX_LOADER_ERR_CANCELLED = 0x86,
	TPX_LOADER_ERR_OVERFLOW  = 0x87,
};

#endif
