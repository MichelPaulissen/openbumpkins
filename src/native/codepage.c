/* SPDX-License-Identifier: GPL-3.0-only */
#include "codepage.h"
#include "codepage_data.h"
#include <stddef.h>

uint32_t bb_codepage_id(uint32_t page) {
  if (page == 0 || page == 3 || page == 1252)
    return 1252;
  return page == 1 || page == 437 ? 437 : 0;
}
uint16_t bb_codepage_decode(uint32_t page, unsigned char byte) {
  if (byte < 128)
    return byte;
  return page == 437 ? oem_high[byte - 128] : ansi_high[byte - 128];
}
uint16_t bb_codepage_glyph(uint32_t page, unsigned char byte) {
  static const uint16_t glyphs[] = {
      0,0x263a,0x263b,0x2665,0x2666,0x2663,0x2660,0x2022,
      0x25d8,0x25cb,0x25d9,0x2642,0x2640,0x266a,0x266b,0x263c,
      0x25ba,0x25c4,0x2195,0x203c,0xb6,0xa7,0x25ac,0x21a8,
      0x2191,0x2193,0x2192,0x2190,0x221f,0x2194,0x25b2,0x25bc};
  if (page == 437) {
    if (byte < 32)
      return glyphs[byte];
    if (byte == 127)
      return 0x2302;
  }
  return bb_codepage_decode(page, byte);
}
int bb_codepage_encode(uint32_t page, uint32_t character, int best_fit) {
  if (character < 128)
    return (int)character;
  for (unsigned byte = 128; byte < 256; ++byte)
    if (bb_codepage_decode(page, (unsigned char)byte) == character)
      return (int)byte;
  if (page == 1252 && best_fit) {
    size_t low = 0, high = sizeof ansi_best_fit / sizeof *ansi_best_fit;
    while (low < high) {
      size_t middle = low + (high - low) / 2;
      if (ansi_best_fit[middle].wide < character)
        low = middle + 1;
      else
        high = middle;
    }
    if (low < sizeof ansi_best_fit / sizeof *ansi_best_fit &&
        ansi_best_fit[low].wide == character)
      return ansi_best_fit[low].byte;
  }
  return -1;
}
unsigned bb_codepage_decompose(uint16_t character, uint16_t output[3]) {
  for (size_t i = 0; i < sizeof decomposed / sizeof *decomposed; ++i)
    if (decomposed[i].wide == character) {
      for (unsigned j = 0; j < decomposed[i].count; ++j)
        output[j] = decomposed[i].text[j];
      return decomposed[i].count;
    }
  output[0] = character;
  return 1;
}
uint16_t bb_codepage_compose(uint16_t first, uint16_t second) {
  uint32_t key = (uint32_t)first << 16 | second;
  size_t low = 0, high = sizeof composed / sizeof *composed;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (composed[middle].pair < key)
      low = middle + 1;
    else
      high = middle;
  }
  return low < sizeof composed / sizeof *composed && composed[low].pair == key
             ? composed[low].wide
             : 0;
}
int bb_codepage_nonspacing(uint16_t character) {
  for (size_t i = 0; i < sizeof nonspacing / sizeof *nonspacing; ++i)
    if (character >= nonspacing[i][0] && character <= nonspacing[i][1])
      return 1;
  return 0;
}
