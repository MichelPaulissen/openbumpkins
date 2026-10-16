/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_calculation.h"
#include "crt_math_helpers.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
#include "numeric_state.h"
#include <math.h>
#include <string.h>
#define TOP bb_numeric_get(0)
#define BELOW bb_numeric_get(1)
static uint16_t fp_compare(long double a, long double b) {
  return isnan(a) || isnan(b) ? 0x4500 : a < b ? 0x100 : a == b ? 0x4000 : 0;
}
static uint16_t fp_class(void) {
  long double value = TOP;
  return (signbit(value) ? 0x200 : 0) | (isnan(value)             ? 0x100
                                         : isinf(value)           ? 0x500
                                         : value == 0             ? 0x4000
                                         : ilogbl(value) < -16382 ? 0x4400
                                                                  : 0x400);
}
static void fp_add_pair(void) {
  long double value = bb_numeric_pop();
  bb_numeric_set(0, TOP + value);
}
static void fp_log_pair(void) {
  long double value = bb_numeric_pop();
  bb_numeric_set(0, TOP * log2l(value));
}
#define COMPARE bb_model_operands()->comparison
#endif
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
struct extended {
  uint8_t bytes[10];
};
struct x87_state {
  uint8_t bytes[108];
};
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
/* Legacy arguments, locals, descriptors and raw x87 operands remain explicit
 * bridge state. Branching and descriptor/range/error routing are C. */
static uintptr_t stack(uint32_t *r) { return r[3] + 2052; }
static void load_word(uintptr_t p) {
#ifdef BB_MODEL_RUNTIME
  bb_numeric_set_word(WORD(p));
#else
  __asm__ volatile("fldcw %0" ::"m"(*(const uint16_t *)p));

#endif
}
static void status(uintptr_t p) {
#ifdef BB_MODEL_RUNTIME
  WORD(p) = COMPARE | bb_numeric_status();
#else
  __asm__ volatile("fwait;fnstsw %0;fwait" : "=m"(*(uint16_t *)p));

#endif
}
static void operand(uint32_t low, uint32_t high) {
#ifdef BB_MODEL_RUNTIME
  bb_numeric_push_bits(low | ((uint64_t)high << 32));
#else
  __asm__ volatile("pushl %1;pushl %0;call " ASM_NAME(
                       bb_crt_math_load_entry) ";addl $8,%%esp" ::"r"(low),
                   "r"(high)
                   : "eax", "ecx", "edx", "cc", "memory", "st");

#endif
}
static void fp_call_divide(void) {
#ifdef BB_MODEL_RUNTIME
  bb_crt_math_register_divide_entry();
#else
  __asm__ volatile("call " ASM_NAME(bb_crt_math_register_divide_entry)::
                       : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)");

#endif
}
static void zero(void) {
#ifdef BB_MODEL_RUNTIME
  bb_numeric_set(0, 0);
#else
  __asm__ volatile("fstp %%st(0);fldz" ::: "st");
#endif
}
static void pair_one(void) {
#ifdef BB_MODEL_RUNTIME
  bb_crt_math_one_pair_entry();
#else
  __asm__ volatile("fstp %%st(0);fstp %%st(0);fld1" ::: "st", "st(1)");

#endif
}
static void discard_lower(void) {
#ifdef BB_MODEL_RUNTIME
  bb_crt_math_discard_lower_entry();
#else
  __asm__ volatile("fxch %%st(1);fstp %%st(0)" ::: "st", "st(1)");

#endif
}
static void constant(uintptr_t p) {
#ifdef BB_MODEL_RUNTIME
  bb_numeric_push(bb_numeric_read_extended(p));
#else
  __asm__ volatile("fldt %0" ::"m"(*(const struct extended *)p) : "st");

#endif
}
static void test_sign(uint32_t *r) {
  uint32_t value = r[6] & 255, folded = value ^ (value >> 4);
  r[8] = (r[8] & ~0x8d5u) | (!value ? 0x40u : 0) | (value & 0x80u) |
         (((0x9669u >> (folded & 15)) & 1) << 2);
}
static void domain(uint32_t *r, uintptr_t frame, unsigned count) {
  while (count--)
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_pop();
  }
#else
    __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
  constant(0x48e2d0);
  if ((int8_t)BYTE(frame - 0x90) <= 0)
    BYTE(frame - 0x90) = 1;
  test_sign(r);
#ifdef BB_MODEL_RUNTIME
  bb_model_return_float(TOP);
#endif
}
/* Return false when the old helper discarded its caller's continuation. */
static unsigned reduce(uint32_t *r, uintptr_t frame) {
#ifdef BB_MODEL_RUNTIME
  {
    COMPARE = fp_compare(bb_numeric_read_extended(0x48d06e), fabsl(TOP));
  }
#else
  __asm__ volatile("fld %%st(0);fabs;fldt %0;fcompp" ::"m"(
                       *(const struct extended *)0x48d06e)
                   : "st", "st(1)", "st(2)");
#endif
  status(frame - 0xa0);
  if (BYTE(frame - 0x9f) & 0x41) {
#ifdef BB_MODEL_RUNTIME
    {
      COMPARE = fp_compare(TOP, 0);
    }
#else
    __asm__ volatile("ftst");
#endif
    status(frame - 0xa0);
    if (BYTE(frame - 0x9f) & 1) {
      BYTE(frame - 0x90) = 4;
      zero();
    } else {
#ifdef BB_MODEL_RUNTIME
      {
        bb_numeric_pop();
      }
#else
      __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
      constant(0x48d050);
      if (r[6] & 0xff00)
#ifdef BB_MODEL_RUNTIME
      {
        bb_numeric_set(0, -TOP);
      }
#else
        __asm__ volatile("fchs" ::: "st");
#endif
    }
    return 0;
  }
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_push(rintl(TOP));
    COMPARE = fp_compare(TOP, 0);
  }
#else
  __asm__ volatile("fld %%st(0);frndint;ftst" ::: "st", "st(1)");
#endif
  status(frame - 0xa0);
  r[5] = (r[5] & ~255u) | BYTE(frame - 0x9f);
/* Leave the fractional remainder in ST0 and the integer part in ST1. */
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_exchange();
    bb_numeric_set(0, TOP - BELOW);
    COMPARE = fp_compare(TOP, 0);
  }
#else
  __asm__ volatile("fxch %%st(1);fsub %%st(1),%%st(0);ftst" ::: "st", "st(1)");
#endif
  status(frame - 0xa0);
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_set(0, expm1l(fabsl(TOP) * logl(2)));
  }
#else
  __asm__ volatile("fabs;f2xm1" ::: "st");
#endif
  return 1;
}
static void exponential_tail(uint32_t *r, uintptr_t frame) {
  if (!reduce(r, frame))
    return;
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_push(1);
    fp_add_pair();
  }
#else
  __asm__ volatile("fld1;faddp" ::: "st", "st(1)");
#endif
  if (BYTE(frame - 0x9f) & 1) {
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_push(1);
    }
#else
    __asm__ volatile("fld1" ::: "st");
#endif
    if (BB_U32(0x48cc04) == 1)
      fp_call_divide();
    else
#ifdef BB_MODEL_RUNTIME
    {
      bb_crt_math_register_divide_entry();
    }
#else
      __asm__ volatile("fdivp" ::: "st", "st(1)");
#endif
  }
  if (!(r[5] & 0x40))
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_set(0, ldexpl(TOP, (int)truncl(BELOW)));
  }
#else
    __asm__ volatile("fscale" ::: "st", "st(1)");
#endif
  if (r[6] & 0xff00)
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_set(0, -TOP);
  }
#else
    __asm__ volatile("fchs" ::: "st");
#endif
  discard_lower();
}
static void power(uint32_t *r, uintptr_t frame) {
#ifdef BB_MODEL_RUNTIME
  BYTE(frame - 0x90) = 0xfe;
  if (r[6] & 0xff00) {
    r[7] = bb_crt_math_extended_parity_entry();
    if (!r[7]) {
      domain(r, frame, 2);
      return;
    }
    r[6] = (r[6] & ~0xff00u) | (r[7] == 2 ? 0 : 0xff00);
  }
  /* A host may have a 64-bit long double. Evaluating log2(DBL_MAX) there
   * rounds to 1024 and spuriously overflows DBL_MAX^1. Use the native power
   * operation while retaining the CRT's sign, domain and reporting rules. */
  long double exponent = bb_numeric_pop();
  long double result = powl(fabsl(TOP), exponent);
  bb_numeric_set(0, r[6] & 0xff00 ? -result : result);
#else
  BYTE(frame - 0x90) = 0xfe;
  if (r[6] & 0xff00) {
    r[7] = bb_crt_math_extended_parity_entry();
    if (!r[7]) {
      domain(r, frame, 2);
      return;
    }
    r[6] = (r[6] & ~0xff00u) | (r[7] == 2 ? 0 : 0xff00);
    __asm__ volatile("fxch %%st(1);fabs;fyl2x" ::: "st", "st(1)");
  } else
    __asm__ volatile("fxch %%st(1);fyl2x" ::: "st", "st(1)");
  exponential_tail(r, frame);
#endif
}
static void special_power(uint32_t *r, uintptr_t legacy_stack) {
  uintptr_t first = legacy_stack - 140, second = legacy_stack - 132,
            result = legacy_stack - 120, saved = legacy_stack - 112;
  BB_U32(legacy_stack - 4) = r[1];
#ifdef BB_MODEL_RUNTIME
  {
    BB_DOUBLE(second) = (double)bb_numeric_pop();
    BB_DOUBLE(first) = (double)bb_numeric_pop();
    (void)saved;
  }
#else
  __asm__ volatile("fstpl %0;fstpl %1;fwait;fnsave %2"
                   : "=m"(*(uint64_t *)second), "=m"(*(uint64_t *)first),
                     "=m"(*(struct x87_state *)saved)::"st", "st(1)", "st(2)",
                     "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
#endif
  r[7] = bb_crt_math_power_infinity_entry(BB_U32(first), BB_U32(first + 4),
                                          BB_U32(second), BB_U32(second + 4),
                                          (void *)result);
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_push(BB_DOUBLE(result));
  }
#else
  __asm__ volatile("frstor %0;fldl %1" ::"m"(*(const struct x87_state *)saved),
                   "m"(*(const uint64_t *)result)
                   : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)",
                     "st(7)");
#endif
  if (r[7]) {
    BYTE(r[2] - 0x90) = 1;
    test_sign(r);
  }
}
static void nan_value(uint32_t *r, uintptr_t frame, unsigned kind) {
  if (kind == 1)
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_exchange();
  }
#else
    __asm__ volatile("fxch %%st(1)" ::: "st", "st(1)");
#endif
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_store_extended(frame - 0x9e, 0);
  }
#else
  __asm__ volatile("fstpt %0;fldt %0"
                   : "+m"(*(struct extended *)(frame - 0x9e))::"st");
#endif
  unsigned quiet = BYTE(frame - 0x97) & 0x40;
  if (kind == 3 && quiet) {
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_exchange();
      bb_numeric_store_extended(frame - 0x9e, 0);
    }
#else
    __asm__ volatile("fxch %%st(1);fstpt %0;fldt %0"
                     : "+m"(*(struct extended *)(frame - 0x9e))::"st", "st(1)");
#endif
    quiet = BYTE(frame - 0x97) & 0x40;
  }
  BYTE(frame - 0x90) = quiet ? 7 : 1;
  if (!kind) {
    if (!quiet)
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_set(0, TOP + BB_DOUBLE(0x48e2e4));
    }
#else
      __asm__ volatile("faddl %0" ::"m"(*(const uint64_t *)0x48e2e4) : "st");
#endif
  } else
#ifdef BB_MODEL_RUNTIME
  {
    fp_add_pair();
  }
#else
    __asm__ volatile("faddp" ::: "st", "st(1)");
#endif
  (void)r;
}
static unsigned calculation(uint32_t *r, uintptr_t target,
                            uintptr_t legacy_stack) {
  uintptr_t frame = r[2];
  switch (target) {
  case 0x473010:
    power(r, frame);
    break;
  case 0x473021:
    BYTE(frame - 0x90) = 0xfe;
    r[6] &= ~0xff00u;
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_set(0, TOP * log2l(expl(1)));
    }
#else
    __asm__ volatile("fldl2e;fmulp" ::: "st", "st(1)");
#endif
    exponential_tail(r, frame);
    break;
  case 0x473078:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_exchange();
    bb_numeric_set(0, fabsl(TOP));
    fp_log_pair();
  }
#else
    __asm__ volatile("fxch %%st(1);fabs;fyl2x" ::: "st", "st(1)");
#endif
    exponential_tail(r, frame);
    break;
  case 0x47307e:
  case 0x4730d8:
  case 0x47cf6b:
    pair_one();
    break;
  case 0x473088:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_pop();
    bb_numeric_pop();
  }
#else
    __asm__ volatile("fstp %%st(0);fstp %%st(0)" ::: "st", "st(1)");
#endif
    constant(0x48d050);
    BYTE(frame - 0x90) = 2;
    break;
  case 0x47309a:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_push(logl(2));
    bb_numeric_exchange();
    COMPARE = fp_compare(TOP, 0);
  }
#else
    __asm__ volatile("fldln2;fxch %%st(1);ftst" ::: "st", "st(1)");
#endif
    status(frame - 0xa0);
    if (BYTE(frame - 0x9f) & 0x41)
      domain(r, frame, 2);
    else
#ifdef BB_MODEL_RUNTIME
    {
      fp_log_pair();
    }
#else
      __asm__ volatile("fyl2x" ::: "st", "st(1)");
#endif
    break;
  case 0x4730b4:
    BYTE(frame - 0x90) = 2;
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_pop();
    }
#else
    __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
    constant(0x48d05a);
    break;
  case 0x4730c4:
    test_sign(r);
    if (r[6] & 255)
      domain(r, frame, 1);
    break;
  case 0x4730c9:
  case 0x4730cd:
  case 0x4730cf:
    if (target == 0x4730c9)
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_push(log10l(2));
    }
#else
      __asm__ volatile("fldlg2" ::: "st");
#endif
    else if (target == 0x4730cd)
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_push(logl(2));
    }
#else
      __asm__ volatile("fldln2" ::: "st");
#endif
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_exchange();
    }
#else
    __asm__ volatile("fxch %%st(1)" ::: "st", "st(1)");
#endif
    test_sign(r);
    if (r[6] & 255)
      domain(r, frame, 2);
    else
#ifdef BB_MODEL_RUNTIME
    {
      fp_log_pair();
    }
#else
      __asm__ volatile("fyl2x" ::: "st", "st(1)");
#endif
    break;
  case 0x4730dd:
    r[7] = bb_crt_math_extended_parity_entry();
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_pop();
      bb_numeric_pop();
    }
#else
    __asm__ volatile("fstp %%st(0);fstp %%st(0)" ::: "st", "st(1)");
#endif
    test_sign(r);
    if (r[6] & 255) {
      BYTE(frame - 0x90) = 2;
      constant(0x48d050);
    } else
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_push(0);
    }
#else
      __asm__ volatile("fldz" ::: "st");
#endif
    if (r[7] == 1 && (r[6] & 0xff00))
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_set(0, -TOP);
    }
#else
      __asm__ volatile("fchs" ::: "st");
#endif
    break;
  case 0x473112:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_pop();
  }
#else
    __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
    zero();
    break;
  case 0x47314f:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_pop();
    bb_numeric_pop();
  }
#else
    __asm__ volatile("fstp %%st(0);fstp %%st(0)" ::: "st", "st(1)");
#endif
    constant(0x48d050);
    BYTE(frame - 0x90) = 3;
    break;
  case 0x473161:
    test_sign(r);
    if (r[6] & 255) {
      zero();
    } else {
#ifdef BB_MODEL_RUNTIME
      {
        bb_numeric_pop();
      }
#else
      __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
      constant(0x48d050);
    }
    break;
  case 0x4731e5:
    special_power(r, legacy_stack);
    break;
  case 0x47cf58:
    discard_lower();
    break;
  case 0x47cf5d:
    BYTE(frame - 0x90) = 1;
    test_sign(r);
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_pop();
    }
#else
    __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
    break;
  case 0x47cf64:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_pop();
  }
#else
    __asm__ volatile("fstp %%st(0)" ::: "st");
#endif
    zero();
    break;
  case 0x47cf66:
    zero();
    break;
  case 0x47cf6d:
#ifdef BB_MODEL_RUNTIME
  {
    bb_numeric_set(0, 1);
  }
#else
    __asm__ volatile("fstp %%st(0);fld1" ::: "st");
#endif
  break;
  case 0x47cf72:
    nan_value(r, frame, 0);
    break;
  case 0x47cf9d:
    nan_value(r, frame, 1);
    break;
  case 0x47cf9f:
    nan_value(r, frame, 2);
    break;
  case 0x47cfc7:
    nan_value(r, frame, 3);
    break;
  case 0x47d006:
    domain(r, frame, 2);
    break;
  case 0x47d008:
    domain(r, frame, 1);
    break;
  case 0x47d019:
    BYTE(frame - 0x90) = 1;
    test_sign(r);
    break;
  case 0x47d023:
    test_sign(r);
    if (r[6] & 255)
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_set(0, -TOP);
    }
#else
      __asm__ volatile("fchs" ::: "st");
#endif
    break;
  default:
    return 0;
  }
#ifdef BB_MODEL_RUNTIME
  bb_model_return_float(TOP);
#endif
  return 1;
}
/* A custom descriptor retains the original register callback boundary. Known
 * calculation targets route to the C implementation above. */
#ifdef BB_MODEL_RUNTIME
static void callback(uint32_t *r, uintptr_t target) {
  struct bb_call_state state = {r[7], r[6], r[5], r[8]};
  struct bb_model_saved saved = {r[0], r[1], r[2], r[4]};
  bb_model_invoke_frame(&state, target, NULL, 0, &saved, NULL);
  r[7] = state.eax;
  r[6] = state.ecx;
  r[5] = state.edx;
  r[8] = state.flags;
}
#else
static void __attribute__((naked, noinline)) callback(uint32_t *r
                                                      __attribute__((unused)),
                                                      uintptr_t target
                                                      __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;pushl %edi;"
      "pushl 8(%ebp);pushl 12(%ebp);movl 8(%ebp),%eax;"
      "pushl 32(%eax);popfl;movl (%eax),%edi;movl 4(%eax),%esi;"
      "movl 16(%eax),%ebx;movl 20(%eax),%edx;movl 24(%eax),%ecx;"
      "movl 8(%eax),%ebp;movl 28(%eax),%eax;call *(%esp);"
      "pushfl;pushal;movl 40(%esp),%eax;"
      "movl (%esp),%edx;movl %edx,(%eax);movl 4(%esp),%edx;movl %edx,4(%eax);"
      "movl 8(%esp),%edx;movl %edx,8(%eax);movl 16(%esp),%edx;movl "
      "%edx,16(%eax);"
      "movl 20(%esp),%edx;movl %edx,20(%eax);movl 24(%esp),%edx;movl "
      "%edx,24(%eax);"
      "movl 28(%esp),%edx;movl %edx,28(%eax);movl 32(%esp),%edx;movl "
      "%edx,32(%eax);"
      "lea 44(%esp),%esp;popl %edi;popl %esi;popl %ebx;popl %ebp;ret");
}
#endif
static uint8_t selector(uint8_t high) {
  high = (uint8_t)((int8_t)(high << 1) >> 1);
  high = (uint8_t)((high << 1) | (high >> 7));
  return BYTE(0x48e2ed + (high & 15));
}
static void dispatch(uint32_t *r, unsigned binary, uintptr_t legacy_stack) {
  uintptr_t descriptor = r[5], frame = r[2];
  uint16_t control = BYTE(descriptor + 14) == 5
                         ? (WORD(frame - 0xa4) | 0x200) & ~0x100u
                         : 0x133f;
  control = (control & 0xff00) | 0x3f;
  WORD(frame - 0xa2) = control;
  load_word(frame - 0xa2);
#ifdef BB_MODEL_RUNTIME
  {
    COMPARE = fp_class();
  }
#else
  __asm__ volatile("fxam");
#endif
  BB_U32(frame - 0x94) = descriptor;
  status(frame - 0xa0);
  BYTE(frame - 0x90) = 0;
  uint8_t first = BYTE(frame - 0x9f), second = 0;
  if (binary) {
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_exchange();
      COMPARE = fp_class();
    }
#else
    __asm__ volatile("fxch %%st(1);fxam" ::: "st", "st(1)");
#endif
    status(frame - 0xa0);
#ifdef BB_MODEL_RUNTIME
    {
      bb_numeric_exchange();
    }
#else
    __asm__ volatile("fxch %%st(1)" ::: "st", "st(1)");
#endif
    second = BYTE(frame - 0x9f);
  }
  uint8_t offset = selector(first) | (binary ? selector(second) << 2 : 0);
  /* The transformed classification retains its sign bit as bit two. */
  r[6] = ((first & 2) ? 4 : 0) | ((second & 2) ? 0x400 : 0);
  r[7] = (int8_t)offset;
  r[4] = descriptor + (int8_t)offset + 16;
  uintptr_t target = BB_U32(r[4]);
  if (!calculation(r, target, legacy_stack))
    callback(r,
#ifdef BB_MODEL_RUNTIME
             target
#else
             BB_CODE(target)
#endif
    );
}
/* Use the completed C x87-error worker with explicit old call-frame storage.
 * This keeps inherited IEEE record slots stable until the bridge is removed. */
extern void bb_crt_math_x87_exception_c(uint32_t *);
static void report(uint32_t *r, uintptr_t frame, uintptr_t legacy_stack) {
  BB_U32(legacy_stack - 4) = r[1];
  BB_U32(legacy_stack - 8) = r[0];
  BB_U32(legacy_stack - 12) = frame - 0xa4;
  BB_U32(legacy_stack - 16) = frame - 0x8e;
  BB_U32(legacy_stack - 20) = (int8_t)BYTE(BB_U32(frame - 0x94) + 14);
  uint32_t saved[8] = {0};
  saved[3] = legacy_stack - 24 - 132;
  bb_crt_math_x87_exception_c(saved);
}
static void finalize(uint32_t *r, uintptr_t legacy_stack) {
  uintptr_t frame = r[2];
  if (!BB_U32(0x48cc00)) {
#ifdef BB_MODEL_RUNTIME
    {
      BB_DOUBLE(0x6c8890) = (double)TOP;
    }
#else
    __asm__ volatile("fstl %0" : "=m"(*(uint64_t *)0x6c8890));
#endif
    int32_t error = (int8_t)BYTE(frame - 0x90);
    unsigned reporting = error && error != -1 && error != -2;
    if (error == -1 || error == -2) {
      uint16_t exponent = WORD(0x6c8896) & 0x7ff0;
      if (!exponent || exponent == 0x7ff0) {
        unsigned underflow = !exponent;
        BB_U32(frame - 0x8e) = underflow ? 4 : 3;
        uintptr_t scale = underflow ? 0x482fd8 : 0x482fd0,
                  limit = underflow ? 0x482fc8 : 0x482fc0;
        uint16_t flags;
#ifdef BB_MODEL_RUNTIME
        {
          bb_numeric_set(0, ldexpl(TOP, (int)BB_DOUBLE(scale)));
          flags = fp_compare(fabsl(TOP), BB_DOUBLE(limit));
        }
#else
        __asm__ volatile("fldl %1;fxch %%st(1);fscale;fstp %%st(1);"
                         "fld %%st(0);fabs;fcompl %2;fwait;fnstsw %%ax"
                         : "=a"(flags)
                         : "m"(*(const uint64_t *)scale),
                           "m"(*(const uint64_t *)limit)
                         : "st", "st(1)");
#endif
        if (underflow ? (flags & 0x100) != 0 : !(flags & 0x4100)) {
          uintptr_t factor = underflow ? 0x482fe8 : 0x482fe0;
#ifdef BB_MODEL_RUNTIME
          {
            bb_numeric_set(0, TOP * BB_DOUBLE(factor));
          }
#else
          __asm__ volatile("fmull %0" ::"m"(*(const uint64_t *)factor) : "st");
#endif
        }
        reporting = 1;
      } else
        error = 0;
    }
    if (reporting && error != -1 && error != -2)
      BB_U32(frame - 0x8e) = error;
    if (!reporting && !(WORD(frame - 0xa4) & 32)) {
      uint16_t flags;
#ifdef BB_MODEL_RUNTIME
      {
        flags = bb_numeric_status();
      }
#else
      __asm__ volatile("fwait;fnstsw %%ax" : "=a"(flags));
#endif
      if (flags & 32) {
        BB_U32(frame - 0x8e) = 8;
        reporting = 1;
      }
    }
    if (reporting) {
      uintptr_t descriptor = BB_U32(frame - 0x94);
      r[4] = descriptor + 1;
      BB_U32(frame - 0x8a) = r[4];
      if (!BYTE(0x6c8898)) {
        BB_U32(frame - 0x86) = BB_U32(frame + 8);
        BB_U32(frame - 0x82) = BB_U32(frame + 12);
        if (BYTE(descriptor + 13) != 1) {
          BB_U32(frame - 0x7e) = BB_U32(frame + 16);
          BB_U32(frame - 0x7a) = BB_U32(frame + 20);
        }
      }
#ifdef BB_MODEL_RUNTIME
      {
        BB_DOUBLE(frame - 0x76) = (double)bb_numeric_pop();
      }
#else
      __asm__ volatile("fstpl %0" : "=m"(*(uint64_t *)(frame - 0x76))::"st");
#endif
      report(r, frame, legacy_stack);
#ifdef BB_MODEL_RUNTIME
      {
        bb_numeric_push(BB_DOUBLE(frame - 0x76));
      }
#else
      __asm__ volatile("fldl %0" ::"m"(*(const uint64_t *)(frame - 0x76))
                       : "st");
#endif
      r[4] = descriptor;
    }
  }
  load_word(frame - 0xa4);
#ifdef BB_MODEL_RUNTIME
  bb_model_return_float(TOP);
#endif
#ifdef BB_MODEL_RUNTIME
  {
    (void)0;
  }
#else
  __asm__ volatile("fwait");
#endif
}
static void wrapper(uint32_t *r, unsigned binary, unsigned memory) {
  uintptr_t caller = stack(r), frame = caller - 4, arguments = caller + 4;
  uint32_t old_frame = r[2], old_bx = r[4];
  BB_U32(frame) = old_frame;
  BB_U32(frame - 0x2a4) = old_bx;
  r[2] = frame;
  if (memory) {
    operand(BB_U32(arguments), BB_U32(arguments + 4));
    if (binary)
      operand(BB_U32(arguments + 8), BB_U32(arguments + 12));
  }
#ifdef BB_MODEL_RUNTIME
  {
    WORD(frame - 0xa4) = bb_numeric_word();
  }
#else
  __asm__ volatile("fwait;fnstcw %0" : "=m"(*(uint16_t *)(frame - 0xa4)));
#endif
  if (!memory && !BB_U32(0x48e468)) {
    if (binary) {
#ifdef BB_MODEL_RUNTIME
      {
        BB_DOUBLE(frame - 0x86) = (double)BELOW;
        BB_DOUBLE(frame - 0x7e) = (double)TOP;
      }
#else
      __asm__ volatile("fxch %%st(1);fstl %0;fxch %%st(1);fstl %1"
                       : "=m"(*(uint64_t *)(frame - 0x86)),
                         "=m"(*(uint64_t *)(frame - 0x7e))::"st", "st(1)");
#endif
    } else
#ifdef BB_MODEL_RUNTIME
    {
      BB_DOUBLE(frame - 0x86) = (double)TOP;
    }
#else
      __asm__ volatile("fstl %0" : "=m"(*(uint64_t *)(frame - 0x86)));
#endif
  }
  dispatch(r, binary, frame - 0x2a8);
  BYTE(0x6c8898) = !memory;
  finalize(r, frame - 0x2a8);
#ifdef BB_MODEL_RUNTIME
  /* Memory-form operands belong to this call; the result slot transfers its
   * value without retaining an implicit register on the next C invocation. */
  if (memory)
    bb_numeric_pop();
#endif
  r[2] = old_frame;
  r[4] = old_bx;
}
void __attribute__((used, noinline)) bb_crt_calculation_reduce_c(uint32_t *r) {
  unsigned normal = reduce(r, r[2]);
  BB_U32(stack(r) - 12) = !normal;
  if (!normal)
    r[7] = BB_U32(stack(r));
#ifdef BB_MODEL_RUNTIME
  bb_model_return_float(TOP);
#endif
}
#define WORKER(name, body)                                                     \
  void __attribute__((used, noinline)) name##_c(uint32_t *r) { body; }
WORKER(bb_crt_calculation_domain, domain(r, r[2], 2))
WORKER(bb_crt_calculation_dispatch_binary, dispatch(r, 1, stack(r)))
WORKER(bb_crt_calculation_dispatch_unary, dispatch(r, 0, stack(r)))
WORKER(bb_crt_calculation_finalize_memory, BYTE(0x6c8898) = 0;
       finalize(r, stack(r)))
WORKER(bb_crt_calculation_finalize, finalize(r, stack(r)))
WORKER(bb_crt_calculation_binary_memory, wrapper(r, 1, 1))
WORKER(bb_crt_calculation_unary_memory, wrapper(r, 0, 1))
WORKER(bb_crt_calculation_binary_register, wrapper(r, 1, 0))
WORKER(bb_crt_calculation_unary_register, wrapper(r, 0, 0))
#define PUBLIC(name, descriptor, binary, memory)                               \
  WORKER(bb_crt_calculation_##name, r[5] = descriptor;                         \
         wrapper(r, binary, memory))
PUBLIC(power, 0x48d094, 1, 1)
PUBLIC(log, 0x48d104, 0, 1)
PUBLIC(log10, 0x48d0e4, 0, 1)
PUBLIC(exp, 0x48d124, 0, 1)
PUBLIC(power_register, 0x48d094, 1, 0)
PUBLIC(log_register, 0x48d104, 0, 0)
PUBLIC(log10_register, 0x48d0e4, 0, 0)
PUBLIC(exp_register, 0x48d124, 0, 0)
#define LEAF(name, address)                                                    \
  WORKER(bb_crt_calculation_##name, calculation(r, address, stack(r)))
LEAF(power_normal, 0x473010)
LEAF(exp_normal, 0x473021)
LEAF(power_one, 0x4730d8)
LEAF(power_zero, 0x4730dd)
LEAF(power_singularity, 0x473088)
LEAF(log_checked, 0x47309a)
LEAF(log_zero, 0x4730b4)
LEAF(log_infinity, 0x4730c4)
LEAF(log10_normal, 0x4730c9)
LEAF(log_normal, 0x4730cd)
LEAF(power_underflow, 0x473112)
LEAF(power_overflow, 0x47314f)
LEAF(exp_infinity, 0x473161)
LEAF(power_special, 0x4731e5)
LEAF(nan_unary, 0x47cf72)
LEAF(nan_binary, 0x47cf9f)
LEAF(nan_pair, 0x47cfc7)
LEAF(negate, 0x47d023)
#ifndef BB_MODEL_RUNTIME
#define PROLOGUE(name)                                                         \
  "lea -2048(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(          \
      name##_c) ";addl $4,%esp;"
#define EPILOGUE "popal;popfl;lea 2048(%esp),%esp;ret"
#define ENTRY(name, signature)                                                 \
  long double __attribute__((naked, noinline)) name##_entry signature {        \
    __asm__ volatile(PROLOGUE(name) EPILOGUE);                                 \
  }
#define A uint32_t a __attribute__((unused))
#define B uint32_t b __attribute__((unused))
#define C uint32_t c __attribute__((unused))
#define D uint32_t d __attribute__((unused))
ENTRY(bb_crt_calculation_power, (A, B, C, D))
ENTRY(bb_crt_calculation_log, (A, B))
ENTRY(bb_crt_calculation_log10, (A, B))
ENTRY(bb_crt_calculation_exp, (A, B))
ENTRY(bb_crt_calculation_binary_memory, (A, B, C, D))
ENTRY(bb_crt_calculation_unary_memory, (A, B))
ENTRY(bb_crt_calculation_power_register, (void))
ENTRY(bb_crt_calculation_log_register, (void))
ENTRY(bb_crt_calculation_log10_register, (void))
ENTRY(bb_crt_calculation_exp_register, (void))
ENTRY(bb_crt_calculation_binary_register, (void))
ENTRY(bb_crt_calculation_unary_register, (void))
ENTRY(bb_crt_calculation_dispatch_binary, (void))
ENTRY(bb_crt_calculation_dispatch_unary, (void))
ENTRY(bb_crt_calculation_finalize_memory, (void))
ENTRY(bb_crt_calculation_finalize, (void))
ENTRY(bb_crt_calculation_domain, (void))
#define LEAF_ENTRY(name) ENTRY(bb_crt_calculation_##name, (void))
BB_CRT_CALCULATION_LEAVES(LEAF_ENTRY)
long double __attribute__((naked, noinline))
bb_crt_calculation_reduce_entry(void) {
  __asm__ volatile(PROLOGUE(
      bb_crt_calculation_reduce) "cmpl $0,2072(%esp);jne 1f;" EPILOGUE
                                 ";1:popal;popfl;lea 2052(%esp),%esp;ret");
}

#endif
