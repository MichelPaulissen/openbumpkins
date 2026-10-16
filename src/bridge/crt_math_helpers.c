/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_math_helpers.h"
#include "crt_math_base.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
#include "numeric_state.h"
#include <math.h>
#include <string.h>
#endif
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
struct extended {
  uint8_t bytes[10];
};
static uint64_t bits(uint32_t low, uint32_t high) {
  return low | ((uint64_t)high << 32);
}
static uint64_t read_bits(uintptr_t p) {
  return BB_U32(p) | ((uint64_t)BB_U32(p + 4) << 32);
}
static void store_bits(uintptr_t p, uint64_t raw) {
  BB_U32(p + 4) = raw >> 32;
  BB_U32(p) = raw;
}
static uint16_t compare(uint64_t raw, uintptr_t other) {
#ifdef BB_MODEL_RUNTIME
  double value;
  memcpy(&value, &raw, sizeof value);
  double right = BB_DOUBLE(other);
  return isnan(value) || isnan(right) ? 0x4500
         : value < right              ? 0x100
         : value == right             ? 0x4000
                                      : 0;
#else
  uint16_t status;
  __asm__ volatile("fldl %1;fcompl %2;fnstsw %%ax"
                   : "=a"(status)
                   : "m"(raw), "m"(*(const uint64_t *)other)
                   : "st");
  return status;
#endif
}
/* The special-power helper only handles infinite operands. Other operands
 * leave the output unchanged, including its original caller-owned contents. */
int32_t bb_crt_math_power_infinity_entry(uint32_t al, uint32_t ah, uint32_t bl,
                                         uint32_t bh, void *out) {
  uint64_t first = bits(al, ah), second = bits(bl, bh), absolute;
#ifdef BB_MODEL_RUNTIME
  absolute = first & UINT64_C(0x7fffffffffffffff);
#else
  __asm__ volatile("fldl %1;fabs;fstpl %0"
                   : "=m"(absolute)
                   : "m"(first)
                   : "st");
#endif
  uintptr_t result = (uintptr_t)out;
  if (second == UINT64_C(0x7ff0000000000000) ||
      second == UINT64_C(0xfff0000000000000)) {
    uint64_t value;
    int32_t error = 0;
    if (!(compare(absolute, 0x4834f0) & 0x4100))
      value = second >> 63 ? 0 : read_bits(0x48dff0);
    else if (compare(absolute, 0x4834f0) & 0x100)
      value = second >> 63 ? read_bits(0x48dff0) : 0;
    else {
      value = read_bits(0x48dff8);
      error = 1;
    }
    store_bits(result, value);
    return error;
  }
  if (first == UINT64_C(0x7ff0000000000000)) {
    if (!(compare(second, 0x4834f8) & 0x4100))
      store_bits(result, read_bits(0x48dff0));
    else if (compare(second, 0x4834f8) & 0x100)
      store_bits(result, 0);
    else
      store_bits(result, UINT64_C(0x3ff0000000000000));
  } else if (first == UINT64_C(0xfff0000000000000)) {
    int32_t parity = bb_crt_math_parity_entry(bl, bh);
    if (!(compare(second, 0x4834f8) & 0x4100)) {
      uint64_t value = read_bits(0x48dff0);
      if (parity == 1) {
#ifdef BB_MODEL_RUNTIME
        value ^= UINT64_C(0x8000000000000000);
#else
        __asm__ volatile("fldl %1;fchs;fstpl %0"
                         : "=m"(value)
                         : "m"(*(const uint64_t *)0x48dff0)
                         : "st");
#endif
      }
      store_bits(result, value);
    } else if (compare(second, 0x4834f8) & 0x100)
      store_bits(result, parity == 1 ? read_bits(0x48e010) : 0);
    else
      store_bits(result, UINT64_C(0x3ff0000000000000));
  }
  return 0;
}
#ifdef BB_MODEL_RUNTIME
/* Pentium FDIV workaround scaling is unnecessary on native targets. Decode
 * the original packed operands and perform their intended division in C. */
long double bb_crt_math_extended_divide_entry(uint32_t a, uint32_t b,
                                              uint32_t c, uint32_t d,
                                              uint32_t e, uint32_t f) {
  uint32_t raw[] = {a, b, c, d, e, f};
  return bb_numeric_read_extended((uintptr_t)(raw + 3)) /
         bb_numeric_read_extended((uintptr_t)raw);
}
long double bb_crt_math_load_entry(uint32_t low, uint32_t high) {
  bb_numeric_push_bits(bits(low, high));
  return bb_numeric_get(0);
}
long double bb_crt_math_register_divide_entry(void) {
  long double numerator = bb_numeric_pop(), denominator = bb_numeric_pop();
  bb_numeric_push(numerator / denominator);
  return bb_numeric_get(0);
}
uint32_t bb_crt_math_extended_parity_entry(void) {
  long double value = bb_numeric_get(0);
  if (isnan(value))
    return 2;
  if (value != rintl(value))
    return 0;
  long double half = value * 0.5L;
  return half == rintl(half) ? 2 : 1;
}
long double bb_crt_math_discard_lower_entry(void) {
  bb_numeric_exchange();
  bb_numeric_pop();
  return bb_numeric_get(0);
}
long double bb_crt_math_zero_entry(void) {
  bb_numeric_set(0, 0);
  return 0;
}
long double bb_crt_math_one_pair_entry(void) {
  bb_numeric_pop();
  bb_numeric_set(0, 1);
  return 1;
}
void bb_crt_math_extended_divide_c(uint32_t *r) {
  uintptr_t a = r[3] + 136u;
  long double value =
      bb_numeric_read_extended(a + 12) / bb_numeric_read_extended(a);
  bb_numeric_push(value);
  bb_model_return_float(value);
}
void bb_crt_math_load_c(uint32_t *r) {
  uintptr_t a = r[3] + 136u;
  bb_model_return_float(bb_crt_math_load_entry(BB_U32(a), BB_U32(a + 4)));
}
void bb_crt_math_extended_parity_c(uint32_t *r) {
  r[7] = bb_crt_math_extended_parity_entry();
}
#define FP_WORKER(name)                                                        \
  void name##_c(uint32_t *r) {                                                 \
    (void)r;                                                                   \
    bb_model_return_float(name##_entry());                                     \
  }
FP_WORKER(bb_crt_math_register_divide)
FP_WORKER(bb_crt_math_discard_lower)
FP_WORKER(bb_crt_math_zero)
FP_WORKER(bb_crt_math_one_pair)
#undef FP_WORKER
#else
/* Raw extended arguments and caller scratch are deliberately live. The old
 * division helper may normalize its first argument in place and stores its
 * control words in the caller's argument area. */
static void extended_divide(uintptr_t a) {
  __asm__ volatile("fldt %0;fldt %1"
                   :
                   : "m"(*(const struct extended *)(a + 12)),
                     "m"(*(const struct extended *)a)
                   : "st", "st(1)");
  for (;;) {
    uint32_t high = BB_U32(a + 4), shifted = high << 1;
    uint32_t exponent = BB_U32(a + 8) & 0x7fff;
    if (high & 0x80000000u) {
      uint32_t trial = shifted ^ 0x0e000000u;
      if ((trial & 0x0e000000u) || !BYTE(0x48e300 + (trial >> 28)) ||
          !exponent || exponent == 0x7fff)
        break;
      __asm__ volatile("fnstcw %0" : "=m"(*(uint16_t *)(a + 24)));
      BB_U32(a + 28) = (BB_U32(a + 24) | 0x33f) & 0xf3ff;
      __asm__ volatile("fldcw %0" ::"m"(*(const uint16_t *)(a + 28)));
      uintptr_t factor = (BB_U32(a + 20) & 0x7fff) == 1 ? 0x48e314 : 0x48e310;
      __asm__ volatile("fmuls %0;fxch %%st(1);fmuls %0;fxch %%st(1);fldcw %1"
                       :
                       : "m"(*(const uint32_t *)factor),
                         "m"(*(const uint16_t *)(a + 24))
                       : "st", "st(1)");
      break;
    }
    if (!(BB_U32(a) | high) || exponent)
      break;
    __asm__ volatile("fnstcw %0" : "=m"(*(uint16_t *)(a + 24)));
    BB_U32(a + 28) = (BB_U32(a + 24) | 0x33f) & 0xf3ff;
    __asm__ volatile("fldcw %0" ::"m"(*(const uint16_t *)(a + 28)));
    uint32_t other_exponent = BB_U32(a + 20) & 0x7fff,
             other_high = BB_U32(a + 16);
    if ((!other_exponent && !(other_high & 0x80000000u)) ||
        (other_exponent && other_exponent != 0x7fff &&
         (other_high & 0x80000000u))) {
      __asm__ volatile(
          "fxch %%st(1);fstp %%st(0);fld %%st(0);fmuls %1;fstpt %0;"
          "fldt %2;fxch %%st(1);fwait;fldcw %3"
          : "=m"(*(struct extended *)a)
          : "m"(*(const uint32_t *)0x48e318),
            "m"(*(const struct extended *)(a + 12)),
            "m"(*(const uint16_t *)(a + 24))
          : "st", "st(1)");
      continue;
    }
    __asm__ volatile("fldcw %0" ::"m"(*(const uint16_t *)(a + 24)));
    break;
  }
  /* Intel FDIVP is GAS FDIVRP: numerator ST1 / denominator ST0. */
  __asm__ volatile("fdivrp" ::: "st", "st(1)");
}
void __attribute__((used, noinline))
bb_crt_math_extended_divide_c(uint32_t *r) {
  extended_divide(r[3] + 136);
}
void __attribute__((used, noinline))
bb_crt_math_register_divide_c(uint32_t *r) {
  uintptr_t a = r[3] + 64;
  __asm__ volatile("fstpt %0;fstpt %1"
                   : "=m"(*(struct extended *)(a + 12)),
                     "=m"(*(struct extended *)a)
                   :
                   : "st", "st(1)");
  extended_divide(a);
}
void __attribute__((used, noinline)) bb_crt_math_load_c(uint32_t *r) {
  uintptr_t a = r[3] + 136;
  uint32_t low = BB_U32(a), high = BB_U32(a + 4);
  if ((high & 0x7ff00000u) == 0x7ff00000u) {
    struct extended raw;
    *(uint32_t *)(void *)raw.bytes = low;
    *(uint32_t *)(void *)(raw.bytes + 4) = (high << 11) | (low >> 21);
    *(uint16_t *)(void *)(raw.bytes + 8) = (high >> 16) | 0x7fff;
    __asm__ volatile("fldt %0" ::"m"(raw) : "st");
  } else
    __asm__ volatile("fldl %0" ::"m"(*(const uint64_t *)a) : "st");
}
void __attribute__((used, noinline))
bb_crt_math_extended_parity_c(uint32_t *r) {
  uint16_t status;
  __asm__ volatile("fld %%st(0);frndint;fcomp %%st(1);fwait;fnstsw %%ax"
                   : "=a"(status)
                   :
                   : "st", "st(1)");
  if (!(status & 0x4000))
    r[7] = 0;
  else {
    __asm__ volatile("fld %%st(0);fmull %1;fld %%st(0);frndint;fcompp;"
                     "fwait;fnstsw %%ax"
                     : "=a"(status)
                     : "m"(*(const uint64_t *)0x48d082)
                     : "st", "st(1)", "st(2)");
    r[7] = status & 0x4000 ? 2 : 1;
  }
}
void __attribute__((used, noinline)) bb_crt_math_discard_lower_c(uint32_t *r) {
  (void)r;
  __asm__ volatile("fxch %%st(1);fstp %%st(0)" ::: "st", "st(1)");
}
void __attribute__((used, noinline)) bb_crt_math_zero_c(uint32_t *r) {
  (void)r;
  __asm__ volatile("fstp %%st(0);fldz" ::: "st");
}
void __attribute__((used, noinline)) bb_crt_math_one_pair_c(uint32_t *r) {
  (void)r;
  __asm__ volatile("fstp %%st(0);fstp %%st(0);fld1" ::: "st", "st(1)");
}
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
#define UNUSED __attribute__((unused))
#define FRAME(name)                                                            \
  __asm__ volatile(                                                            \
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(       \
          name##_c) ";addl $4,%esp;popal;popfl;lea 128(%esp),%esp;ret")
long double __attribute__((naked, noinline))
bb_crt_math_extended_divide_entry(uint32_t a UNUSED, uint32_t b UNUSED,
                                  uint32_t c UNUSED, uint32_t d UNUSED,
                                  uint32_t e UNUSED, uint32_t f UNUSED) {
  FRAME(bb_crt_math_extended_divide);
}
long double __attribute__((naked, noinline))
bb_crt_math_load_entry(uint32_t a UNUSED, uint32_t b UNUSED) {
  FRAME(bb_crt_math_load);
}
#define ENTRY(type, name)                                                      \
  type __attribute__((naked, noinline)) name##_entry(void) { FRAME(name); }
ENTRY(long double, bb_crt_math_register_divide)
ENTRY(uint32_t, bb_crt_math_extended_parity)
ENTRY(long double, bb_crt_math_discard_lower)
ENTRY(long double, bb_crt_math_zero)
ENTRY(long double, bb_crt_math_one_pair)

#endif
