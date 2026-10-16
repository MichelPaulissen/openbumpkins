/* SPDX-License-Identifier: GPL-3.0-only */
#include "codepage.h"
#include "kernel_handle.h"
#include "native.h"
#include "unicode.h"
#include <string.h>
static int W MultiByteToWideChar(U32 page, U32 flags, const char *source,
                                 I32 count, U16 *output, I32 capacity) {
  page = bb_codepage_id(page);
  if (!page || !source || !count || count < -1 || capacity < 0 ||
      (capacity && !output) || (const void *)source == (void *)output)
    return bb_kernel_error(87);
  if ((flags & ~0xfu) || (flags & 3) == 3)
    return bb_kernel_error(1004);
  if (count == -1)
    count = (I32)strlen(source) + 1;
  int length = 0;
  for (int pass = 0; pass < 2; ++pass) {
    int at = 0;
    for (int n = 0; n < count; ++n) {
      unsigned char byte = (unsigned char)source[n];
      uint16_t text[3];
      U16 wide = flags & 4 ? bb_codepage_glyph(page, byte)
                           : bb_codepage_decode(page, byte);
      unsigned size = flags & 2 ? bb_codepage_decompose(wide, text) : 1;
      if (!(flags & 2))
        text[0] = wide;
      if (at > INT32_MAX - (int)size)
        return bb_kernel_error(87);
      if (pass)
        memcpy(output + at, text, size * sizeof *text);
      at += (int)size;
    }
    if (!pass) {
      length = at;
      if (!capacity)
        return length;
      if (capacity < length)
        return bb_kernel_error(122);
    }
  }
  return length;
}
static int wide_byte(U32 page, U32 flags, const U16 *source, int count, int *at,
                     unsigned char replacement, int *used) {
  U16 character = source[(*at)++];
  if (flags & 0x200)
    while (*at < count && bb_codepage_nonspacing(source[*at])) {
      U16 joined = bb_codepage_compose(character, source[*at]);
      if (joined) {
        character = joined;
        ++*at;
      } else if (flags & 0x10) {
        ++*at;
      } else if (flags & 0x20) {
        ++*at;
        *used = 1;
        return replacement;
      } else
        break;
    }
  int byte = bb_codepage_encode(page, character, !(flags & 0x400));
  if (byte < 0) {
    *used = 1;
    return replacement;
  }
  return byte;
}
static int W WideCharToMultiByte(U32 page, U32 flags, const U16 *source,
                                 I32 count, char *output, I32 capacity,
                                 const char *replacement, I32 *used) {
  page = bb_codepage_id(page);
  if (!page || !source || !count || count < -1 || capacity < 0 ||
      (capacity && !output) || (const void *)source == (void *)output)
    return bb_kernel_error(87);
  if ((flags & ~0x670u) ||
      ((flags & 0x70) && ((flags & 0x70) & ((flags & 0x70) - 1))))
    return bb_kernel_error(1004);
  if (count == -1) {
    count = 0;
    while (source[count++])
      ;
  }
  unsigned char fallback = replacement ? (unsigned char)*replacement : '?';
  int length = 0, substituted = 0;
  for (int pass = 0; pass < 2; ++pass) {
    int at = 0, size = 0;
    while (at < count) {
      int byte =
          wide_byte(page, flags, source, count, &at, fallback, &substituted);
      if (pass)
        output[size] = (char)byte;
      ++size;
    }
    if (!pass) {
      length = size;
      if (used)
        *used = substituted;
      if (!capacity)
        return length;
      if (capacity < length)
        return bb_kernel_error(122);
    }
  }
  return length;
}
static int W GetStringTypeA(U32 locale, U32 type, const char *source, int count,
                            U16 *output) {
  if (!source || !output || (const void *)source == (void *)output)
    return bb_kernel_error(87);
  if (type != 1 && type != 2 && type != 4)
    return bb_kernel_error(1004);
  if (count < 0)
    count = (int)strlen(source) + 1;
  for (int n = 0; n < count; n++)
    output[n] = bb_unicode_type(
        bb_codepage_decode(1252, (unsigned char)source[n]), type / 2);
  return 1;
}
static int W GetStringTypeW(U32 type, const U16 *source, int count,
                            U16 *output) {
  if (!source || !output || source == output)
    return bb_kernel_error(87);
  if (type != 1 && type != 2 && type != 4)
    return bb_kernel_error(1004);
  if (count < 0) {
    count = 0;
    while (source[count++])
      ;
  }
  for (int n = 0; n < count; n++)
    output[n] = bb_unicode_type(source[n], type / 2);
  return 1;
}
static int case_flags(U32 flags) {
  return !(flags & ~0x41000b00u) && (flags & 0x300) != 0x300;
}
static U32 case_character(U32 locale, U32 flags, U32 character) {
  int upper = !!(flags & 0x200);
  if (!(flags & 0x300))
    return character;
  /* Locale-independent default casing retains the reciprocal filename pairs.
   * Linguistic Turkish/Azeri casing supplies the dotted/dotless I variants. */
  if ((flags & 0x1000000) &&
      ((locale & 0x3ff) == 0x1f || (locale & 0x3ff) == 0x2c)) {
    if (upper && (character == 'i' || character == 0x131))
      return character == 'i' ? 0x130 : 'I';
    if (!upper && (character == 'I' || character == 0x130))
      return character == 'I' ? 0x131 : 'i';
  }
  return bb_unicode_case(character, upper);
}
static U16 reversed_case(U32 flags, U16 character) {
  return flags & 0x800 ? (U16)((character << 8) | (character >> 8)) : character;
}
static U32 lower_ansi(unsigned char byte) {
  U32 character = bb_unicode_case(bb_codepage_decode(1252, byte), 0);
  int mapped = bb_codepage_encode(1252, character, 1);
  return mapped < 0 ? '?' : (U32)mapped;
}
static int W LCMapStringA(U32 locale, U32 flags, const char *source, int count,
                          char *output, int capacity) {
  if (!source || !count || capacity < 0 || (capacity && !output))
    return bb_kernel_error(87);
  if (!case_flags(flags) || (source == output && (flags & ~0x300u)))
    return bb_kernel_error(1004);
  if (count < 0)
    count = (int)strlen(source) + 1;
  if (!capacity)
    return count;
  if (capacity < count)
    return bb_kernel_error(122);
  for (int n = 0; n < count; n++) {
    U32 character = bb_codepage_decode(1252, (unsigned char)source[n]);
    U16 mapped = (U16)case_character(locale, flags, character);
    int byte = bb_codepage_encode(1252, reversed_case(flags, mapped), 1);
    output[n] = byte < 0 ? '?' : (char)byte;
  }
  return count;
}
static int W LCMapStringW(U32 locale, U32 flags, const U16 *source, int count,
                          U16 *output, int capacity) {
  if (!source || !count || capacity < 0 || (capacity && !output))
    return bb_kernel_error(87);
  if (!case_flags(flags) || (source == output && (flags & ~0x300u)))
    return bb_kernel_error(1004);
  if (count < 0) {
    count = 0;
    while (source[count++])
      ;
  }
  if (!capacity)
    return count;
  if (capacity < count)
    return bb_kernel_error(122);
  for (int n = 0; n < count; n++) {
    U32 character = source[n];
    int pair = character >= 0xd800 && character <= 0xdbff && n + 1 < count &&
               source[n + 1] >= 0xdc00 && source[n + 1] <= 0xdfff;
    if (pair)
      character =
          0x10000 + ((character - 0xd800) << 10) + source[n + 1] - 0xdc00;
    character = case_character(locale, flags, character);
    if (pair) {
      character -= 0x10000;
      output[n] = reversed_case(flags, (U16)(0xd800 | (character >> 10)));
      output[++n] = reversed_case(flags, (U16)(0xdc00 | (character & 0x3ff)));
    } else
      output[n] = reversed_case(flags, (U16)character);
  }
  return count;
}
static int W CompareStringA(U32 locale, U32 flags, const char *a, int na,
                            const char *b, int nb) {
  if (!a || !b)
    return bb_kernel_error(87);
  if (na < 0)
    na = (int)strlen(a);
  if (nb < 0)
    nb = (int)strlen(b);
  for (int n = 0; n < na && n < nb; n++) {
    U32 x = (unsigned char)a[n], y = (unsigned char)b[n];
    if (flags & 1) {
      x = lower_ansi((unsigned char)x);
      y = lower_ansi((unsigned char)y);
    }
    if (x != y)
      return x < y ? 1 : 3;
    if (!x)
      break;
  }
  return na < nb ? 1 : na > nb ? 3 : 2;
}
static U32 compare_wide_character(const U16 *text, int count, int *at,
                                  int fold) {
  U32 character = text[(*at)++];
  if (fold) {
    if (character >= 0xd800 && character <= 0xdbff && *at < count &&
        text[*at] >= 0xdc00 && text[*at] <= 0xdfff)
      character =
          0x10000 + ((character - 0xd800) << 10) + text[(*at)++] - 0xdc00;
    character = bb_unicode_case(character, 0);
  }
  /* Retain UTF-16 ordering while comparing a complete supplementary case pair.
   */
  if (character < 0x10000)
    return character << 16;
  character -= 0x10000;
  return ((0xd800u | (character >> 10)) << 16) | 0xdc00u | (character & 0x3ff);
}
static int W CompareStringW(U32 locale, U32 flags, const U16 *a, int na,
                            const U16 *b, int nb) {
  if (!a || !b)
    return bb_kernel_error(87);
  if (na < 0) {
    na = 0;
    while (a[na])
      na++;
  }
  if (nb < 0) {
    nb = 0;
    while (b[nb])
      nb++;
  }
  int i = 0, j = 0;
  while (i < na && j < nb) {
    U32 x = compare_wide_character(a, na, &i, flags & 1);
    U32 y = compare_wide_character(b, nb, &j, flags & 1);
    if (x != y)
      return x < y ? 1 : 3;
  }
  return i < na ? 3 : j < nb ? 1 : 2;
}

void *bb_locale_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(MultiByteToWideChar), API_ENTRY(WideCharToMultiByte),
      API_ENTRY(GetStringTypeA),      API_ENTRY(GetStringTypeW),
      API_ENTRY(LCMapStringA),        API_ENTRY(LCMapStringW),
      API_ENTRY(CompareStringA),      API_ENTRY(CompareStringW)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "locale_models.inc"
#endif
