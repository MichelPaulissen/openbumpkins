/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MOVIE_IQT_H
#define BUMPKINS_MOVIE_IQT_H
#include "abi.h"
#include <stdint.h>
#define BB_IQT_F BB_FASTCALL __attribute__((noinline))
#define BB_IQT_M BB_THISCALL __attribute__((noinline))
uint32_t BB_IQT_F bb_movie_iqt_delta_entry(void *, const void *);
uint32_t BB_IQT_F bb_movie_iqt_init_entry(void *, const void *);
void BB_IQT_F bb_movie_iqt_signed_entry(void *, const void *, uint32_t,
                                        uint32_t);
void BB_IQT_F bb_movie_iqt_four_color_entry(const void *, void *, uint32_t);
void BB_IQT_M bb_movie_iqt_gamma_entry(uint32_t);
void BB_IQT_M bb_movie_iqt_palette_entry(void *);
void BB_IQT_M bb_movie_iqt_expand_entry(void *);
void __attribute__((noinline)) bb_movie_iqt_lz_entry(const void *, void *,
                                                     uint32_t);
uint32_t __attribute__((noinline))
bb_movie_iqt_indices_entry(void *, const void *, uint32_t, uint32_t);
void __attribute__((noinline)) bb_movie_iqt_blocks_entry(const void *,
                                                         const void *, void *,
                                                         uint32_t, const void *,
                                                         const void *, uint32_t,
                                                         uint32_t, uint32_t);
uint32_t __attribute__((noinline))
bb_movie_iqt_rotate_entry(void *, const void *, uint32_t);
#endif
