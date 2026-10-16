/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MOVIE_CODEC_H
#define BUMPKINS_MOVIE_CODEC_H
#include "abi.h"
#include <stdint.h>
void bb_movie_rle_coefficients_entry(void);
void bb_movie_vlc_coefficients_entry(void);
void __attribute__((noinline)) bb_movie_packet_block_entry(const void *, void *,
                                                           uint32_t);
void __attribute__((noinline)) bb_movie_block_32_entry(void *, uint32_t);
void __attribute__((noinline)) bb_movie_block_16_entry(void *, uint32_t);
#endif
