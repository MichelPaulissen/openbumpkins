/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_input.h"
#include "crt_errors.h"
#include "crt_locale.h"
#include "crt_primitives.h"
#include "crt_stream.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#include "model_buffer.h"
#endif
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
static void assertion(uintptr_t file, uint32_t line, uintptr_t expression) {
#ifdef BB_MODEL_RUNTIME
  if (BB_GAME_CALL(0x470ef0, 0, 2, file, line, 0, expression) == 1)
    bb_crt_errors_break_entry();
#else
  if (bb_crt_errors_report_entry(2, (const char *)file, line, NULL,
                                   (const char *)expression) == 1)
    __asm__ volatile("int3");
#endif
}
static uint32_t classify(uint32_t c, uint32_t mask) {
  if (BB_I32(0x48d35c) > 1)
    return bb_crt_character_type_entry(c, mask);
  return WORD(BB_U32(0x48d150) + c * 2) & mask;
}
static uint32_t space(uint32_t c) { return bb_crt_character_space_entry(c); }
uint32_t bb_crt_scan_hex_entry(uint32_t c) {
  return classify(c, 4) ? c : (c & ~0x20u) - 7;
}
uint32_t bb_crt_scan_read_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  --BB_U32(p + 4);
  if (BB_I32(p + 4) < 0)
    return bb_crt_stream_fill_entry(stream);
  uintptr_t source = BB_U32(p);
  ++BB_U32(p);
  return BYTE(source);
}
void bb_crt_scan_unread_entry(uint32_t c, void *stream) {
  if (c != UINT32_MAX)
    bb_crt_stream_ungetc_entry(c, stream);
}
uint32_t bb_crt_scan_space_entry(uint32_t *count, void *stream) {
  uint32_t c;
  do {
    ++BB_U32((uintptr_t)count);
    c = bb_crt_scan_read_entry(stream);
  } while (space(c));
  return c;
}
struct scan {
  void *stream;
  uint32_t count, ch;
};
static void read_next(struct scan *s) {
  ++s->count;
  s->ch = bb_crt_scan_read_entry(s->stream);
}
static void unread(struct scan *s, uint32_t c) {
  --s->count;
  bb_crt_scan_unread_entry(c, s->stream);
}
static void store_integer(uintptr_t destination, uint32_t value,
                          uint64_t wide_value, uint8_t size, uint32_t wide) {
  if (wide) {
    BB_U32(destination) = wide_value;
    BB_U32(destination + 4) = wide_value >> 32;
  } else if (!size)
    WORD(destination) = value;
  else
    BB_U32(destination) = value;
}
static int32_t __attribute__((noinline)) input(void *stream, uint32_t *format,
                                               uint32_t *arguments,
                                               uint32_t initial,
                                               uint32_t *last) {
  if (!*format)
    assertion(0x482ffc, 0x109, 0x482474);
  if (!stream)
    assertion(0x482ffc, 0x10c, 0x482bf8);
  struct scan s = {.stream = stream, .ch = initial};
  uint32_t assignments = 0;
  uint8_t completed = 0;
  uintptr_t destination = 0;
  uint16_t converted = 0;
  uint8_t multibyte[2] = {0, 0};
  while (BYTE(*format)) {
    if (classify(BYTE(*format), 8)) {
      --s.count;
      uint32_t c = bb_crt_scan_space_entry(&s.count, stream);
      bb_crt_scan_unread_entry(c, stream);
      do {
        ++*format;
      } while (space(BYTE(*format)));
    }
    if (BYTE(*format) == '%') {
      uint32_t value = 0, width = 0, widths = 0, is_wide = 0, digits = 0;
      uint64_t wide_value = 0;
      uint8_t suppress = 0, negative = 0, size = 1, wchar_size = 0;
      uint32_t saved_arguments = *arguments;
      for (;;) {
        ++*format;
        uint32_t c = BYTE(*format);
        if (classify(c, 4)) {
          ++widths;
          width = width * 10 + c - '0';
          continue;
        }
        if (c == '*')
          ++suppress;
        else if (c == 'F' || c == 'N') {
        } else if (c == 'h') {
          --size;
          --wchar_size;
        } else if (c == 'L')
          ++size;
        else if (c == 'l') {
          ++size;
          ++wchar_size;
        } else if (c == 'w')
          ++wchar_size;
        else if (c == 'I' && BYTE(*format + 1) == '6' &&
                 BYTE(*format + 2) == '4') {
          ++is_wide;
          wide_value = 0;
          *format += 2;
        } else
          break;
      }
      if (!suppress) {
        saved_arguments = *arguments;
        *arguments += 4;
        destination = BB_U32(*arguments - 4);
      }
      if (!wchar_size)
        wchar_size = BYTE(*format) == 'S' || BYTE(*format) == 'C' ? 1 : 255;
      uint32_t kind = BYTE(*format) | 0x20u;
      if (kind != 'n') {
        if (kind == 'c' || kind == ('[' | 0x20))
          read_next(&s);
        else
          s.ch = bb_crt_scan_space_entry(&s.count, stream);
      }
      if (widths && !width) {
        unread(&s, s.ch);
        goto done;
      }
      if (kind == 'c' || kind == 's' || kind == ('[' | 0x20)) {
        uint8_t table[32], last = 0, invert = 0;
        unsigned wide_characters = (int8_t)wchar_size > 0;
        uintptr_t set;
        if (kind == 'c') {
          if (!widths) {
            widths = 1;
            ++width;
          }
          set = 0x48d378;
          --invert;
        } else if (kind == 's') {
          set = 0x48d370;
          --invert;
        } else {
          ++*format;
          set = *format;
          if (BYTE(set) == '^') {
            ++set;
            --invert;
          }
        }
        bb_crt_memory_fill_entry(table, 0, 32);
        if (kind == ('[' | 0x20) && BYTE(set) == ']') {
          last = ']';
          table[11] = 0x20;
          ++set;
        }
        while (BYTE(set) != ']') {
          uint8_t c = BYTE(set++);
          if (c == '-' && last && BYTE(set) != ']') {
            uint8_t end = BYTE(set++);
            uint8_t high = last < end ? end : last;
            if (last >= end)
              last = end;
            for (uint8_t n = last; n <= high; ++n)
              table[n >> 3] |= 1u << (n & 7);
            last = 0;
          } else {
            last = c;
            table[c >> 3] |= 1u << (c & 7);
          }
        }
        if (!BYTE(set))
          goto done;
        if (kind == ('[' | 0x20))
          *format = set;
        uintptr_t start = destination;
        unread(&s, s.ch);
        for (;;) {
          if (widths && !width--)
            break;
          read_next(&s);
          if (s.ch == UINT32_MAX ||
              !((table[s.ch >> 3] ^ invert) & (1u << (s.ch & 7)))) {
            unread(&s, s.ch);
            break;
          }
          if (suppress)
            ++start;
          else if (!wide_characters)
            BYTE(destination++) = s.ch;
          else {
            multibyte[0] = s.ch;
            if (WORD(BB_U32(0x48d150) + (s.ch & 255) * 2) & 0x8000) {
              ++s.count;
              multibyte[1] = bb_crt_scan_read_entry(stream);
            }
            bb_crt_locale_mbtowc_entry(&converted, (const char *)multibyte,
                                     BB_U32(0x48d35c));
            WORD(destination) = converted;
            destination += 2;
          }
        }
        if (start == destination)
          goto done;
        if (!suppress) {
          ++assignments;
          if (kind != 'c') {
            if (wide_characters)
              WORD(destination) = 0;
            else
              BYTE(destination) = 0;
          }
        }
      } else if (kind == 'e' || kind == 'f' || kind == 'g') {
        char number[352], *p = number;
        if (s.ch == '-')
          *p++ = '-';
        if (s.ch == '-' || s.ch == '+') {
          --width;
          read_next(&s);
        }
        if (!widths || (int32_t)width > 349)
          width = 349;
        while (classify(s.ch, 4) && width--) {
          ++digits;
          *p++ = s.ch;
          read_next(&s);
        }
        if ((int8_t)BYTE(0x48d360) == (int8_t)s.ch && width--) {
          read_next(&s);
          *p++ = BYTE(0x48d360);
          while (classify(s.ch, 4) && width--) {
            ++digits;
            *p++ = s.ch;
            read_next(&s);
          }
        }
        if (digits && (s.ch == 'e' || s.ch == 'E') && width--) {
          *p++ = 'e';
          read_next(&s);
          if (s.ch == '-')
            *p++ = '-';
          if ((s.ch == '-' || s.ch == '+') && width) {
            --width;
            read_next(&s);
          }
          while (classify(s.ch, 4) && width--) {
            ++digits;
            *p++ = s.ch;
            read_next(&s);
          }
        }
        unread(&s, s.ch);
        if (!digits)
          goto done;
        if (!suppress) {
          ++assignments;
          *p = 0;
#ifdef BB_MODEL_RUNTIME
          struct bb_model_buffer text;
          bb_model_buffer_begin(&text, number, (size_t)(p - number) + 1);
          BB_GAME_CALL(BB_U32(0x48cf98), 0, (int32_t)(int8_t)size - 1,
                         destination, text.reference);
          bb_model_buffer_end(&text, 0);
#else
          ((void (*)(int32_t, void *, const char *))BB_CODE(BB_POINTER(0x48cf98)))(
              (int8_t)size - 1, (void *)destination, number);
#endif
        }
      } else if (kind == 'd' || kind == 'u' || kind == 'o' || kind == 'i' ||
                 kind == 'x' || kind == 'p') {
        uint8_t stop = 0;
        unsigned prefix = kind == 'i' || kind == 'x';
        if (kind == 'i')
          kind = 'd';
        if (kind == 'p')
          size = 1;
        if (s.ch == '-')
          ++negative;
        if (s.ch == '-' || s.ch == '+') {
          --width;
          if (!width && widths)
            ++stop;
          else
            read_next(&s);
        }
        if (prefix && s.ch == '0') {
          read_next(&s);
          if ((uint8_t)s.ch == 'x' || (uint8_t)s.ch == 'X') {
            read_next(&s);
            kind = 'x';
          } else {
            ++digits;
            if (kind == 'x') {
              unread(&s, s.ch);
              s.ch = '0';
            } else
              kind = 'o';
          }
        }
        while (!stop) {
          if (kind == 'x' || (!is_wide && kind == 'p')) {
            if (!classify(s.ch, 0x80))
              ++stop;
            else {
              if (is_wide)
                wide_value <<= 4;
              else
                value <<= 4;
              s.ch = bb_crt_scan_hex_entry(s.ch);
            }
          } else if (!classify(s.ch, 4))
            ++stop;
          else if (kind == 'o') {
            if ((int32_t)s.ch >= '8')
              ++stop;
            else if (is_wide)
              wide_value <<= 3;
            else
              value <<= 3;
          } else if (is_wide)
            wide_value *= 10;
          else
            value *= 10;
          if (!stop) {
            ++digits;
            if (is_wide)
              wide_value += (int64_t)(int32_t)(s.ch - '0');
            else
              value += s.ch - '0';
            if (!widths || --width)
              read_next(&s);
            else
              stop = 1;
          } else
            unread(&s, s.ch);
        }
        if (negative) {
          value = 0u - value;
          wide_value = 0u - wide_value;
        }
        if (kind == 'F')
          digits = 0;
        if (!digits)
          goto done;
        if (!suppress) {
          ++assignments;
          store_integer(destination, value, wide_value, size, is_wide);
        }
      } else if (kind == 'n') {
        value = s.count;
        if (!suppress)
          store_integer(destination, value, wide_value, size, is_wide);
      } else {
        if (BYTE(*format) != s.ch) {
          unread(&s, s.ch);
          goto done;
        }
        --completed;
        if (!suppress)
          *arguments = saved_arguments;
      }
      ++completed;
      ++*format;
    } else {
      uint8_t expected = BYTE((*format)++);
      read_next(&s);
      if (expected != s.ch) {
        unread(&s, s.ch);
        goto done;
      }
      if (WORD(BB_U32(0x48d150) + (s.ch & 255) * 2) & 0x8000) {
        expected = BYTE((*format)++);
        ++s.count;
        uint32_t c = bb_crt_scan_read_entry(stream);
        if (expected != c) {
          unread(&s, c);
          unread(&s, s.ch);
          goto done;
        }
        --s.count;
      }
    }
    if (s.ch == UINT32_MAX &&
        (BYTE(*format) != '%' || BYTE(*format + 1) != 'n'))
      goto done;
  }
done:
  *last = s.ch;
  return s.ch == UINT32_MAX && !assignments && !completed
             ? -1
             : (int32_t)assignments;
}
/* The original debug CRT leaves lookahead uninitialized until it reads input.
 * An initial %n can therefore observe its previous stack-slot value. Retain
 * that slot at the binary interface while old callers remain; the C parser
 * receives explicit initial/final state instead of reading uninitialized C. */
void __attribute__((used, noinline)) bb_crt_input_c(uint32_t *r) {
#ifdef BB_MODEL_RUNTIME
  uintptr_t args = r[3] + 512 + 8;
  uintptr_t lookahead = args - 480;
  uint32_t format = BB_U32(args + 4), arguments = BB_U32(args + 8), last;
  r[7] = input(BB_POINTER(args), &format, &arguments, BB_U32(lookahead), &last);
  BB_U32(args + 4) = format;
  BB_U32(args + 8) = arguments;
#else
  uintptr_t args = r[3] + 8;
  uintptr_t lookahead = r[3] - 0x1d8;
  uint32_t last;
  r[7] = input(BB_POINTER(args), (uint32_t *)(args + 4), (uint32_t *)(args + 8),
               BB_U32(lookahead), &last);
#endif
  BB_U32(lookahead) = last;
}
void __attribute__((used, noinline)) bb_crt_sscanf_c(uint32_t *r) {
#ifdef BB_MODEL_RUNTIME
  uintptr_t args = r[3] + 576 + 8, lookahead = args - 556;
#else
  uintptr_t args = r[3] + 8, lookahead = r[3] - 0x224;
#endif
  const char *source = BB_POINTER(args);
  uint32_t fmt = BB_U32(args + 4), arguments = args + 8;
  if (!source)
    assertion(0x482558, 0x42, 0x482490);
  if (!fmt)
    assertion(0x482558, 0x43, 0x482474);
  uint32_t stream[8], last;
  stream[3] = 0x49;
  stream[2] = stream[0] = bb_address_encode(source);
  stream[1] = bb_crt_string_length_entry(source);
  r[7] = input(stream, &fmt, &arguments, BB_U32(lookahead), &last);
  BB_U32(lookahead) = last;
}
#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_ASM(n) "_" #n
#else
#define BB_ASM(n) #n
#endif
int32_t __attribute__((naked, noinline))
bb_crt_input_entry(void *stream __attribute__((unused)),
                   const char *format __attribute__((unused)),
                   const void *arguments __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(
      bb_crt_input_c) ";addl $4,%esp;popal;popfl;ret");
}
int32_t __attribute__((naked, noinline))
bb_crt_sscanf_entry(const char *source __attribute__((unused)),
                    const char *format __attribute__((unused)), ...) {
  __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(
      bb_crt_sscanf_c) ";addl $4,%esp;popal;popfl;ret");
}
#endif
