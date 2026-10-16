/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MOVIE_DISPATCH_H
#define BUMPKINS_MOVIE_DISPATCH_H
#include "abi.h"
#include <stdint.h>
void BB_FASTCALL __attribute__((noinline))
bb_movie_frame_dispatch_entry(void *, const uint32_t *);
void BB_FASTCALL __attribute__((noinline))
bb_movie_packets_32_entry(void *, const unsigned char *, void *, const void *);
void __attribute__((noinline)) bb_movie_bit_reset_entry(const void *);
#endif
