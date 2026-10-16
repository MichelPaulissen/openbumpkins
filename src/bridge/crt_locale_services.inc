/* SPDX-License-Identifier: GPL-3.0-only */
/* Private native buffers borrow logical stack storage; packed callers retain
 * their references and aliasing. Queries never inspect unused mapped buffers.
 */
#include "model_buffer.h"
static size_t locale_input_bytes(const void *p, int32_t count, size_t unit) {
  uint32_t reference;
  if (bb_address_try_encode(p, &reference))
    return 0;
  if (count >= 0)
    return (size_t)count * unit;
  if (count != -1)
    return 0;
  uintptr_t cursor = (uintptr_t)p;
  size_t bytes = 0;
  do {
    bytes += unit;
    if (unit == 1 ? !BB_U8(cursor) : !BB_U16(cursor))
      return bytes;
    cursor += unit;
  } while (1);
}
static size_t locale_output_bytes(int32_t cap, size_t unit) {
  return cap > 0 ? (size_t)cap * unit : 0;
}
int32_t bb_crt_locale_wide_bytes(uint32_t cp, uint32_t flags,
                                 const uint16_t *src, int32_t count, char *out,
                                 int32_t cap, uint32_t *used) {
  struct bb_model_buffer input, output, result;
  bb_model_buffer_begin(&input, src, locale_input_bytes(src, count, 2));
  bb_model_buffer_begin(&output, out, locale_output_bytes(cap, 1));
  bb_model_buffer_begin(&result, used, used ? 4 : 0);
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca430), 0, cp, flags, input.reference,
                           count, output.reference, cap, 0, result.reference);
  bb_model_buffer_end(&result, 1);
  bb_model_buffer_end(&output, 1);
  bb_model_buffer_end(&input, 0);
  return n;
}
static int32_t byte_wide(uint32_t cp, uint32_t flags, const char *src,
                         int32_t count, uint16_t *out, int32_t cap) {
  struct bb_model_buffer input, output;
  bb_model_buffer_begin(&input, src, locale_input_bytes(src, count, 1));
  bb_model_buffer_begin(&output, out, locale_output_bytes(cap, 2));
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca48c), 0, cp, flags, input.reference,
                           count, output.reference, cap);
  bb_model_buffer_end(&output, 1);
  bb_model_buffer_end(&input, 0);
  return n;
}
static int32_t map_ansi(uint32_t locale, uint32_t flags, const char *src,
                        int32_t count, char *out, int32_t cap) {
  struct bb_model_buffer input, output;
  bb_model_buffer_begin(&input, src, locale_input_bytes(src, count, 1));
  bb_model_buffer_begin(&output, out, locale_output_bytes(cap, 1));
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca3f8), 0, locale, flags, input.reference,
                           count, output.reference, cap);
  bb_model_buffer_end(&output, 1);
  bb_model_buffer_end(&input, 0);
  return n;
}
static int32_t map_wide(uint32_t locale, uint32_t flags, const uint16_t *src,
                        int32_t count, void *out, int32_t cap) {
  struct bb_model_buffer input, output;
  bb_model_buffer_begin(&input, src, locale_input_bytes(src, count, 2));
  bb_model_buffer_begin(&output, out,
                        locale_output_bytes(cap, flags & 0x400 ? 1 : 2));
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca3f4), 0, locale, flags, input.reference,
                           count, output.reference, cap);
  bb_model_buffer_end(&output, 1);
  bb_model_buffer_end(&input, 0);
  return n;
}
static int32_t type_ansi(uint32_t locale, uint32_t type, const char *src,
                         int32_t count, uint16_t *out) {
  struct bb_model_buffer input, output;
  size_t bytes = locale_input_bytes(src, count, 1);
  uint32_t out_reference;
  if (count == -1 && !bytes && !bb_address_try_encode(out, &out_reference))
    bytes = bb_crt_string_length_entry(src) + 1;
  bb_model_buffer_begin(&input, src, bytes);
  bb_model_buffer_begin(&output, out, (count > 0 ? (size_t)count : bytes) * 2);
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca3f0), 0, locale, type, input.reference,
                           count, output.reference);
  bb_model_buffer_end(&output, 1);
  bb_model_buffer_end(&input, 0);
  return n;
}
static int32_t type_wide(uint32_t type, const uint16_t *src, int32_t count,
                         uint16_t *out) {
  struct bb_model_buffer input, output;
  size_t bytes = locale_input_bytes(src, count, 2);
  uint32_t out_reference;
  if (count == -1 && !bytes && !bb_address_try_encode(out, &out_reference))
    bytes = (bb_crt_wide_length_entry(src) + 1) * 2;
  bb_model_buffer_begin(&input, src, bytes);
  bb_model_buffer_begin(&output, out, count > 0 ? (size_t)count * 2 : bytes);
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca3ec), 0, type, input.reference, count,
                           output.reference);
  bb_model_buffer_end(&output, 1);
  bb_model_buffer_end(&input, 0);
  return n;
}
static int32_t compare_ansi(uint32_t locale, uint32_t flags, const char *a,
                            int32_t na, const char *b, int32_t nb) {
  struct bb_model_buffer first, second;
  bb_model_buffer_begin(&first, a, locale_input_bytes(a, na, 1));
  bb_model_buffer_begin(&second, b, locale_input_bytes(b, nb, 1));
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca3e0), 0, locale, flags, first.reference,
                           na, second.reference, nb);
  bb_model_buffer_end(&second, 0);
  bb_model_buffer_end(&first, 0);
  return n;
}
static int32_t compare_wide(uint32_t locale, uint32_t flags, const uint16_t *a,
                            int32_t na, const uint16_t *b, int32_t nb) {
  struct bb_model_buffer first, second;
  bb_model_buffer_begin(&first, a, locale_input_bytes(a, na, 2));
  bb_model_buffer_begin(&second, b, locale_input_bytes(b, nb, 2));
  int32_t n = BB_GAME_CALL(BB_U32(0x6ca3dc), 0, locale, flags, first.reference,
                           na, second.reference, nb);
  bb_model_buffer_end(&second, 0);
  bb_model_buffer_end(&first, 0);
  return n;
}
