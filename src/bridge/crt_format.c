/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_format.h"
#include "crt_numeric.h"
#include "crt_primitives.h"
#include "interop.h"
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
static int negative(uintptr_t record) { return BB_U32(record) == '-'; }
static int32_t position(uintptr_t record) { return BB_I32(record + 4); }
static uintptr_t digits(const void *number) {
  return (uintptr_t)bb_crt_numeric_decimal_entry(BB_U32(number),
                                                 BB_U32((uintptr_t)number + 4));
}
void bb_crt_format_initialize_entry(void) {
  /* Preserve callback addresses while existing binary/data consumers remain;
   * all six entries resolve to checked C replacements. */
  BB_U32(0x48cf90) = 0x4728a0;
  BB_U32(0x48cf94) = 0x472220;
  BB_U32(0x48cf98) = 0x472340;
  BB_U32(0x48cf9c) = 0x472160;
  BB_U32(0x48cfa0) = 0x472300;
  BB_U32(0x48cfa4) = 0x4728a0;
}
void bb_crt_format_decimal_entry(char *text) {
  uintptr_t p = (uintptr_t)text;
  if (bb_crt_character_lower_entry((int8_t)BYTE(p)) != 'e') {
    uint32_t numeric;
    do {
      ++p;
      int32_t c = (int8_t)BYTE(p);
      numeric =
          BB_I32(0x48d35c) <= 1
              ? WORD((uintptr_t)BB_POINTER(0x48d150) + (uint32_t)c * 2) & 4
              : bb_crt_character_type_entry(c, 4);
    } while (numeric);
  }
  uint8_t carried = BYTE(p);
  BYTE(p) = BYTE(0x48d360);
  do {
    ++p;
    uint8_t next = BYTE(p);
    BYTE(p) = carried;
    carried = next;
  } while (BYTE(p));
}
void bb_crt_format_trim_entry(char *text) {
  uintptr_t p = (uintptr_t)text;
  while (BYTE(p) && BYTE(p) != BYTE(0x48d360))
    ++p;
  if (!BYTE(p))
    return;
  uintptr_t source = p + 1;
  while (BYTE(source) && BYTE(source) != 'e' && BYTE(source) != 'E')
    ++source;
  p = source - 1;
  while (BYTE(p) == '0')
    --p;
  if (BYTE(p) == BYTE(0x48d360))
    --p;
  do {
    ++p;
    BYTE(p) = BYTE(source++);
  } while (BYTE(p));
}
uint32_t bb_crt_format_positive_entry(const double *number) {
#ifdef BB_MODEL_RUNTIME
  return BB_DOUBLE(number) >= BB_DOUBLE(0x482f98);
#else
  uint16_t status;
  /* Retain the x87 comparison's invalid/condition flags, including NaNs,
   * at the binary interface; the C result uses the original carry test. */
  __asm__ volatile("fldl %1;fcompl %2;fnstsw %0"
                   : "=a"(status)
                   : "m"(*number), "m"(BB_DOUBLE(0x482f98))
                   : "st");
  return !(status & 0x100);
#endif
}
void bb_crt_format_shift_entry(char *text, uint32_t amount) {
  if (amount)
    bb_crt_memory_move_entry((void *)((uintptr_t)text + amount), text,
                             bb_crt_string_length_entry(text) + 1);
}
char *bb_crt_format_scientific_entry(const void *number, char *out,
                                     int32_t precision, int32_t capital) {
  uintptr_t record, p = (uintptr_t)out;
  if (!BYTE(0x48cfa8)) {
    record = digits(number);
    bb_crt_numeric_round_text_entry(
        (char *)(p + negative(record) + (precision > 0)),
        (uint32_t)precision + 1, (void *)record);
  } else {
    record = BB_U32(0x6c8884);
    bb_crt_format_shift_entry((char *)(p + negative(record)), precision > 0);
  }
  if (negative(record))
    BYTE(p++) = '-';
  if (precision > 0) {
    BYTE(p) = BYTE(p + 1);
    BYTE(++p) = BYTE(0x48d360);
  }
  uintptr_t exponent = p + (uint32_t)precision + !BYTE(0x48cfa8);
  bb_crt_string_copy_entry((char *)exponent, (const char *)0x482fa0);
  if (capital)
    BYTE(exponent) = 'E';
  if (BYTE(BB_U32(record + 12)) != '0') {
    int32_t value = (uint32_t)position(record) - 1;
    if (value < 0) {
      value = 0u - (uint32_t)value;
      BYTE(exponent + 1) = '-';
    }
    if (value > 99) {
      BYTE(exponent + 2) += (uint8_t)(value / 100);
      value %= 100;
    }
    if (value > 9) {
      BYTE(exponent + 3) += (uint8_t)(value / 10);
      value %= 10;
    }
    BYTE(exponent + 4) += (uint8_t)value;
  }
  return out;
}
char *bb_crt_format_fixed_entry(const void *number, char *out,
                                int32_t precision) {
  uintptr_t record, p = (uintptr_t)out;
  if (!BYTE(0x48cfa8)) {
    record = digits(number);
    bb_crt_numeric_round_text_entry(
        (char *)(p + negative(record)),
        (uint32_t)position(record) + (uint32_t)precision, (void *)record);
  } else {
    record = BB_U32(0x6c8884);
    if (precision == BB_I32(0x48cfac)) {
      uintptr_t end = p + negative(record) + (uint32_t)BB_I32(0x48cfac);
      BYTE(end) = '0';
      BYTE(end + 1) = 0;
    }
  }
  if (negative(record))
    BYTE(p++) = '-';
  if (position(record) < 1) {
    bb_crt_format_shift_entry((char *)p, 1);
    BYTE(p++) = '0';
  } else
    p += (uint32_t)position(record);
  if (precision > 0) {
    bb_crt_format_shift_entry((char *)p, 1);
    BYTE(p++) = BYTE(0x48d360);
    if (position(record) < 0) {
      uint32_t count = 0u - (uint32_t)position(record);
      if (!BYTE(0x48cfa8) && precision <= (int32_t)count)
        count = precision;
      bb_crt_format_shift_entry((char *)p, count);
      bb_crt_memory_fill_entry((void *)p, '0', count);
    }
  }
  return out;
}
char *bb_crt_format_scientific_cached_entry(const void *number, char *out,
                                            int32_t precision,
                                            int32_t capital) {
  BYTE(0x48cfa8) = 1;
  char *result =
      bb_crt_format_scientific_entry(number, out, precision, capital);
  BYTE(0x48cfa8) = 0;
  return result;
}
char *bb_crt_format_fixed_cached_entry(const void *number, char *out,
                                       int32_t precision) {
  BYTE(0x48cfa8) = 1;
  char *result = bb_crt_format_fixed_entry(number, out, precision);
  BYTE(0x48cfa8) = 0;
  return result;
}
char *bb_crt_format_general_entry(const void *number, char *out,
                                  int32_t precision, int32_t capital) {
  BB_U32(0x6c8884) = bb_address_encode((void *)digits(number));
  BB_U32(0x48cfac) = (uint32_t)position(BB_U32(0x6c8884)) - 1;
  char *start = out + negative(BB_U32(0x6c8884));
  bb_crt_numeric_round_text_entry(start, precision, BB_POINTER(0x6c8884));
  BYTE(0x48cfb0) =
      BB_I32(0x48cfac) < (int32_t)((uint32_t)position(BB_U32(0x6c8884)) - 1);
  BB_U32(0x48cfac) = (uint32_t)position(BB_U32(0x6c8884)) - 1;
  if (BB_I32(0x48cfac) < -4 || precision <= BB_I32(0x48cfac))
    return bb_crt_format_scientific_cached_entry(number, out, precision,
                                                 capital);
  if (BYTE(0x48cfb0)) {
    uintptr_t end = (uintptr_t)start;
    do {
      ++end;
    } while (BYTE(end - 1));
    BYTE(end - 2) = 0;
  }
  return bb_crt_format_fixed_cached_entry(number, out, precision);
}
char *bb_crt_format_number_entry(const void *number, char *out, uint32_t format,
                                 int32_t precision, int32_t capital) {
  if (format == 'e' || format == 'E')
    return bb_crt_format_scientific_entry(number, out, precision, capital);
  if (format == 'f')
    return bb_crt_format_fixed_entry(number, out, precision);
  return bb_crt_format_general_entry(number, out, precision, capital);
}
