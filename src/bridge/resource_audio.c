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
/* First C forms of resource mixer and sound decoder. Address-derived labels
 * retain provenance for auditing decoding, synthesis and mixer callback
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
/* Numeric comparison compatibility while the i386 boundary remains. */
static uint16_t audio_compare(const long double *a, const long double *b) {
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
/* C forms of resource playback, entropy decoding and synthesis. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */
void bb_audio_resource_volume_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(0x6c8838u);
  edx = BB_U32(esp + 0x4u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(eax);
  AUDIO_CALL(BB_U32(ecx + 0x3cu), 8); /* 0046a17d indirect */
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
uint32_t __attribute__((naked))
bb_audio_resource_volume_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_resource_volume_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_resource_init_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
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
  args[--sp] = 0x10u;
  esp -= 4u;
  edx = 0x2000u;
  args[--sp] = 0x5622u;
  esp -= 4u;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(0x6c883cu) = edx;
  BB_U32(0x6c1c90u) = 0x0u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_buffer_create_entry), 12); /* 0046a1b2 */
  BB_U32(0x6c8838u) = eax;
  eax = 0x6c1cbcu;
L_46a1c1:;
  BB_U32(eax) = 0x0u;
  eax = (eax + 0xd74u);
  a = eax;
  b = 0x6c885cu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a1c1;
  args[--sp] = BB_AUDIO_TARGET(bb_audio_timer_callback_entry);
  esp -= 4u;
  edx = 0xau;
  ecx = 0x6c8840u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_timer_start_entry), 4); /* 0046a1e2 */
  eax = BB_U32(0x6c8838u);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(0x6c8850u) = 0x1u;
  AUDIO_CALL(BB_U32(ecx + 0x30u), 16); /* 0046a1ff indirect */
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
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
uint32_t __attribute__((naked))
bb_audio_resource_init_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_resource_init_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_timer_start_c(uint32_t input[9]) {
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
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = (esp);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = 0x8u;
  esp -= 4u;
  edi = edx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esi) = 0x0u;
  AUDIO_CALL(BB_U32(0x6ca57cu), 8); /* 0046a226 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a23d;
  eax = 0x1u;
  edi = args[sp++];
  esp += 4u;
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
L_46a23d:;
  eax = BB_U32(esp + 0x8u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  ecx = eax;
  if (!(flags & 0x41u))
    goto L_46a24d;
  ecx = 0x1u;
L_46a24d:;
  edx = BB_U32(esp + 0xcu);
  a = ecx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_46a261;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_46a263;
  eax = 0x1u;
  goto L_46a263;
L_46a261:;
  eax = edx;
L_46a263:;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esi + 0xcu) = eax;
  AUDIO_CALL(BB_U32(0x6ca578u), 4); /* 0046a267 indirect */
  ecx = (esi + 0x4u);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca3b8u), 8); /* 0046a273 indirect */
  eax = BB_U32(esi + 0xcu);
  edx = BB_U32(esp + 0x14u);
  /* Drain an entered callback before shutdown releases its DirectSound buffer.
   * A periodic callback can enter before it acquires the resource's busy gate. */
  args[--sp] = 0x101u; /* TIME_PERIODIC | TIME_KILL_SYNCHRONOUS */
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca574u), 20); /* 0046a286 indirect */
  BB_U32(esi + 0x8u) = eax;
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  edi = args[sp++];
  esp += 4u;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) & 0xfeu)) & 0xffu) << 0);
  esi = args[sp++];
  esp += 4u;
  eax = (eax + 0x2u);
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
uint32_t __attribute__((naked)) bb_audio_timer_start_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_timer_start_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "8(%esp),%esp;ret $4");
}
#endif
void bb_audio_buffer_create_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 52u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x30u;
  esp -= b;
  sp -= b / 4u;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebp = edx;
  if (flags & 0x40u)
    goto L_46a3e8;
  eax = (eax ^ eax);
  ebx = BB_U32(esp + 0x4cu);
  BB_U32(esp + 0x18u) = eax;
  edi = BB_U32(esp + 0x44u);
  BB_U32(esp + 0x1cu) = eax;
  esi = BB_U32(esp + 0x48u);
  BB_U32(esp + 0x20u) = eax;
  BB_U32(esp + 0x1cu) = esi;
  BB_U32(esp + 0x24u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  WORD(esp + 0x2cu) = ((uint16_t)(eax));
  eax = ebx;
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x7u);
  WORD(esp + 0x1cu) = 0x1u;
  eax = (eax + edx);
  WORD(esp + 0x1eu) = ((uint16_t)(edi));
  eax = ((uint32_t)((int32_t)(eax) >> 3));
  a = eax;
  b = edi;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  WORD(esp + 0x28u) = ((uint16_t)(eax));
  edx = BB_U32(esp + 0x28u);
  eax = (eax ^ eax);
  a = edx;
  b = 0xffffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edx = result;
  BB_U32(esp + 0x30u) = eax;
  a = esi;
  b = edx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  flags = (flags & ~0x801u) |
          (((int64_t)wide != (int64_t)(int32_t)result) ? 0x801u : 0u);
  esi = result;
  BB_U32(esp + 0x34u) = eax;
  edx = (esp + 0x1cu);
  BB_U32(esp + 0x38u) = eax;
  BB_U32(esp + 0x24u) = esi;
  BB_U32(esp + 0x3cu) = eax;
  WORD(esp + 0x2au) = ((uint16_t)(ebx));
  BB_U32(esp + 0x40u) = eax;
  eax = BB_U32(ecx);
  BB_U32(esp + 0x40u) = edx;
  edx = (esp + 0x48u);
  args[--sp] = edx;
  esp -= 4u;
  edx = (esp + 0x34u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp + 0x3cu) = 0x14u;
  BB_U32(esp + 0x40u) = 0x10080u;
  BB_U32(esp + 0x44u) = ebp;
  AUDIO_CALL(BB_U32(eax + 0xcu), 16); /* 0046a34e indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46a3e8;
  edx = (esp + 0x48u);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(esp + 0x48u);
  args[--sp] = edx;
  esp -= 4u;
  edx = (esp + 0x54u);
  ecx = BB_U32(eax);
  args[--sp] = edx;
  esp -= 4u;
  edx = (esp + 0x1cu);
  args[--sp] = edx;
  esp -= 4u;
  edx = (esp + 0x24u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_U32(ecx + 0x2cu), 32); /* 0046a379 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46a3de;
  ecx = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0x14u);
  edx = ecx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = edx;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  ecx = BB_U32(esp + 0x48u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a3b6;
  edi = BB_U32(esp + 0x4cu);
  edx = ecx;
  eax = (eax ^ eax);
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = edx;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  ecx = BB_U32(esp + 0x48u);
L_46a3b6:;
  eax = BB_U32(esp + 0x44u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x50u);
  edx = BB_U32(eax);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_U32(edx + 0x4cu), 20); /* 0046a3cd indirect */
  eax = BB_U32(esp + 0x44u);
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x30u;
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
L_46a3de:;
  eax = BB_U32(esp + 0x44u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(eax);
  AUDIO_CALL(BB_U32(edx + 0x8u), 4); /* 0046a3e5 indirect */
L_46a3e8:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  eax = (eax ^ eax);
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x30u;
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
uint32_t __attribute__((naked)) bb_audio_buffer_create_entry(void) {
  __asm__ volatile("leal -48(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_buffer_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "48(%esp),%esp;ret $12");
}
#endif
void bb_audio_timer_callback_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  ebx = BB_U32(0x6ca3b8u);
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  edi = (esi + 0x4u);
  args[--sp] = edi;
  esp -= 4u;
  AUDIO_CALL(ebx, 8); /* 0046a413 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46a428;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_resource_update_entry), 0); /* 0046a419 */
  edx = BB_U32(esi);
  args[--sp] = 0x0u;
  esp -= 4u;
  a = edx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  edx = result;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esi) = edx;
  AUDIO_CALL(ebx, 8); /* 0046a426 indirect */
L_46a428:;
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
bb_audio_timer_callback_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused)),
                              uint32_t arg3 __attribute__((unused)),
                              uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_timer_callback_c) ";leal 4(%esp),%esp;popal;popfl;ret $20");
}
#endif
void bb_audio_resource_update_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 16420u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[4300] = {0}, sp = 4280, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = 0x4020u;
  esp -= 0x4020u;
  sp -= 0x1008u;
  eax = BB_U32(0x6c883cu);
  edx = BB_U32(0x6c1c90u);
  ecx = BB_U32(0x6c8838u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  a = ebx;
  b = ebx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_buffer_gap_entry), 4); /* 0046a452 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  BB_U32(esp + 0x24u) = eax;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a6b5;
  ecx = eax;
  ebp = (esp + 0x30u);
  ecx = ((uint32_t)((int32_t)(ecx) >> 1));
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  BB_U32(esp + 0x2cu) = ecx;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a48a;
  ecx = (ecx + 1u);
  eax = (esp + 0x30u);
  ecx = (ecx >> 1);
  edx = 0x4b400000u;
L_46a47f:;
  BB_U32(eax + 0x4u) = edx;
  BB_U32(eax) = edx;
  eax = (eax + 0x8u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_46a47f;
L_46a48a:;
  esi = 0x6c1cb0u;
L_46a48f:;
  eax = BB_U32(esi + 0xcu);
  edi = (esi + 0xcu);
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46a568;
  fp[depth] = F32(esi + 0xfffffffcu);
  ++depth;
  {
    long double other = F64(0x4823f0u);
    fp_status = audio_compare(&fp[depth - 1], &other);
    --depth;
  }
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x40u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_46a4bc;
  ecx = BB_U32(esi);
  edx = ecx;
  BB_U32(esp + 0x10u) = ecx;
  BB_U32(esp + 0x14u) = edx;
  goto L_46a53b;
L_46a4bc:;
  fp[depth] = F32(esi + 0xfffffffcu);
  ++depth;
  {
    long double other = F64(0x4823f0u);
    fp_status = audio_compare(&fp[depth - 1], &other);
    --depth;
  }
  fp[depth] = F32(esi + 0xfffffffcu);
  ++depth;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_46a509;
  {
    long double other = F64(0x4823f8u);
    fp_status = audio_compare(&fp[depth - 1], &other);
    --depth;
  }
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x41u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_46a4f9;
  fp[depth] = F32(esi + 0xfffffffcu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(0x482400u);
  eax = BB_U32(esi);
  BB_U32(esp + 0x14u) = eax;
  fp[depth - 1] = F32(0x482404u) / fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F32(esi);
  F32(esp + 0x10u) = (float)(fp[depth - 1]);
  --depth;
  goto L_46a53b;
L_46a4f9:;
  eax = BB_U32(esi);
  BB_U32(esp + 0x10u) = 0x0u;
  BB_U32(esp + 0x14u) = eax;
  goto L_46a53b;
L_46a509:;
  {
    long double other = F64(0x482410u);
    fp_status = audio_compare(&fp[depth - 1], &other);
    --depth;
  }
  ecx = BB_U32(esi);
  BB_U32(esp + 0x10u) = ecx;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(fp_status | (((8u - depth) & 7u) << 11)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_46a533;
  fp[depth] = F32(0x482404u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(esi + 0xfffffffcu);
  fp[depth - 1] = F32(0x482404u) / fp[depth - 1];
  fp[depth - 1] = fp[depth - 1] * F32(esi);
  F32(esp + 0x14u) = (float)(fp[depth - 1]);
  --depth;
  goto L_46a53b;
L_46a533:;
  BB_U32(esp + 0x14u) = 0x0u;
L_46a53b:;
  edx = BB_U32(esp + 0x14u);
  eax = BB_U32(esp + 0x10u);
  args[--sp] = edx;
  esp -= 4u;
  edx = BB_U32(esp + 0x30u);
  ecx = (esp + 0x34u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = (esi + 0xffffffe8u);
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voice_mix_entry), 12); /* 0046a551 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a563;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca3b8u), 8); /* 0046a55d indirect */
L_46a563:;
  ebx = 0x1u;
L_46a568:;
  esi = (esi + 0xd74u);
  a = esi;
  b = 0x6c8850u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a48f;
  ecx = (esp + 0x18u);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = (esp + 0x28u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(0x6c8838u);
  ecx = (esp + 0x34u);
  args[--sp] = ecx;
  esp -= 4u;
  edx = BB_U32(eax);
  ecx = (esp + 0x2cu);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x38u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(0x6c1c90u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_U32(edx + 0x2cu), 32); /* 0046a5a4 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46a6b5;
  ecx = BB_U32(esp + 0x28u);
  edi = BB_U32(esp + 0x18u);
  esi = ecx;
  esi = (esi >> 1);
  edi = (edi >> 1);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46a5f8;
  edi = BB_U32(esp + 0x1cu);
  edx = ecx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = edx;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a67e;
  edi = BB_U32(esp + 0x20u);
  edx = ecx;
  eax = (eax ^ eax);
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = edx;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  goto L_46a67e;
L_46a5f8:;
  ecx = (ecx ^ ecx);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a637;
L_46a5fe:;
  eax = BB_U32(ebp);
  ebp = (ebp + 0x4u);
  eax = (eax & 0x1ffffu);
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a629;
  a = eax;
  b = 0x18000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_46a629;
  edx = (edx ^ edx);
  a = eax;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(((!!(flags & 0x80u)) == (!!(flags & 0x800u)))) & 0xffu)
         << 0);
  edx = (edx + 0x7fffu);
  eax = edx;
L_46a629:;
  edx = BB_U32(esp + 0x1cu);
  ecx = (ecx + 1u);
  a = ecx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  WORD(edx + ecx * 2u + 0xfffffffeu) = ((uint16_t)(eax));
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a5fe;
L_46a637:;
  eax = BB_U32(esp + 0x18u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a67e;
  ecx = (ecx ^ ecx);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a67e;
L_46a645:;
  eax = BB_U32(ebp);
  ebp = (ebp + 0x4u);
  eax = (eax & 0x1ffffu);
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a670;
  a = eax;
  b = 0x18000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_46a670;
  edx = (edx ^ edx);
  a = eax;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(((!!(flags & 0x80u)) == (!!(flags & 0x800u)))) & 0xffu)
         << 0);
  edx = (edx + 0x7fffu);
  eax = edx;
L_46a670:;
  edx = BB_U32(esp + 0x20u);
  ecx = (ecx + 1u);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  WORD(edx + ecx * 2u + 0xfffffffeu) = ((uint16_t)(eax));
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a645;
L_46a67e:;
  edx = BB_U32(esp + 0x18u);
  eax = BB_U32(0x6c8838u);
  args[--sp] = edx;
  esp -= 4u;
  edx = BB_U32(esp + 0x24u);
  ecx = BB_U32(eax);
  args[--sp] = edx;
  esp -= 4u;
  edx = BB_U32(esp + 0x30u);
  args[--sp] = edx;
  esp -= 4u;
  edx = BB_U32(esp + 0x28u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  AUDIO_CALL(BB_U32(ecx + 0x4cu), 20); /* 0046a69a indirect */
  eax = BB_U32(0x6c1c90u);
  ecx = BB_U32(esp + 0x24u);
  eax = (eax + ecx);
  edx = (uint32_t)((int32_t)eax >> 31);
  b = BB_U32(0x6c883cu);
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  BB_U32(0x6c1c90u) = edx;
L_46a6b5:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x4020u;
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
uint32_t __attribute__((naked)) bb_audio_resource_update_entry(void) {
  __asm__ volatile(
      "leal -16416(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_audio_resource_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "16416(%esp),%esp;ret $0");
}
#endif
void bb_audio_buffer_gap_c(uint32_t input[9]) {
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
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(ecx);
  args[--sp] = esi;
  esp -= 4u;
  esi = edx;
  edx = (esp + 0x8u);
  args[--sp] = edx;
  esp -= 4u;
  edx = (esp + 0x8u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  AUDIO_CALL(BB_U32(eax + 0x10u), 12); /* 0046a6d3 indirect */
  eax = BB_U32(esp + 0x4u);
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_46a6e4;
  ecx = BB_U32(esp + 0x10u);
  eax = (eax + ecx);
L_46a6e4:;
  eax = (eax - esi);
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
uint32_t __attribute__((naked)) bb_audio_buffer_gap_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_buffer_gap_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "8(%esp),%esp;ret $4");
}
#endif
void bb_audio_voice_mix_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 12u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = ecx;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  ebx = BB_U32(esi + 0xd6cu);
  ebp = BB_U32(esi + 0xd70u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp + 0x14u) = edx;
  BB_U32(esp + 0x10u) = 0x0u;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a77c;
  edi = BB_U32(esp + 0x1cu);
L_46a719:;
  a = ebx;
  b = 0x1b0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46a72b;
  ecx = (esi + 0x28u);
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_decode_block_entry), 0); /* 0046a724 */
  ebx = (ebx ^ ebx);
L_46a72b:;
  fp[depth] = F32(esi + ebx * 4u + 0x6acu);
  ++depth;
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  ecx = BB_U32(esp + 0x10u);
  eax = (edi + 0x4u);
  ebx = (ebx + 1u);
  ecx = (ecx + 0x2u);
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  edi = (edi + 0x8u);
  ebp = (ebp + 1u);
  fp[depth - 1] = fp[depth - 1] + F32(eax);
  BB_U32(esp + 0x10u) = ecx;
  F32(eax) = (float)(fp[depth - 1]);
  --depth;
  a = ebp;
  b = BB_U32(esi + 0x4u);
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a770;
  eax = BB_U32(esi + 0x20u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a794;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voice_reset_entry), 0); /* 0046a769 */
  ebp = (ebp ^ ebp);
L_46a770:;
  eax = BB_U32(esp + 0x14u);
  ecx = BB_U32(esp + 0x10u);
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a719;
L_46a77c:;
  BB_U32(esi + 0xd6cu) = ebx;
  BB_U32(esi + 0xd70u) = ebp;
  eax = (eax ^ eax);
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
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
L_46a794:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  eax = 0x1u;
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
uint32_t __attribute__((naked)) bb_audio_voice_mix_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_voice_mix_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "8(%esp),%esp;ret $12");
}
#endif
void bb_audio_voice_reset_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
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
  esi = ecx;
  ecx = BB_U32(esi);
  edx = (esi + 0x28u);
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_decode_init_entry), 0); /* 0046a7b8 */
  BB_U32(esi + 0xd6cu) = 0x1b0u;
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
uint32_t __attribute__((naked)) bb_audio_voice_reset_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_voice_reset_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_voice_play_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edi = (edi ^ edi);
  esi = 0x6c1c98u;
L_46a7d9:;
  eax = BB_U32(esi + 0x24u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a7f5;
  esi = (esi + 0xd74u);
  edi = (edi + 1u);
  a = esi;
  b = 0x6c8838u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a7d9;
  a = eax;
  b = 0xffffffffu;
  result = (a | b) & 0xffffffffu;
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
L_46a7f5:;
  eax = BB_U32(esp + 0xcu);
  ecx = BB_U32(eax + 0x4u);
  a = ecx;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(esi) = ecx;
  ecx = (eax + 0xbu);
  edx = (uint32_t)(int32_t)(int8_t)(BYTE(eax + 0xau));
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_read_integer_entry), 0); /* 0046a807 */
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x18u));
  edx = BB_U32(esp + 0x14u);
  ecx = BB_U32(esp + 0x1cu);
  BB_U32(esi + 0x4u) = eax;
  eax = BB_U32(esp + 0x18u);
  F32(esi + 0x14u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x1cu));
  a = edx;
  result = (a >> 31) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 31, 1);
  edx = result;
  BB_U32(esi + 0xcu) = ecx;
  ecx = esi;
  fp[depth - 1] = fp[depth - 1] - F32(0x482400u);
  BB_U32(esi + 0xd70u) = 0x0u;
  BB_U32(esi + 0x20u) = edx;
  BB_U32(esi + 0x1cu) = 0x3f800000u;
  BB_U32(esi + 0x10u) = 0x5622u;
  fp[depth - 1] = fp[depth - 1] * F32(0x482418u);
  BB_U32(esi + 0x8u) = eax;
  F32(esi + 0x18u) = (float)(fp[depth - 1]);
  --depth;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voice_reset_entry), 0); /* 0046a85b */
  a = esi;
  b = 0x24u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  esi = result;
  args[--sp] = 0x2u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca3b8u), 8); /* 0046a866 indirect */
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
uint32_t __attribute__((naked))
bb_audio_voice_play_entry(uint32_t arg0 __attribute__((unused)),
                          uint32_t arg1 __attribute__((unused)),
                          uint32_t arg2 __attribute__((unused)),
                          uint32_t arg3 __attribute__((unused)),
                          uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_voice_play_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_read_integer_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = (eax ^ eax);
  esi = edx;
  edi = edx;
  edx = (edx - 1u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_46a89c;
  edx = (edx + 1u);
L_46a88f:;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(ecx)) & 0xffu) << 0);
  eax = (eax << 8);
  eax = (eax + ebx);
  ecx = (ecx + 1u);
  a = edx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edx = result;
  if (!(flags & 0x40u))
    goto L_46a88f;
L_46a89c:;
  a = esi;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46a8af;
  a = eax;
  b = 0x7fu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a8af;
  a = eax;
  b = 0x100u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
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
L_46a8af:;
  a = esi;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46a8c4;
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a8c4;
  a = eax;
  b = 0x10000u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
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
L_46a8c4:;
  a = esi;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46a8d5;
  a = eax;
  b = 0x7fffffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46a8d5;
  a = eax;
  b = 0x1000000u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
L_46a8d5:;
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
uint32_t __attribute__((naked)) bb_audio_read_integer_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_read_integer_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_voice_duration_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
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
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a910;
  a = ecx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_46a910;
  eax = (ecx + ecx * 8u);
  eax = (eax << 5);
  a = eax;
  b = ecx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  eax = (eax + eax * 2u);
  fp[depth++] = (long double)(int32_t)(BB_U32(eax * 4u + 0x6c1c9cu));
  fp[depth - 1] = fp[depth - 1] * F32(0x482420u);
  fp[depth - 1] = fp[depth - 1] - F32(0x482424u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    audio_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_46a910:;
  a = eax;
  b = 0xffffffffu;
  result = (a | b) & 0xffffffffu;
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
uint32_t __attribute__((naked))
bb_audio_voice_duration_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_voice_duration_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_voice_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
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
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a94b;
  a = ecx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_46a94b;
  eax = (ecx + ecx * 8u);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax << 5);
  a = eax;
  b = ecx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  eax = (eax + eax * 2u);
  ecx = (eax * 4u + 0x6c1cbcu);
  args[--sp] = ecx;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca3b8u), 8); /* 0046a942 indirect */
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
L_46a94b:;
  a = eax;
  b = 0xffffffffu;
  result = (a | b) & 0xffffffffu;
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
uint32_t __attribute__((naked))
bb_audio_voice_stop_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_voice_stop_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_voices_stop_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
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
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
L_46a953:;
  args[--sp] = esi;
  esp -= 4u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voice_stop_entry), 0); /* 0046a954 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = (esi + 1u);
  a = esi;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46a953;
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
uint32_t __attribute__((naked)) bb_audio_voices_stop_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_voices_stop_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_resource_shutdown_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_voices_stop_entry), 0); /* 0046a970 */
  ecx = 0x6c8840u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_timer_stop_entry), 0); /* 0046a97a */
  eax = BB_U32(0x6c8838u);
  BB_U32(0x6c8850u) = 0x0u;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(eax);
  AUDIO_CALL(BB_U32(ecx + 0x8u), 4); /* 0046a991 indirect */
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
uint32_t __attribute__((naked)) bb_audio_resource_shutdown_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_resource_shutdown_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_timer_stop_c(uint32_t input[9]) {
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
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ecx + 0x8u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp + 0x10u) = ecx;
  if (flags & 0x40u)
    goto L_46aa0f;
  ebx = BB_U32(0x6ca3b8u);
  ebp = BB_U32(0x6ca388u);
  esi = 0x1f3u;
  edi = (ecx + 0x4u);
L_46a9c4:;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  AUDIO_CALL(ebx, 8); /* 0046a9c7 indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46a9d9;
  args[--sp] = 0x2u;
  esp -= 4u;
  AUDIO_CALL(ebp, 4); /* 0046a9d0 indirect */
  eax = esi;
  esi = (esi - 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_46a9c4;
L_46a9d9:;
  esi = BB_U32(esp + 0x10u);
  ecx = BB_U32(esi + 0x8u);
  args[--sp] = ecx;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca580u), 4); /* 0046a9e1 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46aa04;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esi + 0x8u) = eax;
  AUDIO_CALL(ebx, 8); /* 0046a9f0 indirect */
  edx = BB_U32(esi + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  AUDIO_CALL(BB_U32(0x6ca560u), 4); /* 0046a9f6 indirect */
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  ecx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_46aa04:;
  eax = 0x1u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  ecx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_46aa0f:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ebx = args[sp++];
  esp += 4u;
  ecx = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_audio_timer_stop_entry(void) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_timer_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "4(%esp),%esp;ret $0");
}
#endif
void bb_audio_decode_init_c(uint32_t input[9]) {
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
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx)) & 0xffu) << 0);
  esi = edx;
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esi) = ecx;
  edx = 0x1u;
  ecx = esi;
  BB_U32(esi + 0x4u) = eax;
  BB_U32(esi + 0x8u) = 0x8u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046aa3f */
  edx = 0x4u;
  ecx = esi;
  BB_U32(esi + 0xcu) = eax;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046aa4e */
  ecx = 0x20u;
  edx = 0x4u;
  a = ecx;
  b = eax;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(esi + 0x10u) = ecx;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046aa64 */
  eax = (eax + 1u);
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  BB_U32(esp + 0x8u) = eax;
  BB_U32(esp + 0xcu) = edi;
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x8u));
  edx = 0x6u;
  ecx = esi;
  fp[depth - 1] = fp[depth - 1] * F64(0x482428u);
  F32(esi + 0x14u) = (float)(fp[depth - 1]);
  --depth;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046aa88 */
  BB_U32(esp + 0x8u) = eax;
  BB_U32(esp + 0xcu) = edi;
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x8u));
  eax = (esi + 0x18u);
  ecx = 0x3fu;
  fp[depth - 1] = fp[depth - 1] * F64(0x482430u);
  fp[depth - 1] = F64(0x482438u) - fp[depth - 1];
L_46aaad:;
  fp[depth] = F32(eax + 0xfffffffcu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46aaad;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  eax = (esi + 0x144u);
  ecx = 0xcu;
L_46aac8:;
  BB_U32(eax + 0xffffffd0u) = edi;
  BB_U32(eax) = edi;
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_46aac8;
  edi = (esi + 0x174u);
  ecx = 0x144u;
  eax = (eax ^ eax);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  edi = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked)) bb_audio_decode_init_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_decode_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "8(%esp),%esp;ret $0");
}
#endif
void bb_audio_bits_read_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(edx * 4u + 0x48c720u);
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = edi;
  esp -= 4u;
  ecx = edx;
  edi = BB_U32(esi + 0x4u);
  ebp = BB_U32(esi + 0x8u);
  eax = (eax & edi);
  ebp = (ebp - edx);
  edi = (edi >> (((uint8_t)(ecx)) & 31u));
  ecx = ebp;
  BB_U32(esi + 0x8u) = ebp;
  a = ecx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esi + 0x4u) = edi;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_46ab2e;
  ebp = BB_U32(esi);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  edx = (edx << (((uint8_t)(ecx)) & 31u));
  edx = (edx | edi);
  ebp = (ebp + 1u);
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(esi + 0x4u) = edx;
  BB_U32(esi) = ebp;
  BB_U32(esi + 0x8u) = ecx;
L_46ab2e:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked)) bb_audio_bits_read_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_bits_read_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_decode_block_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 552u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[256] = {0}, sp = 240, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x224u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  edx = 0x6u;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ab51 */
  fp[depth] = F32(eax * 4u + 0x48c748u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(esi + 0x114u);
  edi = BB_U32(esi + 0x10u);
  ebx = (esp + 0x30u);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  edi = (esi + 0x118u);
  fp[depth - 1] = fp[depth - 1] * F64(0x482440u);
  a = ecx;
  b = ecx;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  ecx = result;
  ebp = 0x3u;
  a = ecx;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(esp + 0x1cu) = ecx;
  F32(esp + 0x2cu) = (float)(fp[depth - 1]);
  --depth;
L_46ab89:;
  edx = 0x6u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ab90 */
  fp[depth] = F32(eax * 4u + 0x48c748u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(edi);
  edi = (edi + 0x4u);
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  a = ebp;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebp = result;
  fp[depth - 1] = fp[depth - 1] * F64(0x482440u);
  F32(ebx + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46ab89;
  ebx = (esp + 0x3cu);
  edi = (esi + 0x124u);
  ebp = 0x8u;
L_46abbf:;
  edx = 0x5u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046abc6 */
  fp[depth] = F32(eax * 4u + 0x48c788u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] - F32(edi);
  edi = (edi + 0x4u);
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  a = ebp;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebp = result;
  fp[depth - 1] = fp[depth - 1] * F64(0x482440u);
  F32(ebx + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46abbf;
  ebp = 0xd8u;
  eax = (esi + 0x684u);
  BB_U32(esp + 0x14u) = ebp;
  BB_U32(esp + 0x18u) = eax;
L_46abf9:;
  edx = 0x8u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ac00 */
  edx = 0x4u;
  ecx = esi;
  a = ebp;
  b = eax;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ebp = result;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ac0e */
  BB_U32(esp + 0x24u) = eax;
  BB_U32(esp + 0x28u) = 0x0u;
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x24u));
  edx = 0x6u;
  ecx = esi;
  fp[depth - 1] = fp[depth - 1] * F64(0x482448u);
  F32(esp + 0x20u) = (float)(fp[depth - 1]);
  --depth;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ac34 */
  fp[depth] = F32(esi + eax * 4u + 0x14u);
  ++depth;
  eax = BB_U32(esi + 0xcu);
  F32(esp + 0x10u) = (float)(fp[depth - 1]);
  --depth;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_46ac5f;
  edx = BB_U32(esp + 0x1cu);
  ecx = (esp + 0x70u);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_decode_residual_entry), 8); /* 0046ac55 */
  goto L_46ad01;
L_46ac5f:;
  edx = 0x1u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ac66 */
  edx = 0x1u;
  ecx = esi;
  edi = eax;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046ac74 */
  a = edi;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  edi = result;
  args[--sp] = 0x2u;
  esp -= 4u;
  ecx = esi;
  edx = (esp + edi + 0x74u);
  ebx = eax;
  args[--sp] = edx;
  esp -= 4u;
  edx = BB_U32(esp + 0x24u);
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_decode_residual_entry), 8); /* 0046ac8b */
  edx = (edx ^ edx);
  a = ebx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_46acad;
  ecx = (ecx ^ ecx);
L_46ac98:;
  eax = ecx;
  ecx = (ecx + 0x8u);
  eax = (eax - edi);
  a = ecx;
  b = 0x1b0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esp + eax + 0x74u) = edx;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46ac98;
  goto L_46ad01;
L_46acad:;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  BB_U32(esp + 0x220u) = ecx;
  BB_U32(esp + 0x5cu) = eax;
  BB_U32(esp + 0x224u) = ecx;
  BB_U32(esp + 0x60u) = eax;
  BB_U32(esp + 0x228u) = ecx;
  BB_U32(esp + 0x64u) = eax;
  BB_U32(esp + 0x22cu) = ecx;
  BB_U32(esp + 0x68u) = eax;
  BB_U32(esp + 0x230u) = ecx;
  ecx = (esp + 0x74u);
  a = ecx;
  b = edi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(esp + 0x6cu) = eax;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_interpolate_entry), 0); /* 0046acee */
  fp[depth] = F32(esp + 0x10u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F64(0x482450u);
  F32(esp + 0x10u) = (float)(fp[depth - 1]);
  --depth;
L_46ad01:;
  eax = BB_U32(esp + 0x18u);
  edx = (esp + 0x70u);
  ecx = (esi + ebp * 4u + 0x174u);
  edi = 0x6cu;
L_46ad15:;
  fp[depth] = F32(edx);
  ++depth;
  fp[depth] = F32(esp + 0x20u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(ecx);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  ecx = (ecx + 0x4u);
  edx = (edx + 0x4u);
  eax = (eax + 0x4u);
  a = edi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edi = result;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46ad15;
  ebp = BB_U32(esp + 0x14u);
  BB_U32(esp + 0x18u) = eax;
  ebp = (ebp + 0x6cu);
  a = ebp;
  b = 0x288u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esp + 0x14u) = ebp;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46abf9;
  eax = (esi + 0x174u);
  ecx = 0x144u;
L_46ad5a:;
  edx = BB_U32(eax + 0x6c0u);
  BB_U32(eax) = edx;
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_46ad5a;
  ecx = (esp + 0x2cu);
  eax = (esi + 0x114u);
  edx = 0xcu;
L_46ad77:;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(ecx);
  ecx = (ecx + 0x4u);
  eax = (eax + 0x4u);
  a = edx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edx = result;
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46ad77;
  args[--sp] = 0x1u;
  esp -= 4u;
  a = edx;
  b = edx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edx = result;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_synthesize_entry), 4); /* 0046ad8d */
  ecx = (esp + 0x2cu);
  eax = (esi + 0x114u);
  edx = 0xcu;
L_46ada1:;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(ecx);
  ecx = (ecx + 0x4u);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  a = edx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edx = result;
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46ada1;
  args[--sp] = 0x1u;
  esp -= 4u;
  edx = 0xcu;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_synthesize_entry), 4); /* 0046adba */
  ecx = (esp + 0x2cu);
  eax = (esi + 0x114u);
  edx = 0xcu;
L_46adce:;
  fp[depth] = F32(ecx);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax);
  ecx = (ecx + 0x4u);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  a = edx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edx = result;
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46adce;
  args[--sp] = 0x1u;
  esp -= 4u;
  edx = 0x18u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_synthesize_entry), 4); /* 0046ade7 */
  ecx = (esp + 0x2cu);
  eax = (esi + 0x114u);
  edx = 0xcu;
L_46adfb:;
  fp[depth] = F32(eax);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(ecx);
  ecx = (ecx + 0x4u);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  a = edx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edx = result;
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46adfb;
  args[--sp] = 0x21u;
  esp -= 4u;
  edx = 0x24u;
  ecx = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_synthesize_entry), 4); /* 0046ae14 */
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x224u;
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
uint32_t __attribute__((naked)) bb_audio_decode_block_entry(void) {
  __asm__ volatile("leal -548(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_decode_block_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "548(%esp),%esp;ret $0");
}
#endif
void bb_audio_decode_residual_c(uint32_t input[9]) {
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
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebp = ecx;
  if (flags & 0x40u)
    goto L_46af66;
  eax = (eax ^ eax);
  edi = (edi ^ edi);
  goto L_46ae4b;
L_46ae47:;
  eax = BB_U32(esp + 0x14u);
L_46ae4b:;
  ecx = BB_U32(ebp + 0x4u);
  ebx = (ebx ^ ebx);
  eax = (eax << 8);
  ecx = (ecx & 0xffu);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + ecx + 0x48c848u)) & 0xffu) << 0);
  ecx = ebp;
  esi = (ebx + ebx * 2u);
  a = esi;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  esi = result;
  edx = BB_U32(esi + 0x48ca48u);
  BB_U32(esp + 0x14u) = edx;
  edx = BB_U32(esi + 0x48ca4cu);
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_skip_entry), 0); /* 0046ae78 */
  a = ebx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46ae94;
  ecx = BB_U32(esp + 0x1cu);
  eax = BB_U32(esi + 0x48ca50u);
  BB_U32(ecx + edi * 4u) = eax;
  goto L_46af4f;
L_46ae94:;
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46aef0;
  edx = 0x6u;
  ecx = ebp;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046aea0 */
  ecx = BB_U32(esp + 0x20u);
  eax = (eax + 0x7u);
  edx = eax;
  a = edx;
  b = ecx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  edx = result;
  edx = (edx + edi);
  a = edx;
  b = 0x6cu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46aec2;
  eax = 0x6cu;
  eax = (eax - edi);
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
L_46aec2:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46af53;
  ebx = BB_U32(esp + 0x20u);
  ecx = BB_U32(esp + 0x1cu);
  edx = eax;
  a = eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  ecx = (ecx + edi * 4u);
  esi = (ebx * 4u);
  edi = (edi + eax);
L_46aee3:;
  BB_U32(ecx) = 0x0u;
  ecx = (ecx + esi);
  a = edx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edx = result;
  if (!(flags & 0x40u))
    goto L_46aee3;
  goto L_46af53;
L_46aef0:;
  esi = 0x7u;
  edx = 0x1u;
  ecx = ebp;
  BB_U32(esp + 0x10u) = esi;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046af00 */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46af20;
L_46af0a:;
  edx = 0x1u;
  ecx = ebp;
  a = esi;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  esi = result;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046af12 */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_46af0a;
  BB_U32(esp + 0x10u) = esi;
L_46af20:;
  edx = 0x1u;
  ecx = ebp;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_read_entry), 0); /* 0046af27 */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_46af3e;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  edx = BB_U32(esp + 0x1cu);
  F32(edx + edi * 4u) = (float)(fp[depth - 1]);
  --depth;
  goto L_46af4f;
L_46af3e:;
  eax = BB_U32(esp + 0x1cu);
  esi = (0u - esi);
  BB_U32(esp + 0x10u) = esi;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x10u));
  F32(eax + edi * 4u) = (float)(fp[depth - 1]);
  --depth;
L_46af4f:;
  edi = (edi + BB_U32(esp + 0x20u));
L_46af53:;
  a = edi;
  b = 0x6cu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46ae47;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
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
L_46af66:;
  ebx = BB_U32(esp + 0x20u);
  esi = BB_U32(esp + 0x1cu);
  edi = (edi ^ edi);
L_46af70:;
  eax = BB_U32(ebp + 0x4u);
  eax = (eax & 0x3u);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_46afa9;
  switch (eax) {
  case 0:
    goto L_46af82;
  case 1:
    goto L_46af8f;
  case 2:
    goto L_46af82;
  case 3:
    goto L_46af97;
  default:
    __builtin_unreachable();
  }
L_46af82:;
  BB_U32(esi) = 0x0u;
  edx = 0x1u;
  goto L_46afa2;
L_46af8f:;
  BB_U32(esi) = 0xc0000000u;
  goto L_46af9d;
L_46af97:;
  BB_U32(esi) = 0x40000000u;
L_46af9d:;
  edx = 0x2u;
L_46afa2:;
  ecx = ebp;
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_bits_skip_entry), 0); /* 0046afa4 */
L_46afa9:;
  eax = (ebx * 4u);
  edi = (edi + ebx);
  esi = (esi + eax);
  a = edi;
  b = 0x6cu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46af70;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
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
uint32_t __attribute__((naked)) bb_audio_decode_residual_entry(void) {
  __asm__ volatile("leal -8(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_decode_residual_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "8(%esp),%esp;ret $8");
}
#endif
void bb_audio_bits_skip_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = ecx;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ecx = edx;
  edi = BB_U32(eax + 0x4u);
  esi = BB_U32(eax + 0x8u);
  edi = (edi >> (((uint8_t)(ecx)) & 31u));
  esi = (esi - edx);
  ecx = esi;
  BB_U32(eax + 0x8u) = esi;
  a = ecx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(eax + 0x4u) = edi;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_46b013;
  esi = BB_U32(eax);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  edx = (edx << (((uint8_t)(ecx)) & 31u));
  edx = (edx | edi);
  esi = (esi + 1u);
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(eax + 0x4u) = edx;
  BB_U32(eax) = esi;
  BB_U32(eax + 0x8u) = ecx;
L_46b013:;
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
uint32_t __attribute__((naked)) bb_audio_bits_skip_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_bits_skip_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_interpolate_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = 0xffffffecu;
  args[--sp] = edi;
  esp -= 4u;
  eax = (ecx + 0x14u);
  esi = (esi - ecx);
  ebp = 0x36u;
L_46b033:;
  ebx = (esi + eax + 0xffffffecu);
  edi = (esi + eax + 0xfffffff4u);
  edx = (eax + 0xffffffecu);
  a = eax;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  fp[depth] = F32(ecx + ebx);
  ++depth;
  fp[depth] = F32(ecx + edi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0xfffffff0u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0xfffffff8u);
  fp[depth] = F32(edx + 0xfffffffcu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0xffffffe8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F64(0x482458u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F64(0x482460u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F64(0x482468u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  a = ebp;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebp = result;
  F32(edx) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46b033;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
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
uint32_t __attribute__((naked)) bb_audio_interpolate_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_interpolate_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_audio_synthesize_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 52u;
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
  b = 0x30u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  edi = edx;
  edx = (esp + 0x8u);
  ecx = (esi + 0x114u);
  AUDIO_CALL(BB_AUDIO_TARGET(bb_audio_coefficients_entry), 0); /* 0046b093 */
  ecx = BB_U32(esp + 0x3cu);
  eax = (esi + edi * 4u + 0x684u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46b9fb;
L_46b0ab:;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax);
  F32(esi + 0x170u) = (float)(fp[depth - 1]);
  F32(eax) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x4u);
  F32(esi + 0x16cu) = (float)(fp[depth - 1]);
  F32(eax + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x8u);
  F32(esi + 0x168u) = (float)(fp[depth - 1]);
  F32(eax + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0xcu);
  F32(esi + 0x164u) = (float)(fp[depth - 1]);
  F32(eax + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x10u);
  F32(esi + 0x160u) = (float)(fp[depth - 1]);
  F32(eax + 0x10u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x14u);
  F32(esi + 0x15cu) = (float)(fp[depth - 1]);
  F32(eax + 0x14u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x18u);
  F32(esi + 0x158u) = (float)(fp[depth - 1]);
  F32(eax + 0x18u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x1cu);
  F32(esi + 0x154u) = (float)(fp[depth - 1]);
  F32(eax + 0x1cu) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x20u);
  F32(esi + 0x150u) = (float)(fp[depth - 1]);
  F32(eax + 0x20u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0x24u);
  F32(esi + 0x14cu) = (float)(fp[depth - 1]);
  F32(eax + 0x24u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  eax = (eax + 0x30u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0xfffffff8u);
  F32(esi + 0x148u) = (float)(fp[depth - 1]);
  F32(eax + 0xfffffff8u) = (float)(fp[depth - 1]);
  --depth;
  fp[depth] = F32(esi + 0x16cu);
  ++depth;
  fp[depth] = F32(esi + 0x160u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x20u);
  fp[depth] = F32(esi + 0x164u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x2cu);
  fp[depth] = F32(esi + 0x170u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x24u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x30u);
  fp[depth] = F32(esi + 0x144u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x34u);
  fp[depth] = F32(esi + 0x168u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x28u);
  fp[depth] = F32(esi + 0x154u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x14u);
  fp[depth] = F32(esi + 0x14cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0xcu);
  fp[depth] = F32(esi + 0x148u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x8u);
  fp[depth] = F32(esi + 0x158u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x18u);
  fp[depth] = F32(esi + 0x150u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x10u);
  fp[depth] = F32(esi + 0x15cu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x1cu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] + fp[depth - 1];
  --depth;
  fp[depth - 1] = fp[depth - 1] + F32(eax + 0xfffffffcu);
  F32(esi + 0x144u) = (float)(fp[depth - 1]);
  F32(eax + 0xfffffffcu) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46b0ab;
L_46b9fb:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x30u;
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
uint32_t __attribute__((naked)) bb_audio_synthesize_entry(void) {
  __asm__ volatile("leal -48(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_synthesize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "48(%esp),%esp;ret $4");
}
#endif
void bb_audio_coefficients_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 112u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[256] = {0}, sp = 240, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x6cu;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  ebx = edx;
  args[--sp] = ebp;
  esp -= 4u;
  edx = ecx;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp + 0x18u) = ebx;
  esi = (esp + 0x48u);
  eax = (edx + 0x28u);
  edi = 0xbu;
L_46ba2b:;
  ecx = BB_U32(eax);
  eax = (eax - 0x4u);
  BB_U32(esi) = ecx;
  esi = (esi - 0x4u);
  a = edi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edi = result;
  if (!(flags & 0x40u))
    goto L_46ba2b;
  eax = (esp + 0x4cu);
  ebp = (ebp ^ ebp);
  eax = (eax - ebx);
  BB_U32(esp + 0x1cu) = 0x3f800000u;
  BB_U32(esp + 0x10u) = ebp;
  ecx = ebx;
  BB_U32(esp + 0x14u) = eax;
L_46ba52:;
  fp[depth] = F32(esp + 0x48u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(edx + 0x2cu);
  edi = (esp + 0x20u);
  esi = (esp + 0x44u);
  eax = (edx + 0x28u);
  edi = (edi - edx);
  fp[depth - 1] = -fp[depth - 1];
  ebx = 0xbu;
L_46ba6d:;
  fp[depth] = F32(esi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax);
  esi = (esi - 0x4u);
  eax = (eax - 0x4u);
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(eax + 0x4u);
  fp[depth - 1] = fp[depth - 1] + F32(esi + 0x4u);
  F32(edi + eax + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  if (!(flags & 0x40u))
    goto L_46ba6d;
  F32(esp + 0x1cu) = (float)(fp[depth - 1]);
  eax = BB_U32(esp + 0x14u);
  fp[depth] = fp[depth - 1u - 0u];
  ++depth;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  F32(eax + ecx) = (float)(fp[depth - 1]);
  --depth;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_46bab4;
  esi = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  eax = ebp;
L_46baa3:;
  fp[depth] = F32(esp + eax + 0x48u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esi);
  eax = (eax - 0x4u);
  esi = (esi + 0x4u);
  a = edi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edi = result;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] - fp[depth - 1];
  --depth;
  if (!(flags & 0x40u))
    goto L_46baa3;
L_46bab4:;
  ebx = BB_U32(esp + 0x10u);
  ebp = (ebp + 0x4u);
  F32(ecx) = (float)(fp[depth - 1]);
  --depth;
  ebx = (ebx + 1u);
  ecx = (ecx + 0x4u);
  a = ebp;
  b = 0x30u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esp + 0x10u) = ebx;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_46ba52;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebp = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x6cu;
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
uint32_t __attribute__((naked)) bb_audio_coefficients_entry(void) {
  __asm__ volatile("leal -108(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_audio_coefficients_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "108(%esp),%esp;ret $0");
}
#endif
