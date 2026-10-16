/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_output.h"
#include "crt_errors.h"
#include "crt_locale.h"
#include "crt_primitives.h"
#include "crt_stream.h"
#include "interop.h"
#include "game_calls.h"
#include <signal.h>
#ifdef BB_MODEL_RUNTIME
#include "model_buffer.h"
#endif
#include <stdarg.h>
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
static int32_t flush(int32_t ch, void *stream) {
  return bb_crt_stream_write_char_entry(ch, stream);
}
static void assertion(uintptr_t file, uint32_t line, uintptr_t expression) {
#ifdef BB_MODEL_RUNTIME
  if (BB_GAME_CALL(0x00470ef0u, 0, 2, (uint32_t)file, line, 0,
                    (uint32_t)expression) == 1)
    raise(SIGTRAP);
#else
  if (bb_crt_errors_report_entry(2, (const char *)file, line, NULL,
                                   (const char *)expression) == 1)
    __asm__ volatile("int3");
#endif
}
uint32_t bb_crt_argument_int_entry(uint32_t *position) {
  uintptr_t p = (uintptr_t)position;
  BB_U32(p) += 4;
  return BB_U32(BB_U32(p) - 4);
}
uint64_t bb_crt_argument_wide_entry(uint32_t *position) {
  uintptr_t p = (uintptr_t)position;
  BB_U32(p) += 8;
  uintptr_t source = BB_U32(p) - 8;
  return BB_U32(source) | ((uint64_t)BB_U32(source + 4) << 32);
}
uint32_t bb_crt_argument_short_entry(uint32_t *position) {
  uintptr_t p = (uintptr_t)position;
  BB_U32(p) += 4;
  return (BB_U32(p) & 0xffff0000u) | WORD(BB_U32(p) - 4);
}
void bb_crt_write_char_entry(int32_t ch, void *stream, uint32_t *count) {
  uintptr_t p = (uintptr_t)stream, n = (uintptr_t)count;
  uint32_t result;
  --BB_U32(p + 4);
  if (BB_I32(p + 4) < 0)
    result = flush(ch, stream);
  else {
    BYTE(BB_U32(p)) = ch;
    result = BYTE(BB_U32(p));
    ++BB_U32(p);
  }
  if (result == UINT32_MAX)
    BB_U32(n) = UINT32_MAX;
  else
    ++BB_U32(n);
}
void bb_crt_write_repeat_entry(int32_t ch, int32_t length, void *stream,
                               uint32_t *count) {
  while (length > 0) {
    bb_crt_write_char_entry(ch, stream, count);
    length = (uint32_t)length - 1;
    if (BB_U32((uintptr_t)count) == UINT32_MAX)
      return;
  }
}
void bb_crt_write_string_entry(const char *text, int32_t length, void *stream,
                               uint32_t *count) {
  uintptr_t p = (uintptr_t)text;
  while (length > 0) {
    bb_crt_write_char_entry((int8_t)BYTE(p), stream, count);
    ++p;
    length = (uint32_t)length - 1;
    if (BB_U32((uintptr_t)count) == UINT32_MAX)
      return;
  }
}
/* The packed original table combines character classes and transitions.
 * This independent C table retains all eight parser states, including invalid
 * specifiers returning to literal output and repeated size modifiers. */
static unsigned character_class(uint8_t c) {
  if (c == '%')
    return 1;
  if (c == '.')
    return 2;
  if (c == '*')
    return 3;
  if (c == '0')
    return 4;
  if (c >= '1' && c <= '9')
    return 5;
  switch (c) {
  case ' ':
  case '#':
  case '+':
  case '-':
    return 6;
  case 'F':
  case 'I':
  case 'L':
  case 'N':
  case 'h':
  case 'l':
  case 'w':
    return 7;
  case 'B':
  case 'C':
  case 'E':
  case 'G':
  case 'S':
  case 'X':
  case 'Z':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
  case 'g':
  case 'i':
  case 'n':
  case 'o':
  case 'p':
  case 's':
  case 'u':
  case 'x':
    return 8;
  default:
    return 0;
  }
}
static const uint8_t states[9][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 1},
    {0, 4, 4, 4, 0, 0, 0, 0}, {0, 3, 3, 0, 5, 0, 0, 0},
    {0, 2, 2, 3, 5, 5, 0, 0}, {0, 3, 3, 3, 5, 5, 0, 0},
    {0, 2, 2, 0, 0, 0, 0, 0}, {0, 6, 6, 6, 6, 6, 6, 0},
    {0, 7, 7, 7, 7, 7, 7, 0}};
static int32_t wide_character(char *out, uint32_t c) {
  return bb_crt_locale_wctomb_entry(out, c);
}
static uint32_t length(const char *p) { return bb_crt_string_length_entry(p); }
static int32_t output(void *stream, uintptr_t format, uint32_t *arguments) {
  uint32_t count = 0, flags = 0, width = 0, prefix_length = 0;
  int32_t precision = -1;
  unsigned state = 0, wide = 0, skip = 0, capital = 0;
  char buffer[512], prefix[2], converted[4];
  uintptr_t text = 0;
  uint32_t text_length = 0;
  for (;;) {
    uint8_t c = BYTE(BB_U32(format));
    ++BB_U32(format);
    if (!c || (int32_t)count < 0)
      return count;
    state = states[character_class(c)][state];
    switch (state) {
    case 0:
    literal:
      wide = 0;
      if (WORD(BB_U32(0x48d150) + c * 2) & 0x8000) {
        bb_crt_write_char_entry((int8_t)c, stream, &count);
        c = BYTE(BB_U32(format));
        ++BB_U32(format);
        if (!c)
          assertion(0x482ccc, 0x185, 0x482cd8);
      }
      bb_crt_write_char_entry((int8_t)c, stream, &count);
      break;
    case 1:
      capital = skip = width = prefix_length = flags = wide = 0;
      precision = -1;
      break;
    case 2:
      if (c == ' ')
        flags |= 2;
      else if (c == '#')
        flags |= 0x80;
      else if (c == '+')
        flags |= 1;
      else if (c == '-')
        flags |= 4;
      else if (c == '0')
        flags |= 8;
      break;
    case 3:
      if (c == '*') {
        width = bb_crt_argument_int_entry(arguments);
        if ((int32_t)width < 0) {
          flags |= 4;
          width = 0u - width;
        }
      } else
        width = width * 10 + c - '0';
      break;
    case 4:
      precision = 0;
      break;
    case 5:
      if (c == '*') {
        precision = bb_crt_argument_int_entry(arguments);
        if (precision < 0)
          precision = -1;
      } else
        precision = (uint32_t)precision * 10 + c - '0';
      break;
    case 6:
      if (c == 'I') {
        if (BYTE(BB_U32(format)) != '6' || BYTE(BB_U32(format) + 1) != '4') {
          state = 0;
          goto literal;
        }
        flags |= 0x8000;
        BB_U32(format) += 2;
      } else if (c == 'h')
        flags |= 0x20;
      else if (c == 'l')
        flags |= 0x10;
      else if (c == 'w')
        flags |= 0x800;
      break;
    case 7: {
      unsigned radix = 0, letters = 0;
      switch (c) {
      case 'C':
        if (!(flags & 0x830))
          flags |= 0x800;
        /* fall through */
      case 'c':
        text = (uintptr_t)buffer;
        if (!(flags & 0x810)) {
          buffer[0] = bb_crt_argument_int_entry(arguments);
          text_length = 1;
        } else {
          int32_t n =
              wide_character(buffer, bb_crt_argument_short_entry(arguments));
          text_length = n;
          if (n < 0)
            skip = 1;
        }
        break;
      case 'E':
      case 'G':
        capital = 1;
        c += 32;
        /* fall through */
      case 'e':
      case 'f':
      case 'g': {
        flags |= 0x40;
        text = (uintptr_t)buffer;
        if (precision < 0)
          precision = 6;
        else if (!precision && c == 'g')
          precision = 1;
        uint32_t number[2];
        number[0] = bb_crt_argument_int_entry(arguments);
        number[1] = bb_crt_argument_int_entry(arguments);
#ifdef BB_MODEL_RUNTIME
        struct bb_model_scratch scratch;
        bb_model_scratch_begin(&scratch, sizeof number + sizeof buffer);
        uint32_t packed_number = scratch.address, packed_text = packed_number + 8;
        BB_U32(packed_number) = number[0];
        BB_U32(packed_number + 4) = number[1];
        BB_GAME_CALL(BB_U32(0x48cf90), 0, packed_number, packed_text,
                      (uint32_t)(int32_t)(int8_t)c, precision, capital);
        if ((flags & 0x80) && !precision)
          BB_GAME_CALL(BB_U32(0x48cf9c), 0, packed_text);
        if (c == 'g' && !(flags & 0x80))
          BB_GAME_CALL(BB_U32(0x48cf94), 0, packed_text);
        memcpy(buffer, BB_SPAN(packed_text, sizeof buffer), sizeof buffer);
        bb_model_scratch_end(&scratch);
#else
        ((void (*)(const void *, char *, int32_t, int32_t, uint32_t))BB_CODE(BB_POINTER(
            0x48cf90)))(number, buffer, (int8_t)c, precision, capital);
        if ((flags & 0x80) && !precision)
          ((void (*)(char *))BB_CODE(BB_POINTER(0x48cf9c)))(buffer);
        if (c == 'g' && !(flags & 0x80))
          ((void (*)(char *))BB_CODE(BB_POINTER(0x48cf94)))(buffer);
#endif
        if (BYTE(text) == '-') {
          flags |= 0x100;
          ++text;
        }
        text_length = length((const char *)text);
        break;
      }
      case 'S':
        if (!(flags & 0x830))
          flags |= 0x800;
        /* fall through */
      case 's': {
        uint32_t limit = precision == -1 ? 0x7fffffffu : (uint32_t)precision;
        text = bb_crt_argument_int_entry(arguments);
        if (!(flags & 0x810)) {
          if (!text)
            text = BB_U32(0x48ccc0);
          uintptr_t p = text;
          while (limit && BYTE(p)) {
            --limit;
            ++p;
          }
          text_length = p - text;
        } else {
          if (!text)
            text = BB_U32(0x48ccc4);
          wide = 1;
          uintptr_t p = text;
          while (limit && WORD(p)) {
            --limit;
            p += 2;
          }
          text_length = (p - text) >> 1;
        }
        break;
      }
      case 'Z': {
        uintptr_t descriptor = bb_crt_argument_int_entry(arguments);
        if (!descriptor || !BB_U32(descriptor + 4)) {
          text = BB_U32(0x48ccc0);
          text_length = length((const char *)text);
        } else {
          text = BB_U32(descriptor + 4);
          text_length = (uint32_t)(int32_t)(int16_t)WORD(descriptor);
          wide = !!(flags & 0x800);
          if (wide)
            text_length >>= 1;
        }
        break;
      }
      case 'n': {
        uintptr_t destination = bb_crt_argument_int_entry(arguments);
        if (flags & 0x20)
          WORD(destination) = count;
        else
          BB_U32(destination) = count;
        skip = 1;
        break;
      }
      case 'd':
      case 'i':
        flags |= 0x40;
        radix = 10;
        break;
      case 'o':
        radix = 8;
        if (flags & 0x80)
          flags |= 0x200;
        break;
      case 'p':
        precision = 8;
        /* fall through */
      case 'X':
        letters = 7;
        radix = 16;
        break;
      case 'x':
        letters = 39;
        radix = 16;
        break;
      case 'u':
        radix = 10;
        break;
      }
      if (radix) {
        if (radix == 16 && (flags & 0x80)) {
          prefix[0] = '0';
          prefix[1] = 'Q' + letters;
          prefix_length = 2;
        }
        uint64_t number;
        if (flags & 0x8000)
          number = bb_crt_argument_wide_entry(arguments);
        else {
          uint32_t v = bb_crt_argument_int_entry(arguments);
          if (flags & 0x20)
            v = flags & 0x40 ? (uint32_t)(int32_t)(int16_t)v : v & 0xffff;
          number = flags & 0x40 ? (uint64_t)(int64_t)(int32_t)v : v;
        }
        if ((flags & 0x40) && (int64_t)number < 0) {
          flags |= 0x100;
          number = 0u - number;
        }
        if (!(flags & 0x8000))
          number &= UINT32_MAX;
        if (precision < 0)
          precision = 1;
        else
          flags &= ~8u;
        if (!number)
          prefix_length = 0;
        char *end = buffer + sizeof(buffer) - 1, *p = end;
        while (precision > 0 || number) {
          precision = (uint32_t)precision - 1;
          unsigned digit = number % radix;
          number /= radix;
          *p-- = digit < 10 ? digit + '0' : digit + '0' + letters;
        }
        text_length = end - p;
        text = (uintptr_t)(p + 1);
        if ((flags & 0x200) && (!text_length || BYTE(text) != '0')) {
          *p = '0';
          ++text_length;
          text = (uintptr_t)p;
        }
      }
      if (!skip) {
        if (flags & 0x40) {
          if (flags & 0x100) {
            prefix[0] = '-';
            prefix_length = 1;
          } else if (flags & 1) {
            prefix[0] = '+';
            prefix_length = 1;
          } else if (flags & 2) {
            prefix[0] = ' ';
            prefix_length = 1;
          }
        }
        int32_t padding = width - text_length - prefix_length;
        if (!(flags & 12))
          bb_crt_write_repeat_entry(' ', padding, stream, &count);
        bb_crt_write_string_entry(prefix, prefix_length, stream, &count);
        if ((flags & 8) && !(flags & 4))
          bb_crt_write_repeat_entry('0', padding, stream, &count);
        if (!wide || (int32_t)text_length < 1)
          bb_crt_write_string_entry((const char *)text, text_length, stream,
                                    &count);
        else {
          uintptr_t p = text;
          for (uint32_t n = text_length; n; --n, p += 2) {
            int32_t bytes = wide_character(converted, WORD(p));
            if (bytes < 1)
              break;
            bb_crt_write_string_entry(converted, bytes, stream, &count);
          }
        }
        if (flags & 4)
          bb_crt_write_repeat_entry(' ', padding, stream, &count);
      }
      break;
    }
    }
  }
}
/* Retain the original updates of the two caller argument slots while old CRT
 * callers still pass format and va-list cursors on their stack. */
void __attribute__((used, noinline)) bb_crt_output_c(uint32_t *r) {
  uintptr_t args = r[3] + 8;
  r[7] =
      output(BB_POINTER(args), args + 4, (uint32_t *)(args + 8));
}
#ifdef _WIN32
#define BB_ASM(n) "_" #n
#else
#define BB_ASM(n) #n
#endif
#ifndef BB_MODEL_RUNTIME
int32_t __attribute__((naked, noinline))
bb_crt_output_entry(void *stream __attribute__((unused)),
                    const char *format __attribute__((unused)),
                    const void *arguments __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(
      bb_crt_output_c) ";addl $4,%esp;popal;popfl;ret");
}
#endif
static int32_t string_output(char *destination, uint32_t capacity,
                             const char *format, const void *arguments,
                             unsigned vector) {
  if (!destination)
    assertion(vector ? 0x483444 : 0x482484, vector ? 0x5a : 0x5d, 0x482490);
  if (!format)
    assertion(vector ? 0x483444 : 0x482484, vector ? 0x5b : 0x5e, 0x482474);
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffer, pattern;
  uint32_t reference;
  bb_model_buffer_begin(&buffer, destination, capacity);
  bb_model_buffer_begin(&pattern, format,
                        bb_address_try_encode(format, &reference)
                            ? 0 : strlen(format) + 1);
  destination = (char *)(uintptr_t)buffer.reference;
  format = (const char *)(uintptr_t)pattern.reference;
#endif
  uint32_t stream[8] = {0}, fmt = bb_address_encode(format),
           va = bb_address_encode(arguments);
  stream[0] = stream[2] = bb_address_encode(destination);
  stream[1] = capacity;
  stream[3] = 0x42;
  int32_t result = output(stream, (uintptr_t)&fmt, &va);
  --stream[1];
  if ((int32_t)stream[1] < 0)
    flush(0, stream);
  else {
    BYTE(stream[0]) = 0;
    ++stream[0];
  }
#ifdef BB_MODEL_RUNTIME
  bb_model_buffer_end(&pattern, 0);
  bb_model_buffer_end(&buffer, 1);
#endif
  return result;
}
/* Explicit packed arguments for internal C callers, including 64-bit hosts.
 * Strings referenced by these words must already have logical identities. */
int32_t bb_crt_output_words(char *destination, uint32_t capacity,
                             const char *format, const uint32_t *arguments,
                             size_t words) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer args;
  bb_model_buffer_begin(&args, arguments, words * sizeof *arguments);
  int32_t result = string_output(destination, capacity, format,
                                  (void *)(uintptr_t)args.reference, 0);
  bb_model_buffer_end(&args, 0);
  return result;
#else
  (void)words;
  return string_output(destination, capacity, format, arguments, 0);
#endif
}
#ifndef BB_MODEL_RUNTIME
int32_t bb_crt_sprintf_entry(char *destination, const char *format, ...) {
  va_list va;
  va_start(va, format);
  int32_t result = string_output(destination, 0x7fffffff, format, va, 0);
  va_end(va);
  return result;
}
int32_t bb_crt_snprintf_entry(char *destination, uint32_t capacity,
                              const char *format, ...) {
  va_list va;
  va_start(va, format);
  int32_t result = string_output(destination, capacity, format, va, 0);
  va_end(va);
  return result;
}
#endif
int32_t bb_crt_vsnprintf_entry(char *destination, uint32_t capacity,
                               const char *format, const void *arguments) {
  return string_output(destination, capacity, format, arguments, 1);
}

#ifdef BB_MODEL_RUNTIME
void bb_crt_sprintf_c(uint32_t *r) {
  uintptr_t a = r[3] + 8;
  r[7] = string_output(BB_POINTER(a), 0x7fffffff, BB_POINTER(a + 4),
                        (void *)(a + 8), 0);
}
void bb_crt_snprintf_c(uint32_t *r) {
  uintptr_t a = r[3] + 8;
  r[7] = string_output(BB_POINTER(a), BB_U32(a + 4), BB_POINTER(a + 8),
                        (void *)(a + 12), 0);
}
#endif
