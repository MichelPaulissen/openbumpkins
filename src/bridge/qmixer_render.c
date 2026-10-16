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
/* C forms of QMIXER session, device and mixer rendering.
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
static uint32_t __attribute__((unused)) qmix_integer(const long double *x) {
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
void bb_qmix_session_options_c(uint32_t *input) {
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
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x10005550u, 8, 0x10002b72u); /* 10002b6d */
  ecx = BB_U32(esi + 0x70u);
  edx = BB_U32(esi + 0x18u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x10002b7eu); /* 10002b7b indirect */
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
bb_qmix_session_options_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_options_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_session_render_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10002c20u, 0, 0x10002b9du); /* 10002b98 */
  a = BB_U32(esp + 0x18u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10002bf2;
L_10002ba3:;
  ebx = BB_U32(esi + 0x78u);
  a = ebx;
  b = BB_U32(esp + 0x18u);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10002bb0;
  ebx = BB_U32(esp + 0x18u);
L_10002bb0:;
  ebp = BB_U32(esi + 0x24u);
  a = BB_U32(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10002bd3;
L_10002bb9:;
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(ebp);
  SAMPLE_CALL(0x100039b0u, 4, 0x10002bc2u); /* 10002bbd */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  ebp = (ebp + 0x4u);
  eax = (eax + 1u);
  edi = (edi + eax);
  a = BB_U32(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10002bb9;
L_10002bd3:;
  ecx = BB_U32(esi + 0x70u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(esp + 0x18u);
  ebp = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebp + 0x14u), 8, 0x10002be1u); /* 10002bde indirect */
  ecx = BB_U32(esp + 0x14u);
  a = BB_U32(esp + 0x18u);
  b = ebx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esp + 0x18u) = result;
  args[sp + 6u] = BB_U32(esp + 0x18u);
  eax = (ecx + ebx * 4u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 0x14u);
  if (!(flags & 0x40u))
    goto L_10002ba3;
L_10002bf2:;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10002c0b;
  ecx = BB_U32(esi + 0x70u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x10002bfeu); /* 10002bfb indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10002c0b;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
L_10002c0b:;
  eax = 0x1u;
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
bb_qmix_session_render_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_render_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_listener_invalidate_c(uint32_t *input) {
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
  a = BB_U32(ecx + 0x84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = esi;
  esp -= 4u;
  if (flags & 0x40u)
    goto L_10002c4e;
  eax = BB_U32(ecx + 0x24u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10002c44;
  edx = 0x1u;
L_10002c37:;
  esi = BB_U32(eax);
  eax = (eax + 0x4u);
  BB_U32(esi + 0x6cu) = edx;
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10002c37;
L_10002c44:;
  BB_U32(ecx + 0x84u) = 0x0u;
L_10002c4e:;
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
uint32_t __attribute__((naked)) bb_qmix_listener_invalidate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_invalidate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_listener_position_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  esi = (ecx + 0x88u);
  eax = BB_U32(edx);
  BB_U32(esi) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(esi + 0x4u) = eax;
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x84u) = 0x1u;
  BB_U32(esi + 0x8u) = edx;
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
bb_qmix_listener_position_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_orientation_c(uint32_t *input) {
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
  b = 0x18u;
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
  edi = BB_U32(esp + 0x28u);
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x100062c0u, 0, 0x10002c92u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(0x1001a0b0u) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (esp + 0x18u);
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
  SAMPLE_CALL(0x10006150u, 0, 0x10002cadu); /* 10002ca8 */
  edx = (esp + 0x18u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  ebx = BB_U32(eax + 0x4u);
  eax = BB_U32(eax + 0x8u);
  edi = (esi + 0xa0u);
  BB_U32(edx) = ecx;
  ecx = edx;
  BB_U32(edx + 0x4u) = ebx;
  BB_U32(edx + 0x8u) = eax;
  eax = BB_U32(ecx);
  edx = BB_U32(ecx + 0x4u);
  ebx = BB_U32(esp + 0x2cu);
  ecx = BB_U32(ecx + 0x8u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(edi) = eax;
  BB_U32(edi + 0x4u) = edx;
  BB_U32(edi + 0x8u) = ecx;
  SAMPLE_CALL(0x100062c0u, 0, 0x10002ce6u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  fp[depth - 1] = F32(0x1001a0b0u) / fp[depth - 1];
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = (esp + 0x18u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  F32(esp + 0x8u) = (float)(fp[depth - 1]);
  args[sp + 2u] = BB_U32((uint32_t)(esp + 0x8u) + 0u);
  --depth;
  SAMPLE_CALL(0x10006150u, 0, 0x10002d01u); /* 10002cfc */
  edx = (esp + 0x18u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  ebx = BB_U32(eax + 0x4u);
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx) = ecx;
  ecx = edx;
  BB_U32(edx + 0x4u) = ebx;
  ebx = (esi + 0xacu);
  BB_U32(edx + 0x8u) = eax;
  eax = BB_U32(ecx);
  edx = BB_U32(ecx + 0x4u);
  ecx = BB_U32(ecx + 0x8u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(ebx) = eax;
  BB_U32(ebx + 0x4u) = edx;
  BB_U32(ebx + 0x8u) = ecx;
  args[--sp] = edi;
  esp -= 4u;
  eax = (esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10006260u, 0, 0x10002d3cu); /* 10002d37 */
  edx = (esp + 0x18u);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x4u) = ecx;
  ecx = (esi + 0x94u);
  BB_U32(edx + 0x8u) = eax;
  eax = BB_U32(edx);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(ecx) = eax;
  eax = BB_U32(edx + 0x4u);
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx + 0x8u) = edx;
  ecx = (esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x10006260u, 0, 0x10002d75u); /* 10002d70 */
  edx = (esp + 0x18u);
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
  eax = BB_U32(ecx);
  edx = BB_U32(ecx + 0x4u);
  edi = args[sp++];
  esp += 4u;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(ebx) = eax;
  BB_U32(esi + 0x84u) = 0x1u;
  esi = args[sp++];
  esp += 4u;
  BB_U32(ebx + 0x4u) = edx;
  BB_U32(ebx + 0x8u) = ecx;
  ebx = args[sp++];
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
bb_qmix_listener_orientation_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_orientation_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_listener_velocity_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  esi = (ecx + 0xb8u);
  eax = BB_U32(edx);
  BB_U32(esi) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(esi + 0x4u) = eax;
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x84u) = 0x1u;
  BB_U32(esi + 0x8u) = edx;
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
bb_qmix_listener_velocity_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_velocity_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_default_c(uint32_t *input) {
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
bb_qmix_listener_default_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_default_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_speed_c(uint32_t *input) {
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
  BB_U32(ecx + 0x84u) = 0x1u;
  eax = BB_U32(esp + 0x4u);
  BB_U32(ecx + 0xc4u) = eax;
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
bb_qmix_listener_speed_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_speed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_gain_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ecx + 0xc8u) = eax;
  esi = ecx;
  ecx = BB_U32(esi + 0x70u);
  args[--sp] = eax;
  esp -= 4u;
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0xcu), 4, 0x10002e3au); /* 10002e37 indirect */
  ebx = (ebx ^ ebx);
  a = BB_U32(esi + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10002e56;
L_10002e41:;
  eax = BB_U32(esi + 0x20u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ebx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  ecx = BB_U32(eax + edi + 0xfffffffcu);
  SAMPLE_CALL(0x10003650u, 0, 0x10002e51u); /* 10002e4c */
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10002e41;
L_10002e56:;
  BB_U32(esi + 0x84u) = 0x1u;
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
bb_qmix_listener_gain_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_gain_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_c(uint32_t *input) {
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
uint32_t __attribute__((naked)) bb_qmix_session_default_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_listener_position_get_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x4u);
  eax = BB_U32(ecx + 0x88u);
  a = ecx;
  b = 0x88u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(edx) = eax;
  eax = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = eax;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = ecx;
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
bb_qmix_listener_position_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_position_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_orientation_get_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  edx = (ecx + 0xa0u);
  esi = BB_U32(esp + 0x8u);
  eax = BB_U32(edx);
  a = ecx;
  b = 0xacu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(esi) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(esi + 0x4u) = eax;
  edx = BB_U32(edx + 0x8u);
  BB_U32(esi + 0x8u) = edx;
  edx = BB_U32(esp + 0xcu);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  esi = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = esi;
  esi = args[sp++];
  esp += 4u;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = ecx;
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
bb_qmix_listener_orientation_get_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_orientation_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_listener_velocity_get_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x4u);
  eax = BB_U32(ecx + 0xb8u);
  a = ecx;
  b = 0xb8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(edx) = eax;
  eax = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = eax;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = ecx;
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
bb_qmix_listener_velocity_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_velocity_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_speed_get_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x4u);
  eax = BB_U32(ecx + 0xc4u);
  BB_U32(edx) = eax;
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
bb_qmix_listener_speed_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_speed_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_listener_gain_get_c(uint32_t *input) {
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
  fp[depth] = F32(ecx + 0xc8u);
  ++depth;
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
double __attribute__((naked)) bb_qmix_listener_gain_get_entry(void) {
  __asm__ volatile(
      "leal -512(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;movl "
      "%eax,36(%esp);pushl %esp;call " BB_ASM(
          bb_qmix_listener_gain_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;fldt -508(%esp);ret $0");
}
#endif
void bb_qmix_listener_transform_c(uint32_t *input) {
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
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = (ecx + 0x94u);
  edi = ecx;
  esi = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10006240u, 0, 0x10002f58u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  F32(esp + 0x10u) = (float)(fp[depth - 1]);
  args[sp + 4u] = BB_U32((uint32_t)(esp + 0x10u) + 0u);
  --depth;
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  ecx = (edi + 0xa0u);
  a = edi;
  b = 0xacu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10006240u, 0, 0x10002f72u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  F32(esp + 0x14u) = (float)(fp[depth - 1]);
  args[sp + 5u] = BB_U32((uint32_t)(esp + 0x14u) + 0u);
  --depth;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10006240u, 0, 0x10002f80u);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  F32(esp + 0x18u) = (float)(fp[depth - 1]);
  args[sp + 6u] = BB_U32((uint32_t)(esp + 0x18u) + 0u);
  --depth;
  edx = (esp + 0x10u);
  eax = BB_U32(esp + 0x20u);
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(edx);
  esi = BB_U32(edx + 0x4u);
  BB_U32(eax) = ecx;
  edx = BB_U32(edx + 0x8u);
  edi = args[sp++];
  esp += 4u;
  BB_U32(eax + 0x4u) = esi;
  esi = args[sp++];
  esp += 4u;
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
bb_qmix_listener_transform_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_listener_transform_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_timing_begin_c(uint32_t *input) {
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
  eax = (ecx + 0xd8u);
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x10002fc0u); /* 10002fba indirect */
  BB_U32(esi + 0xccu) = 0x1u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(esi + 0xd0u) = eax;
  BB_U32(esi + 0xe0u) = eax;
  BB_U32(esi + 0xe4u) = eax;
  BB_U32(esi + 0xe8u) = eax;
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
uint32_t __attribute__((naked)) bb_qmix_timing_begin_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_timing_begin_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_timing_end_c(uint32_t *input) {
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
  eax = (esp);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(ecx + 0xccu) = 0x0u;
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000300bu); /* 10003005 indirect */
  ecx = BB_U32(esp + 0x4u);
  eax = BB_U32(esp + 0x10u);
  ecx = (ecx - BB_U32(esi + 0xd8u));
  esi = args[sp++];
  esp += 4u;
  BB_U32(eax + 0x10u) = ecx;
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
bb_qmix_timing_end_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_timing_end_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_option_bits_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  esi = edx;
  edx = (edx & BB_U32(esp + 0x8u));
  esi = ~(esi);
  esi = (esi & BB_U32(ecx + 0x18u));
  a = esi;
  b = edx;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  BB_U32(ecx + 0x18u) = esi;
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
bb_qmix_session_option_bits_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_option_bits_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_device_invalidate_c(uint32_t *input) {
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
  BB_U32(ecx + 0x4cu) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_device_invalidate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_invalidate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_device_mode_c(uint32_t *input) {
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
  BB_U32(ecx + 0x2cu) = eax;
  esi = ecx;
  SAMPLE_CALL(0x1000af70u, 0, 0x1000af3fu); /* 1000af3a */
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000af46u); /* 1000af43 indirect */
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
bb_qmix_device_mode_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_mode_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_flags_c(uint32_t *input) {
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
  BB_U32(ecx + 0x30u) = eax;
  esi = ecx;
  SAMPLE_CALL(0x1000af70u, 0, 0x1000af5fu); /* 1000af5a */
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000af66u); /* 1000af63 indirect */
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
bb_qmix_device_flags_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_flags_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_pan_table_c(uint32_t *input) {
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
  a = BYTE(ecx + 0x2cu);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000af7e;
  BB_U32(ecx + 0x28u) = 0x1001eac8u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000af7e:;
  a = BYTE(ecx + 0x30u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000af8c;
  BB_U32(ecx + 0x28u) = 0x1001e6f8u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000af8c:;
  BB_U32(ecx + 0x28u) = 0x1001e328u;
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
uint32_t __attribute__((naked)) bb_qmix_device_pan_table_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_pan_table_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_device_volume_c(uint32_t *input) {
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
  BB_U32(ecx + 0x34u) = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000afacu); /* 1000afa9 indirect */
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
bb_qmix_device_volume_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_volume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_position_c(uint32_t *input) {
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
  edx = (uint32_t)((int32_t)eax >> 31);
  esi = 0x168u;
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
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000afc3;
  a = edx;
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edx = result;
L_1000afc3:;
  BB_U32(ecx + 0x38u) = edx;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000afcbu); /* 1000afc8 indirect */
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
bb_qmix_device_position_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_rate_c(uint32_t *input) {
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
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000afe0;
  eax = 0x7fffu;
L_1000afe0:;
  BB_U32(ecx + 0x3cu) = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000afe8u); /* 1000afe5 indirect */
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
bb_qmix_device_rate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_rate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_distance_c(uint32_t *input) {
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
  BB_U32(ecx + 0x40u) = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000affcu); /* 1000aff9 indirect */
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
bb_qmix_device_distance_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_distance_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_refresh_c(uint32_t *input) {
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
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x20u), 0, 0x1000b005u); /* 1000b002 indirect */
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
bb_qmix_device_refresh_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_refresh_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_transition_c(uint32_t *input) {
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
  BB_U32(ecx + 0x48u) = eax;
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
bb_qmix_device_transition_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_transition_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_gains_c(uint32_t *input) {
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
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(ecx + 0x34u));
  eax = BB_U32(ecx + 0x40u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  esi = ecx;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a3c4u);
  ecx = BB_U32(esi + 0x3cu);
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000b04c;
  F32(esp + 0x14u) = (float)(fp[depth - 1]);
  args[sp + 5u] = BB_U32((uint32_t)(esp + 0x14u) + 0u);
  --depth;
  fp[depth++] = (long double)(int32_t)(BB_U32(esi + 0x40u));
  fp[depth - 1] /= (long double)(int32_t)(BB_U32(esi + 0x3cu));
  fp[depth - 1u - 0u] = fp[depth - 1u - 0u] * fp[depth - 1u - 0u];
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
L_1000b04c:;
  F32(esp + 0x14u) = (float)(fp[depth - 1]);
  args[sp + 5u] = BB_U32((uint32_t)(esp + 0x14u) + 0u);
  --depth;
  a = BB_U32(esp + 0x1cu);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b0ad;
  eax = BB_U32(esi + 0x38u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x1000ba50u, 0, 0x1000b060u); /* 1000b05b */
  eax = (eax << 4);
  ecx = BB_U32(esp + 0x20u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ebx = BB_U32(esi + 0x28u);
  ebx = (ebx + eax);
  a = BB_U32(esi + 0x50u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b07f;
  a = BB_U32(esi + 0x54u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b07f;
  ebp = BB_U32(esi + 0x48u);
  goto L_1000b081;
L_1000b07f:;
  ebp = (ebp ^ ebp);
L_1000b081:;
  BB_U32(esp + 0x10u) = 0x4u;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  edi = (esi + 0x8u);
L_1000b08c:;
  fp[depth] = F32(ebx);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  ecx = BB_U32(edi);
  args[--sp] = ebp;
  esp -= 4u;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  ebx = (ebx + 0x4u);
  F32(esp) = (float)(fp[depth - 1]);
  args[sp + 0u] = BB_U32((uint32_t)(esp) + 0u);
  --depth;
  eax = BB_U32(ecx);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  SAMPLE_CALL(BB_U32(eax), 8, 0x1000b0a5u); /* 1000b0a3 indirect */
  a = BB_U32(esp + 0x10u);
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  BB_U32(esp + 0x10u) = result;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  if (!(flags & 0x40u))
    goto L_1000b08c;
  goto L_1000b0c9;
L_1000b0ad:;
  ecx = BB_U32(esi + 0x8u);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(esp + 0x18u);
  ebx = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebx), 8, 0x1000b0bbu); /* 1000b0b9 indirect */
  ecx = BB_U32(esi + 0xcu);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(esp + 0x18u);
  ebx = BB_U32(ecx);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(ebx), 8, 0x1000b0c9u); /* 1000b0c7 indirect */
L_1000b0c9:;
  eax = BB_U32(esp + 0x1cu);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0x50u) = eax;
  eax = (eax ^ eax);
  BB_U32(esi + 0x54u) = eax;
  BB_U32(esi + 0x4cu) = eax;
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
bb_qmix_device_gains_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_gains_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_device_mono16_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  edi = ecx;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b13a;
  a = BB_U32(edi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b10a;
  a = BB_U32(edi + 0x50u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b113;
L_1000b10a:;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b020u, 4, 0x1000b113u); /* 1000b10e */
L_1000b113:;
  ebp = (edi + 0x8u);
  ebx = 0x4u;
L_1000b11b:;
  ecx = BB_U32(ebp);
  eax = BB_U32(edi + 0x58u);
  edx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  a = ebp;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebp = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 16, 0x1000b134u); /* 1000b131 indirect */
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  if (!(flags & 0x40u))
    goto L_1000b11b;
  a = BB_U32(edi + 0x58u);
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(edi + 0x58u) = result;
L_1000b13a:;
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
bb_qmix_device_mono16_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_device_dual16_c(uint32_t *input) {
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
  esi = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b1c5;
  a = BB_U32(edi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b169;
  a = BB_U32(edi + 0x50u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b172;
L_1000b169:;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b020u, 4, 0x1000b172u); /* 1000b16d */
L_1000b172:;
  ecx = BB_U32(edi + 0x8u);
  eax = BB_U32(edi + 0x58u);
  ebx = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(edi + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 16, 0x1000b188u); /* 1000b185 indirect */
  ecx = BB_U32(edi + 0xcu);
  edx = BB_U32(edi + 0x58u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(edi + 0x1cu);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0x8u), 16, 0x1000b19au); /* 1000b197 indirect */
  ecx = BB_U32(edi + 0x10u);
  eax = BB_U32(edi + 0x58u);
  ebx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(edi + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 16, 0x1000b1b0u); /* 1000b1ad indirect */
  ecx = BB_U32(edi + 0x14u);
  edx = BB_U32(edi + 0x58u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(edi + 0x1cu);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0x8u), 16, 0x1000b1c2u); /* 1000b1bf indirect */
  a = BB_U32(edi + 0x58u);
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(edi + 0x58u) = result;
L_1000b1c5:;
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
bb_qmix_device_dual16_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_dual16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_device_stereo16_c(uint32_t *input) {
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
  edi = ecx;
  esi = BB_U32(esp + 0x14u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b220;
  a = BB_U32(edi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b1e8;
  a = BB_U32(edi + 0x50u);
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b1f1;
L_1000b1e8:;
  args[--sp] = 0x2u;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b020u, 4, 0x1000b1f1u); /* 1000b1ec */
L_1000b1f1:;
  ecx = BB_U32(edi + 0x8u);
  eax = BB_U32(edi + 0x58u);
  edx = BB_U32(esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x18u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 16, 0x1000b207u); /* 1000b204 indirect */
  ecx = BB_U32(edi + 0xcu);
  edx = BB_U32(edi + 0x58u);
  eax = BB_U32(esp + 0x10u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(edi + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 16, 0x1000b21du); /* 1000b21a indirect */
  a = BB_U32(edi + 0x58u);
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(edi + 0x58u) = result;
L_1000b220:;
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
bb_qmix_device_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_stereo16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_device_mono8_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  edi = ecx;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b27a;
  a = BB_U32(edi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b24a;
  a = BB_U32(edi + 0x50u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b253;
L_1000b24a:;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b020u, 4, 0x1000b253u); /* 1000b24e */
L_1000b253:;
  ebp = (edi + 0x8u);
  ebx = 0x4u;
L_1000b25b:;
  ecx = BB_U32(ebp);
  eax = BB_U32(edi + 0x58u);
  edx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  a = ebp;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebp = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 16, 0x1000b274u); /* 1000b271 indirect */
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  if (!(flags & 0x40u))
    goto L_1000b25b;
  a = BB_U32(edi + 0x58u);
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(edi + 0x58u) = result;
L_1000b27a:;
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
bb_qmix_device_mono8_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_device_dual8_c(uint32_t *input) {
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
  esi = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b305;
  a = BB_U32(edi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b2a9;
  a = BB_U32(edi + 0x50u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b2b2;
L_1000b2a9:;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b020u, 4, 0x1000b2b2u); /* 1000b2ad */
L_1000b2b2:;
  ecx = BB_U32(edi + 0x8u);
  eax = BB_U32(edi + 0x58u);
  ebx = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(edi + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x4u), 16, 0x1000b2c8u); /* 1000b2c5 indirect */
  ecx = BB_U32(edi + 0xcu);
  edx = BB_U32(edi + 0x58u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(edi + 0x1cu);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0x4u), 16, 0x1000b2dau); /* 1000b2d7 indirect */
  ecx = BB_U32(edi + 0x10u);
  eax = BB_U32(edi + 0x58u);
  ebx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(edi + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x4u), 16, 0x1000b2f0u); /* 1000b2ed indirect */
  ecx = BB_U32(edi + 0x14u);
  edx = BB_U32(edi + 0x58u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(edi + 0x1cu);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0x4u), 16, 0x1000b302u); /* 1000b2ff indirect */
  a = BB_U32(edi + 0x58u);
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(edi + 0x58u) = result;
L_1000b305:;
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
bb_qmix_device_dual8_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused)),
                           uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_dual8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_device_stereo8_c(uint32_t *input) {
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
  edi = ecx;
  esi = BB_U32(esp + 0x14u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b360;
  a = BB_U32(edi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b328;
  a = BB_U32(edi + 0x50u);
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b331;
L_1000b328:;
  args[--sp] = 0x2u;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b020u, 4, 0x1000b331u); /* 1000b32c */
L_1000b331:;
  ecx = BB_U32(edi + 0x8u);
  eax = BB_U32(edi + 0x58u);
  edx = BB_U32(esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x18u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 16, 0x1000b347u); /* 1000b344 indirect */
  ecx = BB_U32(edi + 0xcu);
  edx = BB_U32(edi + 0x58u);
  eax = BB_U32(esp + 0x10u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(edi + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 16, 0x1000b35du); /* 1000b35a indirect */
  a = BB_U32(edi + 0x58u);
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(edi + 0x58u) = result;
L_1000b360:;
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
bb_qmix_device_stereo8_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_stereo8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_device_flush_c(uint32_t *input) {
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
  a = BB_U32(ecx + 0x58u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b37e;
  BB_U32(ecx + 0x54u) = 0x1u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000b37e:;
  BB_U32(ecx + 0x58u) = 0x0u;
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
uint32_t __attribute__((naked)) bb_qmix_device_flush_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_device_flush_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mixer_default_c(uint32_t *input) {
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
bb_qmix_mixer_default_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_default_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_mixer_noop_c(uint32_t *input) {
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
uint32_t __attribute__((naked)) bb_qmix_mixer_noop_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_noop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mixer_base_delete_c(uint32_t *input) {
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
  a = BYTE(esp + 0x4u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_mixer_base);
  esi = ecx;
  if (flags & 0x40u)
    goto L_1000b469;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10001070u, 0, 0x1000b466u); /* 1000b461 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000b469:;
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
uint32_t __attribute__((naked))
bb_qmix_mixer_base_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_base_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_mixer_delete_c(uint32_t *input) {
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
  esi = ecx;
  SAMPLE_CALL(0x1000b490u, 0, 0x1000b478u); /* 1000b473 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000b488;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10001070u, 0, 0x1000b485u); /* 1000b480 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000b488:;
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
uint32_t __attribute__((naked))
bb_qmix_mixer_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_mixer_configure_c(uint32_t *input) {
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
  eax = 0x1001f020u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = (edi ^ edi);
  ecx = BB_U32(esp + 0x18u);
L_1000b554:;
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000b563;
  eax = (eax + 0x4u);
  edi = (edi + 1u);
  a = eax;
  b = 0x1001f02cu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_1000b554;
L_1000b563:;
  a = edi;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000b574;
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
L_1000b574:;
  eax = edi;
  ebx = (esi + 0x8u);
  eax = (eax << 4);
  BB_U32(esp + 0x10u) = 0x4u;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  ebp = (eax + 0x1001ef78u);
L_1000b58a:;
  ecx = BB_U32(esi + 0x28u);
  eax = BB_U32(esp + 0x1cu);
  eax = (eax + 0x2u);
  ebp = (ebp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x1000b59fu); /* 1000b59d indirect */
  ecx = eax;
  BB_U32(ebx + 0xfffffffcu) = eax;
  edx = BB_U32(ebp + 0xfffffffcu);
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x1000b5adu); /* 1000b5aa indirect */
  a = BB_U32(esp + 0x10u);
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  BB_U32(esp + 0x10u) = result;
  args[sp + 4u] = BB_U32(esp + 0x10u);
  if (!(flags & 0x40u))
    goto L_1000b58a;
  ebx = (esi + 0x18u);
  eax = (edi + edi * 2u);
  edi = 0x2u;
  ebp = (eax * 8u);
L_1000b5c5:;
  eax = (ebp + 0x1001ef30u);
  ecx = BB_U32(esi + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  ebx = (ebx + 0x4u);
  eax = BB_U32(ecx);
  a = ebp;
  b = 0xcu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebp = result;
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x1000b5dau); /* 1000b5d7 indirect */
  edx = (ebp + 0x1001efccu);
  BB_U32(ebx + 0xfffffffcu) = eax;
  ecx = BB_U32(esi + 0x28u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 4, 0x1000b5ecu); /* 1000b5e9 indirect */
  BB_U32(ebx + 0x4u) = eax;
  ecx = BB_U32(ebx + 0xfffffffcu);
  edx = BB_U32(ebx + 0xfffffff4u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x1000b5fbu); /* 1000b5f8 indirect */
  a = edi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edi = result;
  if (!(flags & 0x40u))
    goto L_1000b5c5;
  args[--sp] = 0x3u;
  esp -= 4u;
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x1000b607u); /* 1000b604 indirect */
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
bb_qmix_mixer_configure_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_configure_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_mixer_flags_c(uint32_t *input) {
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
  edi = ecx;
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = 0x1u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b633;
  esi = (esi | 0x2u);
L_1000b633:;
  eax = BB_U32(edi + 0x2cu);
  eax = (eax ^ esi);
  a = ((uint8_t)(eax));
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000b66d;
  ecx = BB_U32(edi + 0x20u);
  eax = BB_U32(edi + 0x10u);
  a = esi;
  b = 0x2u;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  if (flags & 0x40u)
    goto L_1000b65e;
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x1000b650u); /* 1000b64d indirect */
  ecx = BB_U32(edi + 0x24u);
  edx = BB_U32(edi + 0x14u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 4, 0x1000b65cu); /* 1000b659 indirect */
  goto L_1000b66d;
L_1000b65e:;
  SAMPLE_CALL(BB_U32(eax + 0x14u), 4, 0x1000b661u); /* 1000b65e indirect */
  ecx = BB_U32(edi + 0x24u);
  edx = BB_U32(edi + 0x14u);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x14u), 4, 0x1000b66du); /* 1000b66a indirect */
L_1000b66d:;
  BB_U32(edi + 0x2cu) = esi;
  edi = BB_U32(edi + 0x4u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b686;
L_1000b677:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000af50u, 4, 0x1000b67fu); /* 1000b67a */
  edi = BB_U32(edi + 0x4u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000b677;
L_1000b686:;
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
bb_qmix_mixer_flags_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_flags_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_mixer_output_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  esi = BB_U32(esp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x1000b780u, 4, 0x1000b72eu); /* 1000b729 */
  a = BYTE(edi + 0x2cu);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000b73d;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x8u);
  goto L_1000b744;
L_1000b73d:;
  ecx = BB_U32(edi + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0xcu);
L_1000b744:;
  edx = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x10u), 12, 0x1000b74fu); /* 1000b74c indirect */
  args[--sp] = esi;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000b7e0u, 4, 0x1000b757u); /* 1000b752 */
  esi = BB_U32(edi + 0x4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b76c;
L_1000b75e:;
  ecx = esi;
  SAMPLE_CALL(0x1000b370u, 0, 0x1000b765u); /* 1000b760 */
  esi = BB_U32(esi + 0x4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000b75e;
L_1000b76c:;
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
bb_qmix_mixer_output_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_output_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_mixer_effects_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x10u);
  esi = ecx;
  edi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esi + 0x18u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 8, 0x1000b795u); /* 1000b792 indirect */
  ecx = BB_U32(esi + 0x1cu);
  args[--sp] = edi;
  esp -= 4u;
  edx = BB_U32(esi + 0x14u);
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0xcu), 8, 0x1000b7a2u); /* 1000b79f indirect */
  a = BYTE(esi + 0x2cu);
  b = 0x3u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000b7c2;
  ecx = BB_U32(esi + 0x20u);
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 8, 0x1000b7b5u); /* 1000b7b2 indirect */
  ecx = BB_U32(esi + 0x24u);
  args[--sp] = edi;
  esp -= 4u;
  edx = BB_U32(esi + 0x14u);
  eax = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(eax + 0xcu), 8, 0x1000b7c2u); /* 1000b7bf indirect */
L_1000b7c2:;
  ecx = BB_U32(esi + 0x8u);
  eax = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0xcu), 4, 0x1000b7ceu); /* 1000b7cb indirect */
  ecx = BB_U32(esi + 0xcu);
  eax = BB_U32(esi + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0xcu), 4, 0x1000b7dau); /* 1000b7d7 indirect */
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
bb_qmix_mixer_effects_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_effects_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_mixer_flush_c(uint32_t *input) {
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
  ebx = (ecx + 0x8u);
  edi = 0x4u;
  esi = BB_U32(esp + 0x10u);
L_1000b7ef:;
  ecx = BB_U32(ebx);
  args[--sp] = esi;
  esp -= 4u;
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x14u), 4, 0x1000b7fau); /* 1000b7f7 indirect */
  a = edi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edi = result;
  if (!(flags & 0x40u))
    goto L_1000b7ef;
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
bb_qmix_mixer_flush_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_flush_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_mixer_silent_c(uint32_t *input) {
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
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  ebx = (ecx + 0x8u);
  edi = ecx;
L_1000b81a:;
  ecx = BB_U32(ebx);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x1000b821u); /* 1000b81e indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b84f;
  ebx = (ebx + 0x4u);
  esi = (esi + 1u);
  a = esi;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000b81a;
  ecx = BB_U32(edi + 0x18u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x1000b836u); /* 1000b833 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b855;
  ecx = BB_U32(edi + 0x1cu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x1000b842u); /* 1000b83f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000b855;
  eax = 0x1u;
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
L_1000b84f:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
L_1000b855:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_mixer_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mixer_clear_c(uint32_t *input) {
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
  esi = 0x4u;
  ebx = (ecx + 0x8u);
  edi = ecx;
L_1000b86d:;
  ecx = BB_U32(ebx);
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 0, 0x1000b877u); /* 1000b874 indirect */
  a = esi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  esi = result;
  if (!(flags & 0x40u))
    goto L_1000b86d;
  ecx = BB_U32(edi + 0x18u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x1000b882u); /* 1000b87f indirect */
  ecx = BB_U32(edi + 0x1cu);
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0x8u), 0, 0x1000b88au); /* 1000b887 indirect */
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
uint32_t __attribute__((naked)) bb_qmix_mixer_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mixer_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_filter_base_delete_c(uint32_t *input) {
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
  a = BYTE(esp + 0x4u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_base);
  esi = ecx;
  if (flags & 0x40u)
    goto L_1000e1f9;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10001070u, 0, 0x1000e1f6u); /* 1000e1f1 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000e1f9:;
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
uint32_t __attribute__((naked))
bb_qmix_filter_base_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_base_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_filter_default_c(uint32_t *input) {
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
bb_qmix_filter_default_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_default_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_filter_process_c(uint32_t *input) {
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
  edi = ecx;
  eax = BB_U32(eax + 0x10u);
  esi = BB_U32(esp + 0x18u);
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000e23c;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000e232;
  eax = BB_U32(edi);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x1000e22eu); /* 1000e22b indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000e28d;
L_1000e232:;
  ecx = BB_U32(esp + 0x14u);
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x1000c030u, 4, 0x1000e23cu); /* 1000e237 */
L_1000e23c:;
  a = BB_U32(edi + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000e257;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x4u);
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ecx + 0x4u);
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000c550u, 0, 0x1000e254u); /* 1000e24f */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_1000e257:;
  ebp = (ebp ^ ebp);
  a = BB_U32(edi + 0x8u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000e286;
  ebx = (ebx ^ ebx);
L_1000e260:;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x10u);
  eax = (eax + ebx);
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  ebp = (ebp + 1u);
  eax = BB_U32(edi + 0xcu);
  edx = BB_U32(ecx + 0x4u);
  eax = (eax + ebx);
  a = ebx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000e350u, 0, 0x1000e27eu); /* 1000e279 */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(edi + 0x8u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000e260;
L_1000e286:;
  BB_U32(edi + 0x14u) = 0x0u;
L_1000e28d:;
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
bb_qmix_filter_process_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_process_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_filter_noop_c(uint32_t *input) {
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
bb_qmix_filter_noop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_noop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_filter_attach_c(uint32_t *input) {
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
  fp[depth] = F32(ecx + 0x4u);
  ++depth;
  eax = BB_U32(esp + 0x4u);
  fp[depth - 1] = fp[depth - 1] * F32(eax + 0x8u);
  F32(eax + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  BB_U32(ecx + 0x18u) = 0x0u;
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
bb_qmix_filter_attach_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_attach_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_filter_detach_c(uint32_t *input) {
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
  fp[depth] = F32(eax + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] / F32(ecx + 0x4u);
  F32(eax + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  BB_U32(ecx + 0x18u) = 0x1u;
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
bb_qmix_filter_detach_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_detach_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_filter_silent_c(uint32_t *input) {
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
  a = BB_U32(ecx + 0x14u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000e300;
  eax = 0x1u;
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
L_1000e300:;
  eax = BB_U32(esi + 0x8u);
  ecx = BB_U32(esi + 0x10u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x1000c4a0u, 0, 0x1000e30fu);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  {
    long double other = F32(0x1001a564u);
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
    goto L_1000e326;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_1000e326:;
  BB_U32(esi + 0x14u) = eax;
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
uint32_t __attribute__((naked)) bb_qmix_filter_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_filter_clear_c(uint32_t *input) {
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
  edx = ecx;
  eax = (eax ^ eax);
  ecx = BB_U32(edx + 0x8u);
  ecx = (ecx << 3);
  edi = BB_U32(edx + 0x10u);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(edx + 0x14u) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_filter_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_filter_samples_c(uint32_t *input) {
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
  ebx = BB_U32(esp + 0x10u);
  fp[depth] = F32(ebx);
  ++depth;
  fp[depth] = F32(ebx + 0x4u);
  ++depth;
  ebx = BB_U32(esp + 0x14u);
  fp[depth] = F32(ebx);
  ++depth;
  fp[depth] = F32(ebx + 0x4u);
  ++depth;
  esi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  b = 0x1u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000e385;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = F32(esi) - fp[depth - 1];
  F32(esi) = (float)(fp[depth - 1]);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  esi = (esi + 0x4u);
L_1000e385:;
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 1)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 1, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000e3b8;
L_1000e38a:;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = F32(esi) - fp[depth - 1];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1] - fp[depth - 1u - 1u];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = F32(esi + 0x4u) - fp[depth - 1];
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] - fp[depth - 1];
  --depth;
  F32(esi) = (float)(fp[depth - 1]);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(esi + 0x4u) = (float)(fp[depth - 1]);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  a = esi;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  esi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000e38a;
L_1000e3b8:;
  F32(ebx + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  F32(ebx) = (float)(fp[depth - 1]);
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
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
bb_qmix_filter_samples_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused)),
                             uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_filter_samples_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $0");
}
#endif
