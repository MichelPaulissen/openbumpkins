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
/* C forms of QMIXER driver lifecycle, callbacks and wave handle operations.
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
static uint32_t exception_head(void) {
  uint32_t value;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(value));
  return value;
}
static void exception_link(uint32_t value) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(value) : "memory");
}
#endif
void bb_qmix_wave_handle_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = ecx;
  BB_U32(ecx) = 0xffffffffu;
  BB_U32(ecx + 0x4u) = 0x0u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_wave_handle_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_handle_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_handle_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100043a0u, 0, BB_U32(esp - 4u)); /* tail call 100041f0 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_wave_handle_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_handle_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_open_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x10u);
  edx = BB_U32(esp + 0xcu);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x14u);
  edx = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ecx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021370u), 24, 0x10004225u); /* 1000421f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000423c;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x1000423cu); /* 10004237 */
L_1000423c:;
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
bb_qmix_wave_open_entry(uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused)),
                        uint32_t arg2 __attribute__((unused)),
                        uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_open_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_wave_prepare_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(eax + 0x10u) = 0x0u;
  args[--sp] = 0x20u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002136cu), 12, 0x1000426bu); /* 10004265 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004282;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10004282u); /* 1000427d */
L_10004282:;
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
bb_qmix_wave_prepare_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_prepare_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_wave_write_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  ecx = BB_U32(ecx + 0x4u);
  args[--sp] = 0x20u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021368u), 12, 0x100042a4u); /* 1000429e indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100042bb;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100042bbu); /* 100042b6 */
L_100042bb:;
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
bb_qmix_wave_write_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_write_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_wave_unprepare_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  ecx = BB_U32(ecx + 0x4u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100042f0;
  eax = BB_U32(esp + 0x8u);
  a = BYTE(eax + 0x10u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100042f0;
  args[--sp] = 0x20u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002133cu), 12, 0x100042eeu); /* 100042e8 indirect */
  goto L_100042f2;
L_100042f0:;
  eax = (eax ^ eax);
L_100042f2:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004309;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10004309u); /* 10004304 */
L_10004309:;
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
bb_qmix_wave_unprepare_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_unprepare_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_wave_reset_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021364u), 4, 0x1000431du); /* 10004317 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004334;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10004334u); /* 1000432f */
L_10004334:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_reset_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_pause_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021360u), 4, 0x1000434du); /* 10004347 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004364;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10004364u); /* 1000435f */
L_10004364:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_pause_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_pause_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_restart_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002135cu), 4, 0x1000437du); /* 10004377 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004394;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10004394u); /* 1000438f */
L_10004394:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_restart_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_restart_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_close_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx + 0x4u);
  esi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100043b6;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021358u), 4, 0x100043b4u); /* 100043ae indirect */
  goto L_100043b8;
L_100043b6:;
  eax = (eax ^ eax);
L_100043b8:;
  BB_U32(esi + 0x4u) = 0x0u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100043d6;
  BB_U32(esp + 0x4u) = eax;
  args[sp + 1u] = BB_U32(esp + 4u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100043d6u); /* 100043d1 */
L_100043d6:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_close_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_close_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_window_callback_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(0x1001e178u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021330u), 20, 0x100044b3u); /* 100044ad indirect */
  a = esi;
  b = 0x1cu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100044be;
  BB_U32(0x1001e17cu) = edi;
L_100044be:;
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
bb_qmix_directsound_window_callback_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1 __attribute__((unused)),
                                          uint32_t arg2 __attribute__((unused)),
                                          uint32_t arg3
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_window_callback_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_directsound_timer_callback_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x14u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000450d;
  args[--sp] = 0x130u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100044e6u); /* 100044e0 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000450d;
  ecx = esi;
  SAMPLE_CALL(0x100062e0u, 0, 0x100044f1u); /* 100044ec */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000450d;
  edi = (esi + 0x50u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100044ffu); /* 100044f9 indirect */
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x10004506u); /* 10004503 indirect */
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021290u), 4, 0x1000450du); /* 10004507 indirect */
L_1000450d:;
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
bb_qmix_directsound_timer_callback_entry(uint32_t arg0 __attribute__((unused)),
                                         uint32_t arg1 __attribute__((unused)),
                                         uint32_t arg2 __attribute__((unused)),
                                         uint32_t arg3 __attribute__((unused)),
                                         uint32_t arg4
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_timer_callback_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_directsound_timer_start_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  a = BB_U32(ecx + 0x114u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000455a;
  a = BB_U32(esi + 0x108u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000455a;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021350u), 4, 0x1000453du); /* 10004537 indirect */
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0xf4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100044d0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021354u), 20, 0x10004554u); /* 1000454e indirect */
  BB_U32(esi + 0x108u) = eax;
L_1000455a:;
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
uint32_t __attribute__((naked)) bb_qmix_directsound_timer_start_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_timer_start_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_timer_stop_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  a = BB_U32(ecx + 0x114u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000458f;
  eax = BB_U32(esi + 0x108u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004587;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021348u), 4, 0x1000457du); /* 10004577 indirect */
  BB_U32(esi + 0x108u) = 0x0u;
L_10004587:;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002134cu), 4, 0x1000458fu); /* 10004589 indirect */
L_1000458f:;
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
uint32_t __attribute__((naked)) bb_qmix_directsound_timer_stop_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_timer_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10004647u;
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
  BB_U32(ebp + 0xfffffff0u) = ecx;
  esi = ecx;
  SAMPLE_CALL(0x100027b0u, 0, 0x100045c6u); /* 100045c1 */
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(ebp + 0xfffffffcu) = eax;
  BB_U32(esi + 0xf0u) = eax;
  BB_U32(esi + 0xf4u) = eax;
  BB_U32(esi + 0xf8u) = eax;
  BB_U32(esi + 0xfcu) = eax;
  BB_U32(esi + 0x100u) = eax;
  BB_U32(esi + 0x104u) = eax;
  BB_U32(esi + 0x108u) = eax;
  BB_U32(esi + 0x10cu) = eax;
  BB_U32(esi + 0x110u) = eax;
  BB_U32(esi + 0x118u) = eax;
  BB_U32(esi + 0x11cu) = eax;
  BB_U32(esi + 0x120u) = eax;
  BB_U32(esi + 0x124u) = eax;
  BB_U32(esi + 0x128u) = eax;
  eax = esi;
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_directsound);
  BB_U32(esi + 0x114u) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_directsound_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_constructor_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100028c0u, 0, BB_U32(esp - 4u)); /* tail call 10004654 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_directsound_constructor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_constructor_cleanup_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_get_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(ecx + 0x11cu);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_directsound_get_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_delete_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x10004690u, 0, 0x10004678u); /* 10004673 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10004688;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10004685u); /* 10004680 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10004688:;
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
bb_qmix_directsound_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_directsound_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10004722u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_directsound);
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
  eax = BB_U32(0x1001e178u);
  edx = BB_U32(ecx + 0x118u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002132cu), 12, 0x100046d0u); /* 100046ca indirect */
  ecx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(ecx + 0x124u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100046e3;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x100046e3u); /* 100046e0 indirect */
L_100046e3:;
  eax = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(eax + 0x120u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100046f6;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x100046f6u); /* 100046f3 indirect */
L_100046f6:;
  eax = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(eax + 0x11cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004709;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x10004709u); /* 10004706 indirect */
L_10004709:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x1000472cu, 0, 0x10004715u); /* 10004710 */
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
uint32_t __attribute__((naked)) bb_qmix_directsound_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_destructor_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100028c0u, 0, BB_U32(esp - 4u)); /* tail call 1000472f */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_directsound_destructor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_destructor_cleanup_c) ";leal "
                                                "4(%esp),%esp;popal;popfl;leal "
                                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_attach_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx + 0x11cu) = eax;
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 4, 0x10004750u); /* 1000474d indirect */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_directsound_attach_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_attach_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_directsound_window_select_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = (esp);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002131cu), 8, 0x100048a9u); /* 100048a3 indirect */
  eax = BB_U32(esp + 0x8u);
  a = BB_U32(esi);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100048be;
  eax = (eax ^ eax);
  BB_U32(esi + 0x4u) = edi;
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
L_100048be:;
  eax = 0x1u;
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
bb_qmix_directsound_window_select_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_window_select_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_directsound_activate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  edi = BB_U32(esp + 0xcu);
  a = BB_U32(esi + 0x8u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004c18;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005670u, 4, 0x10004be3u); /* 10004bde */
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004bfc;
  a = BB_U32(esi + 0x10cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004c18;
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x10004bf7u); /* 10004bf4 indirect */
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
L_10004bfc:;
  a = BB_U32(esi + 0x10cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004c11;
  eax = BB_U32(esi + 0x128u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(eax);
  SAMPLE_CALL(BB_U32(edi + 0x48u), 4, 0x10004c11u); /* 10004c0e indirect */
L_10004c11:;
  ecx = esi;
  SAMPLE_CALL(0x10004560u, 0, 0x10004c18u); /* 10004c13 */
L_10004c18:;
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
bb_qmix_directsound_activate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_activate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_directsound_profile_end_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = ecx;
  esi = BB_U32(esp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10002ff0u, 4, 0x10004c2eu); /* 10004c29 */
  eax = BB_U32(edi + 0xd0u);
  BB_U32(esi) = eax;
  ecx = BB_U32(edi + 0xe0u);
  BB_U32(esi + 0x4u) = ecx;
  edx = BB_U32(edi + 0xe4u);
  BB_U32(esi + 0x8u) = edx;
  eax = BB_U32(edi + 0xe8u);
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0xcu) = eax;
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
bb_qmix_directsound_profile_end_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_directsound_profile_end_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_profile_interval_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = (esp);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x10004fdfu); /* 10004fd9 indirect */
  eax = BB_U32(esp + 0x4u);
  ecx = BB_U32(esp + 0x10u);
  edx = BB_U32(esp + 0x4u);
  esi = BB_U32(esp + 0x8u);
  eax = (eax - BB_U32(ecx));
  BB_U32(ecx) = edx;
  BB_U32(ecx + 0x4u) = esi;
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
bb_qmix_profile_interval_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_profile_interval_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_window_callback_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(esp + 0x8u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ecx;
  b = 0x3bdu;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if (!(flags & 0x40u))
    goto L_10009c4e;
  ebx = BB_U32(esp + 0x1cu);
  esi = BB_U32(ebx + 0xcu);
  edi = (esi + 0x50u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10009c30u); /* 10009c2a indirect */
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x1000a620u, 4, 0x10009c38u); /* 10009c33 */
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x10009c3fu); /* 10009c3c indirect */
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021290u), 4, 0x10009c46u); /* 10009c40 indirect */
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
L_10009c4e:;
  eax = BB_U32(esp + 0x1cu);
  edx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021314u), 16, 0x10009c64u); /* 10009c5e indirect */
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
bb_qmix_waveout_window_callback_entry(uint32_t arg0 __attribute__((unused)),
                                      uint32_t arg1 __attribute__((unused)),
                                      uint32_t arg2 __attribute__((unused)),
                                      uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_window_callback_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_waveout_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10009da6u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
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
  BB_U32(ebp + 0xfffffff0u) = ecx;
  esi = ecx;
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  SAMPLE_CALL(0x100027b0u, 0, 0x10009c99u); /* 10009c94 */
  ecx = (esi + 0xf0u);
  BB_U32(ebp + 0xfffffffcu) = edi;
  SAMPLE_CALL(0x100041e0u, 0, 0x10009ca7u); /* 10009ca2 */
  ecx = (esi + 0xf8u);
  args[--sp] = 0x10009c10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  args[--sp] = 0x1001e2a8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000aae0u, 8, 0x10009cc0u); /* 10009cbb */
  eax = 0x2u;
  BYTE(ebp + 0xfffffffcu) = ((uint8_t)(eax));
  BB_U32(esi + 0x104u) = edi;
  BB_U32(esi + 0x108u) = edi;
  BB_U32(esi + 0x10cu) = edi;
  BB_U32(esi + 0x110u) = edi;
  BB_U32(esi + 0x114u) = edi;
  BB_U32(esi + 0x118u) = edi;
  BB_U32(esi + 0x120u) = edi;
  BB_U32(esi + 0x124u) = edi;
  BB_U32(esi + 0x11cu) = 0xau;
  BYTE(ebp + 0xfffffffcu) = ((uint8_t)(eax));
  eax = 0x4u;
  BYTE(ebp + 0xfffffffcu) = ((uint8_t)(eax));
  BB_U32(esi + 0x128u) = edi;
  BB_U32(esi + 0x12cu) = edi;
  eax = esi;
  BB_U32(esi + 0x134u) = edi;
  BB_U32(esi + 0x130u) = 0xau;
  BYTE(ebp + 0xfffffffcu) = 0x6u;
  BB_U32(esi + 0x138u) = edi;
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_waveout);
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  edi = args[sp++];
  esp += 4u;
  exception_link(ecx);
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
uint32_t __attribute__((naked)) bb_qmix_waveout_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_constructor_pending_pool_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x130u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000aba0u, 0, BB_U32(esp - 4u)); /* tail call 10009d5b */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_pending_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_pending_pool_cleanup_c) ";leal "
                                                          "4(%esp),%esp;popal;"
                                                          "popfl;leal "
                                                          "512(%esp),%esp;ret "
                                                          "$0");
}
#endif
void bb_qmix_waveout_constructor_pending_list_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x128u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10009de0u, 0, BB_U32(esp - 4u)); /* tail call 10009d69 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_pending_list_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_pending_list_cleanup_c) ";leal "
                                                          "4(%esp),%esp;popal;"
                                                          "popfl;leal "
                                                          "512(%esp),%esp;ret "
                                                          "$0");
}
#endif
void bb_qmix_waveout_constructor_playing_pool_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x11cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000aba0u, 0, BB_U32(esp - 4u)); /* tail call 10009d77 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_playing_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_playing_pool_cleanup_c) ";leal "
                                                          "4(%esp),%esp;popal;"
                                                          "popfl;leal "
                                                          "512(%esp),%esp;ret "
                                                          "$0");
}
#endif
void bb_qmix_waveout_constructor_playing_list_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x114u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10009de0u, 0, BB_U32(esp - 4u)); /* tail call 10009d85 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_playing_list_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_playing_list_cleanup_c) ";leal "
                                                          "4(%esp),%esp;popal;"
                                                          "popfl;leal "
                                                          "512(%esp),%esp;ret "
                                                          "$0");
}
#endif
void bb_qmix_waveout_constructor_window_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0xf8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000ab70u, 0, BB_U32(esp - 4u)); /* tail call 10009d93 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_window_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_window_cleanup_c) ";leal "
                                                    "4(%esp),%esp;popal;popfl;"
                                                    "leal 512(%esp),%esp;ret "
                                                    "$0");
}
#endif
void bb_qmix_waveout_constructor_wave_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0xf0u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100041f0u, 0, BB_U32(esp - 4u)); /* tail call 10009da1 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_wave_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_wave_cleanup_c) ";leal "
                                                  "4(%esp),%esp;popal;popfl;"
                                                  "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_constructor_base_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100028c0u, 0, BB_U32(esp - 4u)); /* tail call 10009db3 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_constructor_base_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_constructor_base_cleanup_c) ";leal "
                                                  "4(%esp),%esp;popal;popfl;"
                                                  "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_delete_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x10009e30u, 0, 0x10009dc8u); /* 10009dc3 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10009dd8;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10009dd5u); /* 10009dd0 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10009dd8:;
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
bb_qmix_waveout_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_waveout_list_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10009e17u;
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
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(ebp + 0xfffffff0u) = ecx;
  SAMPLE_CALL(0x10009e21u, 0, 0x10009e0au); /* 10009e05 */
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
uint32_t __attribute__((naked)) bb_qmix_waveout_list_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_list_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_list_pool_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000aba0u, 0, BB_U32(esp - 4u)); /* tail call 10009e27 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_waveout_list_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_list_pool_cleanup_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10009fbfu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_waveout);
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
  esi = (esi ^ esi);
  BB_U32(ebp + 0xfffffffcu) = esi;
  BYTE(ebp + 0xfffffffcu) = 0x4u;
  a = BB_U32(ecx + 0x10cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009ea8;
  a = BB_U32(ecx + 0x104u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10009e96;
  edi = (edi ^ edi);
L_10009e71:;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = edi;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = esi;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  esi = result;
  eax = BB_U32(edx + 0x10cu);
  ecx = BB_U32(eax + edi + 0xffffffe0u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10009e88u); /* 10009e83 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x104u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10009e71;
L_10009e96:;
  edx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(edx + 0x10cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10009ea5u); /* 10009ea0 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10009ea8:;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x114u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009ef8;
L_10009eb4:;
  edx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(edx + 0x114u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009eec;
  a = BB_U32(edx + 0x118u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10009ed3;
  BB_U32(edx + 0x118u) = 0x0u;
L_10009ed3:;
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(edx + 0x114u) = eax;
  eax = BB_U32(edx + 0x120u);
  BB_U32(ecx) = eax;
  BB_U32(edx + 0x120u) = ecx;
L_10009eec:;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x114u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10009eb4;
L_10009ef8:;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x128u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10009f48;
L_10009f04:;
  edx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(edx + 0x128u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009f3c;
  a = BB_U32(edx + 0x12cu);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10009f23;
  BB_U32(edx + 0x12cu) = 0x0u;
L_10009f23:;
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(edx + 0x128u) = eax;
  eax = BB_U32(edx + 0x134u);
  BB_U32(ecx) = eax;
  BB_U32(edx + 0x134u) = ecx;
L_10009f3c:;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x128u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10009f04;
L_10009f48:;
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  SAMPLE_CALL(0x10009f87u, 0, 0x10009f51u); /* 10009f4c */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  SAMPLE_CALL(0x10009f95u, 0, 0x10009f5au); /* 10009f55 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x10009fa3u, 0, 0x10009f63u); /* 10009f5e */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10009fb1u, 0, 0x10009f6cu); /* 10009f67 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x10009fc9u, 0, 0x10009f78u); /* 10009f73 */
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
uint32_t __attribute__((naked)) bb_qmix_waveout_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_destructor_pending_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x128u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10009de0u, 0, BB_U32(esp - 4u)); /* tail call 10009f90 */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_destructor_pending_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_destructor_pending_cleanup_c) ";leal "
                                                    "4(%esp),%esp;popal;popfl;"
                                                    "leal 512(%esp),%esp;ret "
                                                    "$0");
}
#endif
void bb_qmix_waveout_destructor_playing_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0x114u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10009de0u, 0, BB_U32(esp - 4u)); /* tail call 10009f9e */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_destructor_playing_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_destructor_playing_cleanup_c) ";leal "
                                                    "4(%esp),%esp;popal;popfl;"
                                                    "leal 512(%esp),%esp;ret "
                                                    "$0");
}
#endif
void bb_qmix_waveout_destructor_window_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0xf8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000ab70u, 0, BB_U32(esp - 4u)); /* tail call 10009fac */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_destructor_window_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_destructor_window_cleanup_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_waveout_destructor_wave_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = 0xf0u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100041f0u, 0, BB_U32(esp - 4u)); /* tail call 10009fba */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_destructor_wave_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_destructor_wave_cleanup_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_destructor_base_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100028c0u, 0, BB_U32(esp - 4u)); /* tail call 10009fcc */
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_waveout_destructor_base_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_destructor_base_cleanup_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_complete_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(ecx + 0x128u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  edi = BB_U32(esp + 0x10u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000a69b;
  a = BB_U32(edx + 0x4u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a666;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000a69b;
  a = BB_U32(esi + 0x12cu);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a64e;
  BB_U32(esi + 0x12cu) = 0x0u;
L_1000a64e:;
  eax = BB_U32(edx);
  ecx = BB_U32(esi + 0x134u);
  BB_U32(esi + 0x128u) = eax;
  BB_U32(edx) = ecx;
  BB_U32(esi + 0x134u) = edx;
  goto L_1000a69b;
L_1000a666:;
  ecx = BB_U32(edx);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000a69b;
L_1000a66c:;
  a = BB_U32(ecx + 0x4u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a67b;
  edx = ecx;
  ecx = BB_U32(ecx);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000a66c;
  goto L_1000a69b;
L_1000a67b:;
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  a = BB_U32(esi + 0x12cu);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a68d;
  BB_U32(esi + 0x12cu) = edx;
L_1000a68d:;
  eax = BB_U32(esi + 0x134u);
  BB_U32(ecx) = eax;
  BB_U32(esi + 0x134u) = ecx;
L_1000a69b:;
  a = BB_U32(esi + 0x120u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a700;
  eax = BB_U32(esi + 0x11cu);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000a6b6u); /* 1000a6b1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x124u);
  edx = (edx ^ edx);
  BB_U32(eax) = ecx;
  ecx = BB_U32(esi + 0x11cu);
  BB_U32(esi + 0x124u) = eax;
  eax = (eax + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000a6ec;
  ebx = eax;
L_1000a6d7:;
  ecx = (ebx + 0x8u);
  ebx = (ebx + 0x8u);
  edx = (edx + 1u);
  BB_U32(ebx + 0xfffffff8u) = ecx;
  ecx = BB_U32(esi + 0x11cu);
  ecx = (ecx - 1u);
  a = ecx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1000a6d7;
L_1000a6ec:;
  ecx = BB_U32(esi + 0x11cu);
  BB_U32(eax + ecx * 8u + 0xfffffff8u) = 0x0u;
  BB_U32(esi + 0x120u) = eax;
L_1000a700:;
  ecx = BB_U32(esi + 0x120u);
  eax = BB_U32(ecx);
  BB_U32(esi + 0x120u) = eax;
  BB_U32(ecx + 0x4u) = edi;
  BB_U32(ecx) = 0x0u;
  eax = BB_U32(esi + 0x118u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000a725;
  BB_U32(eax) = ecx;
  goto L_1000a72b;
L_1000a725:;
  BB_U32(esi + 0x114u) = ecx;
L_1000a72b:;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0x118u) = ecx;
  a = BB_U32(esi + 0x110u);
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  BB_U32(esi + 0x110u) = result;
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
bb_qmix_waveout_complete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_complete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_waveout_profile_end_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = ecx;
  esi = BB_U32(esp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10002ff0u, 4, 0x1000a74eu); /* 1000a749 */
  eax = BB_U32(edi + 0xd0u);
  BB_U32(esi) = eax;
  ecx = BB_U32(edi + 0xe0u);
  BB_U32(esi + 0x4u) = ecx;
  edx = BB_U32(edi + 0xe4u);
  BB_U32(esi + 0x8u) = edx;
  eax = BB_U32(edi + 0xe8u);
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0xcu) = eax;
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
bb_qmix_waveout_profile_end_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_waveout_profile_end_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_driver_window_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x28u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx + 0x4u) = eax;
  esi = ecx;
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  BB_U32(esp + 0x2cu) = eax;
  args[sp + 11u] = BB_U32(esp + 44u);
  WORD(esi) = ((uint16_t)(edi));
  BB_U32(esi + 0x8u) = edi;
  ecx = BB_U32(0x100209e0u);
  BB_U32(esp + 0x20u) = edi;
  args[sp + 8u] = BB_U32(esp + 32u);
  BB_U32(esp + 0x1cu) = edi;
  args[sp + 7u] = BB_U32(esp + 28u);
  BB_U32(esp + 0x28u) = edi;
  args[sp + 10u] = BB_U32(esp + 40u);
  edx = BB_U32(esp + 0x38u);
  BB_U32(esp + 0x24u) = edi;
  args[sp + 9u] = BB_U32(esp + 36u);
  BB_U32(esp + 0x18u) = ecx;
  args[sp + 6u] = BB_U32(esp + 24u);
  BB_U32(esp + 0x8u) = edi;
  args[sp + 2u] = BB_U32(esp + 8u);
  eax = (esp + 0x8u);
  BB_U32(esp + 0xcu) = edx;
  args[sp + 3u] = BB_U32(esp + 12u);
  BB_U32(esp + 0x14u) = edi;
  args[sp + 5u] = BB_U32(esp + 20u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esp + 0x14u) = edi;
  args[sp + 5u] = BB_U32(esp + 20u);
  SAMPLE_CALL(BB_U32(0x1002130cu), 4, 0x1000ab33u); /* 1000ab2d indirect */
  WORD(esi) = ((uint16_t)(eax));
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(0x100209e0u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(esi + 0x4u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x8000000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e320u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021304u), 48, 0x1000ab59u); /* 1000ab53 indirect */
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0x8u) = eax;
  eax = esi;
  esi = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x28u;
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
bb_qmix_driver_window_constructor_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_driver_window_constructor_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_driver_window_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx + 0x8u);
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212fcu), 4, 0x1000ab7du); /* 1000ab77 indirect */
  a = WORD(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (flags & 0x40u)
    goto L_1000ab93;
  eax = BB_U32(0x100209e0u);
  ecx = BB_U32(esi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021308u), 8, 0x1000ab93u); /* 1000ab8d indirect */
L_1000ab93:;
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
uint32_t __attribute__((naked)) bb_qmix_driver_window_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_driver_window_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_pool_chain_release_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx + 0x8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000abb9;
L_1000aba8:;
  esi = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000abb0u); /* 1000abab */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = esi;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000aba8;
L_1000abb9:;
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
uint32_t __attribute__((naked)) bb_qmix_pool_chain_release_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pool_chain_release_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
