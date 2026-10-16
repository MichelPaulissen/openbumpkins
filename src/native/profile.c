/* SPDX-License-Identifier: GPL-3.0-only */
#include "codepage.h"
#include "native.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* INI names use the advertised Windows ANSI page; file contents may instead
 * carry a Unicode BOM. Do not depend on the host's locale or cache file data.
 */
static int space(U32 character) {
  return (character >= 9 && character <= 13) || character == 26 ||
         character == 32;
}
static U32 name_case(U32 character) {
  /* The legacy profile lookup uses the NT CRT's ASCII-only name comparison. */
  return character >= 'a' && character <= 'z' ? character - 32 : character;
}
static void trim(const U16 **first, const U16 **last) {
  while (*first < *last && space(**first))
    ++*first;
  while (*last > *first && space((*last)[-1]))
    --*last;
}
static int name_matches(const U16 *first, const U16 *last, const char *name) {
  const unsigned char *a = (const unsigned char *)name;
  const unsigned char *end = a + strlen(name);
  while (a < end && space(*a))
    ++a;
  while (end > a && space(end[-1]))
    --end;
  if ((size_t)(last - first) != (size_t)(end - a))
    return 0;
  while (a < end)
    if (name_case(*first++) != name_case(bb_codepage_decode(1252, *a++)))
      return 0;
  return 1;
}
static U32 utf8_character(const unsigned char **cursor,
                          const unsigned char *last) {
  unsigned char first = *(*cursor)++;
  if (first < 128)
    return first;
  unsigned remaining;
  U32 result, minimum;
  if (first >= 0xc2 && first <= 0xdf)
    remaining = 1, result = first & 31, minimum = 0x80;
  else if (first >= 0xe0 && first <= 0xef)
    remaining = 2, result = first & 15, minimum = 0x800;
  else if (first >= 0xf0 && first <= 0xf4)
    remaining = 3, result = first & 7, minimum = 0x10000;
  else
    return 0xfffd;
  if ((size_t)(last - *cursor) < remaining)
    return 0xfffd;
  const unsigned char *p = *cursor;
  for (unsigned i = 0; i < remaining; ++i) {
    if ((p[i] & 0xc0) != 0x80)
      return 0xfffd;
    result = (result << 6) | (p[i] & 63);
  }
  if (result < minimum || result > 0x10ffff ||
      (result >= 0xd800 && result <= 0xdfff))
    return 0xfffd;
  *cursor += remaining;
  return result;
}
static U16 *read_profile(const char *filename, size_t *count) {
  char *path = bb_path(filename ? filename : "win.ini");
  FILE *file = fopen(path, "rb");
  free(path);
  if (!file)
    return NULL;
  long length;
  if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
#if SIZE_MAX <= ULONG_MAX
      (unsigned long)length > SIZE_MAX / sizeof(U16) - 1 ||
#endif
      fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return NULL;
  }
  size_t size = (size_t)length;
  unsigned char *bytes = malloc(size ? size : 1);
  U16 *text = malloc((size + 1) * sizeof(*text));
  if (!bytes || !text || fread(bytes, 1, size, file) != size || ferror(file)) {
    free(bytes);
    free(text);
    fclose(file);
    return NULL;
  }
  fclose(file);
  const unsigned char *p = bytes, *end = bytes + size;
  size_t used = 0;
  if (size >= 2 &&
      ((p[0] == 0xff && p[1] == 0xfe) || (p[0] == 0xfe && p[1] == 0xff))) {
    int big = p[0] == 0xfe;
    for (p += 2; end - p >= 2; p += 2)
      text[used++] = (U16)(big ? (p[0] << 8) | p[1] : p[0] | (p[1] << 8));
  } else {
    int utf8 = size >= 3 && !memcmp(p, "\xef\xbb\xbf", 3);
    if (utf8)
      p += 3;
    while (p < end) {
      U32 character =
          utf8 ? utf8_character(&p, end) : bb_codepage_decode(1252, *p++);
      if (character > 0xffff) {
        character -= 0x10000;
        text[used++] = (U16)(0xd800 | (character >> 10));
        character = 0xdc00 | (character & 1023);
      }
      text[used++] = (U16)character;
    }
  }
  text[used] = 0;
  free(bytes);
  *count = used;
  return text;
}
static U32 integer(const U16 *first, const U16 *last, I32 fallback) {
  /* The legacy API obtains at most 29 UTF-16 units before integer conversion.
   * Quote removal uses the original string length against the 30-unit buffer,
   * including its boundary case where truncation already dropped the quote.
   * Numeric overflow wraps; a leading zero alone does not select octal. */
  if (last - first >= 2 && (*first == '\'' || *first == '"') &&
      last[-1] == *first) {
    ++first;
    size_t length = (size_t)(last - first);
    if (last - first > 29)
      last = first + 29;
    if (length <= 30)
      --last;
  }
  if (last - first > 29)
    last = first + 29;
  const U16 *zero = first;
  while (zero < last && *zero)
    ++zero;
  last = zero;
  if (first == last)
    return (U32)fallback;
  while (first < last && *first <= 32)
    ++first;
  int negative = first < last && *first == '-';
  if (first < last && (*first == '+' || *first == '-'))
    ++first;
  unsigned base = 10;
  if (last - first >= 2 && first[0] == '0') {
    if (first[1] == 'b')
      base = 2;
    else if (first[1] == 'o')
      base = 8;
    else if (first[1] == 'x')
      base = 16;
    if (base != 10)
      first += 2;
  }
  U32 value = 0;
  for (; first < last; ++first) {
    unsigned digit = *first >= '0' && *first <= '9'   ? *first - '0'
                     : *first >= 'a' && *first <= 'z' ? *first - 'a' + 10
                     : *first >= 'A' && *first <= 'Z' ? *first - 'A' + 10
                                                      : 36;
    if (digit >= base)
      break;
    value = value * base + digit;
  }
  return negative ? 0u - value : value;
}
/* The mixer requests general/backend settings and then overlays a named
 * device section on [default]. This API must return fallback only when a
 * matching value is absent or empty, rather than ignoring the entire file.
 * https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getprivateprofileint
 */
U32 bb_profile_integer(const char *section, const char *key, I32 fallback,
                       const char *filename) {
  if (!section || !key)
    return (U32)fallback;
  size_t count;
  U16 *text = read_profile(filename, &count);
  if (!text)
    return (U32)fallback;
  U32 result = (U32)fallback;
  const U16 *p = text, *end = text + count;
  int active = name_matches(text, text, section);
  while (p < end) {
    const U16 *first = p;
    while (p < end && *p != '\r' && *p != '\n')
      ++p;
    const U16 *last = p;
    while (p < end && (*p == '\r' || *p == '\n'))
      ++p;
    trim(&first, &last);
    if (first == last || *first == ';')
      continue;
    if (*first == '[') {
      const U16 *bracket = last;
      while (bracket > first && bracket[-1] != ']')
        --bracket;
      if (bracket > first) {
        if (active)
          break; /* Only the first matching section participates. */
        ++first;
        last = bracket - 1;
        trim(&first, &last);
        active = name_matches(first, last, section);
        continue;
      }
    }
    if (!active)
      continue;
    const U16 *equal = first;
    while (equal < last && *equal != '=')
      ++equal;
    const U16 *name_end = equal;
    trim(&first, &name_end);
    if (name_matches(first, name_end, key)) {
      if (equal < last) {
        first = equal + 1;
        trim(&first, &last);
        result = integer(first, last, fallback);
      }
      break; /* First key wins, even when it has no value. */
    }
  }
  free(text);
  return result;
}
