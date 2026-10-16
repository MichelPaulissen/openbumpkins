/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "game_audio_calls.h"
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
/* First C forms of sample bank, session, channel and effect management.
 * Address-derived labels retain provenance for auditing sample controls,
 * priority and external callback ordering.
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
#define SAMPLE_CALL(target, consumed)                                          \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                    \
    BB_AUDIO_CALL_WORDS(&call, target, args + sp, esp, edi, esi, ebp, ebx);        \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

static uint32_t
    __attribute__((unused)) borrow_flags(uint32_t flags, uint32_t a, uint32_t b,
                                         unsigned bits) {
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
#endif
/* C forms of sample playback and channel/effect bookkeeping. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */

void bb_sample_bank_load_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 20u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x48bc7cu;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46bdd0u, 0); /* 00463802 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = eax;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4638a6;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46cba0u, 0); /* 0046381f */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46c8f0u, 0); /* 0046382b */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffff4u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46cba0u, 0); /* 0046383e */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca3d0u), 8); /* 0046384c indirect */
  BB_U32(ebp + 0xfffffff8u) = eax;
  a = BB_U32(ebp + 0xfffffff8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_46388e;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46cf40u, 0); /* 0046386d */
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46bbd0u, 0); /* 00463879 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xfffffff8u);
  goto L_4638ad;
  goto L_4638a1;
L_46388e:;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46bbd0u, 0); /* 00463892 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4638ad;
L_4638a1:;
  goto L_4638ad;
L_4638a6:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4638ad;
L_4638ad:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_bank_load_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -16(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_bank_load_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "16(%esp),%esp;ret $0");
}
#endif
void bb_sample_bank_free_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca3d4u), 4); /* 004638bc indirect */
  goto L_4638c7;
L_4638c7:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_bank_free_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_bank_free_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_status_text_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 20u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
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
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  BB_U32(ebp + 0xfffffff8u) = 0x6c13f8u;
  BB_U32(ebp + 0xfffffff4u) = 0x0u;
  goto L_4638f6;
L_4638f3:;
  BB_U32(ebp + 0xfffffff4u) = (BB_U32(ebp + 0xfffffff4u) + 1u);
L_4638f6:;
  eax = BB_U32(0x48bc88u);
  a = BB_U32(ebp + 0xfffffff4u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_463973;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 00463908 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_463956;
  eax = BB_U32(ebp + 0xfffffff4u);
  eax = BB_U32(eax * 4u + 0x6c19d0u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff4u);
  eax = BB_U32(eax * 4u + 0x6c13c0u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff4u);
  eax = BB_U32(eax * 4u + 0x6c19a8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bc90u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff8u);
  args[--sp] = eax;
  esp -= 4u;
#ifdef BB_MODEL_RUNTIME
  memcpy(BB_SPAN(esp, 24), args + sp, 24);
#endif
  SAMPLE_CALL(0x46bae0u, 0); /* 00463946 */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffff8u) = (BB_U32(ebp + 0xfffffff8u) + eax);
  goto L_46396e;
L_463956:;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bca4u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffff8u);
  args[--sp] = eax;
  esp -= 4u;
#ifdef BB_MODEL_RUNTIME
  memcpy(BB_SPAN(esp, 12), args + sp, 12);
#endif
  SAMPLE_CALL(0x46bae0u, 0); /* 00463963 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffff8u) = (BB_U32(ebp + 0xfffffff8u) + eax);
L_46396e:;
  goto L_4638f3;
L_463973:;
  eax = 0x6c13f8u;
  goto L_46397d;
L_46397d:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_status_text_entry(void) {
  __asm__ volatile("leal -16(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_status_text_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "16(%esp),%esp;ret $0");
}
#endif
void bb_sample_session_init_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 16u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
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
  BB_U32(ebp + 0xfffffff8u) = 0x0u;
  a = BB_U32(0x48bc80u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4639a9;
  eax = 0x10u;
  goto L_463bea;
L_4639a9:;
  a = BB_U32(0x48bc70u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4639c0;
  eax = 0x2u;
  goto L_463bea;
L_4639c0:;
  a = BB_U32(0x48bc74u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4639d7;
  eax = 0x2u;
  goto L_463bea;
L_4639d7:;
  a = BB_U32(ebp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4639eb;
  a = BB_U32(ebp + 0xcu);
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4639f2;
L_4639eb:;
  BB_U32(ebp + 0xcu) = 0x8u;
L_4639f2:;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(0x48bc88u) = eax;
  a = BB_U32(ebp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463abb;
  WORD(0x6c13e0u) = 0x14u;
  BB_U32(0x6c13e2u) = 0x7eu;
  eax = BB_U32(0x48bc70u);
  BB_U32(0x6c13eau) = eax;
  a = BB_U32(0x48bc78u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463a3c;
  WORD(0x6c13f2u) = 0xc8u;
  goto L_463a45;
L_463a3c:;
  WORD(0x6c13f2u) = 0x64u;
L_463a45:;
  WORD(0x6c13e8u) = 0x16u;
  eax = BB_U32(ebp + 0xcu);
  WORD(0x6c13eeu) = ((uint16_t)(eax));
  WORD(0x6c13f0u) = 0x1u;
  args[--sp] = 0x6c13e0u;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4c8u), 4); /* 00463a65 indirect */
  BB_U32(0x6c19c8u) = eax;
  a = BB_U32(0x6c19c8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463ab7;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e4u), 8); /* 00463a85 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_463aa8;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4dcu), 4); /* 00463a99 indirect */
  BB_U32(ebp + 0xfffffff8u) = (BB_U32(ebp + 0xfffffff8u) | 0x8u);
  goto L_463ab2;
L_463aa8:;
  BB_U32(0x48bc84u) = 0x1u;
L_463ab2:;
  goto L_463abb;
L_463ab7:;
  BB_U32(ebp + 0xfffffff8u) = (BB_U32(ebp + 0xfffffff8u) | 0x2u);
L_463abb:;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463bcf;
  args[--sp] = 0x2u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca508u), 12); /* 00463ad4 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_463b01;
  a = BB_U32(ebp + 0xfffffff8u);
  b = 0x4u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  BB_U32(ebp + 0xfffffff8u) = result;
  BB_U32(0x48bc84u) = 0x0u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4dcu), 4); /* 00463af6 indirect */
  goto L_463bcf;
L_463b01:;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_463b10;
L_463b0d:;
  BB_U32(ebp + 0xfffffffcu) = (BB_U32(ebp + 0xfffffffcu) + 1u);
L_463b10:;
  eax = BB_U32(ebp + 0xcu);
  a = BB_U32(ebp + 0xfffffffcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_463bcf;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c154cu) = 0x0u;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  eax = (eax + eax * 4u);
  eax = (eax + 0x6c14f8u);
  ecx = BB_U32(ebp + 0xfffffffcu);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1508u) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1518u) = 0x0u;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  WORD(eax + eax * 4u + 0x6c151cu) = 0x30u;
  eax = BB_U32(0x6c19c8u);
  ecx = BB_U32(ebp + 0xfffffffcu);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c151eu) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c153cu) = 0x0u;
  eax = BB_U32(0x6c1338u);
  ecx = BB_U32(ebp + 0xfffffffcu);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c152au) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c152eu) = 0x10u;
  goto L_463b0d;
L_463bcf:;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463be2;
  a = BB_U32(0x48bc80u);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(0x48bc80u) = result;
L_463be2:;
  eax = BB_U32(ebp + 0xfffffff8u);
  goto L_463bea;
L_463bea:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_session_init_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -12(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_session_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "12(%esp),%esp;ret $0");
}
#endif
void bb_sample_session_shutdown_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc80u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463c0c;
  eax = 0x1u;
  goto L_463c41;
L_463c0c:;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463c34;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channels_release_entry), 0); /* 00463c19 */
  BB_U32(0x48bc84u) = 0x0u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4dcu), 4); /* 00463c2e indirect */
L_463c34:;
  BB_U32(0x48bc80u) = (BB_U32(0x48bc80u) - 1u);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_463c41;
L_463c41:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_session_shutdown_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_session_shutdown_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_channels_refresh_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_463c5e;
L_463c5b:;
  BB_U32(ebp + 0xfffffffcu) = (BB_U32(ebp + 0xfffffffcu) + 1u);
L_463c5e:;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = BB_U32(0x48bc88u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_463cae;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 00463c71 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_463ca9;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1554u) = 0x0u;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1558u) = 0xffffffffu;
L_463ca9:;
  goto L_463c5b;
L_463cae:;
  goto L_463cb3;
L_463cb3:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_channels_refresh_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channels_refresh_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_id_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463cdc;
  BB_U32(ebp + 0xcu) = 0x0u;
  eax = 0xffffffffu;
  goto L_463d17;
L_463cdc:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channels_refresh_entry), 0); /* 00463cdc */
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  a = BB_U32(eax + eax * 4u + 0x6c1554u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463d0d;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  eax = (eax + eax * 4u);
  eax = BB_U32(eax + eax * 4u + 0x6c1558u);
  goto L_463d17;
L_463d0d:;
  eax = 0xffffffffu;
  goto L_463d17;
L_463d17:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_id_entry(uint32_t arg0 __attribute__((unused)),
                           uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_id_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_instance_count_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 16u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
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
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463d40;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_463da5;
L_463d40:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channels_refresh_entry), 0); /* 00463d40 */
  BB_U32(ebp + 0xfffffff8u) = 0x0u;
  goto L_463d54;
L_463d51:;
  BB_U32(ebp + 0xfffffff8u) = (BB_U32(ebp + 0xfffffff8u) + 1u);
L_463d54:;
  eax = BB_U32(ebp + 0xfffffff8u);
  a = BB_U32(0x48bc88u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_463d9d;
  eax = BB_U32(ebp + 0xfffffff8u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  ecx = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + eax * 4u + 0x6c1558u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463d98;
  eax = BB_U32(ebp + 0xfffffff8u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  ecx = BB_U32(ebp + 0xcu);
  a = BB_U32(eax + eax * 4u + 0x6c1554u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463d98;
  BB_U32(ebp + 0xfffffffcu) = (BB_U32(ebp + 0xfffffffcu) + 1u);
L_463d98:;
  goto L_463d51;
L_463d9d:;
  eax = BB_U32(ebp + 0xfffffffcu);
  goto L_463da5;
L_463da5:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_instance_count_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -12(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_instance_count_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "12(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_count_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463dc4;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_463dce;
L_463dc4:;
  eax = BB_U32(0x48bc88u);
  goto L_463dce;
L_463dce:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_channel_count_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_count_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_play_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 28u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 28u), 28u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x14u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463df3;
  eax = 0xffffffffu;
  goto L_463f66;
L_463df3:;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463e07;
  eax = 0xffffffffu;
  goto L_463f66;
L_463e07:;
  BB_U32(ebp + 0xfffffff4u) = 0x0u;
  goto L_463e16;
L_463e13:;
  BB_U32(ebp + 0xfffffff4u) = (BB_U32(ebp + 0xfffffff4u) + 1u);
L_463e16:;
  eax = BB_U32(ebp + 0xfffffff4u);
  a = BB_U32(0x48bc88u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_463e87;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4ccu), 8); /* 00463e2f indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463e82;
  eax = BB_U32(ebp + 0xfffffff4u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_submit_entry), 0); /* 00463e5a */
  a = esp;
  b = 0x1cu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463e78;
  eax = BB_U32(ebp + 0xfffffff4u);
  goto L_463f66;
  goto L_463e82;
L_463e78:;
  eax = 0xffffffffu;
  goto L_463f66;
L_463e82:;
  goto L_463e13;
L_463e87:;
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  eax = (eax + BB_U32(ebp + 0x8u));
  eax = (eax + 0x8u);
  BB_U32(ebp + 0xfffffff8u) = eax;
  eax = BB_U32(ebp + 0xfffffff8u);
  eax = BB_U32(eax + 0xcu);
  BB_U32(ebp + 0xfffffffcu) = eax;
  BB_U32(ebp + 0xfffffff0u) = 0xffffffffu;
  BB_U32(ebp + 0xffffffecu) = 0x270fu;
  BB_U32(ebp + 0xfffffff4u) = 0x0u;
  goto L_463ebc;
L_463eb9:;
  BB_U32(ebp + 0xfffffff4u) = (BB_U32(ebp + 0xfffffff4u) + 1u);
L_463ebc:;
  eax = BB_U32(ebp + 0xfffffff4u);
  a = BB_U32(0x48bc88u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_463f02;
  eax = BB_U32(ebp + 0xfffffff4u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  ecx = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + eax * 4u + 0x6c154cu);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_463efd;
  eax = BB_U32(ebp + 0xfffffff4u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  eax = BB_U32(eax + eax * 4u + 0x6c154cu);
  BB_U32(ebp + 0xffffffecu) = eax;
  eax = BB_U32(ebp + 0xfffffff4u);
  BB_U32(ebp + 0xfffffff0u) = eax;
L_463efd:;
  goto L_463eb9;
L_463f02:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(ebp + 0xfffffffcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_463f5c;
  a = BB_U32(ebp + 0xfffffff0u);
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_463f5c;
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_submit_entry), 0); /* 00463f34 */
  a = esp;
  b = 0x1cu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463f52;
  eax = BB_U32(ebp + 0xfffffff0u);
  goto L_463f66;
  goto L_463f5c;
L_463f52:;
  eax = 0xffffffffu;
  goto L_463f66;
L_463f5c:;
  eax = 0xffffffffu;
  goto L_463f66;
L_463f66:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_play_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused)),
                             uint32_t arg3 __attribute__((unused)),
                             uint32_t arg4 __attribute__((unused)),
                             uint32_t arg5 __attribute__((unused))) {
  __asm__ volatile("leal -24(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_play_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "24(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_submit_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 64u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 32u), 32u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x38u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x2b11u;
  BB_U32(ebp + 0xffffffe4u) = 0x5622u;
  BB_U32(ebp + 0xffffffe8u) = 0xac44u;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_463f9a;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_463f9a:;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ebp + 0xcu);
  a = BB_U32(eax + 0x4u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_463fb0;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_463fb0:;
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  eax = (eax + BB_U32(ebp + 0x8u));
  eax = (eax + 0x8u);
  BB_U32(ebp + 0xfffffff8u) = eax;
  eax = BB_U32(ebp + 0xfffffff8u);
  eax = BB_U32(eax);
  eax = (eax + BB_U32(ebp + 0x8u));
  BB_U32(ebp + 0xfffffff8u) = eax;
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  ecx = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + ecx + 0xcu);
  eax = (eax & 0xffffffu);
  BB_U32(ebp + 0xffffffd8u) = eax;
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  ecx = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + ecx + 0x10u);
  BB_U32(ebp + 0xfffffffcu) = eax;
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + ecx + 0x14u)) & 0xffu) << 0);
  BYTE(ebp + 0xffffffd0u) = ((uint8_t)(eax));
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + ecx + 0x15u)) & 0xffu) << 0);
  BYTE(ebp + 0xfffffff4u) = ((uint8_t)(eax));
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + ecx + 0x16u)) & 0xffu) << 0);
  BYTE(ebp + 0xffffffdcu) = ((uint8_t)(eax));
  eax = BB_U32(ebp + 0xcu);
  eax = (eax << 4);
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + ecx + 0x17u)) & 0xffu) << 0);
  BYTE(ebp + 0xffffffecu) = ((uint8_t)(eax));
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xfffffff4u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464043;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_464043:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffd0u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46405b;
  WORD(ebp + 0xfffffff0u) = 0x8u;
  goto L_46407b;
L_46405b:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffd0u)) & 0xffu) << 0);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464074;
  WORD(ebp + 0xfffffff0u) = 0x10u;
  goto L_46407b;
L_464074:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_46407b:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffecu)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_464096;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffecu)) & 0xffu) << 0);
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46409d;
L_464096:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_46409d:;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  a = BB_U32(eax + eax * 4u + 0x6c1518u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_464107;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e0u), 12); /* 004640c0 indirect */
  eax = BB_U32(ebp + 0x20u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  eax = (eax + eax * 4u);
  eax = BB_U32(eax + eax * 4u + 0x6c1518u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4d0u), 8); /* 004640d9 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4640f3;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
  goto L_464107;
L_4640f3:;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1518u) = 0x0u;
L_464107:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1558u) = eax;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1554u) = eax;
  eax = BB_U32(ebp + 0xfffffff8u);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c150cu) = eax;
  eax = BB_U32(ebp + 0xffffffd8u);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1510u) = eax;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  WORD(eax + eax * 4u + 0x6c14f8u) = 0x1u;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  WORD(eax + eax * 4u + 0x6c14fau) = 0x1u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffecu)) & 0xffu) << 0);
  eax = BB_U32(ebp + eax * 4u + 0xffffffe0u);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c14fcu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffecu)) & 0xffu) << 0);
  eax = BB_U32(ebp + eax * 4u + 0xffffffe0u);
  ecx = BB_U32(ebp + 0xfffffff0u);
  ecx = (ecx & 0xffffu);
  ecx = (ecx >> 3);
  a = eax;
  b = ecx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1500u) = eax;
  eax = BB_U32(ebp + 0xfffffff0u);
  eax = (eax & 0xffffu);
  eax = (eax >> 3);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  WORD(ecx + ecx * 4u + 0x6c1504u) = ((uint16_t)(eax));
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD(ebp + 0xfffffff0u)) & 0xffffu) << 0);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  WORD(ecx + ecx * 4u + 0x6c1506u) = ((uint16_t)(eax));
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  WORD(ecx + ecx * 4u + 0x6c1514u) = ((uint16_t)(eax));
  args[--sp] = 0x8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  eax = (eax + eax * 4u);
  eax = (eax + 0x6c14f8u);
  a = eax;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4d8u), 16); /* 0046421e indirect */
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1518u) = eax;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  a = BB_U32(eax + eax * 4u + 0x6c1518u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464252;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_464252:;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state call = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&call, BB_AUDIO_TARGET(bb_sample_pitch_scale_entry), args + sp, esp, edi, esi, ebp, ebx, &fp[depth++]);
    eax = call.eax;
    ecx = call.ecx;
    edx = call.edx;
    flags = call.flags;
  }
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp + 0xffffffecu)) & 0xffu) << 0);
  eax = BB_U32(ebp + eax * 4u + 0xffffffe0u);
  BB_U32(ebp + 0xffffffccu) = eax;
  fp[depth - 1] *= (long double)(int32_t)(BB_U32(ebp + 0xffffffccu));
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  BB_U32(ebp + 0xffffffd4u) = eax;
  eax = BB_U32(ebp + 0xffffffd4u);
  ecx = BB_U32(ebp + 0x20u);
  a = ecx;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  ecx = result;
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1550u) = eax;
  eax = BB_U32(ebp + 0xffffffd4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4d4u), 16); /* 00464298 indirect */
  eax = BB_U32(ebp + 0x20u);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1522u) = eax;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  eax = BB_U32(eax + eax * 4u + 0x6c1518u);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1526u) = eax;
  eax = BB_U32(ebp + 0x10u);
  ecx = eax;
  eax = (eax << 8);
  eax = (eax + ecx);
  eax = (eax + ecx);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1534u) = eax;
  eax = BB_U32(ebp + 0x18u);
  eax = (eax - 1u);
  ecx = BB_U32(ebp + 0x20u);
  a = ecx;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  ecx = result;
  ecx = (ecx + ecx * 4u);
  WORD(ecx + ecx * 4u + 0x6c1532u) = ((uint16_t)(eax));
  eax = BB_U32(ebp + 0x1cu);
  BB_U32(ebp + 0xffffffc8u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffc8u));
  fp[depth - 1] = fp[depth - 1] * F64(0x482390u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  eax = (eax - 0x5au);
  ecx = BB_U32(ebp + 0x20u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1540u) = eax;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1544u) = 0x0u;
  eax = BB_U32(ebp + 0x20u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  eax = (eax + eax * 4u);
  eax = (eax + 0x6c14f8u);
  a = eax;
  b = 0x24u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4c4u), 4); /* 00464352 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464367;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_4643ab;
L_464367:;
  eax = BB_U32(ebp + 0x10u);
  ecx = BB_U32(ebp + 0x20u);
  BB_U32(ecx * 4u + 0x6c19a8u) = eax;
  eax = BB_U32(ebp + 0x14u);
  ecx = BB_U32(ebp + 0x20u);
  BB_U32(ecx * 4u + 0x6c13c0u) = eax;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x20u);
  BB_U32(ecx * 4u + 0x6c19d0u) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  ecx = BB_U32(ebp + 0x20u);
  a = ecx;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  ecx = result;
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c154cu) = eax;
  eax = 0x1u;
  goto L_4643ab;
L_4643ab:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_submit_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused)),
                               uint32_t arg3 __attribute__((unused)),
                               uint32_t arg4 __attribute__((unused)),
                               uint32_t arg5 __attribute__((unused)),
                               uint32_t arg6 __attribute__((unused))) {
  __asm__ volatile("leal -60(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_submit_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "60(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4643cd;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4643d2;
L_4643cd:;
  goto L_4643e9;
L_4643d2:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e0u), 12); /* 004643de indirect */
  goto L_4643e9;
L_4643e9:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_stop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_active_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_46440b;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464412;
L_46440b:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_46443c;
L_464412:;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4ccu), 8); /* 0046441c indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464432;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_46443c;
L_464432:;
  eax = 0x1u;
  goto L_46443c;
L_46443c:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_active_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_active_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_device_text_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BYTE(0x6c1340u) = 0x0u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4644a1;
  eax = (ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4ecu), 8); /* 00464468 indirect */
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46448f;
  args[--sp] = 0x48bcb0u;
  esp -= 4u;
  args[--sp] = 0x6c1340u;
  esp -= 4u;
  SAMPLE_CALL(0x46f4f0u, 0); /* 00464482 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_4644a1;
L_46448f:;
  args[--sp] = 0x48bcd0u;
  esp -= 4u;
  args[--sp] = 0x6c1340u;
  esp -= 4u;
  SAMPLE_CALL(0x46f4f0u, 0); /* 00464499 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_4644a1:;
  eax = 0x6c1340u;
  goto L_4644ab;
L_4644ab:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_device_text_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_device_text_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_pan_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4644d0;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4644d5;
L_4644d0:;
  goto L_46455d;
L_4644d5:;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 004644d9 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46454b;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xfffffffcu) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xfffffffcu));
  fp[depth - 1] = fp[depth - 1] * F64(0x482390u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  eax = (eax - 0x5au);
  ecx = BB_U32(ebp + 0x8u);
  ecx = (ecx << 2);
  ecx = (ecx + ecx * 4u);
  BB_U32(ecx + ecx * 4u + 0x6c1540u) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1544u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  eax = (eax + eax * 4u);
  eax = (eax + 0x6c14f8u);
  a = eax;
  b = 0x48u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4f0u), 16); /* 00464545 indirect */
L_46454b:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx * 4u + 0x6c19d0u) = eax;
  goto L_46455d;
L_46455d:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_pan_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_pan_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_volume_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_46457f;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464584;
L_46457f:;
  goto L_4645c9;
L_464584:;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 00464588 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4645b7;
  eax = BB_U32(ebp + 0xcu);
  ecx = eax;
  eax = (eax << 8);
  eax = (eax + ecx);
  a = eax;
  b = ecx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e8u), 16); /* 004645b1 indirect */
L_4645b7:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx * 4u + 0x6c19a8u) = eax;
  goto L_4645c9;
L_4645c9:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_volume_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_volume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_channel_pitch_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 16u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
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
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4645ee;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4645f3;
L_4645ee:;
  goto L_464659;
L_4645f3:;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 004645f7 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464647;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state call = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&call, BB_AUDIO_TARGET(bb_sample_pitch_scale_entry), args + sp, esp, edi, esi, ebp, ebx, &fp[depth++]);
    eax = call.eax;
    ecx = call.ecx;
    edx = call.edx;
    flags = call.flags;
  }
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  eax = (eax + eax * 4u);
  eax = BB_U32(eax + eax * 4u + 0x6c1550u);
  BB_U32(ebp + 0xfffffff8u) = eax;
  fp[depth - 1] *= (long double)(int32_t)(BB_U32(ebp + 0xfffffff8u));
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  BB_U32(ebp + 0xfffffffcu) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4d4u), 16); /* 00464641 indirect */
L_464647:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx * 4u + 0x6c13c0u) = eax;
  goto L_464659;
L_464659:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_channel_pitch_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -12(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channel_pitch_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "12(%esp),%esp;ret $0");
}
#endif
void bb_sample_channels_release_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464679;
  goto L_4646ff;
L_464679:;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_464688;
L_464685:;
  BB_U32(ebp + 0xfffffffcu) = (BB_U32(ebp + 0xfffffffcu) + 1u);
L_464688:;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = BB_U32(0x48bc88u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4646fa;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e0u), 12); /* 004646a3 indirect */
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  a = BB_U32(eax + eax * 4u + 0x6c1518u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4646f5;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  eax = (eax + eax * 4u);
  eax = BB_U32(eax + eax * 4u + 0x6c1518u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4d0u), 8); /* 004646d3 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4646f5;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  BB_U32(eax + eax * 4u + 0x6c1518u) = 0x0u;
L_4646f5:;
  goto L_464685;
L_4646fa:;
  goto L_4646ff;
L_4646ff:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_channels_release_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_channels_release_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_bank_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46471f;
  goto L_464799;
L_46471f:;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_46472e;
L_46472b:;
  BB_U32(ebp + 0xfffffffcu) = (BB_U32(ebp + 0xfffffffcu) + 1u);
L_46472e:;
  eax = BB_U32(0x48bc88u);
  a = BB_U32(ebp + 0xfffffffcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464794;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 00464740 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46478f;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 2);
  eax = (eax + eax * 4u);
  ecx = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + eax * 4u + 0x6c1554u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46478f;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e8u), 16); /* 00464777 indirect */
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4e0u), 12); /* 00464789 indirect */
L_46478f:;
  goto L_46472b;
L_464794:;
  goto L_464799;
L_464799:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_bank_stop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_bank_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_session_pause_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4647b6;
  goto L_4647cb;
L_4647b6:;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4f8u), 12); /* 004647c0 indirect */
  goto L_4647cb;
L_4647cb:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_session_pause_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_session_pause_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_session_resume_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4647e8;
  goto L_4647fd;
L_4647e8:;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4fcu), 12); /* 004647f2 indirect */
  goto L_4647fd;
L_4647fd:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_session_resume_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_session_resume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_session_enable_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46481c;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_464875;
L_46481c:;
  a = BB_U32(ebp + 0x8u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46484b;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4f4u), 8); /* 0046482e indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_464846;
  BB_U32(0x48bc8cu) = 0x1u;
L_464846:;
  goto L_46486b;
L_46484b:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca4f4u), 8); /* 00464853 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46486b;
  BB_U32(0x48bc8cu) = 0x0u;
L_46486b:;
  eax = BB_U32(0x48bc8cu);
  goto L_464875;
L_464875:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_session_enable_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_session_enable_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_session_options_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  a = BB_U32(0x48bc84u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4648a1;
  eax = 0x1u;
  goto L_464a1c;
L_4648a1:;
  eax = (ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca500u), 8); /* 004648ab indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_464988;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4648f1;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  b = 0x1u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca504u), 12); /* 004648d4 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4648ec;
  eax = 0x1u;
  goto L_464a1c;
L_4648ec:;
  goto L_464918;
L_4648f1:;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  b = 0xfffffffeu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca504u), 12); /* 00464900 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464918;
  eax = 0x1u;
  goto L_464a1c;
L_464918:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464983;
  eax = BB_U32(ebp + 0xcu);
  a = BB_U32(eax);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46495c;
  args[--sp] = 0x2u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  b = 0x2u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca504u), 12); /* 0046493f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464957;
  eax = 0x1u;
  goto L_464a1c;
L_464957:;
  goto L_464983;
L_46495c:;
  args[--sp] = 0x2u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  b = 0xfffffffdu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca504u), 12); /* 0046496b indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464983;
  eax = 0x1u;
  goto L_464a1c;
L_464983:;
  goto L_464992;
L_464988:;
  eax = 0x1u;
  goto L_464a1c;
L_464992:;
  eax = (ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6c19c8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca500u), 8); /* 0046499c indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_464a0b;
  a = BYTE(ebp + 0xfffffffcu);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_4649c2;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax) = 0x1u;
  goto L_4649cb;
L_4649c2:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax) = 0x0u;
L_4649cb:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4649fd;
  a = BYTE(ebp + 0xfffffffcu);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_4649ef;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(eax) = 0x1u;
  goto L_4649f8;
L_4649ef:;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(eax) = 0x0u;
L_4649f8:;
  goto L_464a06;
L_4649fd:;
  eax = BB_U32(ebp + 0xcu);
  BB_U32(eax) = 0x1u;
L_464a06:;
  goto L_464a15;
L_464a0b:;
  eax = 0x1u;
  goto L_464a1c;
L_464a15:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_464a1c;
L_464a1c:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_session_options_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_session_options_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_effect_reset_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c19f0u) = 0xffffffffu;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c19f4u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c19f8u) = 0x40u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c19fcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c1a00u) = 0x40u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c1a04u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  BB_U32(eax + 0x6c1a08u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  BB_U32(eax + 0x6c1a0cu) = 0x1u;
  goto L_464abb;
L_464abb:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_effect_reset_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effect_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_effects_reset_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_464ad8;
L_464ad5:;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(ebp + 0xfffffffcu) = result;
L_464ad8:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_count_entry), 0); /* 00464ad8 */
  a = eax;
  b = BB_U32(ebp + 0xfffffffcu);
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_464af7;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reset_entry), 0); /* 00464aea */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_464ad5;
L_464af7:;
  goto L_464afc;
L_464afc:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_effects_reset_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effects_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_effects_init_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_session_init_entry), 0); /* 00464b0b */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464b3f;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_count_entry), 0); /* 00464b1b */
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_464b3f;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effects_reset_entry), 0); /* 00464b29 */
  BB_U32(0x48bcfcu) = 0x1u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_464b49;
L_464b3f:;
  eax = 0x1u;
  goto L_464b49;
L_464b49:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_effects_init_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effects_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_effects_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464b69;
  goto L_464c0d;
L_464b69:;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_464b78;
L_464b75:;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(ebp + 0xfffffffcu) = result;
L_464b78:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_count_entry), 0); /* 00464b78 */
  a = eax;
  b = BB_U32(ebp + 0xfffffffcu);
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_464c08;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 5);
  ecx = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x6c1a04u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_464bae;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 5);
  a = BB_U32(eax + 0x6c1a04u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464c03;
L_464bae:;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 00464bbb */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_464bf7;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00464bda */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00464bef */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_464bf7:;
  eax = BB_U32(ebp + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reset_entry), 0); /* 00464bfb */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_464c03:;
  goto L_464b75;
L_464c08:;
  goto L_464c0d;
L_464c0d:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_effects_stop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effects_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_effect_reserve_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  goto L_464c2a;
L_464c27:;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(ebp + 0xfffffffcu) = result;
L_464c2a:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_count_entry), 0); /* 00464c2a */
  a = eax;
  b = BB_U32(ebp + 0xfffffffcu);
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_464ca0;
  eax = BB_U32(ebp + 0xfffffffcu);
  eax = (eax << 5);
  a = BB_U32(eax + 0x6c19f0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464c67;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ebp + 0xfffffffcu);
  a = ecx;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  ecx = result;
  BB_U32(ecx + 0x6c1a04u) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  goto L_464caa;
  goto L_464c9b;
L_464c67:;
  eax = BB_U32(ebp + 0xfffffffcu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 00464c74 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_464c9b;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ebp + 0xfffffffcu);
  a = ecx;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  ecx = result;
  BB_U32(ecx + 0x6c1a04u) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  goto L_464caa;
L_464c9b:;
  goto L_464c27;
L_464ca0:;
  eax = 0xffffffffu;
  goto L_464caa;
L_464caa:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_effect_reserve_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effect_reserve_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_effect_release_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reset_entry), 0); /* 00464cb9 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_464cc6;
L_464cc6:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_effect_release_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effect_release_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_effect_bind_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  a = BB_U32(eax + 0x6c1a04u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_464d12;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax << 5);
  a = BB_U32(eax + 0x6c19f0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464d12;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  ecx = (ecx << 5);
  BB_U32(ecx + 0x6c19f0u) = eax;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_464d1c;
  goto L_464d1c;
L_464d12:;
  eax = 0x1u;
  goto L_464d1c;
L_464d1c:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_effect_bind_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effect_bind_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_effect_channel_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  goto L_464d38;
L_464d38:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_effect_channel_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_effect_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_random_range_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 16u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
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
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(ebp + 0xcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464d5a;
  eax = BB_U32(ebp + 0xcu);
  goto L_464d86;
L_464d5a:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_random), 0); /* 00464d5a */
  BB_U32(ebp + 0xfffffffcu) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xfffffffcu));
  eax = BB_U32(ebp + 0xcu);
  a = eax;
  b = BB_U32(ebp + 0x8u);
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(ebp + 0xfffffff8u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xfffffff8u));
  fp[depth - 1] = fp[depth - 1] / F64(0x482398u);
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] * fp[depth - 1];
  --depth;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  a = eax;
  b = BB_U32(ebp + 0x8u);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  goto L_464d86;
L_464d86:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_random_range_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -12(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_random_range_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "12(%esp),%esp;ret $0");
}
#endif
void bb_sample_random_clock_seed_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00464d91 indirect */
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_seed_entry), 0); /* 00464d98 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_464da5;
L_464da5:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
uint32_t __attribute__((naked)) bb_sample_random_clock_seed_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_random_clock_seed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_pitch_scale_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 20u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_465f50;
  a = BB_U32(ebp + 0x8u);
  b = 0xffffffe8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465f40;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  fp[depth] = F64(eax * 8u + 0x48c060u);
  ++depth;
  goto L_465f93;
  goto L_465f4b;
L_465f40:;
  fp[depth] = F64(0x48c120u);
  ++depth;
  goto L_465f93;
L_465f4b:;
  goto L_465f93;
L_465f50:;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_465f88;
  a = BB_U32(ebp + 0x8u);
  b = 0x18u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_465f78;
  eax = BB_U32(ebp + 0x8u);
  fp[depth] = F64(eax * 8u + 0x48bd60u);
  ++depth;
  goto L_465f93;
  goto L_465f83;
L_465f78:;
  fp[depth] = F64(0x48be20u);
  ++depth;
  goto L_465f93;
L_465f83:;
  goto L_465f93;
L_465f88:;
  fp[depth] = F64(0x4823b8u);
  ++depth;
  goto L_465f93;
L_465f93:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
  esp += 4u;
#ifdef BB_MODEL_RUNTIME
  bb_model_return_float(fp[depth - 1]);
#else
  *(volatile long double *)(uintptr_t)(input[3] + 4u) = fp[depth - 1];
#endif
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
bb_sample_pitch_scale_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -16(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_pitch_scale_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "16(%esp),%esp;fldt -16(%esp);ret $0");
}
#endif
void bb_sample_random_seed_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(0x48cba4u) = eax;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += (ebp - esp) / 4u;
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
bb_sample_random_seed_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_random_seed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "4(%esp),%esp;ret $0");
}
#endif
