/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_primitives.h"
#include "crt_locale.h"
#include "crt_models.h"
#include "game_calls.h"
#include "interop.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
/* Canonicalize mapped aliases, while ordinary private C buffers stay native. */
static uintptr_t data_location(const void *p) {
#ifdef BB_PORTABLE_RUNTIME
  uint32_t reference;
  if (bb_address_try_encode(p, &reference))
    return reference;
#endif
  return (uintptr_t)p;
}
static uint32_t character_flags(int32_t c) {
  return WORD(BB_U32(0x48d150) + (uint32_t)c * 2);
}
uint32_t bb_crt_character_type_c(int32_t c, uint32_t mask, uint32_t scratch) {
  if ((uint32_t)c + 1u <= 256)
    return character_flags(c) & mask;
  int32_t count = 1;
  if (character_flags(((uint32_t)c >> 8) & 255) & 0x8000) {
    BYTE(scratch) = (uint32_t)c >> 8;
    BYTE(scratch + 1u) = c;
    BYTE(scratch + 2u) = 0;
    count = 2;
  } else {
    BYTE(scratch) = c;
    BYTE(scratch + 1u) = 0;
  }
#ifdef BB_MODEL_RUNTIME
  int32_t result =
      BB_GAME_CALL(0x0047e2c0u, 0, 1, scratch, count, scratch + 8u, 0, 0);
#else
  int32_t result = bb_crt_locale_type_entry(1, BB_DATA(scratch), count,
                                            BB_DATA(scratch + 8u), 0, 0);
#endif
  return result ? WORD(scratch + 8u) & mask : 0;
}
uint32_t bb_crt_character_space_entry(int32_t c) {
  return BB_I32(0x48d35c) <= 1 ? character_flags(c) & 8
                               : bb_crt_character_type_entry(c, 8);
}
int32_t bb_crt_character_lower_c(int32_t c, uint32_t scratch) {
  if (!BB_U32(0x48cfc0))
    return c >= 'A' && c <= 'Z' ? (int32_t)((uint32_t)c + 32u) : c;
  if (c < 256 && !(BB_I32(0x48d35c) <= 1 ? character_flags(c) & 1
                                         : bb_crt_character_type_entry(c, 1)))
    return c;
  int32_t count = 1;
  if (character_flags(((uint32_t)c >> 8) & 255) & 0x8000) {
    BYTE(scratch) = (uint32_t)c >> 8;
    BYTE(scratch + 1u) = c;
    BYTE(scratch + 2u) = 0;
    count = 2;
  } else {
    BYTE(scratch) = c;
    BYTE(scratch + 1u) = 0;
  }
#ifdef BB_MODEL_RUNTIME
  int32_t result = BB_GAME_CALL(0x0047c1c0u, 0, BB_U32(0x48cfc0), 0x100,
                                scratch, count, scratch + 4u, 3, 0);
#else
  int32_t result =
      bb_crt_locale_map_entry(BB_U32(0x48cfc0), 0x100, BB_DATA(scratch), count,
                              BB_DATA(scratch + 4u), 3, 0);
#endif
  if (!result)
    return c;
  return result == 1 ? BYTE(scratch + 4u)
                     : BYTE(scratch + 4u) | ((uint32_t)BYTE(scratch + 5u) << 8);
}
#ifndef BB_MODEL_RUNTIME
uint32_t bb_crt_character_type_entry(int32_t c, uint32_t mask) {
  uint32_t scratch[3];
  return bb_crt_character_type_c(c, mask, data_location(scratch));
}
int32_t bb_crt_character_lower_entry(int32_t c) {
  uint32_t scratch[3];
  return bb_crt_character_lower_c(c, data_location(scratch));
}
#endif
int32_t bb_crt_string_case_compare_entry(const char *left, const char *right) {
  uintptr_t a = data_location(left), b = data_location(right);
  if (!BB_U32(0x48cfc0)) {
    for (;;) {
      uint32_t x = BYTE(a), y = BYTE(b);
      ++a;
      ++b;
      if (x != y) {
        if (x >= 'A' && x <= 'Z')
          x += 32;
        if (y >= 'A' && y <= 'Z')
          y += 32;
        if (x != y)
          return x < y ? -1 : 1;
      }
      if (!x)
        return 0;
    }
  }
  uint32_t x = 0, y = 255;
  while ((uint8_t)y) {
    x = (x & 0xffffff00u) | BYTE(a++);
    y = (y & 0xffffff00u) | BYTE(b++);
    if ((uint8_t)x != (uint8_t)y) {
      x = bb_crt_character_lower_entry(x);
      y = bb_crt_character_lower_entry(y);
      if ((uint8_t)x != (uint8_t)y)
        return (uint8_t)x < (uint8_t)y ? -1 : 1;
    }
  }
  return y;
}
int32_t bb_crt_string_long_entry(const char *text) {
  uintptr_t s = data_location(text);
  while (bb_crt_character_space_entry(BYTE(s)))
    ++s;
  uint32_t sign = BYTE(s++), c = sign, result = 0;
  if (sign == '-' || sign == '+')
    c = BYTE(s++);
  for (;;) {
    uint32_t digit = BB_I32(0x48d35c) <= 1 ? character_flags(c) & 4
                                           : bb_crt_character_type_entry(c, 4);
    if (!digit)
      break;
    result = result * 10 + c - '0';
    c = BYTE(s++);
  }
  return sign == '-' ? 0u - result : result;
}
int32_t bb_crt_string_int_entry(const char *s) {
  return bb_crt_string_long_entry(s);
}
void *bb_crt_memory_copy_entry(void *out, const void *in, uint32_t n) {
  uintptr_t d = data_location(out), s = data_location(in);
  if (s < d && d < s + n) {
    d += n;
    s += n;
    while (n && (d & 3)) {
      BYTE(--d) = BYTE(--s);
      --n;
    }
    while (n >= 4) {
      d -= 4;
      s -= 4;
      BB_U32(d) = BB_U32(s);
      n -= 4;
    }
    while (n--)
      BYTE(--d) = BYTE(--s);
  } else {
    while (n && (d & 3)) {
      BYTE(d++) = BYTE(s++);
      --n;
    }
    while (n >= 4) {
      BB_U32(d) = BB_U32(s);
      d += 4;
      s += 4;
      n -= 4;
    }
    while (n--)
      BYTE(d++) = BYTE(s++);
  }
  return out;
}
void *bb_crt_memory_move_entry(void *out, const void *in, uint32_t n) {
  return bb_crt_memory_copy_entry(out, in, n);
}
void *bb_crt_memory_fill_entry(void *out, int32_t value, uint32_t n) {
  uintptr_t p = data_location(out);
  uint32_t word = (uint8_t)value * 0x01010101u;
  while (n && (p & 3)) {
    BYTE(p++) = value;
    --n;
  }
  while (n >= 4) {
    BB_U32(p) = word;
    p += 4;
    n -= 4;
  }
  while (n--)
    BYTE(p++) = value;
  return out;
}
uint32_t bb_crt_string_length_entry(const char *s) {
  uintptr_t p = data_location(s);
  while (BYTE(p))
    ++p;
  return p - data_location(s);
}
char *bb_crt_string_copy_entry(char *out, const char *in) {
  uintptr_t d = data_location(out), s = data_location(in);
  while (s & 3) {
    uint8_t c = BYTE(s++);
    BYTE(d++) = c;
    if (!c)
      return out;
  }
  for (;;) {
    uint32_t word = BB_U32(s);
    s += 4;
    if (!(uint8_t)word) {
      BYTE(d) = 0;
      return out;
    }
    if (!(uint8_t)(word >> 8)) {
      WORD(d) = word;
      return out;
    }
    if (!(uint8_t)(word >> 16)) {
      WORD(d) = word;
      BYTE(d + 2) = 0;
      return out;
    }
    BB_U32(d) = word;
    if (!(uint8_t)(word >> 24))
      return out;
    d += 4;
  }
}
char *bb_crt_string_append_entry(char *out, const char *in) {
  bb_crt_string_copy_entry((char *)(uintptr_t)(data_location(out) +
                                               bb_crt_string_length_entry(out)),
                           in);
  return out;
}
char *bb_crt_string_copy_count_entry(char *out, const char *in, uint32_t n) {
  uintptr_t d = data_location(out), s = data_location(in);
  while (n && (s & 3)) {
    uint8_t c = BYTE(s++);
    BYTE(d++) = c;
    --n;
    if (!c) {
      bb_crt_memory_fill_entry((void *)(uintptr_t)d, 0, n);
      return out;
    }
  }
  while (n >= 4) {
    uint32_t word = BB_U32(s), ended = 1;
    if (!(uint8_t)word)
      word = 0;
    else if (!(uint8_t)(word >> 8))
      word &= 255;
    else if (!(uint8_t)(word >> 16))
      word &= 65535;
    else if ((uint8_t)(word >> 24))
      ended = 0;
    BB_U32(d) = word;
    d += 4;
    s += 4;
    n -= 4;
    if (ended) {
      bb_crt_memory_fill_entry((void *)(uintptr_t)d, 0, n);
      return out;
    }
  }
  while (n) {
    uint8_t c = BYTE(s++);
    BYTE(d++) = c;
    --n;
    if (!c)
      break;
  }
  bb_crt_memory_fill_entry((void *)(uintptr_t)d, 0, n);
  return out;
}
int32_t bb_crt_string_compare_entry(const char *left, const char *right) {
  uintptr_t a = data_location(left), b = data_location(right);
  for (;;) {
    uint32_t x = BYTE(a++), y = BYTE(b++);
    if (x != y)
      return x < y ? -1 : 1;
    if (!x)
      return 0;
  }
}
int32_t bb_crt_string_compare_count_entry(const char *left, const char *right,
                                          uint32_t n) {
  uintptr_t a = data_location(left), b = data_location(right);
  while (n--) {
    uint32_t x = BYTE(a++), y = BYTE(b++);
    if (x != y)
      return x < y ? -1 : 1;
    if (!x)
      break;
  }
  return 0;
}
/* Keep a direct native caller in its own pointer space; model workers pass
 * logical references and encode the result into one four-byte word. */
static char *string_result(const void *input, uintptr_t reference) {
  if (!reference)
    return NULL;
  return (uintptr_t)input > UINT32_MAX ? BB_DATA(reference)
                                       : (char *)(uintptr_t)reference;
}
char *bb_crt_string_find_entry(const char *input, int32_t value) {
  uintptr_t s = data_location(input);
  for (;;) {
    uint8_t c = BYTE(s);
    if (c == (uint8_t)value)
      return string_result(input, s);
    if (!c)
      return NULL;
    ++s;
  }
}
char *bb_crt_string_find_last_entry(const char *input, int32_t value) {
  uintptr_t s = data_location(input), last = 0;
  do {
    if (BYTE(s) == (uint8_t)value)
      last = s;
  } while (BYTE(s++));
  return string_result(input, last);
}
char *bb_crt_string_search_entry(const char *input, const char *text) {
  uintptr_t s = data_location(input), needle = data_location(text);
  if (!BYTE(needle))
    return (char *)input;
  if (!BYTE(needle + 1u))
    return bb_crt_string_find_entry(input, BYTE(needle));
  while (BYTE(s)) {
    uint32_t n = 0;
    while (BYTE(needle + n) && BYTE(s + n) == BYTE(needle + n))
      ++n;
    if (!BYTE(needle + n))
      return string_result(input, s);
    ++s;
  }
  return NULL;
}
uint32_t bb_crt_string_count_entry(const char *s, uint32_t n) {
  uintptr_t p = data_location(s);
  uint32_t remaining = n;
  while (remaining && BYTE(p)) {
    ++p;
    --remaining;
  }
  return BYTE(p) ? n : p - data_location(s);
}
uint32_t bb_crt_string_count_alt_entry(const char *s, uint32_t n) {
  return bb_crt_string_count_entry(s, n);
}
uint32_t bb_crt_wide_length_entry(const uint16_t *s) {
  uintptr_t p = data_location(s);
  do {
    p += 2;
  } while (WORD(p - 2));
  return ((int32_t)(p - data_location(s)) >> 1) - 1;
}
uint32_t bb_crt_wide_count_entry(const uint16_t *s, uint32_t n) {
  uintptr_t p = data_location(s);
  uint32_t remaining = n;
  while (remaining && WORD(p)) {
    p += 2;
    --remaining;
  }
  return remaining && !WORD(p)
             ? (uint32_t)(((int32_t)(p - data_location(s)) >> 1) + 1)
             : n;
}
