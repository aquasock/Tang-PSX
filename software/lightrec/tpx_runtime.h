// SPDX-License-Identifier: GPL-3.0-only

#ifndef TPX_LIGHTREC_TPX_RUNTIME_H
#define TPX_LIGHTREC_TPX_RUNTIME_H

#include <stdint.h>

struct tpx_api;

void tpx_runtime_bind(const struct tpx_api *api, uint32_t failure_stage);

#endif
