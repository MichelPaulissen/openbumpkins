/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_number_models.h"
#include "address.h"
#include "crt_bootstrap.h"
#include "crt_format.h"
#include "crt_math_base.h"
#include "crt_math_models.h"
#include "crt_numeric.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
#define P(n) ((void *)(uintptr_t)A(n))
#define POINTER(value) bb_address_encode((const void *)(value))
#define VOID1(name, fn)                                                        \
  static void name(uint32_t *r) { fn(P(0)); }
#define VOID2(name, fn)                                                        \
  static void name(uint32_t *r) { fn(P(0), P(1)); }
#define VALUE1(name, fn)                                                       \
  static void name(uint32_t *r) { r[7] = fn(P(0)); }
#define VALUE2(name, fn)                                                       \
  static void name(uint32_t *r) { r[7] = fn(P(0), A(1)); }
#define PAIR(name, fn)                                                         \
  static void name(uint32_t *r) { r[7] = fn(P(0), P(1)); }
static void assign(uint32_t *r) {
  bb_crt_numeric_assign_entry(A(0), P(1), P(2));
}
VALUE2(tail_zero, bb_crt_numeric_tail_zero_entry)
VALUE2(increment, bb_crt_numeric_increment_entry)
VALUE2(round_value, bb_crt_numeric_round_entry)
VOID2(copy, bb_crt_numeric_copy_entry)
VOID1(clear, bb_crt_numeric_clear_entry)
VALUE1(zero, bb_crt_numeric_zero_entry)
static void shift(uint32_t *r) { bb_crt_numeric_shift_entry(P(0), A(1)); }
static void binary(uint32_t *r) {
  r[7] = bb_crt_numeric_binary_entry(P(0), P(1), P(2));
}
PAIR(double_value, bb_crt_numeric_double_entry)
PAIR(float_value, bb_crt_numeric_float_entry)
PAIR(parse_double, bb_crt_numeric_parse_double_entry)
PAIR(parse_float, bb_crt_numeric_parse_float_entry)
static void round_text(uint32_t *r) {
  r[7] = POINTER(bb_crt_numeric_round_text_entry(P(0), A(1), P(2)));
}
static void decimal(uint32_t *r) {
  r[7] = POINTER(bb_crt_numeric_decimal_entry(A(0), A(1)));
}
VOID2(pack, bb_crt_numeric_pack_entry)
static void add_word(uint32_t *r) {
  r[7] = bb_crt_numeric_add_word_entry(A(0), A(1), P(2));
}
VOID2(add, bb_crt_numeric_add_entry)
VOID1(left, bb_crt_numeric_left_entry)
VOID1(right, bb_crt_numeric_right_entry)
static void accumulate(uint32_t *r) {
  bb_crt_numeric_accumulate_entry(P(0), A(1), P(2));
}
static void parse(uint32_t *r) {
  r[7] = bb_crt_numeric_parse_c(P(0), P(1), P(2), A(3), A(4), A(5), A(6), 1);
}
static void generate(uint32_t *r) {
  r[7] = bb_crt_numeric_generate_entry(A(0), A(1), A(2), A(3), A(4), P(5));
}
extern void bb_crt_numeric_multiply_c(uint32_t *);
static void scale(uint32_t *r) { bb_crt_numeric_scale_entry(P(0), A(1), A(2)); }
static void initialize(uint32_t *r) {
  (void)r;
  bb_crt_format_initialize_entry();
}
VOID1(insert_decimal, bb_crt_format_decimal_entry)
VOID1(trim, bb_crt_format_trim_entry)
VALUE1(positive, bb_crt_format_positive_entry)
static void scientific(uint32_t *r) {
  r[7] = POINTER(bb_crt_format_scientific_entry(P(0), P(1), A(2), A(3)));
}
static void fixed(uint32_t *r) {
  r[7] = POINTER(bb_crt_format_fixed_entry(P(0), P(1), A(2)));
}
static void general(uint32_t *r) {
  r[7] = POINTER(bb_crt_format_general_entry(P(0), P(1), A(2), A(3)));
}
static void scientific_cached(uint32_t *r) {
  r[7] = POINTER(bb_crt_format_scientific_cached_entry(P(0), P(1), A(2), A(3)));
}
static void fixed_cached(uint32_t *r) {
  r[7] = POINTER(bb_crt_format_fixed_cached_entry(P(0), P(1), A(2)));
}
static void number(uint32_t *r) {
  r[7] = POINTER(bb_crt_format_number_entry(P(0), P(1), A(2), A(3), A(4)));
}
static void shift_text(uint32_t *r) { bb_crt_format_shift_entry(P(0), A(1)); }
static void divide(uint32_t *r) {
  uint64_t value = bb_crt_math_divide_entry(A(0), A(1), A(2), A(3));
  r[7] = value;
  r[5] = value >> 32;
}
static void integer_remainder(uint32_t *r) {
  uint64_t value = bb_crt_math_remainder_entry(A(0), A(1), A(2), A(3));
  r[7] = value;
  r[5] = value >> 32;
}
#define MATH2(name, fn)                                                        \
  static void name(uint32_t *r) { r[7] = fn(A(0), A(1)); }
MATH2(control, bb_crt_math_control_entry)
MATH2(control_safe, bb_crt_math_control_safe_entry)
MATH2(word, bb_crt_math_word_entry)
MATH2(classify, bb_crt_math_classify_entry)
MATH2(special, bb_crt_math_special_entry)
MATH2(parity, bb_crt_math_parity_entry)
static void decode(uint32_t *r) { r[7] = bb_crt_math_decode_entry(A(0)); }
static void encode(uint32_t *r) { r[7] = bb_crt_math_encode_entry(A(0)); }
static void status(uint32_t *r) { r[7] = bb_crt_math_status_entry(); }
static void clear_status(uint32_t *r) { r[7] = bb_crt_math_clear_entry(); }
static void round_float(uint32_t *r) {
  bb_model_return_float(bb_crt_math_round_entry(A(0), A(1)));
}
static void exponent(uint32_t *r) {
  bb_model_return_float(bb_crt_math_exponent_entry(A(0), A(1), A(2)));
}
static void fraction(uint32_t *r) {
  bb_model_return_float(bb_crt_math_fraction_entry(A(0), A(1), P(2)));
}
static void numeric_startup(uint32_t *r) {
  (void)r;
  bb_crt_boot_math_entry();
}
static void precision(uint32_t *r) {
  (void)r;
  bb_crt_boot_precision_entry();
}
static void divide_probe(uint32_t *r) {
  r[7] = bb_crt_boot_divide_probe_entry();
}
static void processor_probe(uint32_t *r) {
  r[7] = bb_crt_boot_processor_probe_entry();
}
static const struct bb_model_target targets[] = {
    {0x00472340u, assign, 3, 0, 0},
    {0x0047b640u, tail_zero, 2, 0, 0},
    {0x0047b6e0u, increment, 2, 0, 0},
    {0x0047b7a0u, round_value, 2, 0, 0},
    {0x0047b880u, copy, 2, 0, 0},
    {0x0047b8d0u, clear, 1, 0, 0},
    {0x0047b910u, zero, 1, 0, 0},
    {0x0047b960u, shift, 2, 0, 0},
    {0x0047ba50u, binary, 3, 0, 0},
    {0x0047bcc0u, double_value, 2, 0, 0},
    {0x0047bcf0u, float_value, 2, 0, 0},
    {0x0047bd20u, parse_double, 2, 0, 0},
    {0x0047bd60u, parse_float, 2, 0, 0},
    {0x0047bda0u, round_text, 3, 0, 0},
    {0x0047be80u, decimal, 2, 0, 0},
    {0x0047bef0u, pack, 2, 0, 0},
    {0x0047eca0u, add_word, 3, 0, 0},
    {0x0047ecf0u, add, 2, 0, 0},
    {0x0047eda0u, left, 1, 0, 0},
    {0x0047ee20u, right, 1, 0, 0},
    {0x0047eea0u, accumulate, 3, 0, 0},
    {0x0047efe0u, parse, 7, 0, 0},
    {0x0047fc00u, generate, 6, 0, 0},
    {0x00480880u, bb_crt_numeric_multiply_c, 2, 128, 0},
    {0x00480cb0u, scale, 3, 0, 0},
    {0x0046c0c0u, initialize, 0, 0, 0},
    {0x00472160u, insert_decimal, 1, 0, 0},
    {0x00472220u, trim, 1, 0, 0},
    {0x00472300u, positive, 1, 0, 0},
    {0x004723a0u, scientific, 4, 0, 0},
    {0x00472570u, fixed, 3, 0, 0},
    {0x00472700u, general, 4, 0, 0},
    {0x00472820u, scientific_cached, 4, 0, 0},
    {0x00472860u, fixed_cached, 3, 0, 0},
    {0x004728a0u, number, 5, 0, 0},
    {0x00472920u, shift_text, 2, 0, 0},
    {0x00479e70u, divide, 4, 0, 0},
    {0x00479ee0u, integer_remainder, 4, 0, 0},
    {0x0047b280u, control, 2, 0, 0},
    {0x0047b2d0u, control_safe, 2, 0, 0},
    {0x0047b300u, decode, 1, 0, 0},
    {0x0047b450u, encode, 1, 0, 0},
    {0x00478ad0u, status, 0, 0, 0},
    {0x00478af0u, clear_status, 0, 0, 0},
    {0x00478b10u, word, 2, 0, 0},
    {0x00478bd0u, round_float, 2, 0, 0},
    {0x00480600u, classify, 2, 0, 0},
    {0x00478c40u, special, 2, 0, 0},
    {0x0047d2d0u, parity, 2, 0, 0},
    {0x00478bf0u, exponent, 3, 0, 0},
    {0x00478d00u, fraction, 3, 0, 0},
    {0x0046c080u, numeric_startup, 0, 0, 0},
    {0x00472080u, precision, 0, 0, 0},
    {0x004720a0u, divide_probe, 0, 0, 0},
    {0x00472100u, processor_probe, 0, 0, 0},
};
const struct bb_model_target *bb_crt_number_model_find(void *unused,
                                                       uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return bb_crt_math_model_find(NULL, id);
}
