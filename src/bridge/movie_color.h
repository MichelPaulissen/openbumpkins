/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MOVIE_COLOR_H
#define BUMPKINS_MOVIE_COLOR_H
#include "abi.h"
#include <stdint.h>
void BB_THISCALL __attribute__((noinline)) bb_movie_frame_setup_entry(void *);
void BB_FASTCALL __attribute__((noinline))
bb_movie_color_tables_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t);
uint32_t BB_FASTCALL __attribute__((noinline))
bb_movie_color_pack_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                          uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void BB_THISCALL __attribute__((noinline)) bb_movie_color_rgba_entry(uint32_t);
void BB_FASTCALL __attribute__((noinline))
bb_movie_color_block_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t, uint32_t *, uint32_t);
#endif
