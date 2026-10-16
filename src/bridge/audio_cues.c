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
/* C forms of game-specific sound cues and selection.
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

void bb_sound_bank_replace_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(0x488a44u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_455db0;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_bank_stop_entry), 0); /* 00455d9a */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_bank_free_entry), 0); /* 00455da8 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_455db0:;
  eax = BB_U32(esp + 0x4u);
  args[--sp] = 0x488a4cu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_load_entry), 0); /* 00455dba */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488a44u) = eax;
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
bb_sound_bank_replace_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_bank_replace_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_bank_load_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_bank_load_entry), 0); /* 00455dd5 */
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
bb_sound_bank_load_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_bank_load_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_state_clear_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  BB_U32(eax * 4u + 0x6ba940u) = 0x0u;
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
bb_sound_state_clear_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_state_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_states_clear_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = edi;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  edi = 0x6ba940u;
  ecx = 0x7u;
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
uint32_t __attribute__((naked)) bb_sound_states_clear_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_states_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_state_activate_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  BB_U32(eax * 4u + 0x6ba940u) = 0x1u;
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
bb_sound_state_activate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_state_activate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_state_active_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  eax = BB_U32(eax * 4u + 0x6ba940u);
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
bb_sound_state_active_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_state_active_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_state_count_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edi = (edi ^ edi);
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
L_455e36:;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00455e37 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edi = (edi + eax);
  esi = (esi + 1u);
  a = esi;
  b = 0x7u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_455e36;
  eax = edi;
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
uint32_t __attribute__((naked)) bb_sound_state_count_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_state_count_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selection_update_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(0x488a8cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455e6e;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00455e5a */
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_455e6e;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00455e66 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_455e6e:;
  eax = BB_U32(0x488a90u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455e8c;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00455e78 */
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_455e8c;
  args[--sp] = 0x1u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00455e84 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_455e8c:;
  eax = BB_U32(0x488a88u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455eaa;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00455e96 */
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_455eaa;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00455ea2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_455eaa:;
  eax = BB_U32(0x488a80u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455ee4;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00455eb4 */
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_455ee4;
  args[--sp] = 0x4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00455ec0 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_455ee4;
  args[--sp] = 0x6u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00455ece */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_455ee4;
  args[--sp] = 0x3u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00455edc */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_455ee4:;
  eax = BB_U32(0x488a7cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455f1e;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00455eee */
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_455f1e;
  args[--sp] = 0x3u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00455efa */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_455f1e;
  args[--sp] = 0x6u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00455f08 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_455f1e;
  args[--sp] = 0x4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00455f16 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_455f1e:;
  eax = BB_U32(0x488a84u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455f3b;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00455f28 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_455f3b;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00455f33 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_455f3b:;
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
uint32_t __attribute__((naked)) bb_sound_selection_update_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selection_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_current_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_flags_reset_entry), 0); /* 00455f40 */
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cues_reset_entry), 0);  /* tail call 00455f45 */
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
uint32_t __attribute__((naked)) bb_sound_current_stop_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_current_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_bank_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cues_reset_entry), 0); /* 00455f50 */
  eax = BB_U32(0x488a44u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_455f67;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effects_stop_entry), 0); /* 00455f5f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_455f67:;
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
uint32_t __attribute__((naked)) bb_sound_bank_stop_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_bank_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_flags_reset_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_states_clear_entry), 0); /* 00455f70 */
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(0x488b2cu) = eax;
  BB_U32(0x488b28u) = eax;
  BB_U32(0x488b24u) = eax;
  BB_U32(0x488b20u) = eax;
  BB_U32(0x488b38u) = eax;
  BB_U32(0x488b34u) = eax;
  BB_U32(0x488b30u) = eax;
  BB_U32(0x488ad0u) = eax;
  BB_U32(0x488accu) = eax;
  BB_U32(0x488ac8u) = eax;
  BB_U32(0x488ac4u) = eax;
  BB_U32(0x488ae0u) = eax;
  BB_U32(0x488adcu) = eax;
  BB_U32(0x488ad8u) = eax;
  BB_U32(0x488ad4u) = eax;
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
uint32_t __attribute__((naked)) bb_sound_flags_reset_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_flags_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cues_reset_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(0x488b44u) = eax;
  BB_U32(0x488b40u) = eax;
  BB_U32(0x488b3cu) = eax;
  BB_U32(0x488aecu) = eax;
  BB_U32(0x488ae8u) = eax;
  BB_U32(0x488ae4u) = eax;
  BB_U32(0x488b1cu) = eax;
  BB_U32(0x488b18u) = eax;
  BB_U32(0x488b14u) = eax;
  BB_U32(0x488af8u) = eax;
  BB_U32(0x488af4u) = eax;
  BB_U32(0x488af0u) = eax;
  BB_U32(0x488b04u) = eax;
  BB_U32(0x488b00u) = eax;
  BB_U32(0x488afcu) = eax;
  BB_U32(0x488b10u) = eax;
  BB_U32(0x488b0cu) = eax;
  BB_U32(0x488b08u) = eax;
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
uint32_t __attribute__((naked)) bb_sound_cues_reset_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cues_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_mode_set_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(0x488a70u);
  esi = BB_U32(esp + 0x8u);
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4560bd;
  ecx = (esi + 0xffffffffu);
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_4560bd;
  switch (ecx) {
  case 0:
    goto L_45604d;
  case 1:
    goto L_45606c;
  case 2:
    goto L_456086;
  case 3:
    goto L_4560a5;
  default:
    __builtin_unreachable();
  }
L_45604d:;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456064;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_stop_entry), 0); /* 00456052 */
  args[--sp] = 0x488b70u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_replace_entry), 0); /* 0045605c */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_456064:;
  BB_U32(0x488a70u) = esi;
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
L_45606c:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_stop_entry), 0); /* 0045606c */
  args[--sp] = 0x488b60u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_replace_entry), 0); /* 00456076 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488a70u) = esi;
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
L_456086:;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45609d;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_stop_entry), 0); /* 0045608b */
  args[--sp] = 0x488b70u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_replace_entry), 0); /* 00456095 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_45609d:;
  BB_U32(0x488a70u) = esi;
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
L_4560a5:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_stop_entry), 0); /* 004560a5 */
  args[--sp] = 0x488b50u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_replace_entry), 0); /* 004560af */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488a70u) = esi;
L_4560bd:;
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
bb_sound_mode_set_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_mode_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_transition_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 36u), 36u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45610f;
  eax = BB_U32(0x488a74u);
  ecx = BB_U32(esp + 0x24u);
  edx = BB_U32(esp + 0x18u);
  BB_U32(ecx) = eax;
  ecx = BB_U32(esp + 0x1cu);
  BB_U32(edx) = eax;
  eax = BB_U32(esp + 0x28u);
  edx = BB_U32(esp + 0x20u);
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  BB_U32(eax) = 0x0u;
  ebx = args[sp++];
  esp += 4u;
  BB_U32(ecx) = 0x0u;
  BB_U32(edx) = 0x0u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45610f:;
  eax = BB_U32(0x488a78u);
  edi = 0x200u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456123;
  edi = BB_U32(0x488a74u);
L_456123:;
  eax = BB_U32(esp + 0x14u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45615f;
  ecx = BB_U32(esp + 0x18u);
  eax = BB_U32(esp + 0x24u);
  edx = BB_U32(esp + 0x1cu);
  BB_U32(esi + 0x8u) = ecx;
  ecx = BB_U32(esp + 0x28u);
  BB_U32(esi + 0xcu) = eax;
  eax = BB_U32(esp + 0x20u);
  BB_U32(esi + 0x10u) = edx;
  edx = BB_U32(esp + 0x2cu);
  BB_U32(esi + 0x14u) = ecx;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi) = eax;
  BB_U32(esi + 0x28u) = 0x0u;
  BB_U32(esi + 0x4u) = edx;
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
L_45615f:;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4562f9;
  eax = BB_U32(esi + 0x28u);
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_4562f9;
  switch (eax) {
  case 0:
    goto L_45617b;
  case 1:
    goto L_4561c5;
  case 2:
    goto L_45623b;
  case 3:
    goto L_4562a6;
  case 4:
    goto L_4562c3;
  default:
    __builtin_unreachable();
  }
L_45617b:;
  eax = BB_U32(esi + 0xcu);
  BB_U32(eax) = edi;
  ecx = BB_U32(esi + 0x14u);
  BB_U32(ecx) = 0x0u;
  edx = BB_U32(esi + 0x4u);
  BB_U32(edx) = 0x7fu;
  eax = BB_U32(esi + 0x8u);
  BB_U32(eax) = 0x0u;
  ecx = BB_U32(esi + 0x10u);
  edi = args[sp++];
  esp += 4u;
  BB_U32(ecx) = 0x0u;
  edx = BB_U32(esi);
  BB_U32(edx) = 0x0u;
  BB_U32(esi + 0x18u) = 0x0u;
  BB_U32(esi + 0x20u) = 0x0u;
  BB_U32(esi + 0x28u) = 0x1u;
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
L_4561c5:;
  eax = BB_U32(esi + 0x14u);
  BB_U32(eax) = 0x1u;
  ecx = BB_U32(esi + 0xcu);
  a = BB_U32(ecx);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4562f9;
  eax = BB_U32(esi + 0x4u);
  ecx = BB_U32(esi + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  edx = (esi + 0x2cu);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 004561ec */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi);
  edx = BB_U32(esi + 0x20u);
  BB_U32(esi + 0x1cu) = eax;
  args[--sp] = ecx;
  esp -= 4u;
  eax = (esi + 0x34u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00456209 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x8u);
  BB_U32(esi + 0x18u) = 0x1u;
  BB_U32(esi + 0x20u) = 0x1u;
  BB_U32(esi + 0x24u) = eax;
  BB_U32(ecx) = edi;
  edi = args[sp++];
  esp += 4u;
  edx = BB_U32(esi + 0x10u);
  BB_U32(edx) = 0x0u;
  BB_U32(esi + 0x28u) = 0x2u;
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
L_45623b:;
  eax = BB_U32(esi + 0x10u);
  BB_U32(eax) = 0x1u;
  ecx = BB_U32(esi + 0x1cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456269;
  eax = BB_U32(esi + 0x4u);
  ecx = BB_U32(esi + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  edx = (esi + 0x2cu);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 0045625e */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x1cu) = eax;
L_456269:;
  eax = BB_U32(esi + 0x24u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45628d;
  eax = BB_U32(esi);
  ecx = BB_U32(esi + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = (esi + 0x34u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00456282 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x24u) = eax;
L_45628d:;
  eax = BB_U32(esi + 0x1cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4562f9;
  eax = BB_U32(esi + 0x24u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4562f9;
  BB_U32(esi + 0x28u) = 0x3u;
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
L_4562a6:;
  eax = BB_U32(esi + 0x14u);
  edi = args[sp++];
  esp += 4u;
  BB_U32(eax) = 0x2u;
  ecx = BB_U32(esi + 0x4u);
  BB_U32(ecx) = 0x0u;
  BB_U32(esi + 0x28u) = 0x4u;
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
L_4562c3:;
  ecx = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(ecx) = eax;
  ecx = BB_U32(esi + 0xcu);
  edx = BB_U32(esi + 0x8u);
  BB_U32(esi + 0x20u) = eax;
  BB_U32(esi + 0x18u) = eax;
  BB_U32(esi + 0x8u) = ecx;
  eax = BB_U32(esi + 0x10u);
  ebx = BB_U32(esi);
  ecx = BB_U32(esi + 0x4u);
  BB_U32(esi + 0xcu) = edx;
  edx = BB_U32(esi + 0x14u);
  BB_U32(esi) = ecx;
  BB_U32(esi + 0x10u) = edx;
  BB_U32(esi + 0x14u) = eax;
  BB_U32(esi + 0x28u) = 0x1u;
  BB_U32(esi + 0x4u) = ebx;
L_4562f9:;
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
bb_sound_transition_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused)),
                          uint32_t arg2 __attribute__((unused)),
                          uint32_t arg3 __attribute__((unused)),
                          uint32_t arg4 __attribute__((unused)),
                          uint32_t arg5 __attribute__((unused)),
                          uint32_t arg6 __attribute__((unused)),
                          uint32_t arg7 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_transition_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_456920_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456924 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456940;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456946;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45694c;
  eax = 0x3u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456940:;
  eax = 0x3u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456946:;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45694c:;
  eax = 0x5u;
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
uint32_t __attribute__((naked)) bb_sound_selector_456920_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_456920_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_456960_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456964 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456980;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456986;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45698c;
  eax = 0x6u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456980:;
  eax = 0x6u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456986:;
  eax = 0x7u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45698c:;
  eax = 0x8u;
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
uint32_t __attribute__((naked)) bb_sound_selector_456960_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_456960_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_4569a0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004569a4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4569c0;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4569c6;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4569cc;
  eax = 0x9u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_4569c0:;
  eax = 0x9u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_4569c6:;
  eax = 0xau;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_4569cc:;
  eax = 0xbu;
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
uint32_t __attribute__((naked)) bb_sound_selector_4569a0_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_4569a0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_4569e0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004569e4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456a00;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456a06;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456a0c;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a00:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a06:;
  eax = 0xfu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a0c:;
  eax = 0x10u;
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
uint32_t __attribute__((naked)) bb_sound_selector_4569e0_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_4569e0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_456a20_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456a24 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_456a3b;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_456a41;
  eax = 0xcu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a3b:;
  eax = 0xcu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a41:;
  eax = 0xdu;
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
uint32_t __attribute__((naked)) bb_sound_selector_456a20_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_456a20_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_456a50_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456a54 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_456a68;
  switch (eax) {
  case 0:
    goto L_456a6e;
  case 1:
    goto L_456a74;
  case 2:
    goto L_456a7a;
  case 3:
    goto L_456a80;
  default:
    __builtin_unreachable();
  }
L_456a68:;
  eax = 0x14u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a6e:;
  eax = 0x14u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a74:;
  eax = 0x15u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a7a:;
  eax = 0x16u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_456a80:;
  eax = 0x17u;
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
uint32_t __attribute__((naked)) bb_sound_selector_456a50_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_456a50_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_456aa0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488a94u);
  args[--sp] = edi;
  esp -= 4u;
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
  F64(esp + 0xcu) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ac4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_456b60;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_456b60;
  BB_U32(0x488ac4u) = 0x1u;
  BB_U32(0x488b88u) = 0x0u;
  BB_U32(0x488b8cu) = 0x0u;
  BB_U32(0x488b90u) = 0x7fu;
  args[--sp] = 0x2ee0u;
  esp -= 4u;
  args[--sp] = 0x1770u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456b01 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488b98u) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00456b0e indirect */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488b90u));
  BB_U32(0x488b94u) = eax;
  eax = BB_U32(0x488a98u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x10u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(0x488a48u);
  args[--sp] = 0xaf2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x579u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_456920_entry);
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00456b4e */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
L_456b56:;
  eax = 0x1u;
  edi = args[sp++];
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
L_456b60:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456c46;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456c46;
  eax = BB_U32(0x488a78u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456bf4;
  edi = BB_U32(0x6ca378u);
  SAMPLE_CALL(edi, 0); /* 00456b82 indirect */
  ecx = BB_U32(0x488b94u);
  edx = BB_U32(0x488b98u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_456bff;
  eax = BB_U32(0x488a98u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456ba2 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456baf */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x11u;
  esp -= 4u;
  eax = BB_U32(0x488a48u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00456bd1 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x2ee0u;
  esp -= 4u;
  args[--sp] = 0x1770u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00456be3 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488b98u) = eax;
  SAMPLE_CALL(edi, 0); /* 00456bf0 indirect */
  goto L_456bfa;
L_456bf4:;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00456bf4 indirect */
L_456bfa:;
  BB_U32(0x488b94u) = eax;
L_456bff:;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488b90u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00456c14 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488a98u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 00456c27 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00456c34 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
  edi = args[sp++];
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
L_456c46:;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456cf0;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456cf0;
  args[--sp] = 0x488b90u;
  esp -= 4u;
  eax = BB_U32(0x488b88u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bab60u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00456c6f */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488b90u));
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488b8cu) = eax;
  BB_U32(0x488b88u) = 0x1u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00456c9b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488a98u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 00456caf */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00456cbc */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488b8cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_456b56;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00456cd7 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ac4u) = 0x0u;
  edi = args[sp++];
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
L_456cf0:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456b56;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456b56;
  args[--sp] = 0x6bab68u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00456d07 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ac4u) = 0x0u;
  edi = args[sp++];
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
bb_sound_cue_456aa0_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_456aa0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_456d20_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488a9cu);
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ac8u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_456db3;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_456db3;
  BB_U32(0x488ac8u) = 0x1u;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x4822e0u);
  BB_U32(0x488b9cu) = 0x0u;
  BB_U32(0x488ba0u) = 0x0u;
  BB_U32(0x488ba4u) = 0x7fu;
  eax = BB_U32(0x488aa0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x605u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a48u);
  args[--sp] = 0x302u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_456960_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00456da2 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
L_456daa:;
  eax = 0x1u;
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
L_456db3:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456e03;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456e03;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488ba4u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00456dd2 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488aa0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 00456de5 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00456df2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
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
L_456e03:;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456eac;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456eac;
  args[--sp] = 0x488ba4u;
  esp -= 4u;
  eax = BB_U32(0x488b9cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bac18u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00456e2c */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488ba4u));
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488ba0u) = eax;
  BB_U32(0x488b9cu) = 0x1u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00456e58 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488aa0u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 00456e6c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00456e79 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488ba0u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_456daa;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00456e94 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ac8u) = 0x0u;
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
L_456eac:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456daa;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_456daa;
  args[--sp] = 0x6babc0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00456ec3 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ac8u) = 0x0u;
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
bb_sound_cue_456d20_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_456d20_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_456ee0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488aa4u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  {
    struct bb_call_state call = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&call, BB_AUDIO_TARGET(bb_sample_gain_entry), args + sp, esp, edi, esi, ebp, ebx, &fp[depth++]);
    eax = call.eax;
    ecx = call.ecx;
    edx = call.edx;
    flags = call.flags;
  }
  F64(esp + 0x10u) = (double)(fp[depth - 1]);
  --depth;
  eax = BB_U32(esp + 0x1cu);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488accu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45703a;
  a = ecx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45703a;
  eax = 0xffffffffu;
  BB_U32(0x488bacu) = esi;
  BB_U32(0x488ba8u) = esi;
  BB_U32(0x488bb0u) = esi;
  BB_U32(0x488bb8u) = eax;
  BB_U32(0x488bb4u) = eax;
  BB_U32(0x488bc8u) = esi;
  BB_U32(0x488bd0u) = esi;
  ecx = BB_U32(0x488a48u);
  BB_U32(0x488bccu) = esi;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(0x488bd8u) = esi;
  BB_U32(0x488bc4u) = 0x7fu;
  BB_U32(0x488bd4u) = esi;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reserve_entry), 0); /* 00456f63 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488a48u);
  BB_U32(0x488bbcu) = eax;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_reserve_entry), 0); /* 00456f78 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488bbcu);
  BB_U32(0x488bc0u) = eax;
  a = ecx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_456fbe;
  ecx = BB_U32(0x488aa8u);
  eax = BB_U32(0x488a48u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00456fa0 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488bbcu);
  BB_U32(0x488bb4u) = eax;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_bind_entry), 0); /* 00456fb6 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_456fbe:;
  eax = BB_U32(0x488a48u);
  ecx = BB_U32(0x488aa8u);
  edx = BB_U32(0x488bc0u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_456ffb;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00456fdd */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488bc0u);
  BB_U32(0x488bb8u) = eax;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_bind_entry), 0); /* 00456ff3 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_456ffb:;
  BB_U32(0x488bc4u) = 0x7fu;
  args[--sp] = 0x488bccu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x6bac28u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457018 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bd4u) = eax;
  BB_U32(0x488accu) = 0x1u;
L_45702f:;
  eax = 0x1u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_45703a:;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4571ca;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4571ca;
  eax = BB_U32(0x488aa4u);
  a = eax;
  b = 0x50u;
  flags = sub_flags(flags, a, b, 32);
  eax = BB_U32(0x488bacu);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4570a4;
  ecx = BB_U32(0x488aa4u);
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4570a4;
  eax = BB_U32(0x488ba8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4570e9;
  BB_U32(0x488bc8u) = 0x7fu;
  args[--sp] = 0x488bd0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x6bac20u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 0045708b */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bd8u) = eax;
  BB_U32(0x488ba8u) = 0x1u;
  goto L_4570e9;
L_4570a4:;
  ecx = BB_U32(0x488aa4u);
  a = ecx;
  b = 0x50u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_4570e9;
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4570e9;
  eax = BB_U32(0x488ba8u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4570e9;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0x488bd0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(0x488bc8u) = esi;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x6bac20u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 004570d6 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bd8u) = eax;
  BB_U32(0x488ba8u) = esi;
L_4570e9:;
  eax = BB_U32(0x488bd4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457116;
  args[--sp] = 0x488bccu;
  esp -= 4u;
  eax = BB_U32(0x488bc4u);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac28u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457109 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bd4u) = eax;
L_457116:;
  eax = BB_U32(0x488bd8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457143;
  args[--sp] = 0x488bd0u;
  esp -= 4u;
  eax = BB_U32(0x488bc8u);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac20u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457136 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bd8u) = eax;
L_457143:;
  esi = BB_U32(0x488bb4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_45717c;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488bccu));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0xcu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 0045715e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488aa8u);
  ecx = BB_U32(0x488bb4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00457174 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_45717c:;
  esi = BB_U32(0x488bb8u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4571b5;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488bd0u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0xcu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00457197 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488aa8u);
  ecx = BB_U32(0x488bb8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 004571ad */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_4571b5:;
  eax = BB_U32(0x488aa4u);
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  BB_U32(0x488bacu) = eax;
  eax = 0x1u;
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
L_4571ca:;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45733b;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45733b;
  args[--sp] = 0x488bccu;
  esp -= 4u;
  eax = BB_U32(0x488bb0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bac28u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 004571f3 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bd4u) = eax;
  eax = BB_U32(0x488bb0u);
  args[--sp] = 0x488bd0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bac20u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457218 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  edi = BB_U32(0x488bb4u);
  BB_U32(0x488bd8u) = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  BB_U32(0x488bb0u) = 0x1u;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_45726e;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488bccu));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0xcu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 0045724a */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488aa8u);
  ecx = BB_U32(0x488bb4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 00457260 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  edi = BB_U32(0x488bb4u);
L_45726e:;
  esi = BB_U32(0x488bb8u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4572b3;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488bd0u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0xcu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00457289 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488aa8u);
  ecx = BB_U32(0x488bb8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 0045729f */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = BB_U32(0x488bb8u);
  edi = BB_U32(0x488bb4u);
L_4572b3:;
  eax = BB_U32(0x488bd4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45702f;
  eax = BB_U32(0x488bd8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45702f;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4572fe;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 004572d4 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bb4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 004572e2 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bbcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 004572f0 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = BB_U32(0x488bb8u);
L_4572fe:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_457329;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00457305 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bb8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00457313 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bc0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 00457321 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_457329:;
  eax = (eax ^ eax);
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x488accu) = 0x0u;
  esi = args[sp++];
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
L_45733b:;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45702f;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45702f;
  esi = BB_U32(0x488bb4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_45737e;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 0045735a */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bb4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00457368 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bbcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 00457376 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_45737e:;
  esi = BB_U32(0x488bb8u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4573af;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 0045738b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bb8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_stop_entry), 0); /* 00457399 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bc0u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effect_release_entry), 0); /* 004573a7 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_4573af:;
  eax = (eax ^ eax);
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x488accu) = 0x0u;
  esi = args[sp++];
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
bb_sound_cue_456ee0_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_456ee0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4573d0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488aacu);
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ad0u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457463;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457463;
  BB_U32(0x488ad0u) = 0x1u;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x4822e8u);
  BB_U32(0x488bdcu) = 0x0u;
  BB_U32(0x488be0u) = 0x0u;
  BB_U32(0x488be4u) = 0x7fu;
  eax = BB_U32(0x488ab0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x578u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a48u);
  args[--sp] = 0x2bcu;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_4569a0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00457452 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
L_45745a:;
  eax = 0x1u;
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
L_457463:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4574b3;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4574b3;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488be4u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00457482 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ab0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 00457495 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 004574a2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
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
L_4574b3:;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45755c;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45755c;
  args[--sp] = 0x488be4u;
  esp -= 4u;
  eax = BB_U32(0x488bdcu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bac88u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 004574dc */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488be4u));
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488be0u) = eax;
  BB_U32(0x488bdcu) = 0x1u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00457508 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488ab0u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 0045751c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00457529 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488be0u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45745a;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00457544 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ad0u) = 0x0u;
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
L_45755c:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45745a;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45745a;
  args[--sp] = 0x6bac30u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00457573 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ad0u) = 0x0u;
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
bb_sound_cue_4573d0_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4573d0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_457590_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488ab4u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  {
    struct bb_call_state call = {eax, ecx, edx, flags};
    BB_AUDIO_CALL_FLOAT(&call, BB_AUDIO_TARGET(bb_sample_gain_entry), args + sp, esp, edi, esi, ebp, ebx, &fp[depth++]);
    eax = call.eax;
    ecx = call.ecx;
    edx = call.edx;
    flags = call.flags;
  }
  F64(esp + 0xcu) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ad8u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457650;
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457650;
  BB_U32(0x488be8u) = esi;
  BB_U32(0x488bf0u) = esi;
  args[--sp] = 0xbb8u;
  esp -= 4u;
  BB_U32(0x488bf8u) = esi;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(0x488becu) = esi;
  BB_U32(0x488ad8u) = 0x1u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004575ed */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bf4u) = eax;
  args[--sp] = 0x50u;
  esp -= 4u;
  args[--sp] = 0x19u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004575fe */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488bf8u));
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bfcu) = eax;
  eax = BB_U32(0x488ab8u);
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(0x488a48u);
  args[--sp] = 0x190u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x96u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_4569e0_entry);
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 0045763e */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
L_457646:;
  eax = 0x1u;
  esi = args[sp++];
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
L_457650:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457727;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457727;
  args[--sp] = 0x488bf8u;
  esp -= 4u;
  eax = BB_U32(0x488becu);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(0x488bf4u);
  args[--sp] = ecx;
  esp -= 4u;
  edx = BB_U32(0x488bfcu);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x6bac90u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457680 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457698;
  BB_U32(0x488becu) = 0x1u;
  goto L_4576ca;
L_457698:;
  BB_U32(0x488becu) = 0x0u;
  args[--sp] = 0xbb8u;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004576ac */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bf4u) = eax;
  args[--sp] = 0x50u;
  esp -= 4u;
  args[--sp] = 0x19u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004576bd */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488bfcu) = eax;
L_4576ca:;
  eax = BB_U32(0x488a78u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  eax = BB_U32(0x488bf8u);
  if (flags & 0x40u)
    goto L_4576de;
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax - edx);
  a = eax;
  result = ((uint32_t)((int32_t)a >> 1)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 1, 1);
  eax = result;
L_4576de:;
  BB_U32(esp + 0x4u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x4u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 004576f5 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ab8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 00457708 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00457715 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
  esi = args[sp++];
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
L_457727:;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4577e6;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4577e6;
  args[--sp] = 0x488bf8u;
  esp -= 4u;
  eax = BB_U32(0x488be8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bac90u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457750 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488a78u);
  BB_U32(0x488bf0u) = eax;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(0x488be8u) = 0x1u;
  eax = BB_U32(0x488bf8u);
  if (flags & 0x40u)
    goto L_45777c;
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax - edx);
  a = eax;
  result = ((uint32_t)((int32_t)a >> 1)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 1, 1);
  eax = result;
L_45777c:;
  BB_U32(esp + 0x4u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x4u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00457793 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ab8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 004577a6 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 004577b3 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488bf0u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457646;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 004577cd */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ad8u) = 0x0u;
  esi = args[sp++];
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
L_4577e6:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457646;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457646;
  args[--sp] = 0x6bac98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 004577fd */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ad8u) = 0x0u;
  esi = args[sp++];
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
bb_sound_cue_457590_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_457590_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_457820_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(0x488abcu);
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488adcu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4578b3;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4578b3;
  BB_U32(0x488adcu) = 0x1u;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x4822f0u);
  BB_U32(0x488c00u) = 0x0u;
  BB_U32(0x488c04u) = 0x0u;
  BB_U32(0x488c08u) = 0x7fu;
  eax = BB_U32(0x488ac0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xfau;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a48u);
  args[--sp] = 0x4c4u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_456a20_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 004578a2 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
L_4578aa:;
  eax = 0x1u;
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
L_4578b3:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457903;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457903;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488c08u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 004578d2 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488ac0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 004578e5 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 004578f2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
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
L_457903:;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4579ac;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4579ac;
  args[--sp] = 0x488c08u;
  esp -= 4u;
  eax = BB_U32(0x488c00u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bad48u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 0045792c */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488c08u));
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c04u) = eax;
  BB_U32(0x488c00u) = 0x1u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00457958 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488ac0u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 0045796c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00457979 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488c04u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4578aa;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00457994 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488adcu) = 0x0u;
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
L_4579ac:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4578aa;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4578aa;
  args[--sp] = 0x6bacf0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 004579c3 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488adcu) = 0x0u;
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
bb_sound_cue_457820_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_457820_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4579e0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = (edx ^ edx);
  eax = BB_U32(0x488ae0u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457a8f;
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457a8f;
  BB_U32(0x488c14u) = edx;
  BB_U32(0x488c0cu) = edx;
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488c10u) = edx;
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(0x488c18u) = edx;
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  BB_U32(0x488ae0u) = 0x1u;
  args[--sp] = 0xa8cu;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x546u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_456a50_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00457a50 */
  a = esp;
  b = 0x24u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488c0cu);
  args[--sp] = 0x488c18u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xbb8u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x6bad50u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457a6f */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c10u) = eax;
  BB_U32(0x488c0cu) = 0x1u;
L_457a86:;
  eax = 0x1u;
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
L_457a8f:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457afa;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457afa;
  eax = BB_U32(0x488c10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457ac7;
  args[--sp] = 0x488c18u;
  esp -= 4u;
  ecx = BB_U32(0x488c0cu);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0xbb8u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x6bad50u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457aba */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c10u) = eax;
L_457ac7:;
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488c18u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00457adc */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00457ae9 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
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
L_457afa:;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457bb7;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457bb7;
  eax = BB_U32(0x488c14u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457b23;
  BB_U32(0x488c14u) = 0x1u;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  goto L_457b29;
L_457b23:;
  ecx = BB_U32(0x488c0cu);
L_457b29:;
  args[--sp] = 0x488c18u;
  esp -= 4u;
  BB_U32(0x488c0cu) = ecx;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0xbb8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bad50u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00457b41 */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488c18u));
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c10u) = eax;
  BB_U32(0x488c0cu) = 0x1u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x4u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00457b6d */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00457b7a */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488c10u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_457a86;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00457b95 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488c14u) = 0x0u;
  BB_U32(0x488ae0u) = 0x0u;
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
L_457bb7:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457a86;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_457a86;
  args[--sp] = 0x6bad58u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00457bce */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  BB_U32(0x488ae0u) = 0x0u;
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
bb_sound_cue_4579e0_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4579e0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457bf0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457bf4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_457c08;
  switch (eax) {
  case 0:
    goto L_457c19;
  case 1:
    goto L_457c2a;
  case 2:
    goto L_457c3c;
  case 3:
    goto L_457c50;
  default:
    __builtin_unreachable();
  }
L_457c08:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  a = eax;
  b = 0x6u;
  result = (a & b) & 0xffffffffu;
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
L_457c19:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  a = eax;
  b = 0x6u;
  result = (a & b) & 0xffffffffu;
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
L_457c2a:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  a = eax;
  b = 0x6u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  a = eax;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
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
L_457c3c:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x6u);
  a = eax;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
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
L_457c50:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x6u);
  a = eax;
  b = 0x3u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
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
uint32_t __attribute__((naked)) bb_sound_selector_457bf0_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457bf0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457c80_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457c84 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457ca0;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457ca6;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457cac;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457ca0:;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457ca6:;
  eax = 0x5u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457cac:;
  eax = 0x6u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457c80_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457c80_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457cc0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457cc4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457cee;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457d02;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  eax = BB_U32(0x488a70u);
  if (flags & 0x40u)
    goto L_457d16;
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x3u);
  a = eax;
  b = 0x7u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
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
L_457cee:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x3u);
  a = eax;
  b = 0x7u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
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
L_457d02:;
  eax = BB_U32(0x488a70u);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x3u);
  a = eax;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
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
L_457d16:;
  eax = (eax - 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x3u);
  a = eax;
  b = 0x9u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
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
uint32_t __attribute__((naked)) bb_sound_selector_457cc0_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457cc0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457d30_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457d34 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457d50;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457d56;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457d59;
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
L_457d50:;
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
L_457d56:;
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
L_457d59:;
  eax = 0x2u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457d30_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457d30_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457d60_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457d64 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457d7b;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457d81;
  eax = 0x52u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457d7b:;
  eax = 0x52u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457d81:;
  eax = 0x53u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457d60_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457d60_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457d90_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457d94 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_457da8;
  switch (eax) {
  case 0:
    goto L_457dae;
  case 1:
    goto L_457db4;
  case 2:
    goto L_457dba;
  case 3:
    goto L_457dc0;
  default:
    __builtin_unreachable();
  }
L_457da8:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457dae:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457db4:;
  eax = 0xfu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457dba:;
  eax = 0x10u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457dc0:;
  eax = 0x11u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457d90_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457d90_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457de0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457de4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_457df8;
  switch (eax) {
  case 0:
    goto L_457dfb;
  case 1:
    goto L_457dfe;
  case 2:
    goto L_457e04;
  case 3:
    goto L_457e0a;
  default:
    __builtin_unreachable();
  }
L_457df8:;
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
L_457dfb:;
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
L_457dfe:;
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
L_457e04:;
  eax = 0x2u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457e0a:;
  eax = 0x3u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457de0_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457de0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457e20_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457e24 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457e3b;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457e41;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457e3b:;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457e41:;
  eax = 0x5u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457e20_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457e20_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457e50_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457e54 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457e6b;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457e71;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457e6b:;
  eax = 0x4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457e71:;
  eax = 0x3u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457e50_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457e50_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457e80_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x3u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457e84 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457ea0;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457ea6;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457eac;
  eax = 0x5u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457ea0:;
  eax = 0x5u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457ea6:;
  eax = 0x6u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457eac:;
  eax = 0x7u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457e80_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457e80_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457ec0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457ec5 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457eff;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  eax = BB_U32(0x488a70u);
  if (flags & 0x40u)
    goto L_457f28;
  eax = (eax - 0x2u);
  args[--sp] = 0x14u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = esi;
  b = esi;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  esi = result;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457ee7 */
  ecx = BB_U32(esp + 0x10u);
  esi = (esi & 0xfffffff6u);
  esi = (esi + 0x12u);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ecx) = eax;
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
L_457eff:;
  eax = BB_U32(0x488a70u);
  args[--sp] = 0x14u;
  esp -= 4u;
  eax = (eax - 0x2u);
  args[--sp] = 0x5u;
  esp -= 4u;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = esi;
  b = esi;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  esi = result;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457f10 */
  ecx = BB_U32(esp + 0x10u);
  esi = (esi & 0xfffffff6u);
  esi = (esi + 0x12u);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ecx) = eax;
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
L_457f28:;
  eax = (eax - 0x2u);
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0xfu;
  esp -= 4u;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = esi;
  b = esi;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  esi = result;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457f34 */
  ecx = BB_U32(esp + 0x10u);
  esi = (esi & 0xfffffff6u);
  esi = (esi + 0x13u);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ecx) = eax;
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
bb_sound_selector_457ec0_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457ec0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457f50_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457f54 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_457f6b;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_457f71;
  eax = 0xau;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457f6b:;
  eax = 0xau;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457f71:;
  eax = 0xbu;
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
uint32_t __attribute__((naked)) bb_sound_selector_457f50_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457f50_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457f80_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457f84 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x6u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_457f98;
  switch (eax) {
  case 0:
    goto L_457f9e;
  case 1:
    goto L_457fa4;
  case 2:
    goto L_457faa;
  case 3:
    goto L_457fb0;
  case 4:
    goto L_457fb6;
  case 5:
    goto L_457fbc;
  case 6:
    goto L_457fc2;
  default:
    __builtin_unreachable();
  }
L_457f98:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457f9e:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457fa4:;
  eax = 0xfu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457faa:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457fb0:;
  eax = 0xfu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457fb6:;
  eax = 0xeu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457fbc:;
  eax = 0xfu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_457fc2:;
  eax = 0xcu;
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
uint32_t __attribute__((naked)) bb_sound_selector_457f80_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457f80_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_457ff0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x6u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00457ff4 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x5u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_458008;
  switch (eax) {
  case 0:
    goto L_45800e;
  case 1:
    goto L_458014;
  case 2:
    goto L_45801a;
  case 3:
    goto L_458020;
  case 4:
    goto L_458026;
  case 5:
    goto L_45802c;
  default:
    __builtin_unreachable();
  }
L_458008:;
  eax = 0x18u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45800e:;
  eax = 0x17u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_458014:;
  eax = 0x18u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45801a:;
  eax = 0x19u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_458020:;
  eax = 0x14u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_458026:;
  eax = 0x15u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45802c:;
  eax = 0x16u;
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
uint32_t __attribute__((naked)) bb_sound_selector_457ff0_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_457ff0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_selector_458050_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458054 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_458068;
  switch (eax) {
  case 0:
    goto L_45806e;
  case 1:
    goto L_458074;
  case 2:
    goto L_45807a;
  case 3:
    goto L_458080;
  default:
    __builtin_unreachable();
  }
L_458068:;
  eax = 0x1au;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45806e:;
  eax = 0x1au;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_458074:;
  eax = 0x1bu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_45807a:;
  eax = 0x1cu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_458080:;
  eax = 0x1du;
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
uint32_t __attribute__((naked)) bb_sound_selector_458050_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_selector_458050_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4580a0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0xcu) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488afcu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4580f7;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4580f7;
  BB_U32(0x488afcu) = 0x1u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x1f4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004580da */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c20u) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 004580e7 indirect */
  esi = args[sp++];
  esp += 4u;
  BB_U32(0x488c1cu) = eax;
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
L_4580f7:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458198;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458198;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 0045810f indirect */
  ecx = BB_U32(0x488c1cu);
  edx = BB_U32(0x488c20u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_4581ac;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045812b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045813a */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458147 */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457d90_entry), 0); /* 00458161 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 0045816d */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x1f4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045817f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c20u) = eax;
  SAMPLE_CALL(esi, 0); /* 0045818c indirect */
  esi = args[sp++];
  esp += 4u;
  BB_U32(0x488c1cu) = eax;
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
L_458198:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4581ac;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4581ac;
  BB_U32(0x488afcu) = 0x0u;
L_4581ac:;
  esi = args[sp++];
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
bb_sound_cue_4580a0_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4580a0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4581c0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(esp) = 0x0u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
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
  F64(esp + 0x10u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x20u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b08u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458259;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458259;
  fp[depth] = F64(esp + 0xcu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x4822f8u);
  BB_U32(0x488b08u) = 0x1u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1130u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0x898u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457cc0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6badb0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00458229 */
  a = esp;
  b = 0x24u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045823b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c28u) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00458248 indirect */
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x488c24u) = eax;
  esi = args[sp++];
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
L_458259:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45831f;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45831f;
  fp[depth] = F64(esp + 0xcu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x4822f8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6badb0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00458280 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6badb0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 0045828d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 0045829b indirect */
  ecx = BB_U32(0x488c24u);
  edx = BB_U32(0x488c28u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458340;
  eax = (esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457ec0_entry), 0); /* 004582b8 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  edi = eax;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004582c6 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0xfffffffeu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004582d5 */
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 004582f3 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458305 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c28u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458312 indirect */
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x488c24u) = eax;
  esi = args[sp++];
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
L_45831f:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458340;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458340;
  args[--sp] = 0x6badb0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 0045832e */
  BB_U32(0x488b08u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_458340:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
bb_sound_cue_4581c0_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4581c0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458350_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4583df;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4583df;
  BB_U32(0x488b14u) = 0x1u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x1f4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045838a */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c30u) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00458397 indirect */
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482300u);
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488c2cu) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x15e0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0xaf0u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457bf0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bae08u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 004583d2 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_4583df:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4584a6;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4584a6;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 004583f7 indirect */
  ecx = BB_U32(0x488c2cu);
  edx = BB_U32(0x488c30u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458477;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045840f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045841e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0xfu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045842b */
  BB_U32(esp + 0x20u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x10u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457c80_entry), 0); /* 00458445 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458451 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x1f4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458463 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c30u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458470 indirect */
  BB_U32(0x488c2cu) = eax;
L_458477:;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482300u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bae08u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 0045848c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bae08u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00458499 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_4584a6:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4584c7;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4584c7;
  args[--sp] = 0x6bae08u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 004584b5 */
  BB_U32(0x488b14u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_4584c7:;
  esi = args[sp++];
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
bb_sound_cue_458350_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458350_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4584d0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(esp + 0x4u);
  eax = BB_U32(0x488ae4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458519;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458519;
  args[--sp] = 0x488c48u;
  esp -= 4u;
  args[--sp] = 0x488c40u;
  esp -= 4u;
  args[--sp] = 0x488c38u;
  esp -= 4u;
  args[--sp] = 0x488c44u;
  esp -= 4u;
  args[--sp] = 0x488c3cu;
  esp -= 4u;
  args[--sp] = 0x488c34u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bae60u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_transition_entry), 0); /* 00458506 */
  BB_U32(0x488ae4u) = 0x1u;
  a = esp;
  b = 0x20u;
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
L_458519:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45863e;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45863e;
  args[--sp] = 0x488c48u;
  esp -= 4u;
  args[--sp] = 0x488c40u;
  esp -= 4u;
  args[--sp] = 0x488c38u;
  esp -= 4u;
  args[--sp] = 0x488c44u;
  esp -= 4u;
  args[--sp] = 0x488c3cu;
  esp -= 4u;
  args[--sp] = 0x488c34u;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x6bae60u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_transition_entry), 0); /* 00458550 */
  a = esp;
  b = 0x20u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488c34u);
  a = eax;
  b = 0x200u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_4585c8;
  if (flags & 0x40u)
    goto L_4585b3;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458577;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45858b;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45859f;
  goto L_4585c8;
L_458577:;
  eax = BB_U32(0x488c44u);
  ecx = BB_U32(0x488c3cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4580a0_entry), 0); /* 00458584 */
  goto L_4585c5;
L_45858b:;
  eax = BB_U32(0x488c44u);
  ecx = BB_U32(0x488c3cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4580a0_entry), 0); /* 00458598 */
  goto L_4585c5;
L_45859f:;
  eax = BB_U32(0x488c44u);
  ecx = BB_U32(0x488c3cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4581c0_entry), 0); /* 004585ac */
  goto L_4585c5;
L_4585b3:;
  eax = BB_U32(0x488c44u);
  ecx = BB_U32(0x488c3cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458350_entry), 0); /* 004585c0 */
L_4585c5:;
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_4585c8:;
  eax = BB_U32(0x488c38u);
  a = eax;
  b = 0x200u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_458652;
  if (flags & 0x40u)
    goto L_458628;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4585e6;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4585fc;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458612;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_4585e6:;
  eax = BB_U32(0x488c48u);
  ecx = BB_U32(0x488c40u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4580a0_entry), 0); /* 004585f3 */
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
L_4585fc:;
  eax = BB_U32(0x488c48u);
  ecx = BB_U32(0x488c40u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4580a0_entry), 0); /* 00458609 */
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
L_458612:;
  eax = BB_U32(0x488c48u);
  ecx = BB_U32(0x488c40u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4581c0_entry), 0); /* 0045861f */
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
L_458628:;
  eax = BB_U32(0x488c48u);
  ecx = BB_U32(0x488c40u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458350_entry), 0); /* 00458635 */
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
L_45863e:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458652;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458652;
  BB_U32(0x488ae4u) = 0x0u;
L_458652:;
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
bb_sound_cue_4584d0_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4584d0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458660_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0xcu) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488af4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45871f;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45871f;
  BB_U32(0x488af4u) = 0x1u;
  BB_U32(0x488c54u) = 0x0u;
  BB_U32(0x488c58u) = 0x40u;
  BB_U32(0x488c4cu) = 0x0u;
  BB_U32(0x488c50u) = 0x0u;
  args[--sp] = 0x1b58u;
  esp -= 4u;
  args[--sp] = 0xdacu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004586ca */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c60u) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 004586d7 indirect */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488c54u));
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488c5cu) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x10u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x578u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0x578u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457e80_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6baee0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00458712 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_45871f:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458869;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458869;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 00458737 indirect */
  ecx = BB_U32(0x488c5cu);
  edx = BB_U32(0x488c60u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_4587ac;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045874f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x50u;
  esp -= 4u;
  args[--sp] = 0x14u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458760 */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_458050_entry), 0); /* 0045877a */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458786 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x1b58u;
  esp -= 4u;
  args[--sp] = 0xdacu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458798 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c60u) = eax;
  SAMPLE_CALL(esi, 0); /* 004587a5 indirect */
  BB_U32(0x488c5cu) = eax;
L_4587ac:;
  args[--sp] = 0x488c54u;
  esp -= 4u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004587c2 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  ecx = 0x19u;
  a = eax;
  b = 0x1f4u;
  flags = sub_flags(flags, a, b, 32);
  eax = BB_U32(0x488c4cu);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4587e0;
  ecx = 0x3cu;
L_4587e0:;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6baea0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_ramp_step_entry), 0); /* 004587e9 */
  a = esp;
  b = 0x1cu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488c50u);
  args[--sp] = 0x488c58u;
  esp -= 4u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6baec0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_ramp_step_entry), 0); /* 0045880f */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488c54u));
  a = esp;
  b = 0x1cu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = 0x1u;
  BB_U32(0x488c50u) = eax;
  BB_U32(0x488c4cu) = eax;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6baee0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 0045883b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488c58u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6baee0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_pan_entry), 0); /* 0045884f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6baee0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 0045885c */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_458869:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45888a;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45888a;
  args[--sp] = 0x6baee0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00458878 */
  BB_U32(0x488af4u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_45888a:;
  esi = args[sp++];
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
bb_sound_cue_458660_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458660_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458890_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0xcu) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b00u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458925;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458925;
  BB_U32(0x488b00u) = 0x1u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004588ca */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  esi = BB_U32(0x6ca378u);
  BB_U32(0x488c68u) = eax;
  SAMPLE_CALL(esi, 0); /* 004588dd indirect */
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(0x488c64u) = eax;
  args[--sp] = 0x1f4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004588ee */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c70u) = eax;
  SAMPLE_CALL(esi, 0); /* 004588fb indirect */
  args[--sp] = 0x1770u;
  esp -= 4u;
  BB_U32(0x488c6cu) = eax;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045890c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c78u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458919 indirect */
  esi = args[sp++];
  esp += 4u;
  BB_U32(0x488c74u) = eax;
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
L_458925:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458ab7;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458ab7;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 0045893d indirect */
  ecx = BB_U32(0x488c64u);
  edx = BB_U32(0x488c68u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_4589b2;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458955 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x14u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458966 */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457ff0_entry), 0); /* 00458980 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 0045898c */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045899e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c68u) = eax;
  SAMPLE_CALL(esi, 0); /* 004589ab indirect */
  BB_U32(0x488c64u) = eax;
L_4589b2:;
  SAMPLE_CALL(esi, 0); /* 004589b2 indirect */
  ecx = BB_U32(0x488c6cu);
  edx = BB_U32(0x488c70u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458a32;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004589ca */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004589d9 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x14u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004589e6 */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457f50_entry), 0); /* 00458a00 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458a0c */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  args[--sp] = 0x1f4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458a1e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c70u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458a2b indirect */
  BB_U32(0x488c6cu) = eax;
L_458a32:;
  SAMPLE_CALL(esi, 0); /* 00458a32 indirect */
  ecx = BB_U32(0x488c74u);
  edx = BB_U32(0x488c78u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458acb;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458a4e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458a5d */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458a6a */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xdu;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458a8c */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x1770u;
  esp -= 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458a9e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c78u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458aab indirect */
  esi = args[sp++];
  esp += 4u;
  BB_U32(0x488c74u) = eax;
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
L_458ab7:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458acb;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458acb;
  BB_U32(0x488b00u) = 0x0u;
L_458acb:;
  esi = args[sp++];
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
bb_sound_cue_458890_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458890_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458ad0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(esp + 0x4u) = 0x0u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
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
  F64(esp + 0x14u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x20u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b0cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458b51;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458b51;
  BB_U32(0x488b0cu) = 0x1u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458b13 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  esi = BB_U32(0x6ca378u);
  BB_U32(0x488c80u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458b26 indirect */
  args[--sp] = 0xfa0u;
  esp -= 4u;
  BB_U32(0x488c7cu) = eax;
  args[--sp] = 0x5dcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458b37 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c88u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458b44 indirect */
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x488c84u) = eax;
  esi = args[sp++];
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
L_458b51:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458c6d;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458c6d;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 00458b69 indirect */
  ecx = BB_U32(0x488c7cu);
  edx = BB_U32(0x488c80u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458be3;
  eax = (esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457ec0_entry), 0); /* 00458b82 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  edi = eax;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458b90 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0xfffffffeu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458b9f */
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x1cu));
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458bbd */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  args[--sp] = 0x9c4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458bcf */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c80u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458bdc indirect */
  BB_U32(0x488c7cu) = eax;
L_458be3:;
  SAMPLE_CALL(esi, 0); /* 00458be3 indirect */
  ecx = BB_U32(0x488c84u);
  edx = BB_U32(0x488c88u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458c81;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458bff */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458c0e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458c1b */
  BB_U32(esp + 0x1cu) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x14u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x1cu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457f80_entry), 0); /* 00458c35 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458c41 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0xfa0u;
  esp -= 4u;
  args[--sp] = 0x5dcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458c53 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c88u) = eax;
  SAMPLE_CALL(esi, 0); /* 00458c60 indirect */
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x488c84u) = eax;
  esi = args[sp++];
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
L_458c6d:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458c81;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458c81;
  BB_U32(0x488b0cu) = 0x0u;
L_458c81:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
bb_sound_cue_458ad0_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458ad0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458c90_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b18u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458d1f;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458d1f;
  BB_U32(0x488b18u) = 0x1u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458cca */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c8cu) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00458cd7 indirect */
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482308u);
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488c90u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xce4u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0x672u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457d30_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6baf38u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00458d12 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_458d1f:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458de6;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458de6;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 00458d37 indirect */
  ecx = BB_U32(0x488c90u);
  edx = BB_U32(0x488c8cu);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_458db7;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458d4f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458d5e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458d6b */
  BB_U32(esp + 0x20u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x10u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457e50_entry), 0); /* 00458d85 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00458d91 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00458da3 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488c8cu) = eax;
  SAMPLE_CALL(esi, 0); /* 00458db0 indirect */
  BB_U32(0x488c90u) = eax;
L_458db7:;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482308u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6baf38u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00458dcc */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6baf38u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00458dd9 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_458de6:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458e07;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458e07;
  args[--sp] = 0x6baf38u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00458df5 */
  BB_U32(0x488b18u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_458e07:;
  esi = args[sp++];
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
bb_sound_cue_458c90_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458c90_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458e10_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(esp + 0x4u);
  eax = BB_U32(0x488ae8u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458e59;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_458e59;
  args[--sp] = 0x488ca8u;
  esp -= 4u;
  args[--sp] = 0x488ca0u;
  esp -= 4u;
  args[--sp] = 0x488c98u;
  esp -= 4u;
  args[--sp] = 0x488ca4u;
  esp -= 4u;
  args[--sp] = 0x488c9cu;
  esp -= 4u;
  args[--sp] = 0x488c94u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6baf90u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_transition_entry), 0); /* 00458e46 */
  BB_U32(0x488ae8u) = 0x1u;
  a = esp;
  b = 0x20u;
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
L_458e59:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458f7e;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458f7e;
  args[--sp] = 0x488ca8u;
  esp -= 4u;
  args[--sp] = 0x488ca0u;
  esp -= 4u;
  args[--sp] = 0x488c98u;
  esp -= 4u;
  args[--sp] = 0x488ca4u;
  esp -= 4u;
  args[--sp] = 0x488c9cu;
  esp -= 4u;
  args[--sp] = 0x488c94u;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x6baf90u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_transition_entry), 0); /* 00458e90 */
  a = esp;
  b = 0x20u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488c94u);
  a = eax;
  b = 0x200u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_458f08;
  if (flags & 0x40u)
    goto L_458ef3;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458eb7;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458ecb;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458edf;
  goto L_458f08;
L_458eb7:;
  eax = BB_U32(0x488ca4u);
  ecx = BB_U32(0x488c9cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458660_entry), 0); /* 00458ec4 */
  goto L_458f05;
L_458ecb:;
  eax = BB_U32(0x488ca4u);
  ecx = BB_U32(0x488c9cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458890_entry), 0); /* 00458ed8 */
  goto L_458f05;
L_458edf:;
  eax = BB_U32(0x488ca4u);
  ecx = BB_U32(0x488c9cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458ad0_entry), 0); /* 00458eec */
  goto L_458f05;
L_458ef3:;
  eax = BB_U32(0x488ca4u);
  ecx = BB_U32(0x488c9cu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458c90_entry), 0); /* 00458f00 */
L_458f05:;
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_458f08:;
  eax = BB_U32(0x488c98u);
  a = eax;
  b = 0x200u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_458f92;
  if (flags & 0x40u)
    goto L_458f68;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458f26;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458f3c;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_458f52;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_458f26:;
  eax = BB_U32(0x488ca8u);
  ecx = BB_U32(0x488ca0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458660_entry), 0); /* 00458f33 */
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
L_458f3c:;
  eax = BB_U32(0x488ca8u);
  ecx = BB_U32(0x488ca0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458890_entry), 0); /* 00458f49 */
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
L_458f52:;
  eax = BB_U32(0x488ca8u);
  ecx = BB_U32(0x488ca0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458ad0_entry), 0); /* 00458f5f */
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
L_458f68:;
  eax = BB_U32(0x488ca8u);
  ecx = BB_U32(0x488ca0u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458c90_entry), 0); /* 00458f75 */
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
L_458f7e:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458f92;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_458f92;
  BB_U32(0x488ae8u) = 0x0u;
L_458f92:;
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
bb_sound_cue_458e10_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458e10_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_458fa0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
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
  F64(esp + 0x4u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x10u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488af8u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459008;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459008;
  fp[depth] = F64(esp);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482310u);
  BB_U32(0x488af8u) = 0x1u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x15e0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0xaf0u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457bf0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bafd0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00458fff */
  a = esp;
  b = 0x2cu;
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
L_459008:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45903d;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45903d;
  fp[depth] = F64(esp);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482310u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bafd0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 00459027 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bafd0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00459034 */
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
L_45903d:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45905e;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45905e;
  args[--sp] = 0x6bafd0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 0045904c */
  BB_U32(0x488af8u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_45905e:;
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
bb_sound_cue_458fa0_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_458fa0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_459070_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0xcu) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b04u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459159;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459159;
  BB_U32(0x488b04u) = 0x1u;
  BB_U32(0x488cacu) = 0x0u;
  BB_U32(0x488cc8u) = 0x0u;
  BB_U32(0x488cccu) = 0x28u;
  BB_U32(0x488cd0u) = 0x40u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004590da */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  esi = BB_U32(0x6ca378u);
  BB_U32(0x488ce4u) = eax;
  SAMPLE_CALL(esi, 0); /* 004590ed indirect */
  args[--sp] = 0x9c40u;
  esp -= 4u;
  BB_U32(0x488ce0u) = eax;
  args[--sp] = 0x2710u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004590fe */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cd8u) = eax;
  SAMPLE_CALL(esi, 0); /* 0045910b indirect */
  fp[depth] = F64(esp + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482318u);
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488cd4u) = eax;
  BB_U32(0x488cb0u) = 0x0u;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xfa0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457de0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb030u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 0045914c */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_459159:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459476;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459476;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 00459171 indirect */
  ecx = BB_U32(0x488ce0u);
  edx = BB_U32(0x488ce4u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_4592bf;
  eax = BB_U32(0x488cacu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4591bc;
  eax = BB_U32(0x488cd0u);
  ecx = 0x7fu;
  a = eax;
  b = 0x75u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4591a4;
  ecx = (eax + 0xau);
L_4591a4:;
  a = eax;
  b = 0xau;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4591ae;
  a = eax;
  b = 0xau;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  goto L_4591b0;
L_4591ae:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_4591b0:;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004591b2 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  goto L_4591cd;
L_4591bc:;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004591c0 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cd0u) = eax;
L_4591cd:;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(0x488cc8u);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004591d9 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488cccu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004591eb */
  BB_U32(esp + 0x18u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x14u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(0x488a44u);
  args[--sp] = 0xdu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 0045920e */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488cacu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45928a;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459227 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x12cu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_45925d;
  args[--sp] = 0x190u;
  esp -= 4u;
  args[--sp] = 0x64u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045923d */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488ce4u) = eax;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045924e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cacu) = eax;
  goto L_459274;
L_45925d:;
  args[--sp] = 0x2710u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459267 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488ce4u) = eax;
L_459274:;
  BB_U32(0x488cc8u) = 0x0u;
  BB_U32(0x488cccu) = 0x28u;
  goto L_4592b8;
L_45928a:;
  args[--sp] = 0x190u;
  esp -= 4u;
  args[--sp] = 0x64u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459291 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488ce4u) = eax;
  BB_U32(0x488cc8u) = 0x1u;
  BB_U32(0x488cccu) = 0x40u;
  a = BB_U32(0x488cacu);
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  BB_U32(0x488cacu) = result;
L_4592b8:;
  SAMPLE_CALL(esi, 0); /* 004592b8 indirect */
  BB_U32(0x488ce0u) = eax;
L_4592bf:;
  SAMPLE_CALL(esi, 0); /* 004592bf indirect */
  ecx = BB_U32(0x488cd4u);
  edx = BB_U32(0x488cd8u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_459447;
  eax = BB_U32(0x488cb0u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4593a5;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004592e8 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cbcu) = eax;
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004592f9 */
  BB_U32(esp + 0xcu) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x4u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  BB_U32(0x488cc0u) = eax;
  esi = eax;
  eax = BB_U32(0x488cbcu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0xfffffffeu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459325 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0xeu;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00459337 */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cb4u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_459447;
  args[--sp] = 0xbb8u;
  esp -= 4u;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459356 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cdcu) = eax;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459367 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x488cdcu);
  BB_U32(0x488cc4u) = eax;
  args[--sp] = 0x488cbcu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb028u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 00459389 */
  a = esp;
  b = 0x14u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cb8u) = eax;
  BB_U32(0x488cb0u) = 0x1u;
  goto L_459447;
L_4593a5:;
  eax = BB_U32(0x488cb4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_459447;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_active_entry), 0); /* 004593b3 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45941f;
  args[--sp] = 0x488cbcu;
  esp -= 4u;
  eax = BB_U32(0x488cdcu);
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = BB_U32(0x488cc4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x6bb028u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_ramp_step_entry), 0); /* 004593d8 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cb8u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459447;
  eax = BB_U32(0x488cbcu);
  ecx = BB_U32(0x488cb4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_pan_entry), 0); /* 004593f6 */
  fp[depth++] = (long double)(int32_t)(BB_U32(0x488cc0u));
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(0x488cb4u);
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_volume_entry), 0); /* 00459415 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_459447;
L_45941f:;
  args[--sp] = 0x9c40u;
  esp -= 4u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459429 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cd8u) = eax;
  SAMPLE_CALL(esi, 0); /* 00459436 indirect */
  BB_U32(0x488cb0u) = 0x0u;
  BB_U32(0x488cd4u) = eax;
L_459447:;
  fp[depth] = F64(esp + 0x8u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482318u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb030u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 0045945c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bb030u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00459469 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_459476:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459497;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459497;
  args[--sp] = 0x6bb030u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00459485 */
  BB_U32(0x488b04u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_459497:;
  esi = args[sp++];
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
bb_sound_cue_459070_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_459070_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4594a0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b10u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45952f;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45952f;
  BB_U32(0x488b10u) = 0x1u;
  args[--sp] = 0x4e20u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004594da */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cecu) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 004594e7 indirect */
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482320u);
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488ce8u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1130u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0x898u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457cc0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb088u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 00459522 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_45952f:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4595f2;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4595f2;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 00459547 indirect */
  ecx = BB_U32(0x488ce8u);
  edx = BB_U32(0x488cecu);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_4595c3;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045955f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045956e */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045957b */
  BB_U32(esp + 0x20u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x10u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xfu;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 0045959d */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x4e20u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004595af */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cecu) = eax;
  SAMPLE_CALL(esi, 0); /* 004595bc indirect */
  BB_U32(0x488ce8u) = eax;
L_4595c3:;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482320u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb088u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 004595d8 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bb088u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 004595e5 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_4595f2:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459613;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459613;
  args[--sp] = 0x6bb088u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00459601 */
  BB_U32(0x488b10u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_459613:;
  esi = args[sp++];
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
bb_sound_cue_4594a0_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4594a0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_459620_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
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
  F64(esp + 0x8u) = (double)(fp[depth - 1]);
  --depth;
  ecx = BB_U32(esp + 0x18u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488b1cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4596af;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4596af;
  BB_U32(0x488b1cu) = 0x1u;
  args[--sp] = 0x4e20u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 0045965a */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cf4u) = eax;
  SAMPLE_CALL(BB_U32(0x6ca378u), 0); /* 00459667 indirect */
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482328u);
  args[--sp] = 0x40u;
  esp -= 4u;
  BB_U32(0x488cf0u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xfa0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = BB_AUDIO_TARGET(bb_sound_selector_457de0_entry);
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb0e0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_init_entry), 0); /* 004596a2 */
  a = esp;
  b = 0x24u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_4596af:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459776;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459776;
  esi = BB_U32(0x6ca378u);
  SAMPLE_CALL(esi, 0); /* 004596c7 indirect */
  ecx = BB_U32(0x488cf0u);
  edx = BB_U32(0x488cf4u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_459747;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004596df */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = 0xfffffffeu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004596ee */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x28u;
  esp -= 4u;
  args[--sp] = 0xfu;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 004596fb */
  BB_U32(esp + 0x20u) = eax;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  fp[depth - 1] = fp[depth - 1] * F64(esp + 0x10u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selector_457e20_entry), 0); /* 00459715 */
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x488a44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00459721 */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x4e20u;
  esp -= 4u;
  args[--sp] = 0x1388u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459733 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488cf4u) = eax;
  SAMPLE_CALL(esi, 0); /* 00459740 indirect */
  BB_U32(0x488cf0u) = eax;
L_459747:;
  fp[depth] = F64(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482328u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x6bb0e0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_volume_entry), 0); /* 0045975c */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x6bb0e0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_crossfade_step_entry), 0); /* 00459769 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = args[sp++];
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
L_459776:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459797;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459797;
  args[--sp] = 0x6bb0e0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_sequence_stop_entry), 0); /* 00459785 */
  BB_U32(0x488b1cu) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_459797:;
  esi = args[sp++];
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
bb_sound_cue_459620_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_459620_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_cue_4597a0_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(esp + 0x4u);
  eax = BB_U32(0x488aecu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4597e9;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4597e9;
  args[--sp] = 0x488d0cu;
  esp -= 4u;
  args[--sp] = 0x488d04u;
  esp -= 4u;
  args[--sp] = 0x488cfcu;
  esp -= 4u;
  args[--sp] = 0x488d08u;
  esp -= 4u;
  args[--sp] = 0x488d00u;
  esp -= 4u;
  args[--sp] = 0x488cf8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x6bb138u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_transition_entry), 0); /* 004597d6 */
  BB_U32(0x488aecu) = 0x1u;
  a = esp;
  b = 0x20u;
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
L_4597e9:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45990e;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45990e;
  args[--sp] = 0x488d0cu;
  esp -= 4u;
  args[--sp] = 0x488d04u;
  esp -= 4u;
  args[--sp] = 0x488cfcu;
  esp -= 4u;
  args[--sp] = 0x488d08u;
  esp -= 4u;
  args[--sp] = 0x488d00u;
  esp -= 4u;
  args[--sp] = 0x488cf8u;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x6bb138u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_transition_entry), 0); /* 00459820 */
  a = esp;
  b = 0x20u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x488cf8u);
  a = eax;
  b = 0x200u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_459898;
  if (flags & 0x40u)
    goto L_459883;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459847;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45985b;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45986f;
  goto L_459898;
L_459847:;
  eax = BB_U32(0x488d08u);
  ecx = BB_U32(0x488d00u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458fa0_entry), 0); /* 00459854 */
  goto L_459895;
L_45985b:;
  eax = BB_U32(0x488d08u);
  ecx = BB_U32(0x488d00u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_459070_entry), 0); /* 00459868 */
  goto L_459895;
L_45986f:;
  eax = BB_U32(0x488d08u);
  ecx = BB_U32(0x488d00u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4594a0_entry), 0); /* 0045987c */
  goto L_459895;
L_459883:;
  eax = BB_U32(0x488d08u);
  ecx = BB_U32(0x488d00u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_459620_entry), 0); /* 00459890 */
L_459895:;
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_459898:;
  eax = BB_U32(0x488cfcu);
  a = eax;
  b = 0x200u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_459922;
  if (flags & 0x40u)
    goto L_4598f8;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4598b6;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4598cc;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4598e2;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_4598b6:;
  eax = BB_U32(0x488d0cu);
  ecx = BB_U32(0x488d04u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458fa0_entry), 0); /* 004598c3 */
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
L_4598cc:;
  eax = BB_U32(0x488d0cu);
  ecx = BB_U32(0x488d04u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_459070_entry), 0); /* 004598d9 */
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
L_4598e2:;
  eax = BB_U32(0x488d0cu);
  ecx = BB_U32(0x488d04u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4594a0_entry), 0); /* 004598ef */
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
L_4598f8:;
  eax = BB_U32(0x488d0cu);
  ecx = BB_U32(0x488d04u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_459620_entry), 0); /* 00459905 */
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
L_45990e:;
  a = ecx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459922;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459922;
  BB_U32(0x488aecu) = 0x0u;
L_459922:;
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
bb_sound_cue_4597a0_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_cue_4597a0_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_init_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459941;
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
L_459941:;
  BB_U32(0x488a60u) = 0x8u;
  args[--sp] = 0x8u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_session_init_entry), 0); /* 0045994f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  esi = eax;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_effects_init_entry), 0); /* 00459959 */
  a = eax;
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  if (flags & 0x40u)
    goto L_45996e;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_session_shutdown_entry), 0); /* 00459962 */
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
L_45996e:;
  esi = 0x1u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_session_enable_entry), 0); /* 00459974 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x488a54u) = eax;
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4599a3;
  args[--sp] = 0x488a58u;
  esp -= 4u;
  BB_U32(0x488a58u) = esi;
  args[--sp] = 0x488a5cu;
  esp -= 4u;
  BB_U32(0x488a5cu) = esi;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_session_options_entry), 0); /* 0045999b */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_4599a3:;
  BB_U32(0x488a6cu) = 0x1u;
  BB_U32(0x488a64u) = 0x0u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_states_clear_entry), 0);       /* 004599b7 */
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_clock_seed_entry), 0); /* 004599bc */
  args[--sp] = 0x488a50u;
  esp -= 4u;
  args[--sp] = 0x488d10u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_bank_load_entry), 0); /* 004599cb */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488a48u) = eax;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 004599e3 */
  a = esp;
  b = 0x18u;
  esp += b;
  sp += b / 4u;
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_sound_init_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_init_c) ";leal 4(%esp),%esp;popal;popfl;leal 64(%esp),%esp;ret "
                       "$0");
}
#endif
void bb_sound_shutdown_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_4599ff;
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
L_4599ff:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_session_shutdown_entry), 0); /* 004599ff */
  BB_U32(0x488a6cu) = 0x0u;
  eax = BB_U32(0x488a44u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459a20;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_bank_free_entry), 0); /* 00459a18 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_459a20:;
  eax = BB_U32(0x488a48u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459a32;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_bank_free_entry), 0); /* 00459a2a */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_459a32:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(0x488a48u) = eax;
  BB_U32(0x488a44u) = eax;
  BB_U32(0x488a50u) = eax;
  BB_U32(0x488a4cu) = eax;
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
uint32_t __attribute__((naked)) bb_sound_shutdown_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_shutdown_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_enable_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459b73;
  ecx = BB_U32(esp + 0x4u);
  eax = BB_U32(0x488a64u);
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459b60;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459b60;
  BB_U32(0x488a64u) = 0x1u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channels_release_entry), 0); /* 00459b56 */
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_current_stop_entry), 0);      /* tail call 00459b5b */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459b60:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459b73;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459b73;
  BB_U32(0x488a64u) = 0x0u;
L_459b73:;
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
uint32_t __attribute__((naked)) bb_sound_enable_entry(uint32_t arg0
                                                      __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_enable_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                         "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_event_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459c60;
  eax = BB_U32(0x488a64u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459c60;
  eax = BB_U32(esp + 0x8u);
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_459c60;
  switch (eax) {
  case 0:
    goto L_459bb1;
  case 1:
    goto L_459bc5;
  case 2:
    goto L_459bc5;
  case 3:
    goto L_459bc5;
  case 4:
    goto L_459bc5;
  case 5:
    goto L_459bdd;
  case 6:
    goto L_459c1a;
  case 7:
    goto L_459c26;
  case 8:
    goto L_459c32;
  case 9:
    goto L_459c3e;
  case 10:
    goto L_459c4a;
  case 11:
    goto L_459c56;
  case 12:
    goto L_459bb1;
  case 13:
    goto L_459bb1;
  case 14:
    goto L_459bb1;
  default:
    __builtin_unreachable();
  }
L_459bb1:;
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459c60;
  esi = args[sp++];
  esp += 4u;
  BB_U32(0x488a74u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459bc5:;
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459c60;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_mode_set_entry), 0); /* 00459bd3 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
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
L_459bdd:;
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459c12;
  eax = BB_U32(0x488a78u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459c12;
  args[--sp] = 0x7fu;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459bf3 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x488a48u);
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_channel_play_entry), 0); /* 00459c0a */
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_459c12:;
  BB_U32(0x488a78u) = esi;
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
L_459c1a:;
  esi = BB_U32(esp + 0xcu);
  BB_U32(0x488a7cu) = esi;
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
L_459c26:;
  esi = BB_U32(esp + 0xcu);
  BB_U32(0x488a80u) = esi;
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
L_459c32:;
  esi = BB_U32(esp + 0xcu);
  BB_U32(0x488a84u) = esi;
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
L_459c3e:;
  esi = BB_U32(esp + 0xcu);
  BB_U32(0x488a88u) = esi;
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
L_459c4a:;
  esi = BB_U32(esp + 0xcu);
  BB_U32(0x488a8cu) = esi;
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
L_459c56:;
  esi = BB_U32(esp + 0xcu);
  BB_U32(0x488a90u) = esi;
L_459c60:;
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
uint32_t __attribute__((naked)) bb_sound_event_entry(uint32_t arg0
                                                     __attribute__((unused)),
                                                     uint32_t arg1
                                                     __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_event_c) ";leal 4(%esp),%esp;popal;popfl;leal 64(%esp),%esp;ret "
                        "$0");
}
#endif
void bb_sound_property_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459d43;
  eax = BB_U32(0x488a64u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459d43;
  eax = BB_U32(esp + 0x4u);
  a = eax;
  b = 0xbu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_459d43;
  switch (eax) {
  case 0:
    goto L_459ccc;
  case 1:
    goto L_459cd6;
  case 2:
    goto L_459ce0;
  case 3:
    goto L_459cea;
  case 4:
    goto L_459cf4;
  case 5:
    goto L_459cfe;
  case 6:
    goto L_459d08;
  case 7:
    goto L_459d12;
  case 8:
    goto L_459d1c;
  case 9:
    goto L_459d26;
  case 10:
    goto L_459d30;
  case 11:
    goto L_459d3a;
  default:
    __builtin_unreachable();
  }
L_459ccc:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488aa4u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459cd6:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488aa8u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459ce0:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488a94u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459cea:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488a98u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459cf4:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488a9cu) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459cfe:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488aa0u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459d08:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488aacu) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459d12:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488ab0u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459d1c:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488abcu) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459d26:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488ac0u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459d30:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488ab4u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_459d3a:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(0x488ab8u) = eax;
L_459d43:;
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
bb_sound_property_entry(uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_property_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "64(%esp),%esp;ret $0");
}
#endif
void bb_sound_update_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(0x6bb17cu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_459da0;
  a = ((uint8_t)(eax));
  b = 0x1u;
  result = (a | b) & 0xffu;
  flags = logic_flags(flags, result, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  esi = BB_U32(0x6ca378u);
  BYTE(0x6bb17cu) = ((uint8_t)(eax));
  SAMPLE_CALL(esi, 0); /* 00459d97 indirect */
  BB_U32(0x6bb178u) = eax;
  goto L_459da6;
L_459da0:;
  esi = BB_U32(0x6ca378u);
L_459da6:;
  eax = BB_U32(0x488a6cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a2d7;
  eax = BB_U32(0x488a64u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459dca;
  BB_U32(0x488d2cu) = 0x0u;
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
L_459dca:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_selection_update_entry), 0); /* 00459dca */
  eax = BB_U32(0x488a70u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459e1c;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459e1c;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459dfd;
  eax = BB_U32(0x488b40u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_458e10_entry), 0); /* 00459de9 */
  BB_U32(0x488b40u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_459e34;
L_459dfd:;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459e34;
  eax = BB_U32(0x488b44u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4597a0_entry), 0); /* 00459e08 */
  BB_U32(0x488b44u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_459e34;
L_459e1c:;
  eax = BB_U32(0x488b3cu);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4584d0_entry), 0); /* 00459e22 */
  BB_U32(0x488b3cu) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_459e34:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_count_entry), 0); /* 00459e34 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459ecf;
  eax = BB_U32(0x488a70u);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459e50;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459ecf;
L_459e50:;
  eax = BB_U32(0x488a78u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459ecf;
  SAMPLE_CALL(esi, 0); /* 00459e59 indirect */
  eax = (eax - BB_U32(0x6bb178u));
  a = eax;
  b = 0x7d0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_459ecf;
  eax = BB_U32(0x488d2cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459ec8;
  args[--sp] = 0x7d0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459e78 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x32u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_459ec8;
  args[--sp] = 0x6u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_activate_entry), 0); /* 00459e87 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488b38u) = 0x0u;
  args[--sp] = 0x40u;
  esp -= 4u;
  args[--sp] = 0xau;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459e9d */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488d24u) = eax;
  args[--sp] = 0xea60u;
  esp -= 4u;
  args[--sp] = 0x3a98u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sample_random_range_entry), 0); /* 00459eb4 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488d2cu) = eax;
  SAMPLE_CALL(esi, 0); /* 00459ec1 indirect */
  BB_U32(0x488d28u) = eax;
L_459ec8:;
  SAMPLE_CALL(esi, 0); /* 00459ec8 indirect */
  BB_U32(0x6bb178u) = eax;
L_459ecf:;
  args[--sp] = 0x6u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00459ed1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459f53;
  SAMPLE_CALL(esi, 0); /* 00459edd indirect */
  ecx = BB_U32(0x488d28u);
  edx = BB_U32(0x488d2cu);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_459f2b;
  eax = BB_U32(0x488a70u);
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459f2b;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459f2b;
  eax = BB_U32(0x488a78u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459f2b;
  eax = BB_U32(0x488d24u);
  ecx = BB_U32(0x488b38u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4579e0_entry), 0); /* 00459f17 */
  BB_U32(0x488b38u) = 0x1u;
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_459f53;
L_459f2b:;
  eax = BB_U32(0x488d24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4579e0_entry), 0); /* 00459f33 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459f53;
  args[--sp] = 0x6u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 00459f41 */
  BB_U32(0x488d2cu) = 0x0u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_459f53:;
  args[--sp] = 0x4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00459f55 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_459fe6;
  ecx = BB_U32(0x488a7cu);
  eax = BB_U32(0x488b20u);
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459f7d;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_459f7d;
  args[--sp] = eax;
  esp -= 4u;
  goto L_459fd4;
L_459f7d:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459fbe;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459f92;
  BB_U32(0x488b20u) = 0x3u;
  goto L_459fe6;
L_459f92:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459fbe;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459fbe;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456aa0_entry), 0); /* 00459f9c */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_459fe6;
  BB_U32(0x488b20u) = 0x0u;
  args[--sp] = 0x4u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 00459fb4 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_459fe6;
L_459fbe:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459fe6;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_459fe6;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456aa0_entry), 0); /* 00459fca */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
L_459fd4:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456aa0_entry), 0); /* 00459fd4 */
  BB_U32(0x488b20u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_459fe6:;
  args[--sp] = 0x3u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 00459fe8 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45a079;
  ecx = BB_U32(0x488a80u);
  eax = BB_U32(0x488b24u);
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a010;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45a010;
  args[--sp] = eax;
  esp -= 4u;
  goto L_45a067;
L_45a010:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a051;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a025;
  BB_U32(0x488b24u) = 0x3u;
  goto L_45a079;
L_45a025:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a051;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a051;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456d20_entry), 0); /* 0045a02f */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a079;
  BB_U32(0x488b24u) = 0x0u;
  args[--sp] = 0x3u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 0045a047 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_45a079;
L_45a051:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a079;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a079;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456d20_entry), 0); /* 0045a05d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
L_45a067:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456d20_entry), 0); /* 0045a067 */
  BB_U32(0x488b24u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_45a079:;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 0045a07b */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45a10c;
  eax = BB_U32(0x488a84u);
  ecx = BB_U32(0x488b28u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a0a3;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45a0a3;
  args[--sp] = ecx;
  esp -= 4u;
  goto L_45a0fa;
L_45a0a3:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a0e4;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a0b8;
  BB_U32(0x488b28u) = 0x3u;
  goto L_45a10c;
L_45a0b8:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a0e4;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a0e4;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456ee0_entry), 0); /* 0045a0c2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a10c;
  BB_U32(0x488b28u) = 0x0u;
  args[--sp] = 0x5u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 0045a0da */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_45a10c;
L_45a0e4:;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a10c;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a10c;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456ee0_entry), 0); /* 0045a0f0 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
L_45a0fa:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_456ee0_entry), 0); /* 0045a0fa */
  BB_U32(0x488b28u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_45a10c:;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 0045a10e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45a19f;
  eax = BB_U32(0x488a88u);
  ecx = BB_U32(0x488b2cu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a136;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45a136;
  args[--sp] = ecx;
  esp -= 4u;
  goto L_45a18d;
L_45a136:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a177;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a14b;
  BB_U32(0x488b2cu) = 0x3u;
  goto L_45a19f;
L_45a14b:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a177;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a177;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4573d0_entry), 0); /* 0045a155 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a19f;
  BB_U32(0x488b2cu) = 0x0u;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 0045a16d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_45a19f;
L_45a177:;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a19f;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a19f;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4573d0_entry), 0); /* 0045a183 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
L_45a18d:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_4573d0_entry), 0); /* 0045a18d */
  BB_U32(0x488b2cu) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_45a19f:;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 0045a1a1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45a232;
  ecx = BB_U32(0x488a8cu);
  eax = BB_U32(0x488b30u);
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a1c9;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45a1c9;
  args[--sp] = eax;
  esp -= 4u;
  goto L_45a220;
L_45a1c9:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a20a;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a1de;
  BB_U32(0x488b30u) = 0x3u;
  goto L_45a232;
L_45a1de:;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a20a;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a20a;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457590_entry), 0); /* 0045a1e8 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a232;
  BB_U32(0x488b30u) = 0x0u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 0045a200 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_45a232;
L_45a20a:;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a232;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a232;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457590_entry), 0); /* 0045a216 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
L_45a220:;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457590_entry), 0); /* 0045a220 */
  BB_U32(0x488b30u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_45a232:;
  args[--sp] = 0x1u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_active_entry), 0); /* 0045a234 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45a2d7;
  eax = BB_U32(0x488a90u);
  ecx = BB_U32(0x488b34u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a26e;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_45a26e;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457820_entry), 0); /* 0045a25a */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(0x488b34u) = 0x1u;
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
L_45a26e:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a2af;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a283;
  BB_U32(0x488b34u) = 0x3u;
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
L_45a283:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a2af;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a2af;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457820_entry), 0); /* 0045a28d */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45a2d7;
  BB_U32(0x488b34u) = 0x0u;
  args[--sp] = 0x1u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_state_clear_entry), 0); /* 0045a2a5 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
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
L_45a2af:;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a2d7;
  a = ecx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_45a2d7;
  args[--sp] = 0x2u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457820_entry), 0); /* 0045a2bb */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(BB_AUDIO_TARGET(bb_sound_cue_457820_entry), 0); /* 0045a2c5 */
  BB_U32(0x488b34u) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_45a2d7:;
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
uint32_t __attribute__((naked)) bb_sound_update_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_sound_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                         "64(%esp),%esp;ret $0");
}
#endif
