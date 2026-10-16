/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
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
/* C forms of QMIXER short recursive and convolution filters.
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

static uint16_t __attribute__((unused)) qmix_status(uint16_t comparison,
                                                    unsigned depth) {
  uint16_t status;
  __asm__ volatile("fnstsw %0" : "=am"(status));
  return (status & 0x2ffu) | comparison | (((8u - depth) & 7u) << 11);
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

#ifdef BB_MODEL_RUNTIME
#include "qmixer_lifetime.h"
#define exception_head bb_qmix_model_exception_head
#define exception_link bb_qmix_model_exception_link
#else
/* The working i386 path retains its registration chain; portable cleanup
 * uses the same C-owned state maps through explicit logical owner frames. */
static uint32_t exception_head(void) {
  uint32_t value;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(value));
  return value;
}
static void exception_link(uint32_t value) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(value) : "memory");
}
#endif

/* Packed sample operations use ordinary C values. Preserve the original
 * pairwise accumulation, scaling and saturation before scalar sample tails. */
static uint64_t packed_unpack(uint64_t left, uint64_t right, unsigned bits,
                              unsigned high) {
  uint64_t result = 0, mask = (1ull << bits) - 1;
  unsigned lanes = 32 / bits, start = high ? lanes : 0;
  for (unsigned i = 0; i < lanes; i++) {
    result |= ((left >> ((i + start) * bits)) & mask) << (2 * i * bits);
    result |= ((right >> ((i + start) * bits)) & mask) << ((2 * i + 1) * bits);
  }
  return result;
}
static uint64_t packed_dot_pairs(uint64_t left, uint64_t right) {
  uint64_t result = 0;
  for (unsigned i = 0; i < 2; i++) {
    int64_t sum =
        (int64_t)(int16_t)(left >> (32 * i)) * (int16_t)(right >> (32 * i));
    sum += (int64_t)(int16_t)(left >> (32 * i + 16)) *
           (int16_t)(right >> (32 * i + 16));
    result |= (uint64_t)(uint32_t)sum << (32 * i);
  }
  return result;
}
static uint64_t packed_add_dwords(uint64_t left, uint64_t right) {
  return (uint32_t)(left + right) |
         ((uint64_t)(uint32_t)((left >> 32) + (right >> 32)) << 32);
}
static uint64_t packed_shift_dwords(uint64_t value, unsigned count) {
  if (count > 31)
    count = 31;
  return (uint32_t)((int32_t)value >> count) |
         ((uint64_t)(uint32_t)((int32_t)(value >> 32) >> count) << 32);
}
static uint64_t packed_clip_dwords(uint64_t left, uint64_t right) {
  uint64_t result = 0;
  for (unsigned i = 0; i < 4; i++) {
    int32_t value = (int32_t)((i < 2 ? left : right) >> (32 * (i & 1)));
    if (value > 32767)
      value = 32767;
    else if (value < -32768)
      value = -32768;
    result |= (uint64_t)(uint16_t)value << (16 * i);
  }
  return result;
}
void bb_qmix_short_iir_create_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100100c8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x18u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010083u); /* 1001007e */
  BB_U32(ebp + 0xfffffff0u) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  esi = eax;
  eax = 0x0u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100100b0;
  ecx = BB_U32(ebp + 0x8u);
  eax = BB_U32(ecx + 0x8u);
  edx = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x100100e0u, 12, 0x100100b0u); /* 100100ab */
L_100100b0:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
bb_qmix_short_iir_create_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_iir_free_failed_c(uint32_t *input) {
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
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100100dbu); /* 100100d6 */
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
uint32_t __attribute__((naked)) bb_qmix_short_iir_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_free_failed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_constructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10010311u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x18u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffe4u) = ecx;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_base);
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_intermediate);
  edi = ecx;
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  eax = (esi * 4u);
  BB_U32(edi + 0x4u) = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(edi) = BB_QMIX_VTABLE(bb_qmix_vtable_short_iir);
  SAMPLE_CALL(0x10001020u, 0, 0x10010136u); /* 10010131 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x10u) = eax;
  eax = BB_U32(edi + 0x4u);
  eax = (eax + eax * 2u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1001014au); /* 10010145 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x8u) = eax;
  eax = BB_U32(edi + 0x4u);
  a = eax;
  result = (a << 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1001015cu); /* 10010157 */
  fp[depth] = F32(ebp + 0x8u);
  ++depth;
  fp[depth - 1] = __builtin_fabsl(fp[depth - 1]);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0xcu) = eax;
  BB_U32(ebp + 0xfffffff0u) = esi;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xfffffff0u));
  fp[depth - 1] = F32(0x1001a690u) / fp[depth - 1];
  fp[depth - 2] =
      (long double)pow((double)fp[depth - 2], (double)fp[depth - 1]);
  --depth;
  F64(ebp + 0xffffffdcu) = (double)(fp[depth - 1]);
  --depth;
  BB_U32(ebp + 0xfffffff0u) = 0x0u;
  a = BB_U32(edi + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100102e6;
  fp[depth] = F64(0x1001a6a0u);
  ++depth;
  fp[depth] = F32(0x1001a694u);
  ++depth;
  ebx = (ebx ^ ebx);
  esi = (esi ^ esi);
L_1001019c:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100101b4;
  {
    long double other = F32(ebp + 0x8u);
    fp_status = sample_compare(&fp[depth - 1], &other);
  }
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(qmix_status(fp_status, depth)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x41u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100101b4;
  fp[depth] = F64(ebp + 0xffffffdcu);
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  F64(ebp + 0xffffffe8u) = (double)(fp[depth - 1]);
  --depth;
  goto L_100101c0;
L_100101b4:;
  eax = BB_U32(ebp + 0xffffffe0u);
  ecx = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffecu) = eax;
  BB_U32(ebp + 0xffffffe8u) = ecx;
L_100101c0:;
  fp[depth] = F64(ebp + 0xffffffe8u);
  ++depth;
  eax = BB_U32(ebp + 0x10u);
  BB_U32(ebp + 0x10u) = (BB_U32(ebp + 0x10u) + 0x4u);
  fp[depth] = F32(eax);
  ++depth;
  eax = BB_U32(ebp + 0x10u);
  a = BB_U32(ebp + 0x10u);
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(ebp + 0x10u) = result;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth] = fp[depth - 1u - 2u];
  ++depth;
  fp[depth] = F64(0x1001a698u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 6u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(edi + 0x8u);
  ebx = (ebx + 0x10u);
  WORD(ecx + esi) = ((uint16_t)(eax));
  a = esi;
  b = 0x6u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  esi = result;
  fp[depth] = F64(0x1001a698u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 6u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(edi + 0x8u);
  WORD(ecx + esi + 0xfffffffcu) = ((uint16_t)(eax));
  fp[depth] = F64(0x1001a698u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 6u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6a8u);
  ecx = BB_U32(edi + 0x8u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  WORD(ecx + esi + 0xfffffffeu) = ((uint16_t)(eax));
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebp + 0xffffffecu) = ((uint16_t)(eax));
  edx = BB_U32(edi + 0xcu);
  WORD(edx + ebx + 0xfffffff0u) = ((uint16_t)(eax));
  edx = BB_U32(edi + 0xcu);
  WORD(edx + ebx + 0xfffffff2u) = 0x0u;
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  fp[depth - 1] = -fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6a8u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(edi + 0xcu);
  WORD(ecx + ebx + 0xfffffff4u) = ((uint16_t)(eax));
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD(ebp + 0xffffffecu)) & 0xffffu) << 0);
  edx = BB_U32(edi + 0xcu);
  WORD(edx + ebx + 0xfffffff6u) = ((uint16_t)(eax));
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6a8u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(edi + 0xcu);
  WORD(ecx + ebx + 0xfffffff8u) = ((uint16_t)(eax));
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6a8u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(edi + 0xcu);
  WORD(ecx + ebx + 0xfffffffau) = ((uint16_t)(eax));
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6a8u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(edi + 0xcu);
  WORD(ecx + ebx + 0xfffffffcu) = ((uint16_t)(eax));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 0u] = fp[depth - 1u - 0u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 1u] = fp[depth - 1] - fp[depth - 1u - 1u];
  --depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6a8u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 3u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  ecx = BB_U32(edi + 0xcu);
  BB_U32(ebp + 0xfffffff0u) = (BB_U32(ebp + 0xfffffff0u) + 1u);
  WORD(ecx + ebx + 0xfffffffeu) = ((uint16_t)(eax));
  eax = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edi + 0x4u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1001019c;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
L_100102e6:;
  ecx = edi;
  SAMPLE_CALL(0x100104f0u, 0, 0x100102edu); /* 100102e8 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  eax = edi;
  ecx = BB_U32(ebp + 0xfffffff4u);
  exception_link(ecx);
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
bb_qmix_short_iir_constructor_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_short_iir_unwind_base_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xffffffe4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 1001030c */
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
uint32_t __attribute__((naked)) bb_qmix_short_iir_unwind_base_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_unwind_base_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_unwind_intermediate_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xffffffe4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e0a0u, 0, BB_U32(esp - 4u)); /* tail call 1001031e */
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
bb_qmix_short_iir_unwind_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_unwind_intermediate_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_delete_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x10010350u, 0, 0x10010338u); /* 10010333 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10010348;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010345u); /* 10010340 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10010348:;
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
bb_qmix_short_iir_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_iir_destructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100103beu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_short_iir);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ecx + 0x10u);
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010384u); /* 1001037f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0x8u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010393u); /* 1001038e */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100103a2u); /* 1001039d */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x100103c8u, 0, 0x100103b1u); /* 100103ac */
  eax = BB_U32(ebp + 0xfffffff4u);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  exception_link(eax);
  ebp = args[sp++];
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
uint32_t __attribute__((naked)) bb_qmix_short_iir_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_cleanup_intermediate_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e0a0u, 0, BB_U32(esp - 4u)); /* tail call 100103cb */
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
bb_qmix_short_iir_cleanup_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_cleanup_intermediate_c) ";leal "
                                                "4(%esp),%esp;popal;popfl;leal "
                                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_process_c(uint32_t *input) {
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
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = ecx;
  esi = BB_U32(esp + 0x18u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = BB_U32(esp + 0x20u);
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10010403;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100103f7;
  eax = BB_U32(ebx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x100103efu); /* 100103ec indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100104a3;
L_100103f7:;
  eax = BB_U32(esp + 0x20u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000d690u, 4, 0x10010403u); /* 100103fe */
L_10010403:;
  eax = BB_U32(esi + 0x4u);
  ebp = (ebp ^ ebp);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esp + 0x20u);
  eax = (eax & 0xfffffffcu);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001044e;
  a = BB_U32(ebx + 0x4u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001044e;
  edi = (edi ^ edi);
  esi = (esi ^ esi);
L_10010424:;
  ecx = BB_U32(esp + 0x10u);
  eax = BB_U32(ebx + 0x10u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (eax + esi);
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx + 0xcu);
  esi = (esi + 0x4u);
  eax = (eax + edi);
  ebp = (ebp + 1u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = edi;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10010510u, 0, 0x10010446u); /* 10010441 */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(ebx + 0x4u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10010424;
L_1001044e:;
  ecx = BB_U32(esp + 0x10u);
  edi = (edi ^ edi);
  BB_U32(esp + 0x20u) = (BB_U32(esp + 0x20u) - ecx);
  args[sp + 8u] = BB_U32(esp + 32u);
  a = BB_U32(esp + 0x20u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001049c;
  eax = BB_U32(esp + 0x14u);
  esi = (esi ^ esi);
  a = BB_U32(ebx + 0x4u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  edx = (eax + ecx * 2u);
  BB_U32(esp + 0x14u) = edx;
  args[sp + 5u] = BB_U32(esp + 20u);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001049c;
  ebp = (ebp ^ ebp);
L_10010472:;
  eax = BB_U32(esp + 0x20u);
  ecx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = (esi + 1u);
  eax = BB_U32(ebx + 0x10u);
  eax = (eax + edi);
  edi = (edi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx + 0x8u);
  eax = (eax + ebp);
  a = ebp;
  b = 0x6u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebp = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100105e0u, 0, 0x10010494u); /* 1001048f */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(ebx + 0x4u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10010472;
L_1001049c:;
  BB_U32(ebx + 0x14u) = 0x0u;
L_100104a3:;
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
bb_qmix_short_iir_process_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_process_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_short_iir_silent_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  esi = ecx;
  a = BB_U32(ecx + 0x14u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100104c0;
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
L_100104c0:;
  eax = BB_U32(esi + 0x4u);
  ecx = BB_U32(esi + 0x10u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000dd20u, 0, 0x100104cfu); /* 100104ca */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = ((uint16_t)(eax));
  b = 0xau;
  flags = sub_flags(flags, a, b, 16);
  eax = 0x1u;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100104df;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_100104df:;
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
uint32_t __attribute__((naked)) bb_qmix_short_iir_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_clear_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  edx = ecx;
  eax = (eax ^ eax);
  ecx = BB_U32(edx + 0x4u);
  ecx = (ecx << 2);
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
uint32_t __attribute__((naked)) bb_qmix_short_iir_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_packed_samples_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0x14u);
  mm2 = QWORD(ebx);
  mm3 = QWORD(ebx + 0x8u);
  esi = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x1cu);
  ecx = (ecx << 1);
  edi = (edi + ecx);
  ecx = (0u - ecx);
  mm0 = (uint32_t)(BB_U32(edi + ecx));
  mm0 = packed_unpack(mm0, mm0, 32, 0);
  mm1 = (uint32_t)(BB_U32(esi));
  mm1 = packed_unpack(mm1, mm1, 32, 0);
  mm0 = packed_dot_pairs(mm0, mm2);
  mm1 = packed_dot_pairs(mm1, mm3);
  mm4 = (uint32_t)(BB_U32(edi + ecx + 0x4u));
  mm0 = packed_shift_dwords(mm0, 0xdu);
  mm1 = packed_shift_dwords(mm1, 0xdu);
  mm4 = packed_unpack(mm4, mm4, 32, 0);
  mm1 = packed_add_dwords(mm1, mm0);
  mm1 = packed_clip_dwords(mm1, mm1);
  mm4 = packed_dot_pairs(mm4, mm2);
  BB_U32(edi + ecx) = (uint32_t)(mm1);
  mm1 = packed_dot_pairs(mm1, mm3);
  mm0 = (uint32_t)(BB_U32(edi + ecx + 0x8u));
  mm4 = packed_shift_dwords(mm4, 0xdu);
  mm1 = packed_shift_dwords(mm1, 0xdu);
  mm0 = packed_unpack(mm0, mm0, 32, 0);
  mm1 = packed_add_dwords(mm1, mm4);
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_100105c2;
L_1001057c:;
  mm1 = packed_clip_dwords(mm1, mm1);
  mm0 = packed_dot_pairs(mm0, mm2);
  BB_U32(edi + ecx + 0xfffffffcu) = (uint32_t)(mm1);
  mm1 = packed_dot_pairs(mm1, mm3);
  mm4 = (uint32_t)(BB_U32(edi + ecx + 0x4u));
  mm0 = packed_shift_dwords(mm0, 0xdu);
  mm1 = packed_shift_dwords(mm1, 0xdu);
  mm4 = packed_unpack(mm4, mm4, 32, 0);
  mm1 = packed_add_dwords(mm1, mm0);
  mm1 = packed_clip_dwords(mm1, mm1);
  mm4 = packed_dot_pairs(mm4, mm2);
  BB_U32(edi + ecx) = (uint32_t)(mm1);
  mm1 = packed_dot_pairs(mm1, mm3);
  mm0 = (uint32_t)(BB_U32(edi + ecx + 0x8u));
  mm4 = packed_shift_dwords(mm4, 0xdu);
  mm1 = packed_shift_dwords(mm1, 0xdu);
  mm0 = packed_unpack(mm0, mm0, 32, 0);
  mm1 = packed_add_dwords(mm1, mm4);
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1001057c;
L_100105c2:;
  mm1 = packed_clip_dwords(mm1, mm1);
  BB_U32(edi + ecx + 0xfffffffcu) = (uint32_t)(mm1);
  BB_U32(esi) = (uint32_t)(mm1);
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
bb_qmix_short_iir_packed_samples_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused)),
                                       uint32_t arg2 __attribute__((unused)),
                                       uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_packed_samples_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_iir_scalar_samples_c(uint32_t *input) {
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
  ecx = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(ecx));
  edx = (uint32_t)(int32_t)(int16_t)(WORD(ecx + 0x2u));
  ecx = (uint32_t)(int32_t)(int16_t)(WORD(ecx + 0x4u));
  BB_U32(esp + 0x8u) = eax;
  args[sp + 2u] = BB_U32(esp + 8u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esp + 0x8u) = edx;
  args[sp + 2u] = BB_U32(esp + 8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esp + 0x8u) = ecx;
  args[sp + 2u] = BB_U32(esp + 8u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = BB_U32(esp + 0x28u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10010666;
  esi = BB_U32(esp + 0x24u);
  edi = BB_U32(esp + 0x1cu);
  ecx = BB_U32(esp + 0x28u);
  ecx = (ecx << 1);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1001061b:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(edi + ecx));
  a = eax;
  b = BB_U32(esp + 0x14u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(esi + 0x2u));
  a = ebx;
  b = BB_U32(esp + 0x10u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  ebx = result;
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esi));
  a = edx;
  b = BB_U32(esp + 0xcu);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  edx = result;
  eax = (eax - ebx);
  eax = (eax - edx);
  eax = ((uint32_t)((int32_t)(eax) >> 14));
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1001064a;
  eax = 0xffff8000u;
  goto L_10010656;
L_1001064a:;
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010656;
  eax = 0x7fffu;
L_10010656:;
  WORD(edi + ecx) = ((uint16_t)(eax));
  a = WORD(esi + 0x2u);
  b = ((uint16_t)(eax));
  WORD(esi + 0x2u) = b;
  eax = (eax & 0xffff0000u) | (((uint32_t)(a) & 0xffffu) << 0);
  a = WORD(esi);
  b = ((uint16_t)(eax));
  WORD(esi) = b;
  eax = (eax & 0xffff0000u) | (((uint32_t)(a) & 0xffffu) << 0);
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1001061b;
L_10010666:;
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
bb_qmix_short_iir_scalar_samples_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused)),
                                       uint32_t arg2 __attribute__((unused)),
                                       uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_iir_scalar_samples_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_create_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100106d8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x18u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010693u); /* 1001068e */
  BB_U32(ebp + 0xfffffff0u) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  esi = eax;
  eax = 0x0u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100106c0;
  ecx = BB_U32(ebp + 0x8u);
  eax = BB_U32(ecx + 0x8u);
  edx = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x100106f0u, 12, 0x100106c0u); /* 100106bb */
L_100106c0:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
bb_qmix_short_biquad_create_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_biquad_free_failed_c(uint32_t *input) {
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
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100106ebu); /* 100106e6 */
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
uint32_t __attribute__((naked)) bb_qmix_short_biquad_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_free_failed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_constructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001098du;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x28u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffffe4u) = ecx;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_base);
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_intermediate);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  edi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  eax = (edi * 8u);
  BB_U32(esi + 0x4u) = edi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_short_biquad);
  SAMPLE_CALL(0x10001020u, 0, 0x10010746u); /* 10010741 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x8u) = eax;
  eax = BB_U32(esi + 0x4u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  ecx = (eax + eax * 4u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1001075bu); /* 10010756 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x4u);
  BB_U32(esi + 0xcu) = eax;
  eax = (ecx + ecx * 2u);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010770u); /* 1001076b */
  fp[depth] = F32(ebp + 0x8u);
  ++depth;
  fp[depth - 1] = __builtin_fabsl(fp[depth - 1]);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x10u) = eax;
  BB_U32(ebp + 0xfffffff0u) = edi;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xfffffff0u));
  fp[depth - 1] = F32(0x1001a6c8u) / fp[depth - 1];
  fp[depth - 2] =
      (long double)pow((double)fp[depth - 2], (double)fp[depth - 1]);
  --depth;
  F64(ebp + 0xffffffccu) = (double)(fp[depth - 1]);
  --depth;
  BB_U32(ebp + 0xffffffe0u) = 0x0u;
  a = BB_U32(esi + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010962;
  fp[depth] = F64(0x1001a6d8u);
  ++depth;
  fp[depth] = F32(0x1001a6ccu);
  ++depth;
  BB_U32(ebp + 0xffffffdcu) = 0x0u;
  BB_U32(ebp + 0xfffffff0u) = 0x0u;
  edi = BB_U32(ebp + 0x10u);
L_100107bd:;
  a = BB_U32(ebp + 0xfffffff0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100107d7;
  {
    long double other = F32(ebp + 0x8u);
    fp_status = sample_compare(&fp[depth - 1], &other);
  }
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(qmix_status(fp_status, depth)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x41u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100107d7;
  fp[depth] = F64(ebp + 0xffffffccu);
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  F64(ebp + 0xffffffe8u) = (double)(fp[depth - 1]);
  --depth;
  goto L_100107e3;
L_100107d7:;
  eax = BB_U32(ebp + 0xffffffd0u);
  ecx = BB_U32(ebp + 0xffffffccu);
  BB_U32(ebp + 0xffffffecu) = eax;
  BB_U32(ebp + 0xffffffe8u) = ecx;
L_100107e3:;
  fp[depth] = F64(ebp + 0xffffffe8u);
  ++depth;
  fp[depth] = F32(edi);
  ++depth;
  fp[depth] = F32(edi + 0x4u);
  ++depth;
  fp[depth] = F32(edi + 0x8u);
  ++depth;
  edi = (edi + 0x4u);
  ebx = BB_U32(esi + 0xcu);
  edi = (edi + 0x4u);
  ebx = (ebx + BB_U32(ebp + 0xfffffff0u));
  edi = (edi + 0x4u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  fp[depth] = F32(edi + 0xfffffffcu);
  ++depth;
  fp[depth] = F64(0x1001a6d0u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 5u];
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  WORD(ebp + 0xffffffecu) = ((uint16_t)(eax));
  BB_U32(ebx) = ecx;
  fp[depth] = F64(0x1001a6d0u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  eax = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  BB_U32(ebx + 0x4u) = eax;
  fp[depth] = F64(0x1001a6d0u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  eax = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  BB_U32(ebx + 0x8u) = eax;
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 5u];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebp + 0xffffffd8u) = ((uint16_t)(eax));
  eax = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  BB_U32(ebx + 0xcu) = eax;
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 5u];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebp + 0xffffffd4u) = ((uint16_t)(eax));
  eax = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  BB_U32(ebx + 0x10u) = eax;
  ebx = BB_U32(esi + 0x10u);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD(ebp + 0xffffffecu)) & 0xffffu) << 0);
  a = ebx;
  b = BB_U32(ebp + 0xffffffdcu);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  WORD(ebx) = ((uint16_t)(eax));
  WORD(ebx + 0x2u) = 0x0u;
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] - fp[depth - 1u - 4u];
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 5u];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = (ecx & 0xffff0000u) |
        (((uint32_t)(WORD(ebp + 0xffffffd4u)) & 0xffffu) << 0);
  edx = (edx & 0xffff0000u) |
        (((uint32_t)(WORD(ebp + 0xffffffd8u)) & 0xffffu) << 0);
  WORD(ebx + 0x4u) = ((uint16_t)(eax));
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD(ebp + 0xffffffecu)) & 0xffffu) << 0);
  WORD(ebx + 0x6u) = ((uint16_t)(eax));
  WORD(ebx + 0x8u) = ((uint16_t)(ecx));
  WORD(ebx + 0xau) = ((uint16_t)(edx));
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 4u];
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 5u];
  fp[depth - 1] = -fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 7u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0xcu) = ((uint16_t)(eax));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 3u];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0xeu) = ((uint16_t)(eax));
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0x10u) = ((uint16_t)(eax));
  fp[depth] = fp[depth - 1u - 1u];
  ++depth;
  fp[depth - 1] = -fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0x12u) = ((uint16_t)(eax));
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 2u];
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 5u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0x14u) = ((uint16_t)(eax));
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 0u] = fp[depth - 1u - 0u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 1u] = fp[depth - 1] - fp[depth - 1u - 1u];
  --depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x1001a6d0u);
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 3u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  BB_U32(ebp + 0xffffffdcu) = (BB_U32(ebp + 0xffffffdcu) + 0x18u);
  WORD(ebx + 0x16u) = ((uint16_t)(eax));
  BB_U32(ebp + 0xffffffe0u) = (BB_U32(ebp + 0xffffffe0u) + 1u);
  BB_U32(ebp + 0xfffffff0u) = (BB_U32(ebp + 0xfffffff0u) + 0x14u);
  ecx = BB_U32(ebp + 0xffffffe0u);
  a = BB_U32(esi + 0x4u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_100107bd;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
L_10010962:;
  ecx = esi;
  SAMPLE_CALL(0x10010ca0u, 0, 0x10010969u); /* 10010964 */
  eax = esi;
  edi = args[sp++];
  esp += 4u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  ebx = args[sp++];
  esp += 4u;
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
bb_qmix_short_biquad_constructor_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused)),
                                       uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_short_biquad_unwind_base_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xffffffe4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 10010988 */
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
uint32_t __attribute__((naked)) bb_qmix_short_biquad_unwind_base_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_unwind_base_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_unwind_intermediate_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xffffffe4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e0a0u, 0, BB_U32(esp - 4u)); /* tail call 1001099a */
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
bb_qmix_short_biquad_unwind_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_unwind_intermediate_c) ";leal "
                                                  "4(%esp),%esp;popal;popfl;"
                                                  "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_delete_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x100109c0u, 0, 0x100109a8u); /* 100109a3 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100109b8;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100109b5u); /* 100109b0 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100109b8:;
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
bb_qmix_short_biquad_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_biquad_destructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10010a2eu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_short_biquad);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ecx + 0x8u);
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100109f4u); /* 100109ef */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010a03u); /* 100109fe */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0x10u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010a12u); /* 10010a0d */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x10010a38u, 0, 0x10010a21u); /* 10010a1c */
  eax = BB_U32(ebp + 0xfffffff4u);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  exception_link(eax);
  ebp = args[sp++];
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
uint32_t __attribute__((naked)) bb_qmix_short_biquad_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_cleanup_intermediate_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e0a0u, 0, BB_U32(esp - 4u)); /* tail call 10010a3b */
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
bb_qmix_short_biquad_cleanup_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_cleanup_intermediate_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_short_biquad_process_c(uint32_t *input) {
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
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax + 0xcu);
  ebx = BB_U32(esp + 0x14u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = (ebx - eax);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010a92;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10010a7f;
  ebp = BB_U32(esi);
  edi = BB_U32(ebp + 0x4u);
  SAMPLE_CALL(edi, 0, 0x10010a65u); /* 10010a63 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10010b39;
  ecx = esi;
  a = BB_U32(esi + 0x14u);
  b = ebx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0x14u) = result;
  SAMPLE_CALL(edi, 0, 0x10010a74u); /* 10010a72 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10010a82;
  ecx = esi;
  SAMPLE_CALL(BB_U32(ebp + 0x8u), 0, 0x10010a7du); /* 10010a7a indirect */
  goto L_10010a82;
L_10010a7f:;
  BB_U32(esi + 0x14u) = ebx;
L_10010a82:;
  eax = BB_U32(esp + 0x20u);
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000d690u, 4, 0x10010a90u); /* 10010a8b */
  goto L_10010a99;
L_10010a92:;
  BB_U32(esi + 0x14u) = 0x0u;
L_10010a99:;
  eax = BB_U32(esp + 0x20u);
  ebx = (ebx ^ ebx);
  eax = (eax & 0xfffffffeu);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010ae8;
  eax = BB_U32(esp + 0x1cu);
  a = BB_U32(esi + 0x4u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  ecx = BB_U32(eax + 0x4u);
  BB_U32(esp + 0x14u) = ecx;
  args[sp + 5u] = BB_U32(esp + 20u);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010ae8;
  edi = (edi ^ edi);
  ebp = (ebp ^ ebp);
L_10010abe:;
  ecx = BB_U32(esp + 0x10u);
  eax = BB_U32(esi + 0x8u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (eax + ebp);
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x10u);
  ebp = (ebp + 0x8u);
  eax = (eax + edi);
  ebx = (ebx + 1u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = edi;
  b = 0x18u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10010bd0u, 0, 0x10010ae0u); /* 10010adb */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esi + 0x4u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10010abe;
L_10010ae8:;
  ecx = BB_U32(esp + 0x10u);
  edi = (edi ^ edi);
  BB_U32(esp + 0x20u) = (BB_U32(esp + 0x20u) - ecx);
  args[sp + 8u] = BB_U32(esp + 32u);
  a = BB_U32(esp + 0x20u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010b39;
  eax = BB_U32(esp + 0x1cu);
  ebp = (ebp ^ ebp);
  a = BB_U32(esi + 0x4u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  edx = BB_U32(eax + 0x4u);
  eax = (edx + ecx * 2u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010b39;
  ebx = (ebx ^ ebx);
L_10010b0f:;
  eax = BB_U32(esp + 0x20u);
  ecx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = (ebp + 1u);
  eax = BB_U32(esi + 0x8u);
  eax = (eax + edi);
  edi = (edi + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0xcu);
  eax = (eax + ebx);
  a = ebx;
  b = 0x14u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10010b50u, 0, 0x10010b31u); /* 10010b2c */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esi + 0x4u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10010b0f;
L_10010b39:;
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
bb_qmix_short_biquad_process_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_process_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_short_biquad_scalar_samples_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x18u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = esi;
  esi = (esi - 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10010bc4;
  edi = BB_U32(esp + 0x1cu);
  edx = BB_U32(esp + 0x18u);
  ecx = BB_U32(esp + 0x14u);
L_10010b6b:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x2u)) & 0xffffu) << 0);
  ecx = (ecx + 0x2u);
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(edi));
  a = ebx;
  b = BB_U32(edx + 0x10u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  ebx = result;
  ebp = (uint32_t)(int32_t)(int16_t)(WORD(edi + 0x4u));
  a = ebp;
  b = BB_U32(edx + 0x8u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  ebp = result;
  ebx = (ebx - ebp);
  ebp = (uint32_t)(int32_t)(int16_t)(WORD(edi + 0x6u));
  a = ebp;
  b = BB_U32(edx + 0x4u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  ebp = result;
  ebx = (ebx - ebp);
  ebp = (uint32_t)(int32_t)(int16_t)(WORD(ecx + 0xfffffffeu));
  a = ebp;
  b = BB_U32(edx);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  ebp = result;
  ebx = (ebx + ebp);
  ebp = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  a = ebp;
  b = BB_U32(edx + 0xcu);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  ebp = result;
  WORD(edi) = ((uint16_t)(eax));
  ebx = (ebx + ebp);
  ebx = ((uint32_t)((int32_t)(ebx) >> 13));
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD(ecx + 0xfffffffeu)) & 0xffffu) << 0);
  ebp = (ebp & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x6u)) & 0xffffu) << 0);
  WORD(edi + 0x2u) = ((uint16_t)(eax));
  eax = esi;
  WORD(edi + 0x4u) = ((uint16_t)(ebp));
  esi = (esi - 1u);
  WORD(edi + 0x6u) = ((uint16_t)(ebx));
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  WORD(ecx + 0xfffffffeu) = ((uint16_t)(ebx));
  if (!(flags & 0x40u))
    goto L_10010b6b;
L_10010bc4:;
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
bb_qmix_short_biquad_scalar_samples_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1 __attribute__((unused)),
                                          uint32_t arg2 __attribute__((unused)),
                                          uint32_t arg3
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_scalar_samples_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_packed_samples_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0x14u);
  esi = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x1cu);
  ecx = (ecx << 1);
  edi = (edi + ecx);
  ecx = (0u - ecx);
  mm1 = (uint32_t)(BB_U32(esi));
  mm2 = (uint32_t)(BB_U32(esi + 0x4u));
  mm1 = packed_unpack(mm1, mm1, 32, 0);
  mm2 = packed_unpack(mm2, mm2, 32, 0);
  mm3 = QWORD(ebx);
  mm4 = QWORD(ebx + 0x8u);
  mm5 = QWORD(ebx + 0x10u);
  mm0 = (uint32_t)(BB_U32(edi + ecx));
  mm2 = packed_dot_pairs(mm2, mm5);
  mm0 = packed_unpack(mm0, mm0, 32, 0);
  mm1 = packed_dot_pairs(mm1, mm4);
  mm7 = mm0;
  mm0 = packed_dot_pairs(mm0, mm3);
  mm2 = packed_add_dwords(mm2, mm1);
  mm1 = mm7;
  mm2 = packed_add_dwords(mm2, mm0);
  mm0 = (uint32_t)(BB_U32(edi + ecx + 0x4u));
  mm2 = packed_shift_dwords(mm2, 0xdu);
  mm0 = packed_unpack(mm0, mm0, 32, 0);
  mm6 = mm0;
  a = ecx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_10010c63;
L_10010c32:;
  mm0 = packed_dot_pairs(mm0, mm3);
  mm2 = packed_clip_dwords(mm2, mm2);
  mm1 = packed_dot_pairs(mm1, mm4);
  mm7 = mm6;
  BB_U32(edi + ecx + 0xfffffffcu) = (uint32_t)(mm2);
  mm2 = packed_dot_pairs(mm2, mm5);
  mm6 = (uint32_t)(BB_U32(edi + ecx + 0x4u));
  mm1 = packed_add_dwords(mm1, mm0);
  mm6 = packed_unpack(mm6, mm6, 32, 0);
  mm2 = packed_add_dwords(mm2, mm1);
  mm0 = mm6;
  mm2 = packed_shift_dwords(mm2, 0xdu);
  mm1 = mm7;
  a = ecx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_10010c32;
L_10010c63:;
  mm2 = packed_clip_dwords(mm2, mm2);
  BB_U32(edi + ecx + 0xfffffffcu) = (uint32_t)(mm2);
  BB_U32(esi) = (uint32_t)(mm1);
  BB_U32(esi + 0x4u) = (uint32_t)(mm2);
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
bb_qmix_short_biquad_packed_samples_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1 __attribute__((unused)),
                                          uint32_t arg2 __attribute__((unused)),
                                          uint32_t arg3
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_packed_samples_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_silent_c(uint32_t *input) {
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
  a = BB_U32(ecx + 0x14u);
  b = 0x3e8u;
  flags = sub_flags(flags, a, b, 32);
  eax = 0x1u;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10010c90;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_10010c90:;
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
uint32_t __attribute__((naked)) bb_qmix_short_biquad_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_biquad_clear_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  edx = ecx;
  eax = (eax ^ eax);
  ecx = BB_U32(edx + 0x4u);
  ecx = (ecx << 3);
  edi = BB_U32(edx + 0x8u);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(edx + 0x14u) = 0x3e8u;
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
uint32_t __attribute__((naked)) bb_qmix_short_biquad_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_biquad_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_create_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10010d24u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x34u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010ce3u); /* 10010cde */
  BB_U32(ebp + 0xfffffff0u) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  esi = eax;
  eax = 0x0u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10010d0c;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(eax + 0x4u);
  edx = BB_U32(eax);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x10010d40u, 8, 0x10010d0cu); /* 10010d07 */
L_10010d0c:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
bb_qmix_short_fir_create_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_fir_free_failed_c(uint32_t *input) {
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
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010d37u); /* 10010d32 */
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
uint32_t __attribute__((naked)) bb_qmix_short_fir_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_free_failed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_constructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10010ef5u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_base);
  a = esp;
  b = 0x1cu;
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffff0u) = ecx;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_intermediate);
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = (eax + 0xfu);
  a = esi;
  b = 0xfffffff0u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_short_fir);
  BB_U32(ecx + 0x1cu) = esi;
  eax = (esi * 2u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010d9du); /* 10010d98 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(ebp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(ecx + 0xcu) = eax;
  BB_U32(ebp + 0xffffffe0u) = eax;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010de0;
  fp[depth] = F32(0x1001a6fcu);
  ++depth;
  edi = BB_U32(ebp + 0x8u);
  ebx = eax;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffecu) = eax;
L_10010dc0:;
  fp[depth] = F32(edi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a6f8u);
  edi = (edi + 0x4u);
  a = ebx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  fp[depth - 1] = fp[depth - 1] + fp[depth - 1u - 1u];
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0xfffffffeu) = ((uint16_t)(eax));
  a = BB_U32(ebp + 0xffffffecu);
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  BB_U32(ebp + 0xffffffecu) = result;
  if (!(flags & 0x40u))
    goto L_10010dc0;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
L_10010de0:;
  eax = BB_U32(ebp + 0xcu);
  esi = (esi - BB_U32(ebp + 0xcu));
  eax = (eax + eax);
  esi = (esi + esi);
  edi = BB_U32(ebp + 0xffffffe0u);
  ecx = esi;
  ecx = (ecx >> 2);
  edi = (edi + eax);
  BB_U32(ebp + 0xffffffd8u) = eax;
  eax = (eax ^ eax);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = esi;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  BB_U32(ebp + 0xffffffe4u) = 0x1u;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffecu) = 0x2u;
  ecx = (eax * 2u + 0x2u);
  BB_U32(ebp + 0xffffffdcu) = ecx;
  ecx = BB_U32(ebp + 0xfffffff0u);
  ecx = (ecx + 0x10u);
  BB_U32(ebp + 0xffffffe8u) = ecx;
L_10010e26:;
  ebx = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0xffffffe8u);
  ebx = (ebx + BB_U32(ebp + 0xffffffe4u));
  ebx = (ebx + 0xfu);
  a = ebx;
  b = 0xfffffff0u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  BB_U32(ecx + 0x10u) = ebx;
  eax = (ebx * 2u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010e45u); /* 10010e40 */
  edi = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xffffffe8u);
  edx = eax;
  esi = BB_U32(ebp + 0xffffffecu);
  BB_U32(ecx) = eax;
  eax = (eax ^ eax);
  ecx = esi;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = esi;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  eax = BB_U32(ebp + 0xffffffd8u);
  edi = BB_U32(ebp + 0xffffffecu);
  edi = (edi + edx);
  esi = BB_U32(ebp + 0xffffffe0u);
  ecx = eax;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = eax;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  ebx = (ebx - BB_U32(ebp + 0xcu));
  edi = BB_U32(ebp + 0xffffffdcu);
  ebx = (ebx - BB_U32(ebp + 0xffffffe4u));
  edi = (edi + edx);
  ebx = (ebx + ebx);
  eax = (eax ^ eax);
  ecx = ebx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = ebx;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  BB_U32(ebp + 0xffffffdcu) = (BB_U32(ebp + 0xffffffdcu) + 0x2u);
  BB_U32(ebp + 0xffffffecu) = (BB_U32(ebp + 0xffffffecu) + 0x2u);
  BB_U32(ebp + 0xffffffe8u) = (BB_U32(ebp + 0xffffffe8u) + 0x4u);
  BB_U32(ebp + 0xffffffe4u) = (BB_U32(ebp + 0xffffffe4u) + 1u);
  a = BB_U32(ebp + 0xffffffecu);
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10010e26;
  ecx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(ecx + 0x4u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10010ec2u); /* 10010ebd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ecx + 0x2cu) = eax;
  SAMPLE_CALL(0x10011170u, 0, 0x10010ed0u); /* 10010ecb */
  eax = BB_U32(ebp + 0xfffffff0u);
  edi = args[sp++];
  esp += 4u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  ebx = args[sp++];
  esp += 4u;
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
bb_qmix_short_fir_constructor_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_short_fir_unwind_base_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 10010ef0 */
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
uint32_t __attribute__((naked)) bb_qmix_short_fir_unwind_base_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_unwind_base_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_unwind_intermediate_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e0a0u, 0, BB_U32(esp - 4u)); /* tail call 10010f02 */
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
bb_qmix_short_fir_unwind_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_unwind_intermediate_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_delete_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x10010f30u, 0, 0x10010f18u); /* 10010f13 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10010f28;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010f25u); /* 10010f20 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10010f28:;
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
bb_qmix_short_fir_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_fir_destructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10010f9cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_short_fir);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  BB_U32(ebp + 0xfffffffcu) = esi;
  eax = BB_U32(ecx + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10010f64u); /* 10010f5f */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edi = BB_U32(ebp + 0xfffffff0u);
  edi = (edi + 0xcu);
L_10010f6d:;
  eax = BB_U32(edi);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  esi = result;
  SAMPLE_CALL(0x10001070u, 0, 0x10010f79u); /* 10010f74 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = esi;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10010f6d;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x10010fa6u, 0, 0x10010f8du); /* 10010f88 */
  eax = BB_U32(ebp + 0xfffffff4u);
  edi = args[sp++];
  esp += 4u;
  exception_link(eax);
  esi = args[sp++];
  esp += 4u;
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
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
uint32_t __attribute__((naked)) bb_qmix_short_fir_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_cleanup_intermediate_c(uint32_t *input) {
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
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e0a0u, 0, BB_U32(esp - 4u)); /* tail call 10010fa9 */
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
bb_qmix_short_fir_cleanup_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_cleanup_intermediate_c) ";leal "
                                                "4(%esp),%esp;popal;popfl;leal "
                                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_process_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = ecx;
  eax = BB_U32(ebx + 0xcu);
  esi = BB_U32(esp + 0x18u);
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10010fda;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10010fd2;
  eax = BB_U32(edi);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x10010fceu); /* 10010fcb indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001104e;
L_10010fd2:;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = ebx;
  SAMPLE_CALL(0x1000d690u, 4, 0x10010fdau); /* 10010fd5 */
L_10010fda:;
  ebx = BB_U32(ebx + 0x4u);
  eax = esi;
  esi = (esi - 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011047;
L_10010fe4:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ebx)) & 0xffffu) << 0);
  ecx = BB_U32(edi + 0x8u);
  ecx = (ecx + BB_U32(edi + 0x4u));
  edx = BB_U32(edi + 0x2cu);
  WORD(edx + ecx * 2u) = ((uint16_t)(eax));
  ebx = (ebx + 0x2u);
  edx = BB_U32(edi + 0x8u);
  ecx = BB_U32(edi + 0x2cu);
  eax = BB_U32(edi + 0x4u);
  eax = (eax + edx);
  eax =
      (eax & 0xffff0000u) | (((uint32_t)(WORD(ecx + eax * 2u)) & 0xffffu) << 0);
  WORD(ecx + edx * 2u) = ((uint16_t)(eax));
  edx = BB_U32(edi + 0x8u);
  ecx = edx;
  ecx = (ecx & 0x3u);
  ebp = BB_U32(edi + ecx * 4u + 0x1cu);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = (edx - ecx);
  eax = (edi + ecx * 4u);
  edx = (edx + edx);
  a = edx;
  b = BB_U32(edi + 0x2cu);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edx = result;
  eax = BB_U32(eax + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011060u, 0, 0x1001102bu); /* 10011026 */
  WORD(ebx + 0xfffffffeu) = ((uint16_t)(eax));
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(edi + 0x8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001103c;
  eax = BB_U32(edi + 0x4u);
L_1001103c:;
  eax = (eax - 1u);
  BB_U32(edi + 0x8u) = eax;
  eax = esi;
  esi = (esi - 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10010fe4;
L_10011047:;
  BB_U32(edi + 0x30u) = 0x0u;
L_1001104e:;
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
bb_qmix_short_fir_process_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_process_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_short_fir_dot_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
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
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x18u);
  ecx = BB_U32(esp + 0x1cu);
  ebx = (ebx ^ ebx);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_100110cf;
  ecx = (ecx << 5);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
  mm0 = mm0 ^ mm0;
L_10011085:;
  mm1 = QWORD(esi + ecx);
  mm2 = QWORD(esi + ecx + 0x8u);
  mm3 = QWORD(esi + ecx + 0x10u);
  mm4 = QWORD(esi + ecx + 0x18u);
  mm1 = packed_dot_pairs(mm1, QWORD(edi + ecx));
  mm2 = packed_dot_pairs(mm2, QWORD(edi + ecx + 0x8u));
  mm3 = packed_dot_pairs(mm3, QWORD(edi + ecx + 0x10u));
  mm4 = packed_dot_pairs(mm4, QWORD(edi + ecx + 0x18u));
  a = ecx;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_10011085;
  mm1 = packed_add_dwords(mm1, mm2);
  mm3 = packed_add_dwords(mm3, mm4);
  mm0 = packed_add_dwords(mm0, mm1);
  mm0 = packed_add_dwords(mm0, mm3);
  mm0 = packed_shift_dwords(mm0, 0xfu);
  mm1 = mm0;
  mm0 = (0x20u >= 64u ? 0 : (mm0 >> 0x20u));
  mm0 = packed_add_dwords(mm0, mm1);
  ebx = (uint32_t)(mm0);
L_100110cf:;
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  b = 0xfu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_100110f6;
  ecx = (ecx << 1);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_100110e1:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + ecx)) & 0xffffu) << 0);
  a = (uint16_t)eax;
  b = WORD(edi + ecx);
  wide = (uint64_t)((int64_t)(int16_t)a * (int64_t)(int16_t)b);
  result = (uint16_t)wide;
  eax = (eax & 0xffff0000u) | result;
  edx = (edx & 0xffff0000u) | ((wide >> 16) & 0xffffu);
  a = ((uint16_t)(eax));
  b = ((uint16_t)(edx));
  result = ((a >> 15) | (b << 1)) & 0xffffu;
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  eax = (uint32_t)(int32_t)(int16_t)eax;
  ebx = (ebx + eax);
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_100110e1;
L_100110f6:;
  BB_U32(esp + 0xcu) = ebx;
  args[sp + 3u] = BB_U32(esp + 12u);
  a = BB_U32(esp + 0xcu);
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001110f;
  eax = (eax & 0xffff0000u) | (((uint32_t)(0x7fffu) & 0xffffu) << 0);
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
L_1001110f:;
  eax = BB_U32(esp + 0xcu);
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1001111f;
  eax = 0xffff8000u;
L_1001111f:;
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
bb_qmix_short_fir_dot_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_dot_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_silent_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  esi = ecx;
  a = BB_U32(ecx + 0x30u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10011140;
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
L_10011140:;
  eax = BB_U32(esi + 0x4u);
  ecx = BB_U32(esi + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000dd20u, 0, 0x1001114du); /* 10011148 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = ((uint16_t)(eax));
  b = 0xau;
  flags = sub_flags(flags, a, b, 16);
  eax = 0x1u;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001115d;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_1001115d:;
  BB_U32(esi + 0x30u) = eax;
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
uint32_t __attribute__((naked)) bb_qmix_short_fir_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_fir_clear_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(ecx + 0x2cu);
  edx = ecx;
  ecx = (eax + 0xffffffffu);
  BB_U32(edx + 0x8u) = ecx;
  ecx = (eax * 4u);
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
  BB_U32(edx + 0x30u) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_short_fir_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_fir_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
