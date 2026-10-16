/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_models.h"
#include "crt_io_models.h"
#include "crt_locale_models.h"
#include "crt_number_models.h"
#include "crt_primitives.h"
#include "crt_runtime_models.h"
#include "crt_time_models.h"
#include "game_calls.h"
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
#define A(n) arg(r, n, 0)
#define P(n) ((void *)(uintptr_t)A(n))
#define PTR2(name, fn)                                                         \
  static void name(uint32_t *r) { r[7] = bb_address_encode(fn(P(0), P(1))); }
static void case_compare(uint32_t *r) {
  r[7] = bb_crt_string_case_compare_entry(P(0), P(1));
}
static void find(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_string_find_entry(P(0), A(1)));
}
static void find_last(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_string_find_last_entry(P(0), A(1)));
}
static void copy_count(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_string_copy_count_entry(P(0), P(1), A(2)));
}
static void long_value(uint32_t *r) { r[7] = bb_crt_string_long_entry(P(0)); }
static void int_value(uint32_t *r) { r[7] = bb_crt_string_int_entry(P(0)); }
PTR2(search, bb_crt_string_search_entry)
static void copy(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_memory_copy_entry(P(0), P(1), A(2)));
}
static void move(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_memory_move_entry(P(0), P(1), A(2)));
}
static void fill(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_memory_fill_entry(P(0), A(1), A(2)));
}
PTR2(string_copy, bb_crt_string_copy_entry)
PTR2(append, bb_crt_string_append_entry)
static void compare(uint32_t *r) {
  r[7] = bb_crt_string_compare_entry(P(0), P(1));
}
static void length(uint32_t *r) { r[7] = bb_crt_string_length_entry(P(0)); }
static void compare_count(uint32_t *r) {
  r[7] = bb_crt_string_compare_count_entry(P(0), P(1), A(2));
}
static void lower(uint32_t *r) {
  r[7] = bb_crt_character_lower_c(arg(r, 0, 12), r[3] + 4u);
}
static void type(uint32_t *r) {
  r[7] = bb_crt_character_type_c(arg(r, 0, 12), arg(r, 1, 12), r[3] + 4u);
}
static void count(uint32_t *r) { r[7] = bb_crt_string_count_entry(P(0), A(1)); }
static void count_alt(uint32_t *r) {
  r[7] = bb_crt_string_count_alt_entry(P(0), A(1));
}
static void space(uint32_t *r) { r[7] = bb_crt_character_space_entry(A(0)); }
static void wide_length(uint32_t *r) { r[7] = bb_crt_wide_length_entry(P(0)); }
static void wide_count(uint32_t *r) {
  r[7] = bb_crt_wide_count_entry(P(0), A(1));
}
uint32_t bb_crt_character_type_entry(int32_t c, uint32_t mask) {
  return BB_GAME_CALL(0x00473df0u, 0, c, mask);
}
int32_t bb_crt_character_lower_entry(int32_t c) {
  return BB_GAME_CALL(0x00472960u, 0, c);
}
const struct bb_model_target *bb_crt_model_find(void *unused, uint32_t id) {
  (void)unused;
  switch (id) {
#define MODEL(id, body, argc, reserve)                                         \
  case id: {                                                                   \
    static const struct bb_model_target t = {id, body, argc, reserve, 0};      \
    return &t;                                                                 \
  }
    // clang-format off
  MODEL(0x0046c1c0u, case_compare, 2, 0)
  MODEL(0x0046c260u, find, 2, 0)
  MODEL(0x0046c470u, copy_count, 3, 0)
  MODEL(0x0046d110u, long_value, 1, 0)
  MODEL(0x0046d230u, int_value, 1, 0)
  MODEL(0x0046d280u, search, 2, 0)
  MODEL(0x0046f1d0u, copy, 3, 0)
  MODEL(0x0046f490u, fill, 3, 0)
  MODEL(0x0046f4f0u, string_copy, 2, 0)
  MODEL(0x0046f500u, append, 2, 0)
  MODEL(0x0046f5e0u, compare, 2, 0)
  MODEL(0x0046f670u, find_last, 2, 0)
  MODEL(0x0046f6a0u, length, 1, 0)
  MODEL(0x0046fb20u, compare_count, 3, 0)
  MODEL(0x00472960u, lower, 1, 12)
  MODEL(0x00473df0u, type, 2, 12)
  MODEL(0x0047c070u, move, 3, 0)
  MODEL(0x0047c4e0u, count, 2, 0)
  MODEL(0x0047e600u, space, 1, 0)
  MODEL(0x0047e780u, wide_length, 1, 0)
  MODEL(0x004804a0u, wide_count, 2, 0)
  MODEL(0x00481330u, count_alt, 2, 0)
// clang-format on
#undef MODEL
  default: {
    const struct bb_model_target *target = bb_crt_number_model_find(NULL, id);
    if (!target)
      target = bb_crt_io_model_find(NULL, id);
    if (!target)
      target = bb_crt_locale_model_find(NULL, id);
    if (!target)
      target = bb_crt_time_model_find(NULL, id);
    return target ? target : bb_crt_runtime_model_find(NULL, id);
  }
  }
}
