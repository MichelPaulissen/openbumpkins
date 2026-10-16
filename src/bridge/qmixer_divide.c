/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Numeric boundaries in C. Legacy x87 entry/return adapters remain temporary
 * ABI glue. Status-bit identity and the obsolete processor divide workaround
 * are not needed; numerical classes, signed results and the caller's rounding
 * control remain meaningful. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static double number(const uint32_t *input) {
  double value; memcpy(&value, (const void *)(uintptr_t)(stack(input) + 4u), 8);
  return value;
}
static long double rounded(double value) {
  /* Use the long-double interface so the i386 host honors x87 rounding. The
   * original helper stores its rounded result as a double before returning. */
  return (double)nearbyintl((long double)value);
}
/* 0x100140c0..0x100140d5: integral rounding under the caller's control. */
void bb_qmix_divide_round_c(uint32_t *input) {
  *(long double *)(uintptr_t)input[9] = rounded(number(input));
}
/* 0x10016dc0..0x10016e3f: noninteger/subnormal=0, odd=1, even=2. Original
 * unordered comparisons classify NaNs as 2, as do infinities and signed zero. */
void bb_qmix_divide_parity_c(uint32_t *input) {
  double value = number(input); uint64_t raw;
  memcpy(&raw, &value, 8); raw &= UINT64_C(0x7fffffffffffffff);
  if (raw && raw < UINT64_C(0x0010000000000000)) input[7] = 0;
  else if (raw >= UINT64_C(0x7ff0000000000000)) input[7] = 2;
  else if (rounded(value) != value) input[7] = 0;
  else {
    double half = value * 0.5;
    input[7] = rounded(half) == half ? 2 : 1;
  }
}
/* 0x10016e40..0x10016f57: divide two padded ten-byte x87 arguments, with
 * the first argument as divisor. Hardware/C arithmetic handles all classes. */
void bb_qmix_divide_extended_c(uint32_t *input) {
  long double divisor = 0, dividend = 0;
  memcpy(&divisor, (const void *)(uintptr_t)(stack(input) + 4u), 10);
  memcpy(&dividend, (const void *)(uintptr_t)(stack(input) + 16u), 10);
  *(long double *)(uintptr_t)input[9] = dividend / divisor;
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define FLOAT_ENTRY(name, signature)                                           \
  double __attribute__((naked)) name##_entry signature {                       \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;"              \
        "movl %eax,36(%esp);pushl %esp;call " BB_ASM(                          \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;"   \
                      "fldt -2044(%esp);ret");                                 \
  }
FLOAT_ENTRY(bb_qmix_divide_round, (ARG(0), ARG(1)))
FLOAT_ENTRY(bb_qmix_divide_extended, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)))
uint32_t __attribute__((naked)) bb_qmix_divide_parity_entry(ARG(0), ARG(1)) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_divide_parity_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret");
}
#undef FLOAT_ENTRY
#undef ARG
