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
/* C forms of QMIXER channel and voice controls.
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
  uint32_t overflow = right ? (before >> (bits - 1u)) & 1u
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
static void sample_float_to_int(struct bb_call_state *state,
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
/* C forms of QMIXER playback state and property updates. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */

/* Numeric compatibility boundaries while the i386 bridge remains. These
 * operate on C values; instruction decoding is confined to this retired tool.
 */
static long double qmix_constant(unsigned kind) {
  long double value;
  if (!kind)
    __asm__ volatile("fld1;fstpt %0" : "=m"(value)::"st");
  else if (kind == 1)
    __asm__ volatile("fldln2;fstpt %0" : "=m"(value)::"st");
  else
    __asm__ volatile("fldl2e;fstpt %0" : "=m"(value)::"st");
  return value;
}
static long double qmix_unary(const long double *x, unsigned kind) {
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
static long double qmix_binary(const long double *y, const long double *x,
                               unsigned kind) {
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
static long double qmix_scale(const long double *x,
                              const long double *exponent) {
  long double value;
  __asm__ volatile("fldt %2;fldt %1;fscale;fstpt %0;fstp %%st(0)"
                   : "=m"(value)
                   : "m"(*x), "m"(*exponent)
                   : "st", "st(1)");
  return value;
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
void bb_qmix_channel_bind_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ecx = BB_U32(ecx + 0x34u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003311;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10003311u); /* 1000330f indirect */
L_10003311:;
  ecx = BB_U32(esp + 0xcu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 0, 0x1000331au); /* 10003317 indirect */
  ecx = BB_U32(esi + 0x20u);
  BB_U32(esi + 0x34u) = eax;
  args[--sp] = ecx;
  esp -= 4u;
  edi = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(edi + 0x2cu), 4, 0x10003328u); /* 10003325 indirect */
  ecx = esi;
  SAMPLE_CALL(0x100036f0u, 0, 0x1000332fu); /* 1000332a */
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x4u), 4, 0x1000333bu); /* 10003338 indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0xcu), 4, 0x10003347u); /* 10003344 indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x14u), 4, 0x10003353u); /* 10003350 indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x18u), 4, 0x1000335fu); /* 1000335c indirect */
  edi = BB_U32(esi + 0x34u);
  ecx = BB_U32(esi + 0x68u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x50u), 0, 0x1000336au); /* 10003367 indirect */
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(esi + 0xa0u) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a0c0u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10002790u, 0, 0x10003381u); /* 1000337c */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(edi);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x1000338cu); /* 10003389 indirect */
  eax = BB_U32(esi + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100033a2;
L_10003393:;
  ecx = BB_U32(esi + 0x34u);
  edx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x40u) = ecx;
  eax = BB_U32(eax);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003393;
L_100033a2:;
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
bb_qmix_channel_bind_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_bind_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_cone_set_c(uint32_t *input) {
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
  a = esp;
  b = 0x18u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  edi = BB_U32(esp + 0x24u);
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x100062c0u, 0, 0x10003541u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(0x1001a0b0u) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (esp + 0x14u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  SAMPLE_CALL(0x10006150u, 0, 0x1000355cu); /* 10003557 */
  edx = (esp + 0x14u);
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  edi = BB_U32(eax + 0x4u);
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx) = ecx;
  ecx = edx;
  BB_U32(edx + 0x4u) = edi;
  BB_U32(edx + 0x8u) = eax;
  edx = (esi + 0x88u);
  eax = BB_U32(ecx);
  edi = BB_U32(ecx + 0x4u);
  fp[depth] = F32(esp + 0x28u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a0a0u);
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx) = eax;
  eax = BB_U32(esp + 0x28u);
  BB_U32(edx + 0x4u) = edi;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a0c8u);
  BB_U32(edx + 0x8u) = ecx;
  BB_U32(esi + 0x94u) = eax;
  fp[depth - 1] = qmix_unary(&fp[depth - 1], 1);
  F32(esi + 0x98u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esp + 0x2cu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a0d0u);
  fp[depth++] = qmix_constant(2);
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  fp[depth - 1u - 1u] = fp[depth - 1];
  fp[depth - 1] = qmix_unary(&fp[depth - 1], 2);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1u - 0u];
  BB_U32(esi + 0x6cu) = 0x1u;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = qmix_unary(&fp[depth - 1], 3);
  edi = args[sp++];
  esp += 4u;
  fp[depth++] = qmix_constant(0);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = qmix_scale(&fp[depth - 1], &fp[depth - 2]);
  fp[depth - 1u - 1u] = fp[depth - 1];
  --depth;
  F32(esi + 0x9cu) = (float)(fp[depth - 1]);
  --depth;
  esi = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x18u;
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
bb_qmix_cone_set_entry(uint32_t arg0 __attribute__((unused)),
                       uint32_t arg1 __attribute__((unused)),
                       uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_cone_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                          "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_distance_set_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ecx = BB_U32(eax + 0x4u);
  edi = BB_U32(esi + 0x34u);
  BB_U32(esi + 0xa0u) = ecx;
  ecx = BB_U32(esi + 0x68u);
  edx = BB_U32(eax + 0x8u);
  BB_U32(esi + 0xa4u) = edx;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x50u), 0, 0x10003615u); /* 10003612 indirect */
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(esi + 0xa0u) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a0c0u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10002790u, 0, 0x1000362cu); /* 10003627 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(edi);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x10003637u); /* 10003634 indirect */
  BB_U32(esi + 0x6cu) = 0x1u;
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
bb_qmix_distance_set_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_distance_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_distance_update_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(ecx + 0x34u);
  edi = ecx;
  ecx = BB_U32(ecx + 0x68u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x50u), 0, 0x1000365fu); /* 1000365c indirect */
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(edi + 0xa0u) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a0c0u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10002790u, 0, 0x10003676u); /* 10003671 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x10003681u); /* 1000367e indirect */
  BB_U32(edi + 0x6cu) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_distance_update_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_distance_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_voice_queue_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  edi = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x10005ac0u, 4, 0x1000377fu); /* 1000377a */
  a = BB_U32(esi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003791;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x40u), 4, 0x1000378du); /* 1000378a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003807;
L_10003791:;
  a = BB_U32(esi + 0x58u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100037de;
  eax = BB_U32(esi + 0x54u);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10001020u, 0, 0x100037a6u); /* 100037a1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x5cu);
  edx = (edx ^ edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(esi + 0x54u);
  BB_U32(esi + 0x5cu) = eax;
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_100037d0;
  ebx = eax;
L_100037be:;
  ecx = (ebx + 0x8u);
  ebx = (ebx + 0x8u);
  edx = (edx + 1u);
  BB_U32(ebx + 0xfffffff8u) = ecx;
  ecx = BB_U32(esi + 0x54u);
  ecx = (ecx - 1u);
  a = ecx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100037be;
L_100037d0:;
  ecx = BB_U32(esi + 0x54u);
  BB_U32(eax + ecx * 8u + 0xfffffff8u) = 0x0u;
  BB_U32(esi + 0x58u) = eax;
L_100037de:;
  ecx = BB_U32(esi + 0x58u);
  eax = BB_U32(ecx);
  BB_U32(esi + 0x58u) = eax;
  BB_U32(ecx + 0x4u) = edi;
  BB_U32(ecx) = 0x0u;
  eax = BB_U32(esi + 0x50u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003801;
  edi = args[sp++];
  esp += 4u;
  BB_U32(eax) = ecx;
  BB_U32(esi + 0x50u) = ecx;
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
L_10003801:;
  BB_U32(esi + 0x4cu) = ecx;
  BB_U32(esi + 0x50u) = ecx;
L_10003807:;
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
bb_qmix_voice_queue_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_queue_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_voice_start_c(uint32_t *input) {
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
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x64u);
  edx = BB_U32(ecx + 0x34u);
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(esp + 0x18u);
  ecx = BB_U32(esi + 0x60u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x10005c90u, 12, 0x1000382eu); /* 10003829 */
  ecx = BB_U32(edi + 0x38u);
  eax = BB_U32(ecx + 0x4u);
  BB_U32(esp + 0x8u) = eax;
  args[sp + 2u] = BB_U32(esp + 0x8u);
  BB_U32(esp + 0xcu) = 0x0u;
  args[sp + 3u] = BB_U32(esp + 0xcu);
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x8u));
  fp[depth - 1] = fp[depth - 1] * F32(esi + 0xccu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = esi;
  SAMPLE_CALL(0x10005b30u, 4, 0x10003857u);         /* 10003852 */
  SAMPLE_CALL(BB_U32(0x10021374u), 0, 0x1000385du); /* 10003857 indirect */
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0x2cu) = eax;
  eax = 0x1u;
  esi = args[sp++];
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_voice_start_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_start_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_channel_stop_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  eax = (ecx + 0x4cu);
  edi = ecx;
  esi = BB_U32(esp + 0xcu);
  a = edi;
  b = 0x38u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x100038c0u, 0, 0x100038a5u); /* 100038a0 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x100038c0u, 0, 0x100038afu); /* 100038aa */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
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
bb_qmix_channel_stop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_chain_stop_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(eax);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100038ec;
  esi = BB_U32(esp + 0x18u);
  a = ebp;
  b = ebp;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebp = result;
L_100038d4:;
  ebx = BB_U32(edi + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  ecx = ebx;
  SAMPLE_CALL(0x10005e50u, 4, 0x100038dfu); /* 100038da */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100038e6;
  BB_U32(ebx + 0x4cu) = ebp;
L_100038e6:;
  edi = BB_U32(edi);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100038d4;
L_100038ec:;
  ebp = args[sp++];
  esp += 4u;
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
bb_qmix_chain_stop_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_chain_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_drain_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  SAMPLE_CALL(0x10005b70u, 0, 0x1000390du); /* 10003908 */
L_1000390d:;
  ecx = BB_U32(esi + 0x4cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003918;
  ebx = (ebx ^ ebx);
  goto L_10003930;
L_10003918:;
  ebx = BB_U32(ecx + 0x4u);
  a = BB_U32(esi + 0x50u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003923;
  BB_U32(esi + 0x50u) = edi;
L_10003923:;
  eax = BB_U32(ecx);
  edx = BB_U32(esi + 0x58u);
  BB_U32(esi + 0x4cu) = eax;
  BB_U32(ecx) = edx;
  BB_U32(esi + 0x58u) = ecx;
L_10003930:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100039a8;
  BB_U32(ebx + 0x4cu) = edi;
  a = BB_U32(esi + 0x44u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000397f;
  eax = BB_U32(esi + 0x40u);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10001020u, 0, 0x1000394bu); /* 10003946 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x48u);
  edx = (edx ^ edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(esi + 0x40u);
  BB_U32(esi + 0x48u) = eax;
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_10003975;
  ecx = eax;
L_10003963:;
  ebp = (ecx + 0x8u);
  ecx = (ecx + 0x8u);
  edx = (edx + 1u);
  BB_U32(ecx + 0xfffffff8u) = ebp;
  ebp = BB_U32(esi + 0x40u);
  ebp = (ebp - 1u);
  a = ebp;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10003963;
L_10003975:;
  ecx = BB_U32(esi + 0x40u);
  BB_U32(eax + ecx * 8u + 0xfffffff8u) = edi;
  BB_U32(esi + 0x44u) = eax;
L_1000397f:;
  ecx = BB_U32(esi + 0x44u);
  eax = BB_U32(ecx);
  BB_U32(esi + 0x44u) = eax;
  BB_U32(ecx + 0x4u) = ebx;
  BB_U32(ecx) = edi;
  eax = BB_U32(esi + 0x3cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000399d;
  BB_U32(eax) = ecx;
  BB_U32(esi + 0x3cu) = ecx;
  goto L_1000390d;
L_1000399d:;
  BB_U32(esi + 0x38u) = ecx;
  BB_U32(esi + 0x3cu) = ecx;
  goto L_1000390d;
L_100039a8:;
  ebp = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked)) bb_qmix_channel_drain_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_drain_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_render_c(uint32_t *input) {
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
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  a = BB_U32(esi + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100039cb;
  eax = (eax ^ eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xcu;
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
L_100039cb:;
  a = BYTE(esi + 0x8u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10003a13;
  eax = BB_U32(esp + 0x20u);
  ecx = (ecx ^ ecx);
  edx = BB_U32(esi + 0x30u);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  BB_U32(esp + 0x14u) = ecx;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  eax = BB_U32(edx + 0x4u);
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x10u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  BB_U32(esp + 0x14u) = ecx;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  ecx = (esi + 0xa8u);
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x14u));
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] / fp[depth - 1];
  --depth;
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10004030u, 4, 0x10003a08u); /* 10003a03 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003a13;
  BB_U32(esi + 0x6cu) = 0x1u;
L_10003a13:;
  BB_U32(esp + 0x18u) = 0x0u;
  args[sp + 6u] = BB_U32(esp + 0x18u);
L_10003a1b:;
  a = BB_U32(esp + 0x20u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10003b7e;
  edi = (edi ^ edi);
L_10003a28:;
  ecx = BB_U32(esi + 0x4cu);
  ebx = 0x0u;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003a37;
  ebx = BB_U32(ecx + 0x4u);
L_10003a37:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b8c;
  a = BB_U32(ebx + 0x4cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003ad9;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003a50;
  ebx = (ebx ^ ebx);
  goto L_10003a68;
L_10003a50:;
  ebx = BB_U32(ecx + 0x4u);
  a = BB_U32(esi + 0x50u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003a5b;
  BB_U32(esi + 0x50u) = edi;
L_10003a5b:;
  eax = BB_U32(ecx);
  edx = BB_U32(esi + 0x58u);
  BB_U32(esi + 0x4cu) = eax;
  BB_U32(ecx) = edx;
  BB_U32(esi + 0x58u) = ecx;
L_10003a68:;
  a = BB_U32(esi + 0x44u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003ab0;
  eax = BB_U32(esi + 0x40u);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10001020u, 0, 0x10003a7cu); /* 10003a77 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x48u);
  edx = (edx ^ edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(esi + 0x40u);
  BB_U32(esi + 0x48u) = eax;
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_10003aa6;
  ecx = eax;
L_10003a94:;
  ebp = (ecx + 0x8u);
  ecx = (ecx + 0x8u);
  edx = (edx + 1u);
  BB_U32(ecx + 0xfffffff8u) = ebp;
  ebp = BB_U32(esi + 0x40u);
  ebp = (ebp - 1u);
  a = ebp;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10003a94;
L_10003aa6:;
  ecx = BB_U32(esi + 0x40u);
  BB_U32(eax + ecx * 8u + 0xfffffff8u) = edi;
  BB_U32(esi + 0x44u) = eax;
L_10003ab0:;
  ecx = BB_U32(esi + 0x44u);
  eax = BB_U32(ecx);
  BB_U32(esi + 0x44u) = eax;
  BB_U32(ecx + 0x4u) = ebx;
  BB_U32(ecx) = edi;
  eax = BB_U32(esi + 0x3cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003ace;
  BB_U32(eax) = ecx;
  BB_U32(esi + 0x3cu) = ecx;
  goto L_10003a28;
L_10003ace:;
  BB_U32(esi + 0x38u) = ecx;
  BB_U32(esi + 0x3cu) = ecx;
  goto L_10003a28;
L_10003ad9:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b8c;
  ecx = esi;
  SAMPLE_CALL(0x10003ba0u, 0, 0x10003ae8u); /* 10003ae3 */
  eax = BB_U32(esp + 0x20u);
  ecx = BB_U32(esi + 0x28u);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (esp + 0x14u);
  ecx = ebx;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10005f00u, 8, 0x10003b00u); /* 10003afb */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b6d;
  edi = (esi + 0x40u);
L_10003b07:;
  ecx = BB_U32(esi + 0x4cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003b12;
  a = ebx;
  b = ebx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  goto L_10003b2e;
L_10003b12:;
  ebx = BB_U32(ecx + 0x4u);
  a = BB_U32(esi + 0x50u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003b21;
  BB_U32(esi + 0x50u) = 0x0u;
L_10003b21:;
  eax = BB_U32(ecx);
  edx = BB_U32(esi + 0x58u);
  BB_U32(esi + 0x4cu) = eax;
  BB_U32(ecx) = edx;
  BB_U32(esi + 0x58u) = ecx;
L_10003b2e:;
  ecx = edi;
  SAMPLE_CALL(0x10004180u, 0, 0x10003b35u); /* 10003b30 */
  BB_U32(eax + 0x4u) = ebx;
  BB_U32(eax) = 0x0u;
  ecx = BB_U32(esi + 0x3cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b49;
  BB_U32(ecx) = eax;
  goto L_10003b4c;
L_10003b49:;
  BB_U32(esi + 0x38u) = eax;
L_10003b4c:;
  BB_U32(esi + 0x3cu) = eax;
  eax = BB_U32(esi + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b5b;
  eax = BB_U32(eax + 0x4u);
  goto L_10003b5d;
L_10003b5b:;
  eax = (eax ^ eax);
L_10003b5d:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b6d;
  args[--sp] = eax;
  esp -= 4u;
  ecx = esi;
  eax = BB_U32(esi);
  SAMPLE_CALL(BB_U32(eax + 0x40u), 4, 0x10003b69u); /* 10003b66 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003b07;
L_10003b6d:;
  eax = BB_U32(esp + 0x10u);
  BB_U32(esp + 0x20u) = (BB_U32(esp + 0x20u) - eax);
  args[sp + 8u] = BB_U32(esp + 0x20u);
  BB_U32(esp + 0x18u) = (BB_U32(esp + 0x18u) + eax);
  args[sp + 6u] = BB_U32(esp + 0x18u);
  goto L_10003a1b;
L_10003b7e:;
  eax = BB_U32(esp + 0x18u);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xcu;
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
L_10003b8c:;
  eax = BB_U32(esp + 0x18u);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xcu;
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
bb_qmix_channel_render_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_render_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_spatial_update_c(uint32_t *input) {
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
  a = esp;
  b = 0x58u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ebx = (ebx ^ ebx);
  a = BB_U32(esi + 0x6cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10003ec2;
  ecx = BB_U32(esi + 0x68u);
  eax = (esp + 0x4cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esi + 0x6cu) = ebx;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x48u), 4, 0x10003bc3u); /* 10003bc0 indirect */
  edi = (esi + 0x70u);
  eax = (esp + 0x4cu);
  ecx = (esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x100061a0u, 0, 0x10003bd6u); /* 10003bd1 */
  edx = (esp + 0x1cu);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ecx;
  ecx = (esp + 0x40u);
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  eax = BB_U32(edx);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(ecx) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(ecx + 0x4u) = eax;
  eax = (esp + 0x20u);
  edx = BB_U32(edx + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(ecx + 0x8u) = edx;
  ecx = BB_U32(esi + 0x68u);
  SAMPLE_CALL(0x10002f40u, 8, 0x10003c0fu); /* 10003c0a */
  edx = (esp + 0x10u);
  ecx = BB_U32(eax);
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(edx + 0x4u) = ecx;
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  fp[depth] = F32(esp + 0x24u);
  ++depth;
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  args[sp + 3u] = BB_U32((uint32_t)(esp + 0x8u) + 4u);
  --depth;
  fp[depth] = F32(esp + 0x20u);
  ++depth;
  F64(esp) = (double)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  args[sp + 1u] = BB_U32((uint32_t)(esp) + 4u);
  --depth;
  SAMPLE_CALL(0x100195e0u, 0, 0x10003c3du);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  F32(esp + 0x2cu) = (float)(fp[depth - 1]);
  args[sp + 11u] = BB_U32((uint32_t)(esp + 0x2cu) + 0u);
  --depth;
  fp[depth] = F32(esp + 0x28u);
  ++depth;
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  args[sp + 3u] = BB_U32((uint32_t)(esp + 0x8u) + 4u);
  --depth;
  fp[depth] = F32(esp + 0x2cu);
  ++depth;
  F64(esp) = (double)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  args[sp + 1u] = BB_U32((uint32_t)(esp) + 4u);
  --depth;
  SAMPLE_CALL(0x100195e0u, 0, 0x10003c5eu);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  F32(esp + 0x1cu) = (float)(fp[depth - 1]);
  args[sp + 7u] = BB_U32((uint32_t)(esp + 0x1cu) + 0u);
  --depth;
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x68u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x50u), 0, 0x10003c6du); /* 10003c6a indirect */
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(esp + 0xcu) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a0c0u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10002790u, 0, 0x10003c82u); /* 10003c7d */
  BB_U32(esp + 0x30u) = eax;
  args[sp + 12u] = BB_U32(esp + 0x30u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esp + 0xcu);
  b = 0x7fffffffu;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003c9d;
  BB_U32(esp + 0x28u) = ebx;
  args[sp + 10u] = BB_U32(esp + 0x28u);
  BB_U32(esp + 0x30u) = ebx;
  args[sp + 12u] = BB_U32(esp + 0x30u);
  goto L_10003cf5;
L_10003c9d:;
  fp[depth] = F32(esp + 0x18u);
  ++depth;
  fp[depth] = F32(esp + 0x1cu);
  ++depth;
  fp[depth - 2] = qmix_binary(&fp[depth - 2], &fp[depth - 1], 0);
  --depth;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a0d8u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10002790u, 0, 0x10003cb8u); /* 10003cb3 */
  BB_U32(esp + 0x34u) = eax;
  args[sp + 13u] = BB_U32(esp + 0x34u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esp + 0x1cu);
  b = 0x7fffffffu;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003cd3;
  BB_U32(esp + 0x28u) = 0x0u;
  args[sp + 10u] = BB_U32(esp + 0x28u);
  goto L_10003cf5;
L_10003cd3:;
  fp[depth] = F32(esp + 0x10u);
  ++depth;
  fp[depth] = F32(esp + 0x14u);
  ++depth;
  fp[depth - 2] = qmix_binary(&fp[depth - 2], &fp[depth - 1], 0);
  --depth;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a0d8u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  SAMPLE_CALL(0x10002790u, 0, 0x10003ceeu); /* 10003ce9 */
  BB_U32(esp + 0x2cu) = eax;
  args[sp + 11u] = BB_U32(esp + 0x2cu);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10003cf5:;
  fp[depth] = F32(esi + 0xa4u);
  ++depth;
  {
    long double other = F32(esp + 0xcu);
    fp_status = sample_compare(&fp[depth - 1], &other);
    --depth;
  }
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  eax = 0x1u;
  if (!(flags & 0x40u))
    goto L_10003d0d;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_10003d0d:;
  ecx = esi;
  BB_U32(esi + 0xd4u) = eax;
  eax = (esp + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10003690u, 4, 0x10003d1fu); /* 10003d1a */
  ecx = esi;
  SAMPLE_CALL(0x100036f0u, 0, 0x10003d26u); /* 10003d21 */
  a = BYTE(esi + 0x8u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10003d7a;
  eax = (esp + 0x4cu);
  ecx = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x100061a0u, 0, 0x10003d3cu); /* 10003d37 */
  edx = (esp + 0x40u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(edx) = ecx;
  ecx = (esp + 0x20u);
  ebx = BB_U32(eax + 0x4u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = (esi + 0xa8u);
  BB_U32(edx + 0x4u) = ebx;
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  SAMPLE_CALL(0x10004050u, 8, 0x10003d64u); /* 10003d5f */
  edx = (esp + 0x58u);
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ebx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  goto L_10003dc7;
L_10003d7a:;
  ecx = BB_U32(esi + 0x68u);
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0x4cu), 4, 0x10003d87u); /* 10003d84 indirect */
  ecx = (esp + 0x10u);
  eax = (esi + 0x7cu);
  edx = (esp + 0x1cu);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x100061a0u, 0, 0x10003d9au); /* 10003d95 */
  edx = (esp + 0x40u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = edx;
  ebx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  edx = (esp + 0x58u);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  ebx = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = ecx;
L_10003dc7:;
  eax = (esp + 0x40u);
  ecx = (esp + 0x58u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x10006240u, 0, 0x10003dd6u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  {
    long double other = F32(0x1001a0acu);
    fp_status = sample_compare(&fp[depth - 1], &other);
  }
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  F32(esp + 0x10u) = (float)(fp[depth - 1]);
  args[sp + 4u] = BB_U32((uint32_t)(esp + 0x10u) + 0u);
  --depth;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x40u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10003e5c;
  a = BB_U32(esp + 0xcu);
  b = 0x7fffffffu;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003e5c;
  fp[depth] = F32(esp + 0x10u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] / F32(esp + 0xcu);
  eax = (esp + 0x1cu);
  ecx = BB_U32(esi + 0x68u);
  F32(esp + 0x10u) = (float)(fp[depth - 1]);
  args[sp + 4u] = BB_U32((uint32_t)(esp + 0x10u) + 0u);
  --depth;
  ebx = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebx + 0x54u), 4, 0x10003e0du); /* 10003e0a indirect */
  fp[depth] = F32(esp + 0x10u);
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  {
    long double other = F32(esp + 0x1cu);
    fp_status = sample_compare(&fp[depth - 1], &other);
    --depth;
  }
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10003e2a;
  BB_U32(esi + 0xccu) = 0x41200000u;
  goto L_10003e66;
L_10003e2a:;
  fp[depth] = F32(esp + 0x10u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] / F32(esp + 0x1cu);
  fp[depth - 1] = fp[depth - 1] + F32(0x1001a0b0u);
  fp[depth - 1] = F32(0x1001a0b0u) / fp[depth - 1];
  F32(esi + 0xccu) = (float)(fp[depth - 1]);
  --depth;
  a = BB_U32(esi + 0xccu);
  b = 0x41200000u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10003e66;
  BB_U32(esi + 0xccu) = 0x41200000u;
  goto L_10003e66;
L_10003e5c:;
  BB_U32(esi + 0xccu) = 0x3f800000u;
L_10003e66:;
  eax = BB_U32(esi + 0x24u);
  ebx = BB_U32(esi);
  args[--sp] = eax;
  esp -= 4u;
  ecx = esi;
  SAMPLE_CALL(BB_U32(ebx + 0x28u), 4, 0x10003e71u); /* 10003e6e indirect */
  a = BB_U32(esi + 0x94u);
  b = 0x43b40000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10003e8e;
  BB_U32(esi + 0xd0u) = 0x0u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x58u;
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
L_10003e8e:;
  eax = (esp + 0x40u);
  ecx = (esi + 0x88u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x10006240u, 0, 0x10003e9fu);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth] = F32(esi + 0x98u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  fp_status = sample_compare(&fp[depth - 1], &fp[depth - 2]);
  depth -= 2u;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x41u;
  flags = logic_flags(flags, a & b, 8);
  eax = 0x1u;
  if (flags & 0x40u)
    goto L_10003ebc;
  eax = (eax ^ eax);
L_10003ebc:;
  BB_U32(esi + 0xd0u) = eax;
L_10003ec2:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x58u;
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
uint32_t __attribute__((naked)) bb_qmix_spatial_update_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_spatial_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_progress_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x4cu);
  ecx = 0x0u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003edf;
  ecx = BB_U32(eax + 0x4u);
L_10003edf:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003efa;
  eax = BB_U32(esp + 0x4u);
  ecx = BB_U32(esp + 0x8u);
  BB_U32(eax) = 0x0u;
  BB_U32(ecx) = 0x0u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10003efa:;
  eax = BB_U32(esp + 0xcu);
  edx = BB_U32(esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10006100u, 12, 0x10003f0eu); /* 10003f09 */
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
bb_qmix_channel_progress_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_progress_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_cone_get_c(uint32_t *input) {
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
  esi = BB_U32(esp + 0x8u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003f7f;
  edx = (ecx + 0x88u);
  eax = BB_U32(edx);
  BB_U32(esi) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(esi + 0x4u) = eax;
  edx = BB_U32(edx + 0x8u);
  BB_U32(esi + 0x8u) = edx;
L_10003f7f:;
  eax = BB_U32(esp + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003f8f;
  edx = BB_U32(ecx + 0x94u);
  BB_U32(eax) = edx;
L_10003f8f:;
  eax = BB_U32(esp + 0x10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003fa9;
  fp[depth++] = qmix_constant(1);
  fp[depth] = F32(ecx + 0x9cu);
  ++depth;
  fp[depth - 2] = qmix_binary(&fp[depth - 2], &fp[depth - 1], 1);
  --depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a0e8u);
  F32(eax) = (float)(fp[depth - 1]);
  --depth;
L_10003fa9:;
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
bb_qmix_cone_get_entry(uint32_t arg0 __attribute__((unused)),
                       uint32_t arg1 __attribute__((unused)),
                       uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_cone_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                          "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_motion_constructor_c(uint32_t *input) {
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
  BB_U32(ecx + 0x1cu) = 0x3f4ccccdu;
  eax = 0x3dcccccdu;
  BB_U32(ecx + 0x18u) = eax;
  BB_U32(ecx + 0x20u) = eax;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(ecx + 0x8u) = eax;
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx) = eax;
  BB_U32(ecx + 0x14u) = eax;
  BB_U32(ecx + 0x10u) = eax;
  BB_U32(ecx + 0xcu) = eax;
  eax = ecx;
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
uint32_t __attribute__((naked)) bb_qmix_motion_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_motion_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_motion_tick_c(uint32_t *input) {
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
  fp[depth] = F32(ecx + 0x18u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(esp + 0x4u);
  eax = 0x1u;
  F32(ecx + 0x18u) = (float)(fp[depth - 1]);
  --depth;
  a = BB_U32(ecx + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10004047;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_10004047:;
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
bb_qmix_motion_tick_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_motion_tick_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_motion_filter_c(uint32_t *input) {
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
  b = 0x60u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  a = BB_U32(ecx + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10004141;
  edi = (esi + 0xcu);
  ebx = BB_U32(esp + 0x74u);
  eax = (esp + 0x60u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x100061a0u, 0, 0x10004075u); /* 10004070 */
  edx = (esp + 0x18u);
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  fp[depth] = F32(0x1001a0b0u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(esi + 0x1cu);
  ecx = BB_U32(eax);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  fp[depth - 1] = fp[depth - 1] / F32(esi + 0x20u);
  BB_U32(edx + 0x4u) = ecx;
  eax = BB_U32(eax + 0x8u);
  ecx = (esp + 0x58u);
  BB_U32(edx + 0x8u) = eax;
  edx = (esp + 0x10u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  SAMPLE_CALL(0x10006150u, 0, 0x100040aeu); /* 100040a9 */
  edx = (esp + 0x24u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ecx;
  ecx = (esp + 0x48u);
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  edx = BB_U32(esi + 0x1cu);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x10006150u, 0, 0x100040d4u); /* 100040cf */
  edx = (esp + 0x30u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ecx;
  ecx = (esp + 0x24u);
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  edx = (esp + 0x18u);
  eax = (esp + 0x3cu);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x100061f0u, 0, 0x100040ffu); /* 100040fa */
  edx = (esp + 0x3cu);
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ecx;
  eax = BB_U32(eax + 0x8u);
  ecx = edx;
  BB_U32(edx + 0x8u) = eax;
  eax = BB_U32(ecx);
  edx = BB_U32(ecx + 0x4u);
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(esi) = eax;
  BB_U32(esi + 0x4u) = edx;
  BB_U32(esi + 0x8u) = ecx;
  fp[depth] = F32(esi + 0x18u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(esi + 0x20u);
  eax = BB_U32(ebx);
  BB_U32(edi) = eax;
  ecx = BB_U32(ebx + 0x4u);
  BB_U32(edi + 0x4u) = ecx;
  edx = BB_U32(ebx + 0x8u);
  F32(esi + 0x18u) = (float)(fp[depth - 1]);
  --depth;
  BB_U32(edi + 0x8u) = edx;
L_10004141:;
  eax = BB_U32(esp + 0x70u);
  ecx = BB_U32(esi);
  edi = args[sp++];
  esp += 4u;
  BB_U32(eax) = ecx;
  edx = BB_U32(esi + 0x4u);
  BB_U32(eax + 0x4u) = edx;
  ebx = BB_U32(esi + 0x8u);
  esi = args[sp++];
  esp += 4u;
  BB_U32(eax + 0x8u) = ebx;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x60u;
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
bb_qmix_motion_filter_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_motion_filter_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_pool_take_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(ecx + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  esi = ecx;
  if (!(flags & 0x40u))
    goto L_100041cd;
  eax = BB_U32(esi);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10001020u, 0, 0x10004198u); /* 10004193 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x8u);
  edi = (edi ^ edi);
  BB_U32(eax) = ecx;
  ecx = BB_U32(esi);
  BB_U32(esi + 0x8u) = eax;
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_100041c0;
  edx = eax;
L_100041af:;
  ecx = (edx + 0x8u);
  edx = (edx + 0x8u);
  edi = (edi + 1u);
  BB_U32(edx + 0xfffffff8u) = ecx;
  ecx = BB_U32(esi);
  ecx = (ecx - 1u);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100041af;
L_100041c0:;
  ecx = BB_U32(esi);
  BB_U32(eax + ecx * 8u + 0xfffffff8u) = 0x0u;
  BB_U32(esi + 0x4u) = eax;
L_100041cd:;
  eax = BB_U32(esi + 0x4u);
  edi = args[sp++];
  esp += 4u;
  ecx = BB_U32(eax);
  BB_U32(esi + 0x4u) = ecx;
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
uint32_t __attribute__((naked)) bb_qmix_pool_take_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pool_take_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_decoder_unavailable_c(uint32_t *input) {
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
  args[--sp] = 0x8u;
  esp -= 4u;
  SAMPLE_CALL(0x10005040u, 0, 0x10005c07u); /* 10005c02 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_decoder_unavailable_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_decoder_unavailable_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_voice_constructor_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  edi = ebx;
  esi = BB_U32(esp + 0x10u);
  ecx = 0xcu;
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = (eax ^ eax);
  a = WORD(ebx);
  b = 0x20u;
  flags = sub_flags(flags, a, b, 16);
  BB_U32(ebx + 0x30u) = eax;
  BB_U32(ebx + 0x34u) = eax;
  BB_U32(ebx + 0x4cu) = eax;
  if (!(flags & 0x40u))
    goto L_10005c36;
  BB_U32(ebx + 0x20u) = eax;
L_10005c36:;
  eax = BB_U32(ebx + 0xau);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10002660u, 0, 0x10005c3fu); /* 10005c3a */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebx + 0x30u) = eax;
  eax = BB_U32(ebx + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10002660u, 0, 0x10005c4eu); /* 10005c49 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebx + 0x34u) = eax;
  eax = BB_U32(ebx + 0x30u);
  edi = args[sp++];
  esp += 4u;
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  esi = args[sp++];
  esp += 4u;
  BB_U32(ebx + 0x38u) = eax;
  eax = ebx;
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
bb_qmix_voice_constructor_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_voice_destructor_c(uint32_t *input) {
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
  esi = ecx;
  ecx = BB_U32(ecx + 0x30u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005c80;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10005c80u); /* 10005c7e indirect */
L_10005c80:;
  ecx = BB_U32(esi + 0x34u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005c8d;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10005c8du); /* 10005c8b indirect */
L_10005c8d:;
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
uint32_t __attribute__((naked)) bb_qmix_voice_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_voice_notify_c(uint32_t *input) {
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
  edx = BB_U32(ecx + 0xeu);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005dab;
  eax = BB_U32(ecx + 0xau);
  ecx = BB_U32(ecx + 0x6u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x10021310u), 16, 0x10005dabu); /* 10005da5 indirect */
L_10005dab:;
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
bb_qmix_voice_notify_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_notify_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_voice_matches_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(ecx + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  esi = ecx;
  if (flags & 0x40u)
    goto L_10005e92;
  eax = BB_U32(esi + 0xau);
  edi = BB_U32(esp + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005e73;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10005ea0u, 0, 0x10005e6cu); /* 10005e67 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10005e88;
L_10005e73:;
  eax = BB_U32(esi + 0x20u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005e92;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10005ea0u, 0, 0x10005e81u); /* 10005e7c */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005e92;
L_10005e88:;
  eax = 0x1u;
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
L_10005e92:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_voice_matches_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_matches_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_wave_contains_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edi = BB_U32(esp + 0xcu);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005eb6;
  eax = 0x1u;
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
L_10005eb6:;
  ecx = BB_U32(eax + 0x20u);
  a = ecx;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005ec9;
  a = BB_U32(edi + 0x20u);
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005ec9;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
L_10005ec9:;
  a = BB_U32(edi + 0x20u);
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005ef9;
  a = ecx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005ef9;
  edx = BB_U32(eax + 0x10u);
  esi = BB_U32(edi + 0x10u);
  ecx = BB_U32(eax + 0x14u);
  eax = BB_U32(edi + 0x14u);
  ecx = (ecx + edx);
  eax = (eax + esi);
  a = esi;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10005ef4;
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10005ef4;
  eax = 0x1u;
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
L_10005ef4:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
L_10005ef9:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_wave_contains_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_contains_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_voice_render_c(uint32_t *input) {
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
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = BB_U32(esi + 0x30u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x10005f11u); /* 10005f0e indirect */
  ebx = eax;
  ecx = BB_U32(esi + 0x34u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005f25;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x10005f1fu); /* 10005f1c indirect */
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10005f25;
  ebx = eax;
L_10005f25:;
  edi = BB_U32(esp + 0x20u);
  a = edi;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005f40;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10005f3eu); /* 10005f38 indirect */
  ebx = eax;
L_10005f40:;
  eax = BB_U32(esp + 0x1cu);
  ecx = BB_U32(eax);
  a = ebx;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10005f4e;
  ebx = ecx;
  goto L_10005f50;
L_10005f4e:;
  BB_U32(eax) = ebx;
L_10005f50:;
  ecx = BB_U32(esi + 0x38u);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0x2u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_10006047;
  a = BB_U32(esi + 0x34u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005ff7;
  a = WORD(ecx + 0xeu);
  b = 0x8u;
  flags = sub_flags(flags, a, b, 16);
  eax = BB_U32(esi + 0x44u);
  if (!(flags & 0x40u))
    goto L_10005fb6;
  ecx = BB_U32(esi + 0x48u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  eax = (esp + 0x14u);
  BB_U32(esp + 0x10u) = ecx;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  ecx = BB_U32(esi + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  ebp = BB_U32(ecx);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x14u), 12, 0x10005f8fu); /* 10005f8c indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  ebp = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebp + 0x14u), 12, 0x10005f9eu); /* 10005f9b indirect */
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(esp + 0x14u);
  edx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  ebp = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x2cu), 12, 0x10005fb1u); /* 10005fae indirect */
  goto L_1000609a;
L_10005fb6:;
  ecx = BB_U32(esi + 0x48u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  eax = (esp + 0x14u);
  BB_U32(esp + 0x10u) = ecx;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  ecx = BB_U32(esi + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  ebp = BB_U32(ecx);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x10u), 12, 0x10005fd0u); /* 10005fcd indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  ebp = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebp + 0x10u), 12, 0x10005fdfu); /* 10005fdc indirect */
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(esp + 0x14u);
  edx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  ebp = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x24u), 12, 0x10005ff2u); /* 10005fef indirect */
  goto L_1000609a;
L_10005ff7:;
  a = WORD(ecx + 0xeu);
  b = 0x8u;
  flags = sub_flags(flags, a, b, 16);
  eax = BB_U32(esi + 0x44u);
  if (!(flags & 0x40u))
    goto L_10006024;
  ecx = BB_U32(esi + 0x30u);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  eax = (esp + 0x10u);
  ebp = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x14u), 12, 0x10006014u); /* 10006011 indirect */
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(esp + 0x14u);
  ebp = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x30u), 8, 0x10006022u); /* 1000601f indirect */
  goto L_1000609a;
L_10006024:;
  ecx = BB_U32(esi + 0x30u);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  eax = (esp + 0x10u);
  ebp = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x10u), 12, 0x10006037u); /* 10006034 indirect */
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(esp + 0x14u);
  ebp = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x28u), 8, 0x10006045u); /* 10006042 indirect */
  goto L_1000609a;
L_10006047:;
  a = ((uint16_t)(eax));
  b = 0x2u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_1000609a;
  ebp = BB_U32(esi + 0x44u);
  eax = BB_U32(esi + 0x48u);
  a = WORD(ecx + 0xeu);
  b = 0x8u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_1000607b;
  ecx = BB_U32(esi + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0xcu), 16, 0x1000606au); /* 10006067 indirect */
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = ebx;
  esp -= 4u;
  edx = BB_U32(esp + 0x14u);
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x38u), 12, 0x10006079u); /* 10006076 indirect */
  goto L_1000609a;
L_1000607b:;
  ecx = BB_U32(esi + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 16, 0x1000608bu); /* 10006088 indirect */
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = ebx;
  esp -= 4u;
  edx = BB_U32(esp + 0x14u);
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x34u), 12, 0x1000609au); /* 10006097 indirect */
L_1000609a:;
  ecx = BB_U32(esi + 0x30u);
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 4, 0x100060a3u); /* 100060a0 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100060e8;
  eax = BB_U32(esi + 0x3cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100060c9;
  args[--sp] = 0x3bdu;
  esp -= 4u;
  ecx = esi;
  SAMPLE_CALL(0x10005d90u, 4, 0x100060bau); /* 100060b5 */
  eax = 0x1u;
  ebp = args[sp++];
  esp += 4u;
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
L_100060c9:;
  a = eax;
  b = 0xffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100060d4;
  a = eax;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  eax = result;
  BB_U32(esi + 0x3cu) = eax;
L_100060d4:;
  ecx = BB_U32(esi + 0x30u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x100060dcu); /* 100060d9 indirect */
  ecx = BB_U32(esi + 0x34u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100060e8;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x100060e8u); /* 100060e5 indirect */
L_100060e8:;
  eax = (eax ^ eax);
  ebp = args[sp++];
  esp += 4u;
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_voice_render_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_render_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_voice_progress_c(uint32_t *input) {
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
  a = BB_U32(ecx + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000612b;
  a = BB_U32(ecx + 0x30u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000612b;
  edx = BB_U32(ecx + 0xau);
  eax = BB_U32(esp + 0x4u);
  BB_U32(eax) = edx;
  edx = BB_U32(esp + 0xcu);
  ecx = BB_U32(ecx + 0x30u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x20u), 4, 0x10006122u); /* 1000611f indirect */
  ecx = BB_U32(esp + 0x8u);
  BB_U32(ecx) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000612b:;
  eax = BB_U32(esp + 0x4u);
  ecx = BB_U32(esp + 0x8u);
  BB_U32(eax) = 0x0u;
  BB_U32(ecx) = 0x0u;
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
bb_qmix_voice_progress_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_progress_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_vector_scale_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  edx = (esp);
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  args[sp + 1u] = BB_U32((uint32_t)(esp + 0x4u) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  eax = BB_U32(esp + 0x10u);
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  ecx = BB_U32(edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(edx + 0x4u);
  edx = BB_U32(edx + 0x8u);
  BB_U32(eax + 0x4u) = ecx;
  BB_U32(eax + 0x8u) = edx;
  a = esp;
  b = 0xcu;
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
bb_qmix_vector_scale_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused)),
                           uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_vector_scale_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_vector_subtract_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x8u);
  ecx = BB_U32(esp + 0xcu);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(ecx);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(ecx + 0x4u);
  edx = (esp);
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  args[sp + 1u] = BB_U32((uint32_t)(esp + 0x4u) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(ecx + 0x8u);
  eax = BB_U32(esp + 0x10u);
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  ecx = BB_U32(edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(edx + 0x4u);
  edx = BB_U32(edx + 0x8u);
  BB_U32(eax + 0x4u) = ecx;
  BB_U32(eax + 0x8u) = edx;
  a = esp;
  b = 0xcu;
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
bb_qmix_vector_subtract_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_vector_subtract_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_vector_add_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x8u);
  ecx = BB_U32(esp + 0xcu);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(ecx);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(ecx + 0x4u);
  edx = (esp);
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  args[sp + 1u] = BB_U32((uint32_t)(esp + 0x4u) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(ecx + 0x8u);
  eax = BB_U32(esp + 0x10u);
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  ecx = BB_U32(edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(edx + 0x4u);
  edx = BB_U32(edx + 0x8u);
  BB_U32(eax + 0x4u) = ecx;
  BB_U32(eax + 0x8u) = edx;
  a = esp;
  b = 0xcu;
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
bb_qmix_vector_add_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused)),
                         uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_vector_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_vector_dot_c(uint32_t *input) {
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
  ecx = BB_U32(esp + 0x8u);
  eax = BB_U32(esp + 0x4u);
  fp[depth] = F32(ecx + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax + 0x4u);
  fp[depth] = F32(ecx + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax + 0x8u);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth] = F32(ecx);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
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
bb_qmix_vector_dot_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -512(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;movl "
      "%eax,36(%esp);pushl %esp;call " BB_ASM(
          bb_qmix_vector_dot_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;fldt -508(%esp);ret $0");
}
#endif
void bb_qmix_vector_cross_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x8u);
  ecx = BB_U32(esp + 0xcu);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  fp[depth] = F32(eax + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(ecx + 0x8u);
  fp[depth] = F32(ecx + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax + 0x8u);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  fp[depth] = F32(eax + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(ecx);
  fp[depth] = F32(ecx + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  args[sp + 1u] = BB_U32((uint32_t)(esp + 0x4u) + 0u);
  --depth;
  fp[depth] = F32(ecx + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax);
  fp[depth] = F32(eax + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(ecx);
  edx = (esp);
  eax = BB_U32(esp + 0x10u);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  ecx = BB_U32(edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(edx + 0x4u);
  edx = BB_U32(edx + 0x8u);
  BB_U32(eax + 0x4u) = ecx;
  BB_U32(eax + 0x8u) = edx;
  a = esp;
  b = 0xcu;
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
bb_qmix_vector_cross_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused)),
                           uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_vector_cross_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_vector_length_c(uint32_t *input) {
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
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10006240u, 0, 0x100062cbu);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = qmix_unary(&fp[depth - 1], 0);
  a = esp;
  b = 0x8u;
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
bb_qmix_vector_length_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -512(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;movl "
      "%eax,36(%esp);pushl %esp;call " BB_ASM(
          bb_qmix_vector_length_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;fldt -508(%esp);ret $0");
}
#endif
