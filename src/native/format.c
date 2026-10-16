/* SPDX-License-Identifier: GPL-3.0-only */
#include "format.h"
#include "native.h"
#include "../bridge/address.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static uint32_t word(uint32_t *cursor) {
  uint32_t value = BB_U32(*cursor);
  *cursor += 4;
  return value;
}
static uint64_t wide_word(uint32_t *cursor) {
  uint64_t low = word(cursor);
  return low | ((uint64_t)word(cursor) << 32);
}
static int decimal(const char **text) {
  unsigned value = 0;
  while (**text >= '0' && **text <= '9') {
    unsigned digit = (unsigned)*(*text)++ - '0';
    if (value > (INT_MAX - digit) / 10u)
      bb_die("packed format width/precision overflow");
    value = value * 10u + digit;
  }
  return (int)value;
}
int bb_packed_sprintf(char *out, const char *format, uint32_t arguments) {
  int count = 0;
  while (*format) {
    if (*format != '%') {
      out[count++] = *format++;
      continue;
    }
    ++format;
    if (*format == '%') {
      out[count++] = *format++;
      continue;
    }
    /* Normalize widths/precision and Windows' 32-bit long before asking libc
     * to format one correctly typed value. Never pass the packed cursor as a
     * native va_list: x86-64 and arm64 have different variadic ABIs. */
    char spec[24] = "%";
    unsigned used = 1;
    while (*format && strchr("-+ #0'", *format)) {
      if (!memchr(spec + 1, *format, used - 1))
        spec[used++] = *format;
      ++format;
    }
    int width;
    if (*format == '*') {
      ++format;
      width = (int32_t)word(&arguments);
    } else
      width = decimal(&format);
    int precision = -1;
    if (*format == '.') {
      ++format;
      if (*format == '*') {
        ++format;
        precision = (int32_t)word(&arguments);
      } else
        precision = decimal(&format);
    }
    enum { NORMAL, SHORT, BYTE, LONG, WIDE, WCHAR } size = NORMAL;
    if (*format == 'h') {
      size = SHORT;
      if (*++format == 'h') {
        size = BYTE;
        ++format;
      }
    } else if (*format == 'l') {
      size = LONG;
      if (*++format == 'l') {
        size = WIDE;
        ++format;
      }
    } else if (!strncmp(format, "I64", 3)) {
      size = WIDE;
      format += 3;
    } else if (*format == 'w') {
      size = WCHAR;
      ++format;
    } else if (*format == 'z' || *format == 't' || *format == 'j') {
      /* The old shim has four-byte size_t/ptrdiff_t, eight-byte intmax_t. */
      size = *format++ == 'j' ? WIDE : LONG;
    }
    char conversion = *format++;
    if (!conversion)
      bb_die("unterminated packed format");
    spec[used++] = '*';
    spec[used++] = '.';
    spec[used++] = '*';
    int result;
/* The legacy API has no capacity argument. Preserve its int-sized output
 * contract without using macOS' deprecated sprintf entry. */
#define FORMAT(value)                                                         \
  snprintf(out + count, (size_t)(INT_MAX - count), spec, width, precision, value)
    if (strchr("diuoxX", conversion)) {
      if (size == WIDE) {
        spec[used++] = 'l';
        spec[used++] = 'l';
      }
      spec[used++] = conversion;
      spec[used] = 0;
      if (conversion == 'd' || conversion == 'i') {
        if (size == WIDE)
          result = FORMAT((long long)(int64_t)wide_word(&arguments));
        else {
          int32_t value = (int32_t)word(&arguments);
          if (size == SHORT)
            value = (int16_t)value;
          if (size == BYTE)
            value = (int8_t)value;
          result = FORMAT((int)value);
        }
      } else if (size == WIDE)
        result = FORMAT((unsigned long long)wide_word(&arguments));
      else {
        uint32_t value = word(&arguments);
        if (size == SHORT)
          value = (uint16_t)value;
        if (size == BYTE)
          value = (uint8_t)value;
        result = FORMAT((unsigned)value);
      }
    } else if (strchr("aAeEfFgG", conversion)) {
      uint64_t bits = wide_word(&arguments);
      double value;
      memcpy(&value, &bits, sizeof value);
      spec[used++] = conversion;
      spec[used] = 0;
      result = FORMAT(value);
    } else if (strchr("cCsS", conversion)) {
      unsigned wide = size == LONG || size == WCHAR ||
                      conversion == 'C' || conversion == 'S';
      if (wide)
        spec[used++] = 'l';
      conversion = conversion == 'C' ? 'c' : conversion == 'S' ? 's' : conversion;
      spec[used++] = conversion;
      spec[used] = 0;
      uint32_t value = word(&arguments);
      if (conversion == 'c')
        result = wide ? FORMAT((wint_t)value) : FORMAT((int)value);
      else {
        void *text = value ? BB_DATA(value) : NULL;
        result = wide ? FORMAT((const wchar_t *)text)
                      : FORMAT((const char *)text);
      }
    } else if (conversion == 'p') {
      spec[used++] = 'p';
      spec[used] = 0;
      result = FORMAT((void *)(uintptr_t)word(&arguments));
    } else if (conversion == 'n') {
      uint32_t destination = word(&arguments);
      if (size == BYTE)
        BB_U8(destination) = count;
      else if (size == SHORT)
        BB_U16(destination) = count;
      else if (size == WIDE)
        BB_U64(destination) = count;
      else
        BB_U32(destination) = count;
      result = 0;
    } else
      bb_die("unsupported packed format conversion: %c", conversion);
#undef FORMAT
    if (result < 0) {
      out[count] = 0;
      return result;
    }
    if (result > INT_MAX - count)
      bb_die("packed format output overflow");
    count += result;
  }
  out[count] = 0;
  return count;
}
