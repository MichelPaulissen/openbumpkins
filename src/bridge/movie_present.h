/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MOVIE_PRESENT_H
#define BUMPKINS_MOVIE_PRESENT_H
#include "abi.h"
#include <stdint.h>
uint32_t BB_THISCALL __attribute__((noinline))
bb_movie_present_16_entry(void *);
uint32_t BB_THISCALL __attribute__((noinline))
bb_movie_present_32_entry(void *);
#endif
