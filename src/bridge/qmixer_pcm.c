/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <signal.h>
#include <string.h>
#define QWORD(a) BB_U64(a)
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
/* C forms of QMIXER PCM decoder and mixer kernels.
 * Address-derived labels retain provenance for auditing phase transitions,
 * arithmetic boundaries and callback ordering.
 */
static uint32_t mask_for(unsigned bits) {
  return bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
}
static uint32_t common_flags(uint32_t value, unsigned bits) {
  value &= mask_for(bits);
  uint32_t low = value & 255u;
  low ^= low >> 4;
  uint32_t parity = (0x9669u >> (low & 15u)) & 1u;
  return (!value ? 0x40u : 0u) | (value & (1u << (bits - 1)) ? 0x80u : 0u) |
         (parity << 2);
}
static uint32_t logic_flags(uint32_t flags, uint32_t value, unsigned bits) {
  return (flags & ~0x8d5u) | common_flags(value, bits);
}
static uint32_t add_flags(uint32_t flags, uint32_t a, uint32_t b,
                          unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1);
  a &= mask;
  b &= mask;
  uint32_t result = (a + b) & mask;
  return logic_flags(flags, result, bits) | ((uint64_t)a + b > mask ? 1u : 0u) |
         (((a ^ b ^ result) & 16u) ? 16u : 0u) |
         ((~(a ^ b) & (a ^ result) & sign) ? 0x800u : 0u);
}
static uint32_t sub_flags(uint32_t flags, uint32_t a, uint32_t b,
                          unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1);
  a &= mask;
  b &= mask;
  uint32_t result = (a - b) & mask;
  return logic_flags(flags, result, bits) | (a < b ? 1u : 0u) |
         (((a ^ b ^ result) & 16u) ? 16u : 0u) |
         (((a ^ b) & (a ^ result) & sign) ? 0x800u : 0u);
}
static uint32_t __attribute__((unused))
shift_flags(uint32_t flags, uint32_t before, uint32_t after, unsigned bits,
            unsigned count, unsigned right) {
  count &= 31u;
  if (!count)
    return flags;
  uint32_t carry = right ? ((before >> (count - 1u)) & 1u)
                         : ((before >> (bits - count)) & 1u);
  uint32_t overflow = right == 2 ? 0
                      : right    ? (before >> (bits - 1u)) & 1u
                                 : ((after >> (bits - 1u)) & 1u) ^ carry;
  return logic_flags(flags, after, bits) | carry | (overflow ? 0x800u : 0u);
}
static void __attribute__((noreturn, unused)) division_fault(void) {
  raise(SIGFPE);
  __builtin_unreachable();
}
static uint32_t __attribute__((unused))
borrow_flags(uint32_t flags, uint32_t a, uint32_t b, unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1), carry = flags & 1u;
  a &= mask;
  b &= mask;
  uint32_t r = (a - b - carry) & mask;
  return logic_flags(flags, r, bits) |
         ((uint64_t)a < (uint64_t)b + carry ? 1u : 0u) |
         (((a ^ b ^ r) & 16u) ? 16u : 0u) |
         (((a ^ b) & (a ^ r) & sign) ? 0x800u : 0u);
}

#ifdef BB_MODEL_RUNTIME
#include "qmixer_numeric_portable.h"
#else
/* Preserve the original x87 truncation boundary, including control-word
 * restoration and 64-bit invalid/overflow results used by original callers. */
static void __attribute__((unused))
sample_float_to_int(struct bb_call_state *state,
                    const long double *value) {
  uint16_t control, truncated;
  int64_t result;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  truncated = control | 0x0c00u;
  __asm__ volatile("fldt %2;fldcw %3;fistpq %0;fldcw %1"
                   : "=m"(result)
                   : "m"(control), "m"(*value), "m"(truncated)
                   : "st", "memory");
  state->eax = (uint32_t)result;
  state->edx = (uint64_t)result >> 32;
  state->flags = logic_flags(state->flags, (control >> 8) | 12u, 8);
}
/* Numeric comparison compatibility while the i386 boundary remains. */
static uint16_t __attribute__((unused)) sample_compare(const long double *a,
                                                       const long double *b) {
  uint16_t status;
  __asm__ volatile("fldt %1;fldt %2;fxch;fcompp;fnstsw %0"
                   : "=am"(status)
                   : "m"(*a), "m"(*b)
                   : "st", "memory");
  return status & 0x4500u;
}
#define F32(a) BB_FLOAT(a)
#define F64(a) BB_DOUBLE(a)
/* C forms of QMIXER sample buffers and mixing state. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */

/* Numeric compatibility boundaries while the i386 bridge remains. These
 * operate on C values; instruction decoding is confined to this retired tool.
 */
static long double __attribute__((unused)) qmix_constant(unsigned kind) {
  long double value;
  if (!kind)
    __asm__ volatile("fld1;fstpt %0" : "=m"(value)::"st");
  else if (kind == 1)
    __asm__ volatile("fldln2;fstpt %0" : "=m"(value)::"st");
  else
    __asm__ volatile("fldl2e;fstpt %0" : "=m"(value)::"st");
  return value;
}
static long double __attribute__((unused)) qmix_unary(const long double *x,
                                                      unsigned kind) {
  long double value;
  if (!kind)
    __asm__ volatile("fldt %1;fsqrt;fstpt %0" : "=m"(value) : "m"(*x) : "st");
  else if (kind == 1)
    __asm__ volatile("fldt %1;fcos;fstpt %0" : "=m"(value) : "m"(*x) : "st");
  else if (kind == 2)
    __asm__ volatile("fldt %1;frndint;fstpt %0" : "=m"(value) : "m"(*x) : "st");
  else
    __asm__ volatile("fldt %1;f2xm1;fstpt %0" : "=m"(value) : "m"(*x) : "st");
  return value;
}
static long double __attribute__((unused))
qmix_binary(const long double *y, const long double *x, unsigned kind) {
  long double value;
  if (!kind)
    __asm__ volatile("fldt %1;fldt %2;fpatan;fstpt %0"
                     : "=m"(value)
                     : "m"(*y), "m"(*x)
                     : "st", "st(1)");
  else
    __asm__ volatile("fldt %1;fldt %2;fyl2x;fstpt %0"
                     : "=m"(value)
                     : "m"(*y), "m"(*x)
                     : "st", "st(1)");
  return value;
}
static long double __attribute__((unused))
qmix_scale(const long double *x, const long double *exponent) {
  long double value;
  __asm__ volatile("fldt %2;fldt %1;fscale;fstpt %0;fstp %%st(0)"
                   : "=m"(value)
                   : "m"(*x), "m"(*exponent)
                   : "st", "st(1)");
  return value;
}

static uint16_t qmix_status(uint16_t comparison, unsigned depth) {
  uint16_t status;
  __asm__ volatile("fnstsw %0" : "=am"(status));
  return (status & 0x2ffu) | comparison | (((8u - depth) & 7u) << 11);
}
static uint32_t qmix_integer(const long double *x) {
  int32_t value;
  __asm__ volatile("fldt %1;fistpl %0"
                   : "=m"(value)
                   : "m"(*x)
                   : "st", "memory");
  return (uint32_t)value;
}

#endif
#define SAMPLE_CALL(target, consumed, returned)                                \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                   \
    BB_QMIX_DISPATCH(&call, (uintptr_t)(target), args + sp, esp, edi, esi, ebp,    \
                 ebx, returned);                                               \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)
void bb_qmix_decoder_base_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = ecx;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_decoder_base);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_decoder_base_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_decoder_base_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_decoder_base_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_decoder_base);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_decoder_base_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_decoder_base_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_pcm_remaining_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x24u);
  eax = (eax - BB_U32(ecx + 0x20u));
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  ecx = BB_U32(ecx + 0x18u);
  edx = (uint32_t)((int32_t)eax >> 31);
  esi = (esi & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0xcu)) & 0xffffu) << 0);
  b = esi;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_pcm_remaining_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_remaining_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_pcm_progress_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x20u);
  eax = (eax - BB_U32(ecx + 0x1cu));
  edx = BB_U32(esp + 0x8u);
  a = edx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edx = result;
  if (flags & 0x40u)
    goto L_10001ad4;
  a = edx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001ab0;
  a = edx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001ac1;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10001ab0:;
  ecx = BB_U32(ecx + 0x18u);
  esi = (esi ^ esi);
  esi = (esi & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0xcu)) & 0xffffu) << 0);
  a = edx;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edx = result;
  b = esi;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10001ac1:;
  ecx = BB_U32(ecx + 0x18u);
  edx = BB_U32(ecx + 0x8u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10001ad4u); /* 10001ace indirect */
L_10001ad4:;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_pcm_progress_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_progress_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_pcm_mono8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ebx = BB_U32(esp + 0x14u);
  a = ebx;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001b20;
  edi = BB_U32(esp + 0x10u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000abc0u, 0, 0x10001b07u); /* 10001b02 */
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10001b17u); /* 10001b11 indirect */
  edi = args[sp++];
  esp += 4u;
  a = BB_U32(esi + 0x20u);
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10001b20:;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  BB_U32(ecx) = eax;
  a = BB_U32(esi + 0x20u);
  b = edi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_pcm_mono8_entry(uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused)),
                        uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_pcm_mono16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ebx = BB_U32(esp + 0x14u);
  a = ebx;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001b82;
  edi = BB_U32(esp + 0x10u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000ac60u, 0, 0x10001b67u); /* 10001b62 */
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10001b77u); /* 10001b71 indirect */
  eax = (eax + eax);
  edi = args[sp++];
  esp += 4u;
  a = BB_U32(esi + 0x20u);
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10001b82:;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  BB_U32(ecx) = eax;
  edi = (edi + edi);
  a = BB_U32(esi + 0x20u);
  b = edi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_pcm_mono16_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused)),
                         uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_pcm_stereo8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ebx = BB_U32(esp + 0x14u);
  a = ebx;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001be5;
  edi = BB_U32(esp + 0x10u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000ac20u, 0, 0x10001bcau); /* 10001bc5 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10001bdau); /* 10001bd4 indirect */
  eax = (eax + eax);
  edi = args[sp++];
  esp += 4u;
  a = BB_U32(esi + 0x20u);
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10001be5:;
  edi = BB_U32(esp + 0x10u);
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = edi;
  esp -= 4u;
  edx = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  a = edi;
  b = edi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000ad10u, 0, 0x10001bffu); /* 10001bfa */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esi + 0x20u);
  b = edi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_pcm_stereo8_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused)),
                          uint32_t arg2 __attribute__((unused)),
                          uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_stereo8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_pcm_stereo16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ebx = BB_U32(esp + 0x14u);
  a = ebx;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001c56;
  edi = BB_U32(esp + 0x10u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000acd0u, 0, 0x10001c3au); /* 10001c35 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10001c4au); /* 10001c44 indirect */
  eax = (eax << 2);
  edi = args[sp++];
  esp += 4u;
  a = BB_U32(esi + 0x20u);
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10001c56:;
  edi = BB_U32(esp + 0x10u);
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = edi;
  esp -= 4u;
  a = edi;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  edi = result;
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000ad50u, 0, 0x10001c71u); /* 10001c6c */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esi + 0x20u);
  b = edi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x20u) = result;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_pcm_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused)),
                           uint32_t arg2 __attribute__((unused)),
                           uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_stereo16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_pcm_reset_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(ecx + 0x1cu);
  BB_U32(ecx + 0x20u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_pcm_reset_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_pcm_advance_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax ^ eax);
  esi = (esi ^ esi);
  esi = (esi & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0x10u)) & 0xffffu) << 0);
  a = esi;
  b = BB_U32(esp + 0x8u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  esi = result;
  esi = (esi >> 16);
  esi = (esi + BB_U32(ecx + 0x20u));
  a = esi;
  b = BB_U32(ecx + 0x24u);
  flags = sub_flags(flags, a, b, 32);
  esi = args[sp++];
  esp += 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(!(flags & 1u)) & 0xffu) << 0);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_pcm_advance_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pcm_advance_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_resample_mono8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ecx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x18u));
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  eax = (eax ^ eax);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000abd6:;
  a = eax;
  b = 0x8000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000ac00;
  edx = eax;
  eax = (eax + BB_U32(esp + 0x1cu));
  edx = (edx >> 16);
  ebx = (ebx & 0xffff0000u) | (((uint32_t)(BYTE(esi + edx)) & 0xffffu) << 0);
  edx = (edx & 0xffff0000u) |
        (((uint32_t)(BYTE(esi + edx + 0x1u)) & 0xffffu) << 0);
  ebx = (ebx & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(ebx)) + ((uint16_t)(edx)))) & 0xffffu) << 0);
  a = ((uint16_t)(ebx));
  result = ((uint32_t)((int16_t)a >> 1)) & 0xffffu;
  flags = shift_flags(flags, a, result, 16, 1, 2);
  ebx = (ebx & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  BYTE(edi + ecx) = ((uint8_t)(ebx));
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000abd6;
  goto L_1000ac14;
L_1000ac00:;
  edx = eax;
  eax = (eax + BB_U32(esp + 0x1cu));
  a = edx;
  result = (a >> 16) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 16, 1);
  edx = result;
  ebx = (ebx & 0xffff0000u) | (((uint32_t)(WORD(esi + edx)) & 0xffffu) << 0);
  WORD(edi + ecx) = ((uint16_t)(ebx));
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000abd6;
L_1000ac14:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_resample_mono8_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused)),
                             uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_resample_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_resample_stereo8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = WORD(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  if (flags & 0x40u)
    goto L_1000ac5a;
  ecx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  esi = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  edx = BB_U32(esp + 0x14u);
  eax = (eax ^ eax);
  edi = (edi + ecx);
  edx = (edx + ecx);
  ecx = (0u - ecx);
L_1000ac44:;
  ebx = eax;
  eax = (eax + BB_U32(esp + 0x20u));
  a = ebx;
  result = (a >> 16) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 16, 1);
  ebx = result;
  ebx =
      (ebx & 0xffff0000u) | (((uint32_t)(WORD(esi + ebx * 2u)) & 0xffffu) << 0);
  BYTE(edi + ecx) = ((uint8_t)(ebx));
  BYTE(edx + ecx) = ((uint8_t)(ebx >> 8));
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000ac44;
L_1000ac5a:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_resample_stereo8_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused)),
                               uint32_t arg3 __attribute__((unused)),
                               uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_resample_stereo8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_resample_mono16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = WORD(esp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  if (flags & 0x40u)
    goto L_1000acc0;
  ecx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x18u));
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ecx = (ecx << 1);
  eax = (eax ^ eax);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000ac81:;
  a = eax;
  b = 0x8000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000acaa;
  edx = eax;
  eax = (eax + BB_U32(esp + 0x1cu));
  edx = (edx >> 16);
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(esi + edx * 2u));
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esi + edx * 2u + 0x2u));
  ebx = (ebx + edx);
  ebx = ((uint32_t)((int32_t)(ebx) >> 1));
  WORD(edi + ecx) = ((uint16_t)(ebx));
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000ac81;
  goto L_1000acc0;
L_1000acaa:;
  edx = eax;
  eax = (eax + BB_U32(esp + 0x1cu));
  edx = (edx >> 16);
  ebx =
      (ebx & 0xffff0000u) | (((uint32_t)(WORD(esi + edx * 2u)) & 0xffffu) << 0);
  WORD(edi + ecx) = ((uint16_t)(ebx));
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000ac81;
L_1000acc0:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_resample_mono16_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused)),
                              uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_resample_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_resample_stereo16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ecx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  esi = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  edx = BB_U32(esp + 0x14u);
  ecx = (ecx << 1);
  eax = (eax ^ eax);
  edi = (edi + ecx);
  edx = (edx + ecx);
  ecx = (0u - ecx);
L_1000acef:;
  ebx = eax;
  eax = (eax + BB_U32(esp + 0x20u));
  ebx = (ebx >> 16);
  ebx = BB_U32(esi + ebx * 4u);
  WORD(edi + ecx) = ((uint16_t)(ebx));
  ebx = (ebx >> 16);
  WORD(edx + ecx) = ((uint16_t)(ebx));
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000acef;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_resample_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused)),
                                uint32_t arg3 __attribute__((unused)),
                                uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_resample_stereo16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_split_stereo8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  if (flags & 0x40u)
    goto L_1000ad43;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0xcu);
  edx = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x18u);
  esi = (esi + ecx * 2u);
  edi = (edi + ecx);
  edx = (edx + ecx);
  ecx = (0u - ecx);
L_1000ad34:;
  eax =
      (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + ecx * 2u)) & 0xffffu) << 0);
  BYTE(edi + ecx) = ((uint8_t)(eax));
  BYTE(edx + ecx) = ((uint8_t)(eax >> 8));
  a = ecx;
  b = 0x1u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000ad34;
L_1000ad43:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_split_stereo8_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused)),
                            uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_split_stereo8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_split_stereo16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  if (flags & 0x40u)
    goto L_1000ad8d;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0xcu);
  edx = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x18u);
  ecx = (ecx << 1);
  esi = (esi + ecx * 2u);
  edi = (edi + ecx);
  edx = (edx + ecx);
  ecx = (0u - ecx);
L_1000ad77:;
  eax =
      (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + ecx * 2u)) & 0xffffu) << 0);
  WORD(edi + ecx) = ((uint16_t)(eax));
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD(esi + ecx * 2u + 0x2u)) & 0xffffu) << 0);
  WORD(edx + ecx) = ((uint16_t)(eax));
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000ad77;
L_1000ad8d:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_split_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused)),
                             uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_split_stereo16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_angle_quantize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  ecx = 0x6u;
  eax = (eax + 0x3u);
  edx = (uint32_t)((int32_t)eax >> 31);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  eax = (eax + 0xfu);
  a = eax;
  b = 0x3cu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000ba6a;
  a = eax;
  b = 0x3cu;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
L_1000ba6a:;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_angle_quantize_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_angle_quantize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_angle_expand_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  eax = (eax + eax * 2u + 0x87u);
  eax = (eax + eax);
  a = eax;
  b = 0x168u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ba89;
  a = eax;
  b = 0x168u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
L_1000ba89:;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_angle_expand_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_angle_expand_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_ramp_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_float_ramp);
  esi = ecx;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(0x1000bab0u, 8, 0x1000baa2u); /* 1000ba9d */
  eax = esi;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_ramp_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_ramp_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_ramp_set_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  BB_U32(ecx + 0xcu) = eax;
  eax = BB_U32(esp + 0xcu);
  BB_U32(ecx + 0x4u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000bad9;
  BB_U32(ecx + 0x10u) = 0x0u;
  eax = BB_U32(esp + 0x8u);
  BB_U32(ecx + 0x8u) = eax;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000bad9:;
  fp[depth] = F32(esp + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(ecx + 0x8u);
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp);
  fp[depth - 1] /= (long double)(int32_t)(BB_U32(esp));
  {
    long double other = F32(0x1001a414u);
    fp_status = sample_compare(&fp[depth - 1], &other);
  }
  F32(ecx + 0x10u) = (float)(fp[depth - 1]);
  --depth;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(qmix_status(fp_status, depth)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x40u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000bb06;
  BB_U32(ecx + 0x4u) = 0x0u;
  eax = BB_U32(esp + 0x8u);
  BB_U32(ecx + 0x8u) = eax;
L_1000bb06:;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_ramp_set_entry(uint32_t arg0 __attribute__((unused)),
                       uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_ramp_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                          "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_ramp_mono8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  esi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000bb55;
  a = BB_U32(esi + 0x8u);
  b = 0x7fffffffu;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000bbdd;
  ebp = BB_U32(esp + 0x24u);
  edi = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(esi + 0x8u);
  ebx = BB_U32(esp + 0x20u);
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000be90u, 20, 0x1000bb4bu); /* 1000bb46 */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000bb55:;
  edi = BB_U32(esp + 0x20u);
  ebp = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  a = edi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000bb97;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(esi + 0x8u);
  ebx = BB_U32(esp + 0x28u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000be90u, 20, 0x1000bb79u); /* 1000bb74 */
  BB_U32(esp + 0x10u) = edi;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  BB_U32(esi + 0x4u) = (BB_U32(esi + 0x4u) - edi);
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  fp[depth - 1] = fp[depth - 1] * F32(esi + 0x10u);
  fp[depth - 1] = fp[depth - 1] + F32(esi + 0x8u);
  F32(esi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000bb97:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(esi + 0x8u);
  eax = BB_U32(esi + 0x10u);
  ebx = BB_U32(esp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000be90u, 20, 0x1000bbaeu); /* 1000bba9 */
  ecx = BB_U32(esi + 0x4u);
  eax = BB_U32(esi + 0xcu);
  ebp = (ebp + ecx);
  edi = (edi - ecx);
  args[--sp] = ebp;
  esp -= 4u;
  a = ecx;
  b = ebx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x2cu);
  SAMPLE_CALL(0x1000be90u, 20, 0x1000bbc9u); /* 1000bbc4 */
  BB_U32(esi + 0x10u) = 0x0u;
  BB_U32(esi + 0x4u) = 0x0u;
  ecx = BB_U32(esi + 0xcu);
  BB_U32(esi + 0x8u) = ecx;
L_1000bbdd:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_ramp_mono8_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused)),
                         uint32_t arg2 __attribute__((unused)),
                         uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_ramp_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_ramp_mono16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  esi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000bc35;
  a = BB_U32(esi + 0x8u);
  b = 0x7fffffffu;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000bcbe;
  ebp = BB_U32(esp + 0x24u);
  edi = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(esi + 0x8u);
  ebx = BB_U32(esp + 0x20u);
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000bf60u, 20, 0x1000bc2bu); /* 1000bc26 */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000bc35:;
  edi = BB_U32(esp + 0x20u);
  ebp = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  a = edi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000bc77;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(esi + 0x8u);
  ebx = BB_U32(esp + 0x28u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000bf60u, 20, 0x1000bc59u); /* 1000bc54 */
  BB_U32(esp + 0x10u) = edi;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  BB_U32(esi + 0x4u) = (BB_U32(esi + 0x4u) - edi);
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  fp[depth - 1] = fp[depth - 1] * F32(esi + 0x10u);
  fp[depth - 1] = fp[depth - 1] + F32(esi + 0x8u);
  F32(esi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000bc77:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(esi + 0x8u);
  eax = BB_U32(esi + 0x10u);
  ebx = BB_U32(esp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000bf60u, 20, 0x1000bc8eu); /* 1000bc89 */
  ecx = BB_U32(esi + 0x4u);
  eax = BB_U32(esi + 0xcu);
  ebp = (ebp + ecx);
  a = edi;
  b = ecx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = (ebx + ecx * 2u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x2cu);
  SAMPLE_CALL(0x1000bf60u, 20, 0x1000bcaau); /* 1000bca5 */
  BB_U32(esi + 0x10u) = 0x0u;
  BB_U32(esi + 0x4u) = 0x0u;
  ecx = BB_U32(esi + 0xcu);
  BB_U32(esi + 0x8u) = ecx;
L_1000bcbe:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_ramp_mono16_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused)),
                          uint32_t arg2 __attribute__((unused)),
                          uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_ramp_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_accumulator_feed_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  BB_U32(ecx + 0xcu) = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 0, 0x1000be1cu); /* 1000be19 indirect */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_feed_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_feed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_accumulator_mono8_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  fp[depth] = F32(ecx + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(esi + 0xcu);
  edi = (edi + BB_U32(esp + 0x28u));
  eax = BB_U32(esi + 0x10u);
  F32(esp + 0x1cu) = (float)(fp[depth - 1]);
  args[sp + 7u] = BB_U32((uint32_t)(esp + 0x1cu) + 0u);
  --depth;
  fp[depth] = F32(esi + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  edi = (edi << 2);
  eax = (eax - BB_U32(esp + 0x28u));
  edi = (edi + BB_U32(esi + 0x4u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  F32(esp + 0x20u) = (float)(fp[depth - 1]);
  args[sp + 8u] = BB_U32((uint32_t)(esp + 0x20u) + 0u);
  --depth;
  a = eax;
  b = BB_U32(esp + 0x24u);
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x24u);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000bed5;
  eax = (esp + 0x10u);
L_1000bed5:;
  ebx = BB_U32(eax);
  ebp = BB_U32(esp + 0x24u);
  ebp = (ebp - ebx);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000befa;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000c290u, 0, 0x1000bef7u); /* 1000bef2 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000befa:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000bf2e;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp + 0x14u) = ebx;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  eax = BB_U32(esp + 0x20u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  a = eax;
  b = ebx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  ecx = (edi + ebx * 4u);
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  fp[depth - 1] = fp[depth - 1] + F32(esp + 0x30u);
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  SAMPLE_CALL(0x1000c1f0u, 0, 0x1000bf2bu); /* 1000bf26 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000bf2e:;
  eax = BB_U32(esp + 0x24u);
  eax = (eax + BB_U32(esp + 0x28u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  a = BB_U32(esi + 0x10u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esi + 0x10u);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000bf46;
  eax = (esp + 0x10u);
L_1000bf46:;
  eax = BB_U32(eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0x10u) = eax;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_mono8_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused)),
                                uint32_t arg3 __attribute__((unused)),
                                uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_accumulator_mono16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  fp[depth] = F32(ecx + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(esi + 0xcu);
  edi = (edi + BB_U32(esp + 0x28u));
  eax = BB_U32(esi + 0x10u);
  F32(esp + 0x1cu) = (float)(fp[depth - 1]);
  args[sp + 7u] = BB_U32((uint32_t)(esp + 0x1cu) + 0u);
  --depth;
  fp[depth] = F32(esi + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  edi = (edi << 2);
  eax = (eax - BB_U32(esp + 0x28u));
  edi = (edi + BB_U32(esi + 0x4u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  F32(esp + 0x20u) = (float)(fp[depth - 1]);
  args[sp + 8u] = BB_U32((uint32_t)(esp + 0x20u) + 0u);
  --depth;
  a = BB_U32(esp + 0x24u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x24u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000bfa5;
  eax = (esp + 0x10u);
L_1000bfa5:;
  ebx = BB_U32(eax);
  ebp = BB_U32(esp + 0x24u);
  ebp = (ebp - ebx);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000bfca;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000c400u, 0, 0x1000bfc7u); /* 1000bfc2 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000bfca:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000bfff;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp + 0x14u) = ebx;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  ecx = BB_U32(esp + 0x20u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = (edi + ebx * 4u);
  edx = (ecx + ebx * 2u);
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  fp[depth - 1] = fp[depth - 1] + F32(esp + 0x30u);
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  SAMPLE_CALL(0x1000c340u, 0, 0x1000bffcu); /* 1000bff7 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000bfff:;
  eax = BB_U32(esp + 0x24u);
  eax = (eax + BB_U32(esp + 0x28u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  a = BB_U32(esi + 0x10u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esi + 0x10u);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000c017;
  eax = (esp + 0x10u);
L_1000c017:;
  eax = BB_U32(eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0x10u) = eax;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_mono16_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused)),
                                 uint32_t arg2 __attribute__((unused)),
                                 uint32_t arg3 __attribute__((unused)),
                                 uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_accumulator_zero_extend_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(ecx + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  esi = BB_U32(esp + 0x10u);
  a = esi;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000c060;
  eax = esi;
  edi = BB_U32(ebx + 0xcu);
  eax = (eax - edx);
  edi = (edi + edx);
  edi = (edi << 2);
  ecx = (eax * 4u);
  ecx = (ecx >> 2);
  edi = (edi + BB_U32(ebx + 0x4u));
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(ebx + 0x10u) = esi;
L_1000c060:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_zero_extend_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_zero_extend_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_accumulator_merge_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  eax = (eax + BB_U32(ecx + 0x10u));
  args[--sp] = ebp;
  esp -= 4u;
  ebx = ecx;
  esi = BB_U32(esp + 0x24u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  eax = BB_U32(esi + 0xcu);
  eax = (eax + BB_U32(esi + 0x10u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  a = eax;
  b = BB_U32(esp + 0x14u);
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x10u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000c09f;
  eax = (esp + 0x14u);
L_1000c09f:;
  ebp = BB_U32(eax);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000c0b6;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(esi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebx + 0x4u);
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x1000c4d0u, 0, 0x1000c0b3u); /* 1000c0ae */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_1000c0b6:;
  BB_U32(esp + 0x1cu) = 0x0u;
  args[sp + 7u] = BB_U32(esp + 0x1cu);
  eax = BB_U32(esp + 0x10u);
  a = eax;
  b = BB_U32(esp + 0x14u);
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(esp + 0x18u) = eax;
  args[sp + 6u] = BB_U32(esp + 0x18u);
  eax = (esp + 0x18u);
  if (!(flags & 0x80u))
    goto L_1000c0d4;
  eax = (esp + 0x1cu);
L_1000c0d4:;
  eax = BB_U32(eax);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000c0ff;
  edx = (ebp * 4u);
  ecx = (eax * 4u);
  ecx = (ecx >> 2);
  esi = BB_U32(esi + 0x4u);
  edi = BB_U32(ebx + 0x4u);
  esi = (esi + edx);
  edi = (edi + edx);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = (eax - BB_U32(ebx + 0xcu));
  eax = (eax + ebp);
  BB_U32(ebx + 0x10u) = eax;
L_1000c0ff:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_merge_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_merge_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_accumulator_stereo_output_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  edi = BB_U32(esp + 0x18u);
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000c030u, 4, 0x1000c11fu); /* 1000c11a */
  esi = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  ecx = esi;
  SAMPLE_CALL(0x1000c030u, 4, 0x1000c12bu); /* 1000c126 */
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebx + 0x4u);
  edx = BB_U32(esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000c5b0u, 0, 0x1000c13eu); /* 1000c139 */
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_stereo_output_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused)),
                                        uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_stereo_output_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_accumulator_flush_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000c18a;
  edx = BB_U32(esp + 0x10u);
  a = BB_U32(ebx + 0x10u);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000c179;
  edi = BB_U32(ebx + 0x4u);
  ecx = (eax * 4u);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  esi = (edi + edx * 4u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  goto L_1000c18a;
L_1000c179:;
  ecx = (eax * 4u);
  edi = BB_U32(ebx + 0x4u);
  ecx = (ecx >> 2);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
L_1000c18a:;
  BB_U32(ebx + 0x10u) = 0x0u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_accumulator_flush_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_flush_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_accumulator_silent_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(ecx + 0xcu);
  edx = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000c4a0u, 0, 0x1000c1adu);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  {
    long double other = F32(0x1001a418u);
    fp_status = sample_compare(&fp[depth - 1], &other);
    --depth;
  }
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(qmix_status(fp_status, depth)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x41u;
  flags = logic_flags(flags, a & b, 8);
  eax = 0x1u;
  if (!(flags & 0x40u))
    goto L_1000c1c4;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_1000c1c4:;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_accumulator_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_accumulator_clear_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = edi;
  esp -= 4u;
  eax = ecx;
  ecx = BB_U32(eax + 0xcu);
  edi = BB_U32(eax + 0x4u);
  ecx = (ecx << 2);
  eax = (eax ^ eax);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  edi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_accumulator_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_accumulator_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mix_mono8_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = 0x1001f030u;
  fp[depth] = F32(esp + 0x1cu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a40cu);
  fp[depth] = F32(esp + 0x18u);
  ++depth;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c256;
L_1000c217:;
  eax = BYTE(esi);
  edx = BYTE(esi + 0x1u);
  fp[depth] = F32(ebx + eax * 4u);
  ++depth;
  fp[depth] = F32(ebx + edx * 4u);
  ++depth;
  eax = BYTE(esi + 0x2u);
  edx = BYTE(esi + 0x3u);
  fp[depth] = F32(ebx + eax * 4u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth] = F32(ebx + edx * 4u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth] = fp[depth - 1u - 4u];
  ++depth;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 3u] = fp[depth - 1u - 3u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 4u] = fp[depth - 1u - 4u] * fp[depth - 1];
  --depth;
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 0x4u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c217;
L_1000c256:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c27a;
  fp[depth] = F32(0x1001a410u);
  ++depth;
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1];
  --depth;
L_1000c267:;
  eax = BYTE(esi);
  fp[depth] = F32(ebx + eax * 4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 1u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c267;
L_1000c27a:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_mix_mono8_store_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused)),
                              uint32_t arg3 __attribute__((unused)),
                              uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mix_mono8_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mix_mono8_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = 0x1001f030u;
  fp[depth] = F32(esp + 0x1cu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a40cu);
  fp[depth] = F32(esp + 0x18u);
  ++depth;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c309;
L_1000c2b7:;
  eax = BYTE(esi);
  edx = BYTE(esi + 0x1u);
  fp[depth] = F32(ebx + eax * 4u);
  ++depth;
  fp[depth] = F32(ebx + edx * 4u);
  ++depth;
  eax = BYTE(esi + 0x2u);
  edx = BYTE(esi + 0x3u);
  fp[depth] = F32(ebx + eax * 4u);
  ++depth;
  fp[depth] = F32(ebx + edx * 4u);
  ++depth;
  fp[depth] = fp[depth - 1u - 4u];
  ++depth;
  fp[depth - 1u - 4u] = fp[depth - 1u - 4u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 3u] = fp[depth - 1u - 3u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] * fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0x4u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0x8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0xcu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 0x4u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c2b7;
L_1000c309:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c331;
  fp[depth] = F32(0x1001a410u);
  ++depth;
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1];
  --depth;
L_1000c31a:;
  eax = BYTE(esi);
  fp[depth] = F32(ebx + eax * 4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 0x1u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c31a;
L_1000c331:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_mix_mono8_add_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused)),
                            uint32_t arg3 __attribute__((unused)),
                            uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mix_mono8_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mix_mono16_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  fp[depth] = F32(esp + 0x18u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a40cu);
  fp[depth] = F32(esp + 0x14u);
  ++depth;
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c3d3;
  fp[depth++] = (long double)(int16_t)(WORD(esi));
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x2u));
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x4u));
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x6u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  esi = (esi + 0x8u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c3b5;
L_1000c378:;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  fp[depth++] = (long double)(int16_t)(WORD(esi));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x2u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 4u];
    fp[depth - 1u - 4u] = v;
  }
  fp[depth - 1u - 3u] = fp[depth - 1u - 3u] + fp[depth - 1u - 0u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 4u];
    fp[depth - 1u - 4u] = v;
  }
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x4u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x6u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  esi = (esi + 0x8u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c378;
L_1000c3b5:;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  edi = (edi + 0x10u);
L_1000c3d3:;
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c3f5;
  fp[depth] = F32(0x1001a410u);
  ++depth;
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1];
  --depth;
L_1000c3e4:;
  fp[depth++] = (long double)(int16_t)(WORD(esi));
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 0x2u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c3e4;
L_1000c3f5:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_mix_mono16_store_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused)),
                               uint32_t arg3 __attribute__((unused)),
                               uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mix_mono16_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mix_mono16_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  fp[depth] = F32(esp + 0x18u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a40cu);
  fp[depth] = F32(esp + 0x14u);
  ++depth;
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c469;
L_1000c421:;
  fp[depth++] = (long double)(int16_t)(WORD(esi));
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x2u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x4u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  fp[depth++] = (long double)(int16_t)(WORD(esi + 0x6u));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0x4u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0x8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0xcu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 0x8u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c421;
L_1000c469:;
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c48d;
  fp[depth] = F32(0x1001a410u);
  ++depth;
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1];
  --depth;
L_1000c47a:;
  fp[depth++] = (long double)(int16_t)(WORD(esi));
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  esi = (esi + 0x2u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c47a;
L_1000c48d:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_mix_mono16_add_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused)),
                             uint32_t arg3 __attribute__((unused)),
                             uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mix_mono16_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_accumulator_magnitude_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  fp[depth++] = 0.0L;
  esi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esp + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000c4bb;
L_1000c4b1:;
  fp[depth] = F32(esi + ecx * 4u);
  ++depth;
  fp[depth - 1] = __builtin_fabsl(fp[depth - 1]);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000c4b1;
L_1000c4bb:;
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  args[sp + 1u] = BB_U32((uint32_t)(esp + 0x4u) + 0u);
  --depth;
  fp[depth] = F32(esp + 0x4u);
  ++depth;
  esi = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_QMIX_RETURN_FLOAT(input, fp[depth - 1]);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
double __attribute__((naked))
bb_qmix_accumulator_magnitude_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -512(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;movl "
      "%eax,36(%esp);pushl %esp;call " BB_ASM(
          bb_qmix_accumulator_magnitude_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;fldt -508(%esp);ret "
                                           "$0");
}
#endif
void bb_qmix_mix_float_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esp + 0x14u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c52c;
  ecx = (ecx << 4);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000c4ee:;
  fp[depth] = F32(esi + ecx);
  ++depth;
  fp[depth] = F32(esi + ecx + 0x4u);
  ++depth;
  fp[depth] = F32(esi + ecx + 0x8u);
  ++depth;
  fp[depth] = F32(esi + ecx + 0xcu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + ecx);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + ecx + 0x4u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + ecx + 0x8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + ecx + 0xcu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi + ecx) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + ecx + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(edi + ecx + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + ecx + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  a = ecx;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c4ee;
L_1000c52c:;
  ecx = BB_U32(esp + 0x14u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c544;
L_1000c535:;
  fp[depth] = F32(edi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(esi);
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  esi = (esi + 0x4u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c535;
L_1000c544:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_mix_float_add_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mix_float_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mix_float_scale_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = edi;
  esp -= 4u;
  fp[depth] = F32(esp + 0xcu);
  ++depth;
  edi = BB_U32(esp + 0x8u);
  ecx = BB_U32(esp + 0x10u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c58c;
L_1000c562:;
  fp[depth] = F32(edi);
  ++depth;
  fp[depth] = F32(edi + 0x4u);
  ++depth;
  fp[depth] = F32(edi + 0x8u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth] = F32(edi + 0xcu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth] = fp[depth - 1u - 4u];
  ++depth;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 3u] = fp[depth - 1u - 3u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 4u] = fp[depth - 1u - 4u] * fp[depth - 1];
  --depth;
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c562;
L_1000c58c:;
  ecx = BB_U32(esp + 0x10u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c5a1;
L_1000c595:;
  fp[depth] = F32(edi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c595;
L_1000c5a1:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_mix_float_scale_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mix_float_scale_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_clip_stereo16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  a = ecx;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000c61a;
  esi = BB_U32(esp + 0x1cu);
  ebx = BB_U32(esp + 0x20u);
  edi = BB_U32(esp + 0x18u);
  esi = (esi + ecx);
  ebx = (ebx + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000c5d6:;
  fp[depth] = F32(esi + ecx);
  ++depth;
  fp[depth] = F32(ebx + ecx);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  BB_U32(esp + 0x10u) = qmix_integer(&fp[--depth]);
  args[sp + 4u] = BB_U32(esp + 0x10u);
  BB_U32(esp + 0xcu) = qmix_integer(&fp[--depth]);
  args[sp + 3u] = BB_U32(esp + 0xcu);
  eax = BB_U32(esp + 0x10u);
  edx = BB_U32(esp + 0xcu);
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000c63d;
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000c621;
  WORD(edi + ecx) = ((uint16_t)(eax));
L_1000c600:;
  a = edx;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000c629;
  a = edx;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000c632;
  WORD(edi + ecx + 0x2u) = ((uint16_t)(edx));
L_1000c615:;
  a = ecx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000c5d6;
L_1000c61a:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000c621:;
  WORD(edi + ecx) = 0x8000u;
  goto L_1000c600;
L_1000c629:;
  WORD(edi + ecx + 0x2u) = 0x7fffu;
  goto L_1000c615;
L_1000c632:;
  WORD(edi + ecx + 0x2u) = 0x8000u;
  goto L_1000c615;
  goto L_1000c61a;
L_1000c63d:;
  WORD(edi + ecx) = 0x7fffu;
  goto L_1000c600;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_clip_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused)),
                            uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_clip_stereo16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
