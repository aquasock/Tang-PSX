/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal <dlfcn.h> for running GNU Lightning's check driver on newlib. */
#ifndef TANG_PSX_LIGHTNING_DLFCN_H
#define TANG_PSX_LIGHTNING_DLFCN_H

#define RTLD_LAZY    1
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT    ((void *)-1)

void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
char *dlerror(void);

#endif
