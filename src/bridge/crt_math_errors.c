/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_math_errors.h"
#include "abi.h"
#include "crt_math_base.h"
#include "crt_signals.h"
#include "interop.h"
#include <stddef.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
#include <fenv.h>
#include <math.h>
#endif
#define WIN BB_STDCALL
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
static double load_bits(uint64_t raw) {
  double result;
#ifdef BB_MODEL_RUNTIME
  memcpy(&result, &raw, sizeof result);
#else
  __asm__ volatile("fldl %1" : "=t"(result) : "m"(raw));
#endif
  return result;
}
static uint16_t compare_zero(uint64_t raw) {
#ifdef BB_MODEL_RUNTIME
  double value = load_bits(raw);
  return isnan(value) ? 0x4500 : value < 0 ? 0x100 : value == 0 ? 0x4000 : 0;
#else
  uint16_t status;
  __asm__ volatile("fldl %1;fcompl %2;fnstsw %%ax"
                   : "=a"(status)
                   : "m"(raw), "m"(*(const double *)0x4833b0)
                   : "st");
  return status;
#endif
}
int32_t bb_crt_math_errno_entry(int32_t kind) {
  if (kind == 1)
    BB_U32(0x48cba8) = 33;
  else if (kind == 2 || kind == 3)
    BB_U32(0x48cba8) = 34;
  return kind;
}
const char *bb_crt_math_name_entry(uint32_t operation) {
  for (uint32_t i = 0; i < 27; ++i)
    if (BB_U32(0x48def8 + i * 8) == operation)
      return BB_POINTER(0x48defc + i * 8);
  return NULL;
}
uint32_t bb_crt_math_error_kind_entry(uint32_t flags) {
  return flags & 32  ? 5
         : flags & 8 ? 1
         : flags & 4 ? 2
         : flags & 1 ? 3
         : flags & 2 ? 4
                     : 0;
}
void bb_crt_math_raise_status_entry(uint32_t flags) {
#ifdef BB_MODEL_RUNTIME
  int raised = (flags & 1 ? FE_INVALID : 0) |
               (flags & 8 ? FE_OVERFLOW | FE_INEXACT : 0) |
               (flags & 16 ? FE_UNDERFLOW | FE_INEXACT : 0) |
               (flags & 4 ? FE_DIVBYZERO : 0) | (flags & 32 ? FE_INEXACT : 0);
  feraiseexcept(raised);
#else
  int32_t integer;
  uint64_t temporary;
  uint16_t status;
  if (flags & 1)
    __asm__ volatile("fldt %1;fistpl %0;fwait"
                     : "=m"(integer)
                     : "m"(*(const struct extended *)0x48dfd0)
                     : "st");
  if (flags & 8)
    __asm__ volatile(
        "fwait;fnstsw %%ax;fldt %2;fstpl %1;fwait;fwait;fnstsw %%ax"
        : "=a"(status), "=m"(temporary)
        : "m"(*(const struct extended *)0x48dfd0)
        : "st");
  if (flags & 16)
    __asm__ volatile("fldt %1;fstpl %0;fwait"
                     : "=m"(temporary)
                     : "m"(*(const struct extended *)0x48dfe0)
                     : "st");
  if (flags & 4)
    __asm__ volatile("fldz;fld1;fdivp;fstp %%st(0);fwait" ::: "st", "st(1)");
  if (flags & 32)
    __asm__ volatile("fldpi;fstpl %0;fwait" : "=m"(temporary)::"st");
#endif
}
int32_t bb_crt_math_normalize_entry(uint32_t flags, void *result,
                                    uint32_t control) {
  uint32_t remaining = flags & 31;
  uintptr_t p = (uintptr_t)result;
  if ((flags & 8) && (control & 1)) {
    bb_crt_math_raise_status_entry(1);
    remaining &= ~8u;
  } else if ((flags & 4) && (control & 4)) {
    bb_crt_math_raise_status_entry(4);
    remaining &= ~4u;
  } else if ((flags & 1) && (control & 8)) {
    bb_crt_math_raise_status_entry(8);
    unsigned positive = !(compare_zero(read_bits(p)) & 0x4100),
             rounding = (control >> 10) & 3;
    uintptr_t source =
        rounding == 3 || (positive ? rounding == 1 : rounding == 2) ? 0x48e000
                                                                    : 0x48dff0;
    if (positive)
      store_bits(p, read_bits(source));
    else {
#ifdef BB_MODEL_RUNTIME
      store_bits(p, read_bits(source) ^ UINT64_C(0x8000000000000000));
#else
      __asm__ volatile("fldl %1;fchs;fstpl %0"
                       : "=m"(*(uint64_t *)p)
                       : "m"(*(const uint64_t *)source)
                       : "st");
#endif
    }
    remaining &= ~1u;
  } else if ((flags & 2) && (control & 16)) {
    unsigned lost = !!(flags & 16);
    uint64_t raw = read_bits(p);
    if (compare_zero(raw) & 0x4000)
      lost = 1;
    else {
      int32_t exponent;
      double number = bb_crt_math_fraction_entry(raw, raw >> 32, &exponent);
      uint64_t fraction;
#ifdef BB_MODEL_RUNTIME
      memcpy(&fraction, &number, sizeof fraction);
#else
      __asm__ volatile("fstpl %0" : "=m"(fraction) : "t"(number) : "st");
#endif
      exponent -= 1536;
      if (exponent < -1074) {
        fraction = 0;
        lost = 1;
      } else {
        unsigned negative = !!(compare_zero(fraction) & 0x100);
        fraction = (fraction & UINT64_C(0x000fffffffffffff)) |
                   UINT64_C(0x0010000000000000);
        while (exponent < -1021) {
          if ((fraction & 1) && !lost)
            lost = 1;
          fraction >>= 1;
          ++exponent;
        }
        if (negative) {
#ifdef BB_MODEL_RUNTIME
          fraction ^= UINT64_C(0x8000000000000000);
#else
          __asm__ volatile("fldl %1;fchs;fstpl %0"
                           : "=m"(fraction)
                           : "m"(fraction)
                           : "st");
#endif
        }
      }
      store_bits(p, fraction);
    }
    if (lost)
      bb_crt_math_raise_status_entry(16);
    remaining &= ~2u;
  }
  if ((flags & 16) && (control & 32)) {
    bb_crt_math_raise_status_entry(32);
    remaining &= ~16u;
  }
  return remaining == 0;
}
static void raise_exception(uintptr_t record, uintptr_t control, uint32_t flags,
                            uint32_t operation, uintptr_t operand,
                            uintptr_t result, uint32_t code) {
  BB_U32(record + 4) = 0;
  BB_U32(record + 8) = 0;
  BB_U32(record + 12) = 0;
  static const uint32_t flag_order[] = {16, 2, 1, 4, 8},
                        codes[] = {0xc000008f, 0xc0000093, 0xc0000091,
                                   0xc000008e, 0xc0000090};
  for (unsigned i = 0; i < 5; ++i)
    if (flags & flag_order[i]) {
      code = codes[i];
      BB_U32(record + 4) |= 1u << i;
    }
  static const uint32_t hardware[] = {1, 4, 8, 16, 32},
                        ieee[] = {16, 8, 4, 2, 1};
  for (unsigned i = 0; i < 5; ++i) {
    if (BB_U32(control) & hardware[i])
      BB_U32(record + 8) &= ~ieee[i];
    else
      BB_U32(record + 8) |= ieee[i];
  }
  uint32_t status = bb_crt_math_status_entry();
  for (unsigned i = 0; i < 5; ++i)
    if (status & hardware[i])
      BB_U32(record + 12) |= ieee[i];
  switch (BB_U32(control) & 0xc00) {
  case 0:
    BB_U32(record) &= ~3u;
    break;
  case 0x400:
    BB_U32(record) = (BB_U32(record) & ~2u) | 1;
    break;
  case 0x800:
    BB_U32(record) = (BB_U32(record) & ~1u) | 2;
    break;
  case 0xc00:
    BB_U32(record) |= 3;
    break;
  }
  switch (BB_U32(control) & 0x300) {
  case 0:
    BB_U32(record) = (BB_U32(record) & ~0x14u) | 8;
    break;
  case 0x200:
    BB_U32(record) = (BB_U32(record) & ~0x18u) | 4;
    break;
  case 0x300:
    BB_U32(record) &= ~0x1cu;
    break;
  }
  BB_U32(record) = ((operation & 0xfff) << 5) | (BB_U32(record) & 0xfffe001f);
  BB_U32(record + 32) |= 1;
  BB_U32(record + 32) = (BB_U32(record + 32) & ~0x1cu) | 2;
  BB_U32(record + 20) = BB_U32(operand + 4);
  BB_U32(record + 16) = BB_U32(operand);
  BB_U32(record + 80) |= 1;
  BB_U32(record + 80) = (BB_U32(record + 80) & ~0x1cu) | 2;
  BB_U32(record + 68) = BB_U32(result + 4);
  BB_U32(record + 64) = BB_U32(result);
  bb_crt_math_clear_entry();
#ifdef BB_MODEL_RUNTIME
  /* Unmasked floating failures use the portable C recovery boundary. The
   * diagnostic IEEE record remains populated; x86 context resumption is not
   * part of the native port's failure contract. */
  bb_model_raise(code);
#else
  uint32_t parameter = record;
  ((void(WIN *)(uint32_t, uint32_t, uint32_t, uint32_t *))BB_CODE(
      BB_POINTER(0x6ca404)))(code, 0, 1, &parameter);
  record = parameter;
#endif
  for (unsigned i = 0; i < 5; ++i)
    if (BB_U32(record + 8) & ieee[i])
      BB_U32(control) &= ~hardware[i];
  switch (BB_U32(record) & 3) {
  case 0:
    BB_U32(control) &= ~0xc00u;
    break;
  case 1:
    BB_U32(control) = (BB_U32(control) & ~0x800u) | 0x400;
    break;
  case 2:
    BB_U32(control) = (BB_U32(control) & ~0x400u) | 0x800;
    break;
  case 3:
    BB_U32(control) |= 0xc00;
    break;
  }
  /* The legacy resume code clears rounding bits while OR-ing precision. */
  switch ((BB_U32(record) >> 2) & 7) {
  case 0:
    BB_U32(control) = (BB_U32(control) & ~0xc00u) | 0x300;
    break;
  case 1:
    BB_U32(control) = (BB_U32(control) & ~0xc00u) | 0x200;
    break;
  case 2:
    BB_U32(control) &= ~0xc00u;
    break;
  }
  BB_U32(result + 4) = BB_U32(record + 68);
  BB_U32(result) = BB_U32(record + 64);
}
static uint64_t report(int32_t kind, uint32_t operation, uint64_t first,
                       uint64_t second, uint64_t result, uint32_t control) {
  const char *name = bb_crt_math_name_entry(operation);
  if (!name) {
    bb_crt_math_word_entry(control, 0xffff);
    bb_crt_math_errno_entry(kind);
    return result;
  }
  struct {
    int32_t type;
    uint32_t name;
    uint32_t first[2], second[2], result[2];
  } record = {kind,
              bb_address_encode(name),
              {first, first >> 32},
              {second, second >> 32},
              {result, result >> 32}};
  bb_crt_math_word_entry(control, 0xffff);
  if (!bb_crt_signal_math_error_entry(&record))
    bb_crt_math_errno_entry(kind);
  return read_bits((uintptr_t)record.result);
}
double bb_crt_math_report_entry(int32_t kind, uint32_t operation, uint32_t al,
                                uint32_t ah, uint32_t bl, uint32_t bh,
                                uint32_t rl, uint32_t rh, uint32_t control) {
  return load_bits(report(kind, operation, bits(al, ah), bits(bl, bh),
                          bits(rl, rh), control));
}
static uint64_t quiet_nan(uint32_t operation, uint64_t raw, uint32_t control) {
  if (!BB_U32(0x48e468))
    return report(1, operation, raw, 0, raw, control);
  BB_U32(0x48cba8) = 33;
  bb_crt_math_word_entry(control, 0xffff);
  return raw;
}
double bb_crt_math_quiet_nan_entry(uint32_t operation, uint32_t low,
                                   uint32_t high, uint32_t control) {
  return load_bits(quiet_nan(operation, bits(low, high), control));
}
static uint64_t exception(uint32_t flags, uint32_t operation, uint64_t operand,
                          uint64_t result, uint32_t control, uintptr_t record) {
  if (!bb_crt_math_normalize_entry(flags, &result, control)) {
    BB_U32(record + 56) &= ~1u;
    raise_exception(record, (uintptr_t)&control, flags, operation,
                    (uintptr_t)&operand, (uintptr_t)&result, 0);
  }
  uint32_t kind = bb_crt_math_error_kind_entry(flags);
  if (!BB_U32(0x48e468) && kind)
    return report(kind, operation, operand, 0, result, control);
  bb_crt_math_errno_entry(kind);
  bb_crt_math_word_entry(control, 0xffff);
  return result;
}
static uint64_t floor_value(uint64_t raw, uintptr_t record) {
  uint32_t control = bb_crt_math_word_entry(BB_U32(0x48cca0), 0xffff);
  if ((raw & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) {
    int32_t kind = bb_crt_math_special_entry(raw, raw >> 32);
    if (kind >= 1 && kind <= 2) {
      bb_crt_math_word_entry(control, 0xffff);
      return raw;
    }
    if (kind == 3)
      return quiet_nan(11, raw, control);
    uint64_t result;
#ifdef BB_MODEL_RUNTIME
    result = raw | UINT64_C(0x0008000000000000);
#else
    __asm__ volatile("fldl %1;faddl %2;fstpl %0"
                     : "=m"(result)
                     : "m"(raw), "m"(*(const double *)0x482be8)
                     : "st");
#endif
    return exception(8, 11, raw, result, control, record);
  }
  double rounded = bb_crt_math_round_entry(raw, raw >> 32);
  uint64_t result;
  uint16_t status;
#ifdef BB_MODEL_RUNTIME
  memcpy(&result, &rounded, sizeof result);
  status = rounded == load_bits(raw) ? 0x4000 : 0;
#else
  __asm__ volatile("fcoml %2;fstpl %1;fnstsw %%ax"
                   : "=a"(status), "=m"(result)
                   : "m"(raw), "t"(rounded)
                   : "st");
#endif
  if ((status & 0x4000) || (control & 32)) {
    bb_crt_math_word_entry(control, 0xffff);
    return result;
  }
  return exception(16, 11, raw, result, control, record);
}
static void x87_exception(uint32_t operation, uintptr_t error,
                          uintptr_t control_pointer, uintptr_t record) {
  uint32_t control = BB_U16(control_pointer), flags;
  switch (BB_U32(error)) {
  case 1:
  case 5:
    flags = 8;
    break;
  case 2:
    flags = 4;
    break;
  case 3:
    flags = 17;
    break;
  case 4:
    flags = 18;
    break;
  case 7:
    BB_U32(error) = 1;
    flags = 0;
    break;
  case 8:
    flags = 16;
    break;
  default:
    flags = 0;
    break;
  }
  if (flags &&
      !bb_crt_math_normalize_entry(flags, (void *)(error + 24), control)) {
    if (operation == 16 || operation == 22 || operation == 29) {
      BB_U32(record + 56) |= 1;
      BB_U32(record + 56) = (BB_U32(record + 56) & ~0x1cu) | 2;
      BB_U32(record + 44) = BB_U32(error + 20);
      BB_U32(record + 40) = BB_U32(error + 16);
    } else
      BB_U32(record + 56) &= ~1u;
    raise_exception(record, (uintptr_t)&control, flags, operation, error + 8,
                    error + 24, 0);
  }
  bb_crt_math_word_entry(control, 0xffff);
  int32_t handled = 0;
  if (BB_U32(error) != 8 && !BB_U32(0x48e468))
    handled = bb_crt_signal_math_error_entry((void *)error);
  if (!handled)
    bb_crt_math_errno_entry(BB_U32(error));
}
/* Preserve the old undefined record/code slots as explicit bridge storage. */
static void floating_output(uint32_t *r, uint64_t raw) {
#ifdef BB_MODEL_RUNTIME
  (void)r;
  bb_model_return_float(load_bits(raw));
#else
  __asm__ volatile("fldl %1;fstpt %0"
                   : "=m"(*(struct extended *)(uintptr_t)(r[3] + 8))
                   : "m"(raw)
                   : "st");
#endif
}
void __attribute__((used, noinline)) bb_crt_math_floor_c(uint32_t *r) {
  uintptr_t a = r[3] + 264;
  floating_output(r, floor_value(bits(BB_U32(a), BB_U32(a + 4)), r[3] + 100));
}
void __attribute__((used, noinline)) bb_crt_math_exception_c(uint32_t *r) {
  uintptr_t a = r[3] + 136;
  floating_output(r, exception(BB_U32(a), BB_U32(a + 4),
                               bits(BB_U32(a + 8), BB_U32(a + 12)),
                               bits(BB_U32(a + 16), BB_U32(a + 20)),
                               BB_U32(a + 24), r[3] + 36));
}
void __attribute__((used, noinline)) bb_crt_math_raise_c(uint32_t *r) {
  uintptr_t a = r[3] + 136;
  raise_exception(BB_U32(a), BB_U32(a + 4), BB_U32(a + 8), BB_U32(a + 12),
                  BB_U32(a + 16), BB_U32(a + 20), BB_U32(r[3] + 124));
}
void __attribute__((used, noinline)) bb_crt_math_x87_exception_c(uint32_t *r) {
  uintptr_t a = r[3] + 136;
  x87_exception(BB_U32(a), BB_U32(a + 4), BB_U32(a + 8), r[3] + 24);
}
#ifdef BB_MODEL_RUNTIME
double bb_crt_math_floor_entry(uint32_t low, uint32_t high) {
  unsigned char record[84] = {0};
  return load_bits(floor_value(bits(low, high), (uintptr_t)record));
}
double bb_crt_math_exception_entry(uint32_t flags, uint32_t operation,
                                   uint32_t al, uint32_t ah, uint32_t rl,
                                   uint32_t rh, uint32_t control) {
  unsigned char record[84] = {0};
  return load_bits(exception(flags, operation, bits(al, ah), bits(rl, rh),
                             control, (uintptr_t)record));
}
void bb_crt_math_raise_entry(void *record, uint32_t *control, uint32_t flags,
                             uint32_t operation, const void *operand,
                             void *result) {
  raise_exception((uintptr_t)record, (uintptr_t)control, flags, operation,
                  (uintptr_t)operand, (uintptr_t)result, 0);
}
void bb_crt_math_x87_exception_entry(uint32_t operation, void *error,
                                     const uint16_t *control) {
  unsigned char record[84] = {0};
  x87_exception(operation, (uintptr_t)error, (uintptr_t)control,
                (uintptr_t)record);
}
#else
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
#define UNUSED __attribute__((unused))
#define CALL_FRAME(name, reserve)                                              \
  "lea -" #reserve "(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(  \
      name##_c) ";addl $4,%esp;popal;popfl;lea " #reserve "(%esp),%esp;"
double __attribute__((naked, noinline))
bb_crt_math_floor_entry(uint32_t a UNUSED, uint32_t b UNUSED) {
  __asm__ volatile(CALL_FRAME(bb_crt_math_floor, 256) "fldt -252(%esp);ret");
}
double __attribute__((naked, noinline)) bb_crt_math_exception_entry(
    uint32_t a UNUSED, uint32_t b UNUSED, uint32_t c UNUSED, uint32_t d UNUSED,
    uint32_t e UNUSED, uint32_t f UNUSED, uint32_t g UNUSED) {
  __asm__ volatile(
      CALL_FRAME(bb_crt_math_exception, 128) "fldt -124(%esp);ret");
}
void __attribute__((naked, noinline))
bb_crt_math_raise_entry(void *a UNUSED, uint32_t *b UNUSED, uint32_t c UNUSED,
                        uint32_t d UNUSED, const void *e UNUSED,
                        void *f UNUSED) {
  __asm__ volatile(CALL_FRAME(bb_crt_math_raise, 128) "ret");
}
void __attribute__((naked, noinline))
bb_crt_math_x87_exception_entry(uint32_t a UNUSED, void *b UNUSED,
                                const uint16_t *c UNUSED) {
  __asm__ volatile(CALL_FRAME(bb_crt_math_x87_exception, 128) "ret");
}

#endif
