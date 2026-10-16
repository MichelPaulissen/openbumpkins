/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <fenv.h>
#include <string.h>

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* CRT result normalization and mutable FPIEEE records in C. The temporary
 * bridge still forwards RaiseException through the installed platform API.
 * Native general exception dispatch is a separate dependency. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned index) {
  return BB_U32(stack(input) + 4u * (index + 1u));
}
static void call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 128u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
}
static uint64_t bits(uint32_t address) {
  uint64_t value;
  memcpy(&value, (const void *)(uintptr_t)address, sizeof(value));
  return value;
}
static void store_bits(uint32_t address, uint64_t value) {
  memcpy((void *)(uintptr_t)address, &value, sizeof(value));
}
static void output(uint32_t *input, uint64_t raw) {
  double value;
  memcpy(&value, &raw, sizeof(value));
  *(long double *)(uintptr_t)input[9] = value;
}
#define SIGN UINT64_C(0x8000000000000000)
#define MANTISSA UINT64_C(0x000fffffffffffff)
#define HIDDEN UINT64_C(0x0010000000000000)

/* The CRT's private fraction helpers also treat infinity/NaN as raw encoded
 * mantissas. This is not the public frexp contract. */
static uint64_t fraction(uint64_t raw, int32_t *exponent) {
  uint64_t magnitude = raw & ~SIGN, mantissa = raw & MANTISSA;
  if (!magnitude) {
    *exponent = 0;
    return 0;
  }
  unsigned encoded = (unsigned)(magnitude >> 52);
  if (encoded)
    *exponent = (int32_t)encoded - 1022;
  else {
    *exponent = -1021;
    while (!(mantissa & HIDDEN)) {
      mantissa <<= 1;
      --*exponent;
    }
    mantissa &= MANTISSA;
  }
  return (raw & SIGN) | (UINT64_C(1022) << 52) | mantissa;
}
/* 0x100140e0..0x10014119: install a private fraction exponent. */
void bb_qmix_math_fraction_scale_c(uint32_t *input) {
  uint64_t raw = (uint64_t)arg(input, 0) | ((uint64_t)arg(input, 1) << 32);
  uint16_t header = (uint16_t)((arg(input, 2) + 1022u) << 4);
  output(input, (raw & (SIGN | MANTISSA)) | ((uint64_t)header << 48));
}
/* 0x10014120..0x10014135: encoded exponent relative to the fraction bias. */
void bb_qmix_math_exponent_c(uint32_t *input) {
  input[7] = (uint32_t)(int32_t)(int16_t)(((arg(input, 1) >> 20) & 0x7ffu) -
                                       1022u);
}
/* 0x100141b0..0x1001429e: normalize normal/subnormal encoded doubles. */
void bb_qmix_math_fraction_c(uint32_t *input) {
  uint64_t raw = (uint64_t)arg(input, 0) | ((uint64_t)arg(input, 1) << 32);
  int32_t exponent;
  raw = fraction(raw, &exponent);
  BB_U32(arg(input, 2)) = (uint32_t)exponent;
  output(input, raw);
}
/* 0x10014060..0x100140bb: raise host floating-point status. Playback does
 * not require identity of the old CRT's x87 register/status side effects. */
void bb_qmix_fpu_raise_c(uint32_t *input) {
  uint32_t flags = arg(input, 0);
  int raised = 0;
  if (flags & 1u) raised |= FE_INVALID;
  if (flags & 4u) raised |= FE_DIVBYZERO;
  if (flags & 8u) raised |= FE_OVERFLOW | FE_INEXACT;
  if (flags & 0x10u) raised |= FE_UNDERFLOW | FE_INEXACT;
  if (flags & 0x20u) raised |= FE_INEXACT;
  input[7] = (uint32_t)feraiseexcept(raised);
}
static void raise_status(uint32_t *input, uint32_t flags) {
  uint32_t args[16] = {flags};
  call(input, 0x10014060u, args);
}
/* 0x10013bd0..0x10013e90: normalize masked exception results, preserving
 * the CRT's primary-error priority and its scaled underflow convention. */
void bb_qmix_math_result_normalize_c(uint32_t *input) {
  uint32_t flags = arg(input, 0), result = arg(input, 1), cw = arg(input, 2);
  uint32_t remaining = flags & 0x1fu;
  if ((flags & 8u) && (cw & 1u)) {
    raise_status(input, 1);
    remaining &= ~8u;
  } else if ((flags & 4u) && (cw & 4u)) {
    raise_status(input, 4);
    remaining &= ~4u;
  } else if ((flags & 1u) && (cw & 8u)) {
    raise_status(input, 8);
    uint64_t raw = bits(result), magnitude = raw & ~SIGN;
    /* The original unordered comparison selects the positive result. */
    unsigned positive = (!(raw & SIGN) && magnitude) ||
                        magnitude > UINT64_C(0x7ff0000000000000);
    unsigned rounding = (cw >> 10) & 3u;
    unsigned finite = rounding == 3u || (positive ? rounding == 1u :
                                                   rounding == 2u);
    raw = finite ? UINT64_C(0x7fefffffffffffff) :
                   UINT64_C(0x7ff0000000000000);
    store_bits(result, raw | (positive ? 0 : SIGN));
    remaining &= ~1u;
  } else if ((flags & 2u) && (cw & 0x10u)) {
    uint64_t raw = bits(result);
    unsigned loss = (flags & 0x10u) != 0;
    if (raw & ~SIGN) {
      int32_t exponent;
      uint64_t normalized = fraction(raw, &exponent);
      exponent -= 1536;
      if (exponent < -1074) {
        raw = 0;
        loss = 1;
      } else {
        raw = (normalized & MANTISSA) | HIDDEN;
        if (exponent < -1021) {
          unsigned shift = (unsigned)(-1021 - exponent);
          loss |= (raw & ((UINT64_C(1) << shift) - 1u)) != 0;
          raw >>= shift;
        }
        raw |= normalized & SIGN;
      }
      store_bits(result, raw);
    } else
      loss = 1;
    if (loss) raise_status(input, 0x10);
    remaining &= ~2u;
  }
  if ((flags & 0x10u) && (cw & 0x20u)) {
    raise_status(input, 0x20);
    remaining &= ~0x10u;
  }
  input[7] = remaining == 0;
}
/* Convert raw x87 masks/status into the FPIEEE record's five flag bits. */
static uint32_t ieee_flags(uint32_t raw) {
  return ((raw & 1u) << 4) | ((raw & 4u) << 1) | ((raw & 8u) >> 1) |
         ((raw & 0x10u) >> 3) | ((raw & 0x20u) >> 5);
}
/* 0x10013860..0x10013bba: construct and resume a mutable FPIEEE record. */
void bb_qmix_math_ieee_exception_c(uint32_t *input) {
  uint32_t record = arg(input, 0), control = arg(input, 1);
  uint32_t flags = arg(input, 2), operation = arg(input, 3);
  uint32_t operand = arg(input, 4), result = arg(input, 5);
  uint32_t code = 0, cause = 0, cw = BB_U32(control);
  if (flags & 0x10u) { cause |= 1u; code = 0xc000008fu; }
  if (flags & 2u) { cause |= 2u; code = 0xc0000093u; }
  if (flags & 1u) { cause |= 4u; code = 0xc0000091u; }
  if (flags & 4u) { cause |= 8u; code = 0xc000008eu; }
  if (flags & 8u) { cause |= 0x10u; code = 0xc0000090u; }
  BB_U32(record + 4) = cause;
  BB_U32(record + 8) = ieee_flags(~cw);
  uint32_t args[16] = {0};
  call(input, 0x10013fe0u, args);
  BB_U32(record + 12) = ieee_flags(input[7]);
  uint32_t header = (BB_U32(record) & ~3u) | ((cw >> 10) & 3u);
  switch (cw & 0x300u) {
    case 0: header = (header & ~0x14u) | 8u; break;
    case 0x200: header = (header & ~0x18u) | 4u; break;
    case 0x300: header &= ~0x1cu; break;
  }
  BB_U32(record) = (header & ~0x1ffe0u) | ((operation << 5) & 0x1ffe0u);
  BB_U32(record + 32) = (BB_U32(record + 32) & ~0x1cu) | 3u;
  BB_U32(record + 80) = (BB_U32(record + 80) & ~0x1cu) | 3u;
  store_bits(record + 16, bits(operand));
  store_bits(record + 64, bits(result));
  call(input, 0x10014000u, args);
  uint32_t parameters = stack(input) - 4u;
  BB_U32(parameters) = record;
  args[0] = code; args[1] = 0; args[2] = 1; args[3] = parameters;
  call(input, BB_U32(0x100212dcu), args);
  /* A handler may have edited the record or the pointed-to control word. */
  cw = BB_U32(control);
  uint32_t enabled = BB_U32(record + 8);
  if (enabled & 0x10u) cw &= ~1u;
  if (enabled & 8u) cw &= ~4u;
  if (enabled & 4u) cw &= ~8u;
  if (enabled & 2u) cw &= ~0x10u;
  if (enabled & 1u) cw &= ~0x20u;
  header = BB_U32(record);
  cw = (cw & ~0xc00u) | ((header & 3u) << 10);
  /* Preserve the original resume routine's mask: these three branches clear
   * the rounding field while OR-ing precision, rather than clearing 0x300. */
  switch ((header >> 2) & 7u) {
    case 0: cw = (cw & ~0xc00u) | 0x300u; break;
    case 1: cw = (cw & ~0xc00u) | 0x200u; break;
    case 2: cw &= ~0xc00u; break;
  }
  BB_U32(control) = cw;
  store_bits(result, bits(record + 64));
  input[7] = BB_U32(record + 68); /* Opaque CRT return, callers use the record. */
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
#define FLOAT_ENTRY(name, signature)                                           \
  double __attribute__((naked)) name##_entry signature {                       \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;"              \
        "movl %eax,36(%esp);pushl %esp;call " BB_ASM(                          \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;"   \
                      "fldt -2044(%esp);ret");                                 \
  }
ENTRY(bb_qmix_math_ieee_exception, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)))
ENTRY(bb_qmix_math_result_normalize, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_fpu_raise, (ARG(0)))
FLOAT_ENTRY(bb_qmix_math_fraction_scale, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_math_exponent, (ARG(0), ARG(1)))
FLOAT_ENTRY(bb_qmix_math_fraction, (ARG(0), ARG(1), ARG(2)))
#undef FLOAT_ENTRY
#undef ENTRY
#undef ARG
