/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CODEPAGE_H
#define BB_CODEPAGE_H
#include <stdint.h>

/* The native platform advertises ANSI 1252 and OEM 437. Zero means that an
 * unsupported page was requested; aliases resolve without host locale state. */
uint32_t bb_codepage_id(uint32_t page);
uint16_t bb_codepage_decode(uint32_t page, unsigned char byte);
uint16_t bb_codepage_glyph(uint32_t page, unsigned char byte);
int bb_codepage_encode(uint32_t page, uint32_t character, int best_fit);
unsigned bb_codepage_decompose(uint16_t character, uint16_t output[3]);
uint16_t bb_codepage_compose(uint16_t first, uint16_t second);
int bb_codepage_nonspacing(uint16_t character);
#endif
