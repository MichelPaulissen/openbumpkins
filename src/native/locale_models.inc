/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "locale_models.h"
/* Decode only spans the shared routine will use. Queries, rejected flags and
 * insufficient output capacity must not access otherwise ignored buffers. */
static uint32_t locale_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static size_t locale_length(uint32_t ref, int count, unsigned width,
                            int terminator) {
  if (count >= 0)
    return (size_t)count;
  size_t n = 0;
  while (width == 2 ? BB_U16(ref + (uint32_t)n * 2u) : BB_U8(ref + (uint32_t)n))
    n++;
  return n + (unsigned)terminator;
}
#define A(n) locale_arg(r, n)
#define I(n) ((int32_t)A(n))
static void model_multibyte(uint32_t *r) {
  if (!bb_codepage_id(A(0)) || !A(2) || !I(3) || I(3) < -1 || I(5) < 0 ||
      (I(5) && !A(4)) || A(2) == A(4)) {
    r[7] = bb_kernel_error(87);
    return;
  }
  if ((A(1) & ~0xfu) || (A(1) & 3) == 3) {
    r[7] = bb_kernel_error(1004);
    return;
  }
  size_t n = locale_length(A(2), I(3), 1, 1);
  const char *source = BB_SPAN(A(2), n);
  int length = MultiByteToWideChar(A(0), A(1), source, I(3), NULL, 0);
  if (!length || !I(5)) {
    r[7] = length;
    return;
  }
  if (I(5) < length) {
    r[7] = bb_kernel_error(122);
    return;
  }
  r[7] = MultiByteToWideChar(A(0), A(1), source, I(3),
                             BB_SPAN(A(4), (size_t)length * 2), I(5));
}
static void model_wide(uint32_t *r) {
  if (!bb_codepage_id(A(0)) || !A(2) || !I(3) || I(3) < -1 || I(5) < 0 ||
      (I(5) && !A(4)) || A(2) == A(4)) {
    r[7] = bb_kernel_error(87);
    return;
  }
  if ((A(1) & ~0x670u) ||
      ((A(1) & 0x70) && ((A(1) & 0x70) & ((A(1) & 0x70) - 1)))) {
    r[7] = bb_kernel_error(1004);
    return;
  }
  size_t n = locale_length(A(2), I(3), 2, 1);
  const U16 *source = BB_SPAN(A(2), n * 2);
  const char *replacement = A(6) ? BB_SPAN(A(6), 1) : NULL;
  I32 *used = A(7) ? BB_SPAN(A(7), 4) : NULL;
  if (!I(5)) {
    r[7] = WideCharToMultiByte(A(0), A(1), source, I(3), NULL, 0, replacement,
                               used);
    return;
  }
  /* Determine the output span without publishing `used`: the real provider
   * preserves its ordering if that optional output aliases source/destination.
   */
  int at = 0, length = 0, substituted = 0;
  unsigned char fallback = replacement ? (unsigned char)*replacement : '?';
  while ((size_t)at < n) {
    wide_byte(bb_codepage_id(A(0)), A(1), source, (int)n, &at, fallback,
              &substituted);
    length++;
  }
  char ignored;
  char *output = I(5) >= length ? BB_SPAN(A(4), (size_t)length) : &ignored;
  r[7] = WideCharToMultiByte(A(0), A(1), source, I(3), output, I(5),
                             replacement, used);
}
static void model_type(uint32_t *r, int wide) {
  uint32_t type = A(wide ? 0 : 1), source = A(wide ? 1 : 2),
           output = A(wide ? 3 : 4);
  int count = I(wide ? 2 : 3);
  if (!source || !output || source == output) {
    r[7] = bb_kernel_error(87);
    return;
  }
  if (type != 1 && type != 2 && type != 4) {
    r[7] = bb_kernel_error(1004);
    return;
  }
  size_t n = locale_length(source, count, wide ? 2 : 1, 1);
  U16 ignored = 0;
  const void *text = n ? BB_SPAN(source, n * (wide ? 2 : 1)) : &ignored;
  U16 *out = n ? BB_SPAN(output, n * 2) : &ignored + 1;
  r[7] = wide ? GetStringTypeW(type, text, count, out)
              : GetStringTypeA(A(0), type, text, count, out);
}
static void model_type_ansi(uint32_t *r) { model_type(r, 0); }
static void model_type_wide(uint32_t *r) { model_type(r, 1); }
static void model_map(uint32_t *r, int wide) {
  if (!A(2) || !I(3) || I(5) < 0 || (I(5) && !A(4))) {
    r[7] = bb_kernel_error(87);
    return;
  }
  if (!case_flags(A(1)) || (A(2) == A(4) && (A(1) & ~0x300u))) {
    r[7] = bb_kernel_error(1004);
    return;
  }
  size_t n = locale_length(A(2), I(3), wide ? 2 : 1, 1);
  const void *source = BB_SPAN(A(2), n * (wide ? 2 : 1));
  if (!I(5)) {
    r[7] = (uint32_t)n;
    return;
  }
  if ((size_t)I(5) < n) {
    r[7] = bb_kernel_error(122);
    return;
  }
  void *out = BB_SPAN(A(4), n * (wide ? 2 : 1));
  r[7] = wide ? LCMapStringW(A(0), A(1), source, I(3), out, I(5))
              : LCMapStringA(A(0), A(1), source, I(3), out, I(5));
}
static void model_map_ansi(uint32_t *r) { model_map(r, 0); }
static void model_map_wide(uint32_t *r) { model_map(r, 1); }
static void model_compare(uint32_t *r, int wide) {
  if (!A(2) || !A(4)) {
    r[7] = bb_kernel_error(87);
    return;
  }
  size_t na = locale_length(A(2), I(3), wide ? 2 : 1, 0);
  size_t nb = locale_length(A(4), I(5), wide ? 2 : 1, 0);
  /* Negative counts still require the terminator for the shared length scan. */
  size_t sa = na + (I(3) < 0), sb = nb + (I(5) < 0);
  U16 empty = 0;
  const void *a = sa ? BB_SPAN(A(2), sa * (wide ? 2 : 1)) : &empty;
  const void *b = sb ? BB_SPAN(A(4), sb * (wide ? 2 : 1)) : &empty;
  r[7] = wide ? CompareStringW(A(0), A(1), a, I(3), b, I(5))
              : CompareStringA(A(0), A(1), a, I(3), b, I(5));
}
static void model_compare_ansi(uint32_t *r) { model_compare(r, 0); }
static void model_compare_wide(uint32_t *r) { model_compare(r, 1); }
#undef I
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} locale_targets[] = {
#define T(i, name, body, words)                                                \
  {#name, {BB_LOCALE_MODEL_API + i * 4u, body, words, 0, 0}}
    T(0, MultiByteToWideChar, model_multibyte, 6),
    T(1, WideCharToMultiByte, model_wide, 8),
    T(2, GetStringTypeA, model_type_ansi, 5),
    T(3, GetStringTypeW, model_type_wide, 4),
    T(4, LCMapStringA, model_map_ansi, 6),
    T(5, LCMapStringW, model_map_wide, 6),
    T(6, CompareStringA, model_compare_ansi, 6),
    T(7, CompareStringW, model_compare_wide, 6)
#undef T
};
uint32_t bb_locale_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof locale_targets / sizeof *locale_targets; n++)
    if (!strcmp(name, locale_targets[n].name))
      return locale_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_locale_model_find(void *context, uint32_t id) {
  (void)context;
  for (unsigned n = 0; n < sizeof locale_targets / sizeof *locale_targets; n++)
    if (locale_targets[n].target.identity == id)
      return &locale_targets[n].target;
  return NULL;
}
