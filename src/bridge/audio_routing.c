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
/* First C forms of sound routing and sample scheduling. Address-derived labels
 * retain provenance for auditing distance, voice selection and callback
 * ordering.
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
#define AUDIO_CALL(target, consumed)                                           \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                     \
    BB_AUDIO_CALL_WORDS(&call, target, args + sp, esp, edi, esi, ebp, ebx);                         \
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
#define audio_float_to_int sample_float_to_int
#define audio_compare sample_compare
#else
/* Preserve the original x87 truncation boundary, including control-word
 * restoration and 64-bit invalid/overflow results used by original callers. */
static void audio_float_to_int(struct bb_call_state *state,
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
#endif
/* Resource voice status/position queries and sample lookup expose their
 * original local outputs to callbacks. Entry wrappers protect those sixteen
 * bytes while the C implementations use ordinary argument/register storage. */
void bb_sound_play_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  ecx = BB_U32((uintptr_t)(args + sp) + 4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41573f;
  args[--sp] = 0x0u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sound_resource_play_entry), 4); /* 0041573a */
L_41573f:;
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
uint32_t __attribute__((naked)) bb_sound_play_entry(uint32_t arg0
                                                    __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_play_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_sound_position_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32((uintptr_t)(args + sp) + 12u);
  args[--sp] = edi;
  esp -= 4u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_415850;
  edi = BB_U32((uintptr_t)(args + sp) + 20u);
  eax = BB_U32(0x4846e4u);
  ecx = edi;
  ecx = (ecx - eax);
  a = ecx;
  b = 0x1eu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_415850;
  ebx = BB_U32((uintptr_t)(args + sp) + 24u);
  edx = BB_U32(0x4846e8u);
  eax = ebx;
  eax = (eax - edx);
  a = eax;
  b = 0x1eu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_415850;
  a = ecx;
  b = 0xffffffe2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_415850;
  a = eax;
  b = 0xffffffe2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_415850;
  args[--sp] = edx;
  esp -= 4u;
  ecx = BB_U32(0x4846e4u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_entity_distance), 0); /* 00415798 */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  ecx = 0x0u;
  BB_U32(0x6b08f4u) = eax;
  a = eax;
  b = 0x60000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4157cb;
  eax = (eax - 0x60000u);
  BB_U32(0x6b08f4u) = eax;
  eax = ((uint32_t)((int32_t)(eax) >> 16));
  a = eax;
  b = eax;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (eax + eax * 4u);
  ecx = (edx * 2u);
L_4157cb:;
  eax = BB_U32(0x4846e4u);
  edx = BB_U32(0x4846e8u);
  edi = (edi - eax);
  edi = (edi - ebx);
  edi = (edi + edx);
  edi = (edi << 2);
  eax = (edi + edi * 4u);
  edx = (eax + eax * 4u);
  BB_U32(0x6b08f0u) = edx;
  a = edx;
  b = 0xffffe0c0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4157f8;
  edx = 0xffffe0c0u;
L_4157f8:;
  BB_U32(0x6b08f0u) = edx;
  a = edx;
  b = 0x1f40u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_41580b;
  edx = 0x1f40u;
L_41580b:;
  BB_U32(0x6b08f8u) = ecx;
  a = ecx;
  b = 0x1f40u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_41581e;
  ecx = 0x1f40u;
L_41581e:;
  BB_U32(0x6b08f0u) = edx;
  BB_U32(0x6b08f8u) = ecx;
  a = ecx;
  b = 0x1f40u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_415850;
  args[--sp] = edx;
  esp -= 4u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sound_resource_pan_entry), 4); /* 00415835 */
  eax = BB_U32(0x6b08f8u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sound_resource_volume_entry), 4); /* 00415842 */
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sound_resource_play_entry), 4); /* 0041584b */
L_415850:;
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
bb_sound_position_entry(uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused)),
                        uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_position_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_sample_play_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32((uintptr_t)(args + sp) + 4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_415877;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_schedule_entry), 0); /* 0041586f */
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_415877:;
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
uint32_t __attribute__((naked)) bb_sample_play_entry(uint32_t arg0
                                                     __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_play_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_sample_position_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double fp0 = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32((uintptr_t)(args + sp) + 16u);
  args[--sp] = edi;
  esp -= 4u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4159b3;
  edi = BB_U32((uintptr_t)(args + sp) + 24u);
  eax = BB_U32(0x4846e4u);
  ecx = edi;
  ecx = (ecx - eax);
  a = ecx;
  b = 0x1eu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4159b3;
  ebx = BB_U32((uintptr_t)(args + sp) + 28u);
  edx = BB_U32(0x4846e8u);
  eax = ebx;
  eax = (eax - edx);
  a = eax;
  b = 0x1eu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4159b3;
  a = ecx;
  b = 0xffffffe2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4159b3;
  a = eax;
  b = 0xffffffe2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4159b3;
  args[--sp] = edx;
  esp -= 4u;
  ecx = BB_U32(0x4846e4u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_entity_distance), 0); /* 004158db */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  ecx = 0x0u;
  BB_U32(0x6b0908u) = eax;
  a = eax;
  b = 0x60000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_41590e;
  eax = (eax - 0x60000u);
  BB_U32(0x6b0908u) = eax;
  eax = ((uint32_t)((int32_t)(eax) >> 16));
  a = eax;
  b = eax;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (eax + eax * 4u);
  ecx = (edx * 2u);
L_41590e:;
  eax = BB_U32(0x4846e4u);
  edx = BB_U32(0x4846e8u);
  edi = (edi - eax);
  edi = (edi - ebx);
  edi = (edi + edx);
  edi = (edi << 2);
  eax = (edi + edi * 4u);
  edx = (eax + eax * 4u);
  BB_U32(0x6b0900u) = edx;
  a = edx;
  b = 0xffffe0c0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_41593b;
  edx = 0xffffe0c0u;
L_41593b:;
  BB_U32(0x6b0900u) = edx;
  a = edx;
  b = 0x1f40u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_41594e;
  edx = 0x1f40u;
L_41594e:;
  BB_U32(0x6b090cu) = ecx;
  a = ecx;
  b = 0x1f40u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_415961;
  ecx = 0x1f40u;
L_415961:;
  edi = 0x1f40u;
  BB_U32(0x6b0900u) = edx;
  edi = (edi - ecx);
  BB_U32(0x6b090cu) = ecx;
  BB_U32(0x6b0904u) = edi;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4159b3;
  a = edx;
  b = 0x1f40u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edx = result;
  BB_U32((uintptr_t)(args + sp) + 12u) = edx;
  fp0 = (long double)(int32_t)(BB_U32((uintptr_t)(args + sp) + 12u));
  fp0 *= (long double)BB_DOUBLE(0x4821d0u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    audio_float_to_int(&converted, &fp0);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x1f40u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = edi;
  eax = (eax << 7);
  a = eax;
  b = edi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
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
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_schedule_entry), 0); /* 004159ab */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
L_4159b3:;
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
bb_sample_position_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused)),
                         uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_position_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_sound_resource_play_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  AUDIO_CALL(BB_U32(0x6ca378u), 0); /* 00402215 indirect */
  ecx = eax;
  edx = BB_U32(esi + 0x3au);
  ecx = (ecx - edx);
  a = ecx;
  b = 0x12cu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_402232;
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
L_402232:;
  BB_U32(esi + 0x3au) = eax;
  eax = BB_U32(esi + 0x36u);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_402287;
  eax = BB_U32(esi + 0x2cu);
  ecx = BB_U32(esi + 0x30u);
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x20u);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voice_play_entry), 0); /* 00402250 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x28u) = eax;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voice_duration_entry), 0); /* 0040225c */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = 0x3e8u;
  edx = (edx - edx);
  BB_U32(esi + 0x7eu) = eax;
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  edi = args[sp++];
  esp += 4u;
  eax = (eax + eax * 8u);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BB_U32(esi + 0x82u) = eax;
  eax = 0x1u;
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
L_402287:;
  ebx = BB_U32((uintptr_t)(args + sp) + 16u);
  edi = (esi + 0x34u);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_402297;
  BYTE(edi) = 0x0u;
  goto L_4022a0;
L_402297:;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sound_resource_voice_entry), 0); /* 00402299 */
  BYTE(edi) = ((uint8_t)(eax));
L_4022a0:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi)) & 0xffu) << 0);
  eax = BB_U32(esi + eax * 4u + 0x10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4022b4;
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
L_4022b4:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4022cf;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(eax);
  AUDIO_CALL(BB_U32(eax + 0x30u), 16); /* 004022c1 indirect */
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
L_4022cf:;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(eax);
  AUDIO_CALL(BB_U32(eax + 0x34u), 8); /* 004022d4 indirect */
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi)) & 0xffu) << 0);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(esi + eax * 4u + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(eax);
  AUDIO_CALL(BB_U32(ebx + 0x30u), 16); /* 004022e8 indirect */
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked, thiscall))
bb_sound_resource_play_entry(void *object __attribute__((unused)),
                             uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_resource_play_c) ";leal 4(%esp),%esp;popal;popfl;ret $4");
}
#endif
void bb_sound_resource_pan_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x36u);
  esi = ecx;
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_40231c;
  edi = BB_U32((uintptr_t)(args + sp) + 12u);
  eax = 0x1u;
  BB_U32(esi + 0x30u) = edi;
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
L_40231c:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x34u)) & 0xffu) << 0);
  eax = BB_U32(esi + eax * 4u + 0x10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_402330;
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
L_402330:;
  edi = BB_U32((uintptr_t)(args + sp) + 12u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(eax);
  AUDIO_CALL(BB_U32(eax + 0x40u), 8); /* 00402338 indirect */
  eax = 0x1u;
  BB_U32(esi + 0x30u) = edi;
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
uint32_t __attribute__((naked, thiscall))
bb_sound_resource_pan_entry(void *object __attribute__((unused)),
                            uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_resource_pan_c) ";leal 4(%esp),%esp;popal;popfl;ret $4");
}
#endif
void bb_sound_resource_volume_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32(ecx + 0x36u);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_402366;
  edx = BB_U32((uintptr_t)(args + sp) + 4u);
  eax = 0x1u;
  BB_U32(ecx + 0x2cu) = edx;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_402366:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0x34u)) & 0xffu) << 0);
  edx = BB_U32(ecx + eax * 4u + 0x10u);
  eax = (ecx + eax * 4u + 0x10u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_40237c;
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
L_40237c:;
  edx = BB_U32((uintptr_t)(args + sp) + 4u);
  BB_U32(ecx + 0x2cu) = edx;
  a = edx;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edx = result;
  eax = BB_U32(eax);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(eax);
  AUDIO_CALL(BB_U32(eax + 0x3cu), 8); /* 0040238b indirect */
  eax = 0x1u;
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
uint32_t __attribute__((naked, thiscall))
bb_sound_resource_volume_entry(void *object __attribute__((unused)),
                               uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_resource_volume_c) ";leal 4(%esp),%esp;popal;popfl;ret $4");
}
#endif
void bb_sound_resource_voice_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 20u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = 0xffffffffu;
  args[--sp] = ebp;
  esp -= 4u;
  edi = (edi ^ edi);
  ebp = (ebp ^ ebp);
  esi = (ecx + 0x10u);
  BB_U32(esp + 0x14u) = edi;
L_4023b7:;
  eax = BB_U32(esi);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4023f1;
  ecx = (esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(eax);
  AUDIO_CALL(BB_U32(eax + 0x24u), 8); /* 004023c5 indirect */
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4023fc;
  ecx = BB_U32(esi);
  eax = (esp + 0x1cu);
  edx = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = ecx;
  esp -= 4u;
  AUDIO_CALL(BB_U32(eax + 0x10u), 12); /* 004023e0 indirect */
  ecx = BB_U32(esp + 0x10u);
  a = ecx;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_4023f1;
  ebp = ecx;
  BB_U32(esp + 0x14u) = edi;
L_4023f1:;
  esi = (esi + 0x4u);
  edi = (edi + 1u);
  a = edi;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4023b7;
  goto L_4023fe;
L_4023fc:;
  ebx = edi;
L_4023fe:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_402406;
  ebx = BB_U32(esp + 0x14u);
L_402406:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
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
uint32_t __attribute__((naked, thiscall))
bb_sound_resource_voice_entry(void *object __attribute__((unused))) {
  __asm__ volatile("leal -16(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_resource_voice_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "16(%esp),%esp;ret $0");
}
#endif
void bb_sample_lookup_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32((uintptr_t)(args + sp) + 16u);
  args[--sp] = edi;
  esp -= 4u;
  ebx = BB_U32((uintptr_t)(args + sp) + 28u);
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32((uintptr_t)(args + sp) + 28u);
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  eax = (eax - 1u);
  BB_U32(esi) = 0x80u;
  BB_U32(ebx) = 0x80u;
  a = eax;
  b = 0x54u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(edi) = 0x0u;
  if (!(flags & 0x41u))
    goto L_456353;
  switch (eax) {
  case 0:
    goto L_45635c;
  case 1:
    goto L_456368;
  case 2:
    goto L_456353;
  case 3:
    goto L_456374;
  case 4:
    goto L_456380;
  case 5:
    goto L_45638c;
  case 6:
    goto L_456398;
  case 7:
    goto L_45641d;
  case 8:
    goto L_456429;
  case 9:
    goto L_456435;
  case 10:
    goto L_456441;
  case 11:
    goto L_45644d;
  case 12:
    goto L_456459;
  case 13:
    goto L_456465;
  case 14:
    goto L_456471;
  case 15:
    goto L_45647d;
  case 16:
    goto L_456489;
  case 17:
    goto L_456495;
  case 18:
    goto L_4564a1;
  case 19:
    goto L_456353;
  case 20:
    goto L_4564ad;
  case 21:
    goto L_4564b9;
  case 22:
    goto L_456505;
  case 23:
    goto L_456551;
  case 24:
    goto L_45655d;
  case 25:
    goto L_456569;
  case 26:
    goto L_456353;
  case 27:
    goto L_456353;
  case 28:
    goto L_456353;
  case 29:
    goto L_456575;
  case 30:
    goto L_456581;
  case 31:
    goto L_45658d;
  case 32:
    goto L_456599;
  case 33:
    goto L_4565a5;
  case 34:
    goto L_456353;
  case 35:
    goto L_4565b3;
  case 36:
    goto L_4565bf;
  case 37:
    goto L_4565cb;
  case 38:
    goto L_456353;
  case 39:
    goto L_4565d7;
  case 40:
    goto L_456353;
  case 41:
    goto L_456353;
  case 42:
    goto L_4565e0;
  case 43:
    goto L_4565ec;
  case 44:
    goto L_4565f8;
  case 45:
    goto L_456353;
  case 46:
    goto L_456604;
  case 47:
    goto L_456610;
  case 48:
    goto L_45661c;
  case 49:
    goto L_456628;
  case 50:
    goto L_456353;
  case 51:
    goto L_456634;
  case 52:
    goto L_456640;
  case 53:
    goto L_456652;
  case 54:
    goto L_45665e;
  case 55:
    goto L_456353;
  case 56:
    goto L_45666a;
  case 57:
    goto L_456353;
  case 58:
    goto L_456353;
  case 59:
    goto L_4566a3;
  case 60:
    goto L_4566af;
  case 61:
    goto L_4566bb;
  case 62:
    goto L_4566c7;
  case 63:
    goto L_4566d3;
  case 64:
    goto L_456353;
  case 65:
    goto L_4566df;
  case 66:
    goto L_4566eb;
  case 67:
    goto L_4566f7;
  case 68:
    goto L_456703;
  case 69:
    goto L_45670f;
  case 70:
    goto L_45671b;
  case 71:
    goto L_456727;
  case 72:
    goto L_456733;
  case 73:
    goto L_45673f;
  case 74:
    goto L_456353;
  case 75:
    goto L_45674b;
  case 76:
    goto L_456795;
  case 77:
    goto L_4567a1;
  case 78:
    goto L_456353;
  case 79:
    goto L_456353;
  case 80:
    goto L_4567ad;
  case 81:
    goto L_456353;
  case 82:
    goto L_456353;
  case 83:
    goto L_456353;
  case 84:
    goto L_4567b9;
  default:
    __builtin_unreachable();
  }
L_456353:;
  a = ebp;
  b = ebp;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebp = result;
  eax = ebp;
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
L_45635c:;
  ebp = 0x17u;
  eax = ebp;
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
L_456368:;
  ebp = 0x2eu;
  eax = ebp;
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
L_456374:;
  ebp = 0x24u;
  eax = ebp;
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
L_456380:;
  ebp = 0x2au;
  eax = ebp;
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
L_45638c:;
  ebp = 0x2fu;
  eax = ebp;
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
L_456398:;
  edi = BB_U32(0x6ca378u);
  AUDIO_CALL(edi, 0); /* 0045639e indirect */
  ecx = BB_U32(0x488b80u);
  edx = BB_U32(0x488b84u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_456414;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004563b6 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4563ce;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4563d5;
  ebp = 0x4fu;
  goto L_4563da;
L_4563ce:;
  ebp = 0x4fu;
  goto L_4563da;
L_4563d5:;
  ebp = 0x50u;
L_4563da:;
  args[--sp] = 0x64u;
  esp -= 4u;
  args[--sp] = 0x32u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004563de */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi) = eax;
  BB_U32(ebx) = 0xau;
  AUDIO_CALL(edi, 0); /* 004563ee indirect */
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(0x488b80u) = eax;
  args[--sp] = 0xfau;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004563ff */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488b84u) = eax;
  eax = ebp;
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
L_456414:;
  a = ebp;
  b = ebp;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebp = result;
  eax = ebp;
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
L_45641d:;
  ebp = 0x37u;
  eax = ebp;
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
L_456429:;
  ebp = 0x38u;
  eax = ebp;
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
L_456435:;
  ebp = 0x40u;
  eax = ebp;
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
L_456441:;
  ebp = 0x1au;
  eax = ebp;
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
L_45644d:;
  ebp = 0x45u;
  eax = ebp;
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
L_456459:;
  ebp = 0x46u;
  eax = ebp;
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
L_456465:;
  ebp = 0x3eu;
  eax = ebp;
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
L_456471:;
  ebp = 0x16u;
  eax = ebp;
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
L_45647d:;
  ebp = 0x32u;
  eax = ebp;
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
L_456489:;
  ebp = 0x30u;
  eax = ebp;
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
L_456495:;
  ebp = 0x51u;
  eax = ebp;
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
L_4564a1:;
  ebp = 0x42u;
  eax = ebp;
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
L_4564ad:;
  ebp = 0x18u;
  eax = ebp;
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
L_4564b9:;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004564bd */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4564d5;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4564dc;
  ebp = 0x56u;
  goto L_4564e1;
L_4564d5:;
  ebp = 0x56u;
  goto L_4564e1;
L_4564dc:;
  ebp = 0x1cu;
L_4564e1:;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004564e5 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(edi) = eax;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004564f3 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi) = eax;
  eax = ebp;
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
L_456505:;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456509 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456521;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456528;
  ebp = 0x1bu;
  goto L_45652d;
L_456521:;
  ebp = 0x1bu;
  goto L_45652d;
L_456528:;
  ebp = 0x57u;
L_45652d:;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456531 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(edi) = eax;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045653f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi) = eax;
  eax = ebp;
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
L_456551:;
  ebp = 0x13u;
  eax = ebp;
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
L_45655d:;
  ebp = 0x44u;
  eax = ebp;
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
L_456569:;
  ebp = 0x1eu;
  eax = ebp;
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
L_456575:;
  ebp = 0x54u;
  eax = ebp;
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
L_456581:;
  ebp = 0x3cu;
  eax = ebp;
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
L_45658d:;
  ebp = 0x2bu;
  eax = ebp;
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
L_456599:;
  ebp = 0x29u;
  eax = ebp;
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
L_4565a5:;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sound_selector_457d60_entry), 0); /* 004565a5 */
  ebp = eax;
  eax = ebp;
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
L_4565b3:;
  ebp = 0x3bu;
  eax = ebp;
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
L_4565bf:;
  ebp = 0x15u;
  eax = ebp;
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
L_4565cb:;
  ebp = 0x14u;
  eax = ebp;
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
L_4565d7:;
  a = ebp;
  b = ebp;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebp = result;
  eax = ebp;
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
L_4565e0:;
  ebp = 0x41u;
  eax = ebp;
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
L_4565ec:;
  ebp = 0x20u;
  eax = ebp;
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
L_4565f8:;
  ebp = 0x28u;
  eax = ebp;
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
L_456604:;
  ebp = 0x47u;
  eax = ebp;
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
L_456610:;
  ebp = 0x2du;
  eax = ebp;
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
L_45661c:;
  ebp = 0x49u;
  eax = ebp;
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
L_456628:;
  ebp = 0x35u;
  eax = ebp;
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
L_456634:;
  ebp = 0x34u;
  eax = ebp;
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
L_456640:;
  ebp = 0x37u;
  BB_U32(edi) = 0xfffffff8u;
  eax = ebp;
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
L_456652:;
  ebp = 0x23u;
  eax = ebp;
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
L_45665e:;
  ebp = 0x2cu;
  eax = ebp;
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
L_45666a:;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045666e */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45668b;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456697;
  ebp = 0x39u;
  eax = ebp;
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
L_45668b:;
  ebp = 0x39u;
  eax = ebp;
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
L_456697:;
  ebp = 0x3au;
  eax = ebp;
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
L_4566a3:;
  ebp = 0x4du;
  eax = ebp;
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
L_4566af:;
  ebp = 0x19u;
  eax = ebp;
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
L_4566bb:;
  ebp = 0x1fu;
  eax = ebp;
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
L_4566c7:;
  ebp = 0x4eu;
  eax = ebp;
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
L_4566d3:;
  ebp = 0x48u;
  eax = ebp;
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
L_4566df:;
  ebp = 0x4cu;
  eax = ebp;
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
L_4566eb:;
  ebp = 0x1du;
  eax = ebp;
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
L_4566f7:;
  ebp = 0x3du;
  eax = ebp;
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
L_456703:;
  ebp = 0x21u;
  eax = ebp;
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
L_45670f:;
  ebp = 0x1fu;
  eax = ebp;
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
L_45671b:;
  ebp = 0x12u;
  eax = ebp;
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
L_456727:;
  ebp = 0x3fu;
  eax = ebp;
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
L_456733:;
  ebp = 0x43u;
  eax = ebp;
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
L_45673f:;
  ebp = 0x4bu;
  eax = ebp;
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
L_45674b:;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045674f */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456771;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45677d;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456789;
  ebp = 0x26u;
  eax = ebp;
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
L_456771:;
  ebp = 0x26u;
  eax = ebp;
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
L_45677d:;
  ebp = 0x27u;
  eax = ebp;
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
L_456789:;
  ebp = 0x25u;
  eax = ebp;
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
L_456795:;
  ebp = 0x22u;
  eax = ebp;
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
L_4567a1:;
  ebp = 0x36u;
  eax = ebp;
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
L_4567ad:;
  ebp = 0x4au;
  eax = ebp;
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
L_4567b9:;
  ebp = 0x55u;
  eax = ebp;
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
bb_sample_lookup_entry(uint32_t arg0 __attribute__((unused)),
                       uint32_t arg1 __attribute__((unused)),
                       uint32_t arg2 __attribute__((unused)),
                       uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_lookup_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_sample_schedule_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 20u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double fp0 = 0, fp1 = 0;
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  if (!(flags & 0x40u))
    goto L_459b27;
  eax = BB_U32(0x488a64u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459b27;
  eax = (esp + 0x10u);
  ecx = (esp + 0x14u);
  edx = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esp + 0x28u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_lookup_entry), 0); /* 00459a88 */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459b27;
  eax = BB_U32(0x488d20u);
  esi = BB_U32(0x6ca378u);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459ab6;
  AUDIO_CALL(esi, 0); /* 00459aa9 indirect */
  eax = (eax - BB_U32(0x488d1cu));
  a = eax;
  b = 0x64u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_459b27;
L_459ab6:;
  eax = BB_U32(esp + 0x10u);
  a = eax;
  b = 0x80u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459ac7;
  ebp = BB_U32(esp + 0x30u);
  goto L_459acb;
L_459ac7:;
  ebp = BB_U32(esp + 0x10u);
L_459acb:;
  eax = BB_U32(esp + 0x14u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459ad9;
  ebx = BB_U32(esp + 0x2cu);
  goto L_459add;
L_459ad9:;
  ebx = BB_U32(esp + 0x14u);
L_459add:;
  eax = BB_U32(esp + 0x18u);
  a = eax;
  b = 0x80u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459aee;
  eax = BB_U32(esp + 0x28u);
  goto L_459b06;
L_459aee:;
  eax = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state returned = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&returned, BB_AUDIO_TARGET(bb_sample_gain_entry), args + sp, esp, edi, esi, ebp, ebx, &fp0);
    eax = returned.eax;
    ecx = returned.ecx;
    edx = returned.edx;
    flags = returned.flags;
  }
  fp1 = (long double)(int32_t)(BB_U32(esp + 0x2cu));
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp0 *= fp1;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    audio_float_to_int(&converted, &fp0);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
L_459b06:;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(0x488d20u) = edi;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(0x488a48u);
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00459b18 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  AUDIO_CALL(esi, 0); /* 00459b20 indirect */
  BB_U32(0x488d1cu) = eax;
L_459b27:;
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
bb_sample_schedule_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused)),
                         uint32_t arg2 __attribute__((unused)),
                         uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -16(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_schedule_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "16(%esp),%esp;ret $0");
}
#endif

/* 0x00464daa..0x00464e0b: clamp a signed gain and round the original
 * extended product to binary64 before returning it on the x87 stack. */
void bb_sample_gain_c(uint32_t input[9]) {
  uint32_t value = BB_U32(input[3] + 16u), flags = input[8];
  double result;
  flags = sub_flags(flags, value, 127u, 32);
  if ((int32_t)value > 127)
    result = 1.0;
  else {
    flags = sub_flags(flags, value, 0u, 32);
    if ((int32_t)value <= 0)
      result = 0.0;
    else {
      input[7] = value;
      result = (double)((long double)(int32_t)value *
                        (long double)BB_DOUBLE(0x4823a8));
    }
  }
#ifdef BB_MODEL_RUNTIME
  bb_model_return_float(result);
#else
  BB_DOUBLE(input[3] + 4u) = result;
#endif
  input[8] = flags;
}
#ifndef BB_MODEL_RUNTIME
double __attribute__((naked)) bb_sample_gain_entry(int32_t value
                                                   __attribute__((unused))) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sample_gain_c) ";leal 4(%esp),%esp;popal;popfl;leal 8(%esp),%esp;fldl "
                        "-8(%esp);ret");
}
#endif
