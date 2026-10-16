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
/* C forms of QMIXER voice queueing and automatic channel selection.
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
/* The working i386 bridge keeps its physical cleanup records. Portable bodies
 * use logical owner records and native C recovery scopes instead of FS. */
static uint32_t exception_head(void) {
  uint32_t value;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(value));
  return value;
}
static void exception_link(uint32_t value) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(value) : "memory");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void session_play_body(uint32_t *input) {
#else
void bb_qmix_session_play_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10009a0fu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  a = BB_U32(ecx + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if (!(flags & 0x40u))
    goto L_1000989b;
  args[--sp] = 0xdu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x1000987du); /* 10009878 */
  BB_U32(ebp + 0xffffffecu) = 0x4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(ebp + 0xffffffecu) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffecu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x1000989bu); /* 10009896 */
L_1000989b:;
  args[--sp] = 0x50u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100098a2u); /* 1000989d */
  BB_U32(ebp + 0xffffffecu) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = eax;
  edi = 0x0u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100098c7;
  eax = BB_U32(ebp + 0x8u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005c10u, 4, 0x100098c5u); /* 100098c0 */
  edi = eax;
L_100098c7:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = BB_U32(ecx + 0x48u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10009927;
  eax = BB_U32(ecx + 0x44u);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100098e6u); /* 100098e1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0x4cu);
  BB_U32(eax) = edx;
  edx = (edx ^ edx);
  BB_U32(ecx + 0x4cu) = eax;
  eax = (eax + 0x4u);
  ecx = BB_U32(ecx + 0x44u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_10009916;
  esi = eax;
L_10009901:;
  ecx = (esi + 0x8u);
  esi = (esi + 0x8u);
  edx = (edx + 1u);
  BB_U32(esi + 0xfffffff8u) = ecx;
  ecx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(ecx + 0x44u);
  ecx = (ecx - 1u);
  a = ecx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10009901;
L_10009916:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0x44u);
  BB_U32(eax + edx * 8u + 0xfffffff8u) = 0x0u;
  BB_U32(ecx + 0x48u) = eax;
L_10009927:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0x48u);
  eax = BB_U32(edx);
  BB_U32(ecx + 0x48u) = eax;
  BB_U32(edx + 0x4u) = edi;
  BB_U32(edx) = 0x0u;
  eax = BB_U32(ecx + 0x40u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009946;
  BB_U32(eax) = edx;
  goto L_1000994c;
L_10009946:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ecx + 0x3cu) = edx;
L_1000994c:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ecx + 0x40u) = edx;
  a = BYTE(eax + 0x12u);
  b = 0x8u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100099f6;
L_1000995f:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esi = BB_U32(ecx + 0x3cu);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000996d;
  edx = (edx ^ edx);
  goto L_1000998f;
L_1000996d:;
  edx = BB_U32(esi + 0x4u);
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(ecx + 0x40u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000997f;
  BB_U32(ecx + 0x40u) = 0x0u;
L_1000997f:;
  eax = BB_U32(esi);
  ecx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ecx + 0x3cu) = eax;
  eax = BB_U32(ecx + 0x48u);
  BB_U32(esi) = eax;
  BB_U32(ecx + 0x48u) = esi;
L_1000998f:;
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  BB_U32(ebp + 0xffffffecu) = edx;
  a = BB_U32(ebp + 0xffffffecu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100099cf;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005db0u, 8, 0x100099afu); /* 100099aa */
  ecx = eax;
  edx = BB_U32(ebp + 0xffffffecu);
#ifndef BB_MODEL_RUNTIME
  BB_U32(ebp + 0xffffffecu) = 0x0u;
#endif
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 4, 0x100099c1u); /* 100099be indirect */
#ifdef BB_MODEL_RUNTIME
  /* The channel owns the voice only after its queue node is committed. */
  BB_U32(ebp + 0xffffffecu) = 0x0u;
#endif
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x10009a07u, 0, 0x100099cdu); /* 100099c8 */
  goto L_1000995f;
L_100099cf:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x10009a07u, 0, 0x100099dbu); /* 100099d6 */
  eax = BB_U32(ebp + 0x8u);
  a = BYTE(eax + 0x12u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100099ee;
  ecx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ecx + 0x14u) = 0x1u;
L_100099ee:;
  ecx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x100099f6u); /* 100099f3 indirect */
L_100099f6:;
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
uint32_t __attribute__((naked))
bb_qmix_session_play_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_play_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif

void bb_qmix_session_play_cleanup_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10009a30u, 0, BB_U32(esp - 4u)); /* tail call 10009a0a */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_session_play_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_play_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif

void bb_qmix_voice_select_channel_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx + 0x12u);
  esi = ecx;
  a = ((uint8_t)(eax));
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005e20;
  edi = (esi + 0x6u);
  ecx = BB_U32(esp + 0x10u);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005dd0;
  SAMPLE_CALL(0x10009a50u, 0, 0x10005dceu); /* 10005dc9 */
  goto L_10005dd5;
L_10005dd0:;
  SAMPLE_CALL(0x10009ab0u, 0, 0x10005dd5u); /* 10005dd0 */
L_10005dd5:;
  BB_U32(edi) = eax;
  a = BB_U32(edi);
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005e05;
  args[--sp] = 0xeu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10005de3u); /* 10005dde */
  BB_U32(esp + 0xcu) = 0xbu;
  args[sp + 3u] = BB_U32(esp + 12u);
  eax = BB_U32(esp + 0xcu);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esp + 0x8u) = eax;
  args[sp + 2u] = BB_U32(esp + 8u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10005e05u); /* 10005e00 */
L_10005e05:;
  args[--sp] = 0x3bbu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x10005d90u, 4, 0x10005e11u); /* 10005e0c */
  a = BYTE(esi + 0x12u);
  b = 0x8u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10005e20;
  ecx = BB_U32(edi);
  eax = BB_U32(esp + 0x14u);
  BB_U32(eax + 0x6u) = ecx;
L_10005e20:;
  eax = BB_U32(esi + 0x6u);
  ecx = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005590u, 4, 0x10005e2du); /* 10005e28 */
  a = BYTE(esi + 0x12u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  edi = eax;
  if (flags & 0x40u)
    goto L_10005e3c;
  eax = BB_U32(edi);
  ecx = edi;
  SAMPLE_CALL(BB_U32(eax + 0x10u), 0, 0x10005e3cu); /* 10005e39 indirect */
L_10005e3c:;
  eax = edi;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
bb_qmix_voice_select_channel_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_select_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $8");
}
#endif

void bb_qmix_session_oldest_channel_c(uint32_t *input) {
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
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
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
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10009b10u, 0, 0x10009a5eu); /* 10009a59 */
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10009aa2;
  edi = 0x80000000u;
  SAMPLE_CALL(BB_U32(0x10021374u), 0, 0x10009a71u); /* 10009a6b indirect */
  ebx = (ebx ^ ebx);
  edx = BB_U32(esi + 0x1cu);
  a = edx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10009a9e;
L_10009a7a:;
  ecx = BB_U32(esi + 0x20u);
  ecx = BB_U32(ecx + ebx * 4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009a99;
  a = BB_U32(ecx + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009a99;
  ebp = eax;
  ebp = (ebp - BB_U32(ecx + 0x2cu));
  a = edi;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10009a99;
  edi = ebp;
  BB_U32(esp + 0x10u) = ebx;
  args[sp + 4u] = BB_U32(esp + 16u);
L_10009a99:;
  ebx = (ebx + 1u);
  a = ebx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10009a7a;
L_10009a9e:;
  eax = BB_U32(esp + 0x10u);
L_10009aa2:;
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
uint32_t __attribute__((naked)) bb_qmix_session_oldest_channel_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_oldest_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif

void bb_qmix_session_soonest_channel_c(uint32_t *input) {
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
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
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
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = ecx;
  SAMPLE_CALL(0x10009b10u, 0, 0x10009abeu); /* 10009ab9 */
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10009b08;
  esi = 0x7fffffffu;
  SAMPLE_CALL(BB_U32(0x10021374u), 0, 0x10009acfu); /* 10009ac9 indirect */
  ebp = (ebp ^ ebp);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(edi + 0x1cu);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10009b06;
L_10009ada:;
  eax = BB_U32(edi + 0x20u);
  ecx = BB_U32(eax + ebp * 4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009b00;
  a = BB_U32(ecx + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009b00;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x54u), 0, 0x10009aefu); /* 10009aec indirect */
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009b00;
  eax = (eax - BB_U32(esp + 0x10u));
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10009b00;
  ebx = ebp;
  esi = eax;
L_10009b00:;
  ebp = (ebp + 1u);
  a = BB_U32(edi + 0x1cu);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10009ada;
L_10009b06:;
  eax = ebx;
L_10009b08:;
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
uint32_t __attribute__((naked)) bb_qmix_session_soonest_channel_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_soonest_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif

void bb_qmix_session_idle_channel_c(uint32_t *input) {
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
  BB_U32(esp) = args[sp];
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
  edi = (edi ^ edi);
  ebp = (ebp ^ ebp);
  a = BB_U32(esi + 0x1cu);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10009b42;
  ebx = (ebx ^ ebx);
L_10009b21:;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(eax + ebx);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009b39;
  a = BB_U32(ecx + 0x4u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009b39;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x64u), 0, 0x10009b35u); /* 10009b32 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009b4c;
L_10009b39:;
  ebx = (ebx + 0x4u);
  edi = (edi + 1u);
  a = BB_U32(esi + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10009b21;
L_10009b42:;
  eax = 0xffffffffu;
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
L_10009b4c:;
  eax = edi;
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
uint32_t __attribute__((naked)) bb_qmix_session_idle_channel_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_idle_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif

void bb_qmix_session_play_free_failed_c(uint32_t *input) {
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
  eax = BB_U32(ebp + 0xffffffecu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10009a22u); /* 10009a1d */
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
uint32_t __attribute__((naked)) bb_qmix_session_play_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_play_free_failed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif

void bb_qmix_session_play_release_c(uint32_t *input) {
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
  esi = BB_U32(ecx);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009a47;
  ecx = esi;
  SAMPLE_CALL(0x10005c70u, 0, 0x10009a3eu); /* 10009a39 */
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10009a44u); /* 10009a3f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10009a47:;
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
uint32_t __attribute__((naked)) bb_qmix_session_play_release_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_play_release_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif

#ifdef BB_MODEL_RUNTIME
void bb_qmix_session_play_c(uint32_t *input) {
  uint32_t owner = input[3] + 500u;
  BB_U32(owner - 8u) = 0;
  struct bb_qmix_model_error error;
  if (bb_qmix_model_try_body(session_play_body, input, owner, &error))
    return;
  /* A constructed voice is still local until a queue node owns it. The old
   * state map omits that interval if allocating the node pool fails. */
  if (BB_I32(owner + 8u) == -1 && BB_U32(owner - 8u)) {
    struct bb_call_state call = {0, owner - 8u, 0, 0x202};
    bb_model_invoke(&call, 0x10009a30u, NULL, 0);
    BB_U32(owner - 8u) = 0;
  }
  bb_qmix_model_raise_error(error);
}
#endif
