/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MOVIE_PIXELS_H
#define BUMPKINS_MOVIE_PIXELS_H
#include "abi.h"
#include <stdint.h>
void BB_FASTCALL __attribute__((noinline))
bb_movie_chroma_convert_entry(void *, uint32_t);
/* Temporary original register conventions: ESI coefficients, global output;
 * row colors use ESI luma, EBX chroma and EDI output. */
void bb_movie_idct_column_entry(void);
void bb_movie_idct_row_entry(void);
void bb_movie_color_row_entry(void);
/* Internal C workers share the temporary PUSHAL/flags register record. */
void bb_movie_idct_column_c(uint32_t *);
void bb_movie_idct_row_c(uint32_t *);
void bb_movie_color_row_c(uint32_t *);
#endif
