// SPDX-License-Identifier: GPL-3.0-only
//
// runtime.h's platform hooks for programs run by the Gate 1 ROM loader.
// Output (Lightrec and GNU Lightning messages on stderr) goes to the loader's
// log ring. An exit, which only an abort inside Lightrec or GNU Lightning
// makes, publishes failure TPX_PLATFORM_EXIT_FAILURE | (status & 0xffff) at
// stage TPX_PLATFORM_EXIT_STAGE and stops; there is no process to end, so the
// core must be reset.

#ifndef TPX_LIGHTREC_PLATFORM_H
#define TPX_LIGHTREC_PLATFORM_H

#include "tpx_api.h"

#define TPX_PLATFORM_EXIT_FAILURE 0x4c520000u   /* "LR" */
#define TPX_PLATFORM_EXIT_STAGE   0x800fbad0u

// Call first in main, before anything that can reach the runtime.
void tpx_platform_attach(const struct tpx_api *api);

#endif
