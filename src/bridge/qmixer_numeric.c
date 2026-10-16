/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#ifdef BB_MODEL_RUNTIME
#include "numeric_state.h"
#endif
#include <fenv.h>

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Reconstructed numeric startup, control-format conversion and CRT reporting
 * helpers. Native hardware accessors retain the working i386 behavior; the
 * portable build uses C fenv with explicit precision/mask metadata. All
 * dispatch, bit mappings and table traversal use C. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u + n * 4u);
}
#ifdef BB_MODEL_RUNTIME
static uint16_t word(void) { return bb_numeric_word(); }
static void set_word(uint16_t value) { bb_numeric_set_word(value); }
static uint16_t status(void) { return bb_numeric_status(); }
#else
static uint16_t word(void) {
  uint16_t value;
  __asm__ volatile("fnstcw %0" : "=m"(value));
  return value;
}
static void set_word(uint16_t value) {
  __asm__ volatile("fldcw %0" : : "m"(value));
}
static uint16_t status(void) {
  uint16_t value;
  __asm__ volatile("fnstsw %0" : "=am"(value));
  return value;
}
#endif
static uint32_t decode(uint16_t value) {
  uint32_t result = 0;
  static const uint32_t masks[] = {0x10, 0x80000, 8, 4, 2, 1};
  for (unsigned bit = 0; bit < 6; bit++)
    if (value & (1u << bit))
      result |= masks[bit];
  result |= (value & 0xc00u) >> 2;
  switch (value & 0x300u) {
  case 0:
    result |= 0x20000;
    break;
  case 0x200:
    result |= 0x10000;
    break;
  }
  if (value & 0x1000u)
    result |= 0x40000;
  return result;
}
static uint16_t encode(uint32_t value) {
  uint16_t result = 0;
  static const uint32_t masks[] = {0x10, 0x80000, 8, 4, 2, 1};
  for (unsigned bit = 0; bit < 6; bit++)
    if (value & masks[bit])
      result |= 1u << bit;
  result |= (value & 0x300u) << 2;
  switch (value & 0x30000u) {
  case 0:
    result |= 0x300;
    break;
  case 0x10000:
    result |= 0x200;
    break;
  }
  if (value & 0x40000u)
    result |= 0x1000;
  return result;
}
static void call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  long double call_float __attribute__((unused)) = 0;
  BB_QMIX_DISPATCH(&state, target, args, stack(input) - 64u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
}

/* 0x10011da0..0x10011db7: initialization callback in the CRT startup table. */
void bb_qmix_numeric_bootstrap_c(uint32_t *input) {
  uint32_t args[16] = {0};
  call(input, 0x10011dd0u, args);
  call(input, 0x10014310u, args);
  BB_U32(0x1001f480u) = input[7];
  call(input, 0x100142a0u, args);
  feclearexcept(FE_ALL_EXCEPT);
}
/* 0x10011dc0..0x10011dc1: empty matching finalizer. */
void bb_qmix_numeric_finalize_c(uint32_t *input) { (void)input; }
/* 0x10011dd0..0x10011e08: numeric formatting callback bindings. */
void bb_qmix_numeric_tables_c(uint32_t *input) {
  BB_U32(0x1001fe24u) = 0x100143b0u;
  BB_U32(0x1001fe28u) = 0x10014430u;
  BB_U32(0x1001fe2cu) = 0x10014340u;
  BB_U32(0x1001fe30u) = 0x10014410u;
  BB_U32(0x1001fe20u) = 0x100147d0u;
  BB_U32(0x1001fe34u) = 0x100147d0u;
  input[7] = 0x100147d0u;
}
/* 0x10013f40..0x10013f6a: CRT errno selection from the math error class. */
void bb_qmix_math_errno_c(uint32_t *input) {
  uint32_t kind = arg(input, 0);
  if (kind == 1)
    BB_U32(0x1001fe44u) = 33;
  else if (kind == 2 || kind == 3)
    BB_U32(0x1001fe44u) = 34;
  input[7] = kind;
}
/* 0x10013f70..0x10013f96: operation-name lookup in the original data table. */
void bb_qmix_math_name_c(uint32_t *input) {
  uint32_t operation = arg(input, 0), p = 0x1001fd00u;
  for (; p < 0x1001fdd8u; p += 8)
    if (BB_U32(p) == operation)
      break;
  input[7] = p < 0x1001fdd8u ? BB_U32(p + 4) : 0;
  input[5] = p;
  input[6] = operation;
}
/* 0x10013fa0..0x10013fd2: prioritized math exception class. */
void bb_qmix_math_error_kind_c(uint32_t *input) {
  uint32_t value = arg(input, 0);
  input[7] = value & 0x20u ? 5
             : value & 8u  ? 1
             : value & 4u  ? 2
             : value & 1u  ? 3
                           : (value & 2u) * 2;
}
/* 0x10013fe0..0x10013ff1 and 0x10014000..0x10014012: status boundaries. */
void bb_qmix_fpu_status_c(uint32_t *input) {
  input[7] = (uint32_t)(int32_t)(int16_t)status();
}
void bb_qmix_fpu_clear_c(uint32_t *input) {
  input[7] = (uint32_t)(int32_t)(int16_t)status();
  feclearexcept(FE_ALL_EXCEPT);
}
/* 0x10014020..0x10014052: masked raw control-word update, returning old word.
 */
void bb_qmix_fpu_word_c(uint32_t *input) {
  uint16_t old = word(), mask = arg(input, 1);
  uint16_t value = (old & ~mask) | (arg(input, 0) & mask);
  set_word(value);
  input[5] = (input[5] & 0xffff0000u) | value;
  input[7] = (uint32_t)(int32_t)(int16_t)old;
}
/* 0x10014140..0x100141a3: double classification without FP evaluation. */
void bb_qmix_math_classify_c(uint32_t *input) {
  uint64_t value = arg(input, 0) | (uint64_t)arg(input, 1) << 32;
  uint64_t magnitude = value & UINT64_C(0x7fffffffffffffff);
  if (magnitude == UINT64_C(0x7ff0000000000000))
    input[7] = value >> 63 ? 2 : 1;
  else if ((magnitude & UINT64_C(0x7ff8000000000000)) ==
           UINT64_C(0x7ff8000000000000))
    input[7] = 3;
  else if ((magnitude & UINT64_C(0x7ff0000000000000)) ==
           UINT64_C(0x7ff0000000000000))
    input[7] = 4;
  else
    input[7] = 0;
}
/* 0x100142a0..0x100142b3: CRT default of 53-bit intermediate precision. */
void bb_qmix_fpu_default_c(uint32_t *input) {
  uint32_t args[16] = {0x10000, 0x30000};
  call(input, 0x10015e20u, args);
}
/* 0x100142c0..0x1001430f: obsolete Pentium FDIV hardware defect probe.
 * Supported Linux/Windows processors have correctly implemented division. */
void bb_qmix_fpu_divide_probe_c(uint32_t *input) { input[7] = 0; }
/* 0x10014310..0x10014339: prefer the loaded CRT's adjustment callback. */
void bb_qmix_fpu_divide_detect_c(uint32_t *input) {
  uint32_t args[16] = {0x1001a8ec};
  call(input, BB_U32(0x1002122cu), args);
  if (input[7]) {
    args[0] = input[7];
    args[1] = 0x1001a8d0;
    call(input, BB_U32(0x100211f0u), args);
    if (input[7]) {
      uint32_t target = input[7];
      args[0] = 0;
      call(input, target, args);
      return;
    }
  }
  call(input, 0x100142c0u, args);
}
/* 0x10015dc0..0x10015dc3: default matherr callback declines handling. */
void bb_qmix_math_callback_c(uint32_t *input) { input[7] = 0; }
/* 0x10015dd0..0x10015e11: update the Microsoft control format. */
void bb_qmix_fpu_control_c(uint32_t *input) {
  uint32_t value = arg(input, 0), mask = arg(input, 1);
  uint32_t result = (decode(word()) & ~mask) | (value & mask);
  set_word(encode(result));
  input[7] = result;
}
/* 0x10015e20..0x10015e38: _controlfp ignores denormal exception-mask changes.
 */
void bb_qmix_fpu_control_safe_c(uint32_t *input) {
  uint32_t args[16] = {arg(input, 0), arg(input, 1) & ~0x80000u};
  call(input, 0x10015dd0u, args);
}
/* 0x10015e40..0x10015ee1 and 0x10015ef0..0x10015f7d: pure bit conversions. */
void bb_qmix_fpu_decode_c(uint32_t *input) {
  uint16_t value = arg(input, 0);
  input[7] = decode(value);
  input[5] = (input[5] & 0xffff0000u) | value;
  input[6] = value & 0x300u;
}
void bb_qmix_fpu_encode_c(uint32_t *input) {
  uint32_t value = arg(input, 0);
  input[7] = (input[7] & 0xffff0000u) | encode(value);
  input[5] = value;
  input[6] = value & 0x30000u;
}

#ifndef BB_MODEL_RUNTIME
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal "                   \
                      "2048(%esp),%esp;ret");                                  \
  }
#define NONE(name) ENTRY(name, (void))
#define ONE(name) ENTRY(name, (uint32_t a __attribute__((unused))))
#define TWO(name)                                                              \
  ENTRY(name, (uint32_t a __attribute__((unused)),                             \
               uint32_t b __attribute__((unused))))
NONE(bb_qmix_numeric_bootstrap)
NONE(bb_qmix_numeric_finalize)
NONE(bb_qmix_numeric_tables)
ONE(bb_qmix_math_errno)
ONE(bb_qmix_math_name)
ONE(bb_qmix_math_error_kind)
NONE(bb_qmix_fpu_status)
NONE(bb_qmix_fpu_clear)
TWO(bb_qmix_fpu_word)
TWO(bb_qmix_math_classify)
NONE(bb_qmix_fpu_default)
NONE(bb_qmix_fpu_divide_probe)
NONE(bb_qmix_fpu_divide_detect)
ONE(bb_qmix_math_callback)
TWO(bb_qmix_fpu_control)
TWO(bb_qmix_fpu_control_safe)
ONE(bb_qmix_fpu_decode)
ONE(bb_qmix_fpu_encode)
#undef TWO
#undef ONE
#undef NONE
#undef ENTRY
#endif
