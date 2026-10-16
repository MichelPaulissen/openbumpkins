/* SPDX-License-Identifier: GPL-3.0-only */
#include "unicode.h"
#include "unicode_data.h"
#include <stddef.h>

uint32_t bb_unicode_case(uint32_t character, int upper) {
  const struct UnicodeCaseRange *ranges = upper ? upper_ranges : lower_ranges;
  size_t count = upper ? sizeof upper_ranges / sizeof *upper_ranges
                       : sizeof lower_ranges / sizeof *lower_ranges;
  size_t low = 0, high = count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (ranges[middle].last < character)
      low = middle + 1;
    else
      high = middle;
  }
  if (low < count && character >= ranges[low].first &&
      (character - ranges[low].first) % ranges[low].step == 0)
    return character + (uint32_t)ranges[low].delta;
  return character;
}
uint16_t bb_unicode_type(uint16_t character, unsigned level) {
  size_t low = 0, high = sizeof type_ranges / sizeof *type_ranges;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (type_ranges[middle].last < character)
      low = middle + 1;
    else
      high = middle;
  }
  return type_values[type_ranges[low].index][level];
}
