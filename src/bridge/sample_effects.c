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
/* C forms of timed sound-effect envelopes and playback sequences.
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

void bb_sample_oscillation_step_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
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
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00464e15 indirect */
  BB_U32(ebp + 0xfffffffcu) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  eax = (eax + BB_U32(ebp + 0xcu));
  a = eax;
  b = BB_U32(ebp + 0xfffffffcu);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_464ee9;
  eax = BB_U32(ebp + 0xfffffffcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx) = eax;
  eax = BB_U32(ebp + 0x14u);
  a = BB_U32(ebp + 0x18u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464e48;
  goto L_464eee;
L_464e48:;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x14u);
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464e63;
  eax = BB_U32(ebp + 0x14u);
  ecx = BB_U32(ebp + 0x1cu);
  BB_U32(ecx) = eax;
  goto L_464e79;
L_464e63:;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x18u);
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_464e79;
  eax = BB_U32(ebp + 0x18u);
  ecx = BB_U32(ebp + 0x1cu);
  BB_U32(ecx) = eax;
L_464e79:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x4u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464eb8;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x18u);
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_464ea1;
  eax = BB_U32(ebp + 0x10u);
  ecx = BB_U32(ebp + 0x1cu);
  a = BB_U32(ecx);
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(ecx) = result;
  goto L_464eb3;
L_464ea1:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x4u) = 0xffffffffu;
  eax = BB_U32(ebp + 0x18u);
  ecx = BB_U32(ebp + 0x1cu);
  BB_U32(ecx) = eax;
L_464eb3:;
  goto L_464ee9;
L_464eb8:;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x14u);
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_464ed7;
  eax = (eax ^ eax);
  eax = (eax - BB_U32(ebp + 0x10u));
  eax = (0u - eax);
  ecx = BB_U32(ebp + 0x1cu);
  a = BB_U32(ecx);
  b = eax;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(ecx) = result;
  goto L_464ee9;
L_464ed7:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x4u) = 0x1u;
  eax = BB_U32(ebp + 0x14u);
  ecx = BB_U32(ebp + 0x1cu);
  BB_U32(ecx) = eax;
L_464ee9:;
  goto L_464eee;
L_464eee:;
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
bb_sample_oscillation_step_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused)),
                                 uint32_t arg2 __attribute__((unused)),
                                 uint32_t arg3 __attribute__((unused)),
                                 uint32_t arg4 __attribute__((unused)),
                                 uint32_t arg5 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_oscillation_step_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_oscillation_init_c(uint32_t input[9]) {
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
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x4u) = eax;
  goto L_464f07;
L_464f07:;
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
bb_sample_oscillation_init_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_oscillation_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_ramp_step_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
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
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00464f15 indirect */
  BB_U32(ebp + 0xfffffffcu) = eax;
  a = BB_U32(ebp + 0x14u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464f40;
  eax = BB_U32(ebp + 0x18u);
  eax = BB_U32(eax);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx) = eax;
  eax = BB_U32(ebp + 0xfffffffcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x4u) = eax;
  goto L_464fac;
L_464f40:;
  a = BB_U32(ebp + 0x14u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464fac;
  eax = BB_U32(ebp + 0xfffffffcu);
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax - BB_U32(ecx + 0x4u));
  a = eax;
  b = BB_U32(ebp + 0x10u);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_464f93;
  eax = BB_U32(ebp + 0x18u);
  ecx = BB_U32(ebp + 0xcu);
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_464f8e;
  eax = BB_U32(ebp + 0xfffffffcu);
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax - BB_U32(ecx + 0x4u));
  ecx = BB_U32(ebp + 0xcu);
  edx = BB_U32(ebp + 0x8u);
  ecx = (ecx - BB_U32(edx));
  a = eax;
  b = ecx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)((int32_t)eax >> 31);
  b = BB_U32(ebp + 0x10u);
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  ecx = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ecx);
  a = ecx;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  eax = BB_U32(ebp + 0x18u);
  BB_U32(eax) = ecx;
L_464f8e:;
  goto L_464fac;
L_464f93:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  ecx = BB_U32(ebp + 0x18u);
  BB_U32(ecx) = eax;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_464fb6;
L_464fac:;
  eax = 0x1u;
  goto L_464fb6;
L_464fb6:;
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
bb_sample_ramp_step_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused)),
                          uint32_t arg2 __attribute__((unused)),
                          uint32_t arg3 __attribute__((unused)),
                          uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_ramp_step_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_random_ramp_step_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
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
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  a = BB_U32(ebp + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464fd3;
  goto L_4650bc;
L_464fd3:;
  a = BB_U32(ebp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_464feb;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax) = 0x0u;
  goto L_4650b7;
L_464feb:;
  a = BB_U32(ebp + 0xcu);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4650b7;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  BB_U32(ebp + 0xfffffffcu) = eax;
  goto L_46509e;
L_465002:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x4u) = 0x0u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00465014 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0xcu) = eax;
  eax = BB_U32(ebp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0046502a */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x10u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax) = 0x1u;
  goto L_4650b7;
L_465046:;
  eax = BB_U32(ebp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x14u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465066 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x8u) = eax;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46508a;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax) = 0x0u;
L_46508a:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x4u) = 0x1u;
  goto L_4650b7;
  goto L_4650b7;
L_46509e:;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_465002;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_465046;
  goto L_4650b7;
L_4650b7:;
  goto L_4650bc;
L_4650bc:;
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
bb_sample_random_ramp_step_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused)),
                                 uint32_t arg2 __attribute__((unused)),
                                 uint32_t arg3 __attribute__((unused)),
                                 uint32_t arg4 __attribute__((unused)),
                                 uint32_t arg5 __attribute__((unused)),
                                 uint32_t arg6 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_random_ramp_step_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_loop_fade_step_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 32u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 44u), 44u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = esp;
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
  eax = BB_U32(ebp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state call = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&call, BB_AUDIO_TARGET(bb_sample_gain_entry), args + sp, esp, edi, esi, ebp, ebx, &fp[depth++]);
    eax = call.eax;
    ecx = call.ecx;
    edx = call.edx;
    flags = call.flags;
  }
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  F64(ebp + 0xfffffff8u) = (double)(fp[depth - 1]);
  --depth;
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4650ed;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
L_4650ed:;
  a = BB_U32(ebp + 0x10u);
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46517f;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bd00u;
  esp -= 4u;
  SAMPLE_CALL(0x46f5e0u, 0); /* 00465100 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_465117;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
L_465117:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x30u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_465178;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x20u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46515f;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x0u;
  eax = BB_U32(ebp + 0x20u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00465144 indirect */
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x38u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x3u;
  goto L_465169;
L_46515f:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x4u;
L_465169:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x30u) = 0x1u;
  goto L_46517f;
L_465178:;
  BB_U32(ebp + 0x10u) = 0x1u;
L_46517f:;
  a = BB_U32(ebp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4651d6;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bd10u;
  esp -= 4u;
  SAMPLE_CALL(0x46f5e0u, 0); /* 00465192 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4651d1;
  args[--sp] = 0x48bd20u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46f4f0u, 0); /* 004651ab */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x2cu) = 0x1u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x30u) = 0x0u;
L_4651d1:;
  goto L_4655f4;
L_4651d6:;
  a = BB_U32(ebp + 0x10u);
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46525b;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bd30u;
  esp -= 4u;
  SAMPLE_CALL(0x46f5e0u, 0); /* 004651e9 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_465200;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
L_465200:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x24u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46523e;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 0046521d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 0046522c */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x24u) = 0xffffffffu;
L_46523e:;
  args[--sp] = 0x48bd40u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46f4f0u, 0); /* 00465247 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
  goto L_4655f4;
L_46525b:;
  a = BB_U32(ebp + 0x10u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4655f4;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bd44u;
  esp -= 4u;
  SAMPLE_CALL(0x46f5e0u, 0); /* 0046526e */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_465285;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
L_465285:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x20u);
  BB_U32(ebp + 0xfffffff4u) = eax;
  goto L_4655cc;
L_465293:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reserve_entry), 0); /* 00465297 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x24u) = eax;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x24u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4652ff;
  eax = BB_U32(ebp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 004652c6 */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ecx + 0x24u);
  a = ecx;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  ecx = result;
  BB_U32(ecx + 0x6c19f0u) = eax;
  eax = BB_U32(ebp + 0x18u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x1u;
  goto L_465309;
L_4652ff:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x4u;
L_465309:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x2cu) = 0x1u;
  goto L_4655f4;
L_465318:;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x34u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x40u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465336 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4653ba;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x1u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x34u);
  BB_U32(ebp + 0xfffffff0u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xfffffff0u));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465375 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 00465391 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 004653ad */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_4653e3;
L_4653ba:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x0u;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 004653cd indirect */
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x38u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x2u;
L_4653e3:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x2cu) = 0x1u;
  goto L_4655f4;
L_4653f2:;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 004653f2 indirect */
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax - BB_U32(ecx + 0x38u));
  ecx = BB_U32(ebp + 0x8u);
  a = eax;
  b = BB_U32(ecx + 0x3cu);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_46542e;
  eax = BB_U32(ebp + 0x20u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00465413 indirect */
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x38u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x3u;
  goto L_465493;
L_46542e:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x34u);
  BB_U32(ebp + 0xffffffecu) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffecu));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465453 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 0046546f */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 0046548b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_465493:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x2cu) = 0x1u;
  goto L_4655f4;
L_4654a2:;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x34u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x40u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 004654c0 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_465544;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x1u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x34u);
  BB_U32(ebp + 0xffffffe8u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffe8u));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 004654ff */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 0046551b */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00465537 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_46558c;
L_465544:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x24u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465582;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00465561 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 00465570 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x24u) = 0xffffffffu;
L_465582:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x20u) = 0x4u;
L_46558c:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x2cu) = 0x1u;
  goto L_4655f4;
L_46559b:;
  args[--sp] = 0x48bd54u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x46f4f0u, 0); /* 004655a4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x30u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x2cu) = 0x0u;
L_4655c0:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
  goto L_4655f4;
L_4655cc:;
  a = BB_U32(ebp + 0xfffffff4u);
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_4655c0;
  eax = BB_U32(ebp + 0xfffffff4u);
  switch (eax) {
  case 0:
    goto L_465293;
  case 1:
    goto L_465318;
  case 2:
    goto L_4653f2;
  case 3:
    goto L_4654a2;
  case 4:
    goto L_46559b;
  default:
    __builtin_unreachable();
  }
L_4655f4:;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x48bd58u;
  esp -= 4u;
  SAMPLE_CALL(0x46f5e0u, 0); /* 004655fd */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_465619;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_465624;
  goto L_465624;
L_465619:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x2cu);
  goto L_465624;
L_465624:;
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
bb_sample_loop_fade_step_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused)),
                               uint32_t arg3 __attribute__((unused)),
                               uint32_t arg4 __attribute__((unused)),
                               uint32_t arg5 __attribute__((unused)),
                               uint32_t arg6 __attribute__((unused)),
                               uint32_t arg7 __attribute__((unused)),
                               uint32_t arg8 __attribute__((unused)),
                               uint32_t arg9 __attribute__((unused))) {
  __asm__ volatile("leal -28(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_loop_fade_step_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "28(%esp),%esp;ret $0");
}
#endif
void bb_sample_sequence_init_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 40u), 40u);
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
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_465641;
  goto L_4656e7;
L_465641:;
  eax = BB_U32(ebp + 0x14u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx) = eax;
  eax = BB_U32(ebp + 0x18u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x4u) = eax;
  eax = BB_U32(ebp + 0x1cu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x8u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0xcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x10u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x14u) = 0x1u;
  eax = BB_U32(ebp + 0x28u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x18u) = eax;
  eax = BB_U32(ebp + 0x24u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x1cu) = eax;
  eax = BB_U32(ebp + 0x20u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x20u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x24u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x0u;
  eax = BB_U32(ebp + 0x10u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x2cu) = eax;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x30u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x34u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x3cu) = 0xffffffffu;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x40u) = 0xffffffffu;
  goto L_4656e7;
L_4656e7:;
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
bb_sample_sequence_init_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused)),
                              uint32_t arg3 __attribute__((unused)),
                              uint32_t arg4 __attribute__((unused)),
                              uint32_t arg5 __attribute__((unused)),
                              uint32_t arg6 __attribute__((unused)),
                              uint32_t arg7 __attribute__((unused)),
                              uint32_t arg8 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_sequence_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_sequence_stop_c(uint32_t input[9]) {
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
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_465704;
  goto L_465799;
L_465704:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x3cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465742;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00465721 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 00465730 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x3cu) = 0xffffffffu;
L_465742:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x40u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465780;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x40u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 0046575f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x40u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 0046576e */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x40u) = 0xffffffffu;
L_465780:;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x34u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x0u;
  goto L_465799;
L_465799:;
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
bb_sample_sequence_stop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_sequence_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_sequence_volume_c(uint32_t input[9]) {
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
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4657b6;
  goto L_4657c4;
L_4657b6:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x20u) = eax;
  goto L_4657c4;
L_4657c4:;
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
bb_sample_sequence_volume_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_sequence_volume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_sequence_pan_c(uint32_t input[9]) {
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
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4657e1;
  goto L_4657ef;
L_4657e1:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x18u) = eax;
  goto L_4657ef;
L_4657ef:;
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
bb_sample_sequence_pan_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_sequence_pan_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_sequence_pitch_c(uint32_t input[9]) {
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
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46580c;
  goto L_46581a;
L_46580c:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x1cu) = eax;
  goto L_46581a;
L_46581a:;
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
bb_sample_sequence_pitch_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_sequence_pitch_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_sequence_loops_c(uint32_t input[9]) {
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
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_465837;
  goto L_465845;
L_465837:;
  eax = BB_U32(ebp + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x14u) = eax;
  goto L_465845;
L_465845:;
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
bb_sample_sequence_loops_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_sequence_loops_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "4(%esp),%esp;ret $0");
}
#endif
void bb_sample_continuous_step_c(uint32_t input[9]) {
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
    goto L_465865;
  goto L_4659d1;
L_465865:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x34u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_465893;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x34u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46588e;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x34u) = 0x1u;
  goto L_465893;
L_46588e:;
  goto L_4659d1;
L_465893:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x38u);
  BB_U32(ebp + 0xfffffffcu) = eax;
  goto L_4659b3;
L_4658a1:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reserve_entry), 0); /* 004658a8 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x3cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46590e;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  SAMPLE_CALL(BB_U32(eax + 0x2cu), 0); /* 004658e2 indirect */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 004658ed */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ecx + 0x3cu);
  ecx = (ecx << 5);
  BB_U32(ecx + 0x6c19f0u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x1u;
L_46590e:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 0046591e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46594c;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 00465935 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x0u;
  goto L_4659a9;
L_46594c:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465963 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 00465982 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 004659a1 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_4659a9:;
  goto L_4659cc;
  goto L_4659cc;
L_4659b3:;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4658a1;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_46590e;
  goto L_4659cc;
L_4659cc:;
  goto L_4659d1;
L_4659d1:;
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
bb_sample_continuous_step_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_continuous_step_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "8(%esp),%esp;ret $0");
}
#endif
void bb_sample_crossfade_step_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 48u;
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
  b = 0x28u;
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
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state call = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&call, BB_AUDIO_TARGET(bb_sample_gain_entry), args + sp, esp, edi, esi, ebp, ebx, &fp[depth++]);
    eax = call.eax;
    ecx = call.ecx;
    edx = call.edx;
    flags = call.flags;
  }
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  F64(ebp + 0xfffffff4u) = (double)(fp[depth - 1]);
  --depth;
  a = BB_U32(0x48bcfcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_465a0a;
  goto L_465f00;
L_465a0a:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x34u);
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_465a38;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x34u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_465a33;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x34u) = 0x2u;
  goto L_465a38;
L_465a33:;
  goto L_465f00;
L_465a38:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x38u);
  BB_U32(ebp + 0xfffffff0u) = eax;
  goto L_465ed7;
L_465a46:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00465a54 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x10u) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reserve_entry), 0); /* 00465a69 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x24u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x3cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465b0c;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  BB_U32(ebp + 0xffffffecu) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffecu));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  SAMPLE_CALL(BB_U32(eax + 0x2cu), 0); /* 00465ac0 indirect */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00465acb */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ecx + 0x3cu);
  ecx = (ecx << 5);
  BB_U32(ecx + 0x6c19f0u) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x78u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x44u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465afa */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x1u;
L_465b0c:;
  goto L_465efb;
L_465b11:;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x78u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x44u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465b29 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_465b4f;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00465b39 indirect */
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0xcu) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x2u;
L_465b4f:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  BB_U32(ebp + 0xffffffe8u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffe8u));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465b74 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 00465b93 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00465bb2 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_465efb;
L_465bbf:;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00465bbf indirect */
  ecx = BB_U32(ebp + 0x8u);
  eax = (eax - BB_U32(ecx + 0xcu));
  ecx = BB_U32(ebp + 0x8u);
  a = eax;
  b = BB_U32(ecx + 0x10u);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_465cae;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00465be5 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x10u) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reserve_entry), 0); /* 00465bfa */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x40u) = eax;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x40u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465ca9;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x28u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffe4u));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  SAMPLE_CALL(BB_U32(eax + 0x2cu), 0); /* 00465c3d indirect */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00465c48 */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ecx + 0x40u);
  ecx = (ecx << 5);
  BB_U32(ecx + 0x6c19f0u) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x78u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x4cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465c77 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x44u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465c97 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x3u;
L_465ca9:;
  goto L_465d19;
L_465cae:;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  BB_U32(ebp + 0xffffffe0u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffe0u));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465cd3 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 00465cf2 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00465d11 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_465d19:;
  goto L_465efb;
L_465d1e:;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x44u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465d36 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  ebx = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = (eax + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x78u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = 0x4cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00465d58 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  ebx = (ebx + eax);
  BB_U32(ebp + 0xfffffffcu) = ebx;
  a = BB_U32(ebp + 0xfffffffcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_465dd8;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00465d7f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 00465d8e */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00465d96 indirect */
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0xcu) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x40u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x3cu) = eax;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x28u);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x24u) = eax;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x40u) = 0xffffffffu;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x28u) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(eax + 0x38u) = 0x2u;
L_465dd8:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x40u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465e50;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x28u);
  BB_U32(ebp + 0xffffffdcu) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffdcu));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x40u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465e0a */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x40u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 00465e29 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x40u);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00465e48 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_465e50:;
  eax = BB_U32(ebp + 0x8u);
  a = BB_U32(eax + 0x3cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_465ec8;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x24u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(ebp + 0xffffffd8u));
  fp[depth - 1] = fp[depth - 1] * F64(ebp + 0xfffffff4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00465e82 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pitch_entry), 0); /* 00465ea1 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp + 0x8u);
  eax = BB_U32(eax + 0x3cu);
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  eax = BB_U32(eax + 0x6c19f0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00465ec0 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_465ec8:;
  goto L_465efb;
  goto L_465efb;
  goto L_465efb;
L_465ed7:;
  a = BB_U32(ebp + 0xfffffff0u);
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_465efb;
  eax = BB_U32(ebp + 0xfffffff0u);
  switch (eax) {
  case 0:
    goto L_465a46;
  case 1:
    goto L_465b11;
  case 2:
    goto L_465bbf;
  case 3:
    goto L_465d1e;
  default:
    __builtin_unreachable();
  }
L_465efb:;
  goto L_465f00;
L_465f00:;
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
bb_sample_crossfade_step_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -44(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_crossfade_step_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "44(%esp),%esp;ret $0");
}
#endif
