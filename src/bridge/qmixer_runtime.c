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
/* C forms of QMIXER DLL entry, initialization, teardown and allocation
 * wrappers. Address-derived labels retain provenance for auditing phase
 * transitions, arithmetic boundaries and callback ordering.
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

void bb_qmix_dll_module_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  a = BB_U32(esp + 0x8u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10001010;
  eax = BB_U32(esp + 0x4u);
  BB_U32(0x100209e0u) = eax;
L_10001010:;
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
uint32_t __attribute__((naked))
bb_qmix_dll_module_entry(uint32_t arg0 __attribute__((unused)),
                         uint32_t arg1 __attribute__((unused)),
                         uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_dll_module_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "2048(%esp),%esp;ret $12");
}
#endif
void bb_qmix_allocate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  a = eax;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10001031u); /* 1000102c */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = eax;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10001051;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esp + 0x7u)) & 0xffu) << 0);
  args[--sp] = 0x1001ad90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BYTE(esp + 0xbu) = ((uint8_t)(eax));
  args[sp + 2u] = BB_U32(esp + 8u);
  eax = (esp + 0xbu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10001051u); /* 1000104c */
L_10001051:;
  eax = esi;
  eax = (eax & 0x4u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  eax = 0x2u;
  a = eax & 0xffffffffu;
  b = 0xffffffffu & 0xffffffffu;
  wide = (uint64_t)a + b + (flags & 1u);
  result = (uint32_t)wide & 0xffffffffu;
  eax = result;
  BB_U32(esi + eax * 4u + 0xfffffffcu) = esi;
  eax = (esi + eax * 4u);
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
bb_qmix_allocate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_allocate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_free_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32(esp + 0x4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10001084;
  eax = BB_U32(eax + 0xfffffffcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100117c0u, 0, 0x10001081u); /* 1000107c */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10001084:;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_free_entry(uint32_t arg0
                                                   __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_free_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                          "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_factory_bootstrap_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000c6e0u, 0, BB_U32(esp - 4u)); /* tail call 1000c6d0 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_integer_factory_bootstrap_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_integer_factory_bootstrap_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_factory_bootstrap_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000d160u, 0, BB_U32(esp - 4u)); /* tail call 1000d150 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_short_factory_bootstrap_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_short_factory_bootstrap_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_factory_bootstrap_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000df30u, 0, BB_U32(esp - 4u)); /* tail call 1000df20 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_float_factory_bootstrap_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_float_factory_bootstrap_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_crt_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  eax = BB_U32(esp + 0x8u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10011f2f;
  SAMPLE_CALL(BB_U32(0x10021228u), 0, 0x10011e53u); /* 10011e4d indirect */
  a = BB_U32(0x1001f4a4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(0x1001fe50u) = eax;
  if (!(flags & 0x40u))
    goto L_10011e9f;
  a = ((uint8_t)(eax));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10011e76;
  a = eax;
  b = 0x80000000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011e76;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10015380u, 0, 0x10011e73u); /* 10011e6e */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10011e76:;
  args[--sp] = 0x1001a768u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002122cu), 4, 0x10011e81u); /* 10011e7b indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011e9f;
  args[--sp] = 0x1001a760u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100211f0u), 8, 0x10011e91u); /* 10011e8b indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011e9f;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10015380u, 0, 0x10011e9cu); /* 10011e97 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10011e9f:;
  SAMPLE_CALL(0x10012190u, 0, 0x10011ea4u); /* 10011e9f */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10011ead;
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
L_10011ead:;
  edx = (edx ^ edx);
  ecx = BB_U32(0x1001fe50u);
  edx =
      (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx >> 8))) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx >> 16);
  eax = (eax & 0xffu);
  BB_U32(0x1001fe5cu) = edx;
  BB_U32(0x1001fe58u) = eax;
  eax = (eax << 8);
  BB_U32(0x1001fe50u) = ecx;
  a = eax;
  b = edx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  BB_U32(0x1001fe54u) = eax;
  SAMPLE_CALL(BB_U32(0x100212f0u), 0, 0x10011ee2u); /* 10011edc indirect */
  BB_U32(0x10020e70u) = eax;
  SAMPLE_CALL(0x100151f0u, 0, 0x10011eecu); /* 10011ee7 */
  a = BB_U32(0x10020e70u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(0x1001f494u) = eax;
  if (flags & 0x40u)
    goto L_10011f25;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011f25;
  SAMPLE_CALL(0x100149a0u, 0, 0x10011f03u); /* 10011efe */
  SAMPLE_CALL(0x100151e0u, 0, 0x10011f08u); /* 10011f03 */
  SAMPLE_CALL(0x10014ca0u, 0, 0x10011f0du); /* 10011f08 */
  SAMPLE_CALL(0x10014bb0u, 0, 0x10011f12u); /* 10011f0d */
  SAMPLE_CALL(0x10014870u, 0, 0x10011f17u); /* 10011f12 */
  a = BB_U32(0x1001f490u);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(0x1001f490u) = result;
L_10011f1d:;
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
L_10011f25:;
  SAMPLE_CALL(0x100121d0u, 0, 0x10011f2au); /* 10011f25 */
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
L_10011f2f:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10011f1d;
  eax = BB_U32(0x1001f490u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10011f62;
  eax = (eax - 1u);
  a = BB_U32(0x1001fe88u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(0x1001f490u) = eax;
  if (!(flags & 0x40u))
    goto L_10011f50;
  SAMPLE_CALL(0x100148c0u, 0, 0x10011f50u); /* 10011f4b */
L_10011f50:;
  SAMPLE_CALL(0x10014b80u, 0, 0x10011f55u); /* 10011f50 */
  SAMPLE_CALL(0x100121d0u, 0, 0x10011f5au); /* 10011f55 */
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
L_10011f62:;
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
bb_qmix_crt_initialize_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "2048(%esp),%esp;ret $12");
}
#endif
void bb_qmix_dll_entry_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = 0x1u;
  ebx = BB_U32(esp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if (!(flags & 0x40u))
    goto L_10011f93;
  a = BB_U32(0x1001f490u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10011f93;
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
L_10011f93:;
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10011f9d;
  a = ebx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10011fd7;
L_10011f9d:;
  ecx = BB_U32(0x10020e74u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011fb6;
  edi = BB_U32(esp + 0x1cu);
  esi = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ecx, 12, 0x10011fb4u); /* 10011fb2 indirect */
  goto L_10011fbe;
L_10011fb6:;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x1cu);
L_10011fbe:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011fce;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011e40u, 12, 0x10011fcau); /* 10011fc5 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10011fdf;
L_10011fce:;
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
L_10011fd7:;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x1cu);
L_10011fdf:;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001000u, 12, 0x10011fe7u); /* 10011fe2 */
  ebp = eax;
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10011ffb;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10011ffb;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011e40u, 12, 0x10011ffbu); /* 10011ff6 */
L_10011ffb:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012004;
  a = ebx;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10012026;
L_10012004:;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011e40u, 12, 0x1001200cu); /* 10012007 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10012012;
  ebp = (ebp ^ ebp);
L_10012012:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012026;
  eax = BB_U32(0x10020e74u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012026;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(eax, 12, 0x10012024u); /* 10012022 indirect */
  ebp = eax;
L_10012026:;
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
bb_qmix_dll_entry_entry(uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused)),
                        uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_dll_entry_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "2048(%esp),%esp;ret $12");
}
#endif
void bb_qmix_exception_bootstrap_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100120d0u, 0, BB_U32(esp - 4u)); /* tail call 100120c0 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_exception_bootstrap_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exception_bootstrap_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exception_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = 0x10012150u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10015ab0u, 0, 0x100120dau); /* 100120d5 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x100120f0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021230u), 4, 0x100120e8u); /* 100120e2 indirect */
  BB_U32(0x10020c00u) = eax;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_exception_initialize_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exception_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exception_restore_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  eax = BB_U32(0x10020c00u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021230u), 4, 0x1001215cu); /* 10012156 indirect */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_exception_restore_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exception_restore_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_allocation_retry_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  ecx = BB_U32(0x10020c04u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001217e;
  eax = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ecx, 0, 0x10012171u); /* 1001216f indirect */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001217e;
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
L_1001217e:;
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
bb_qmix_allocation_retry_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_allocation_retry_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_heap_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002121cu), 12, 0x1001219fu); /* 10012199 indirect */
  BB_U32(0x10020e6cu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100121ab;
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
L_100121ab:;
  SAMPLE_CALL(0x10012210u, 0, 0x100121b0u); /* 100121ab */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100121c3;
  eax = BB_U32(0x10020e6cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021220u), 4, 0x100121c0u); /* 100121ba indirect */
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
L_100121c3:;
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
uint32_t __attribute__((naked)) bb_qmix_heap_initialize_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_heap_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_heap_destroy_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x1001f4b8u;
  edi = BB_U32(0x10021224u);
L_100121dd:;
  eax = BB_U32(esi + 0x810u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100121f1;
  args[--sp] = 0x8000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 12, 0x100121f1u); /* 100121ef indirect */
L_100121f1:;
  esi = BB_U32(esi);
  a = esi;
  b = 0x1001f4b8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100121dd;
  eax = BB_U32(0x10020e6cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021220u), 4, 0x10012207u); /* 10012201 indirect */
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
uint32_t __attribute__((naked)) bb_qmix_heap_destroy_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_heap_destroy_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_constructors_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  eax = BB_U32(0x1001f484u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001487b;
  SAMPLE_CALL(eax, 0, 0x1001487bu); /* 10014879 indirect */
L_1001487b:;
  args[--sp] = 0x1001e020u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e018u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014980u, 0, 0x1001488au); /* 10014885 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0x1001e014u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014980u, 0, 0x1001489cu); /* 10014897 */
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
uint32_t __attribute__((naked)) bb_qmix_constructors_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_constructors_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exit_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100148d0u, 0, 0x100148cbu); /* 100148c6 */
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
uint32_t __attribute__((naked)) bb_qmix_exit_cleanup_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exit_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exit_dispatch_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  a = BB_U32(0x1001fe8cu);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if (!(flags & 0x40u))
    goto L_100148ee;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021208u), 0, 0x100148e7u); /* 100148e1 indirect */
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021218u), 8, 0x100148eeu); /* 100148e8 indirect */
L_100148ee:;
  BB_U32(0x1001fe88u) = 0x1u;
  a = BB_U32(esp + 0x14u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  ebx = BB_U32(esp + 0x18u);
  BYTE(0x1001fe84u) = ((uint8_t)(ebx));
  if (!(flags & 0x40u))
    goto L_10014948;
  a = BB_U32(0x10020e68u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10014936;
  edi = BB_U32(0x10020e64u);
  edi = (edi - 0x4u);
  a = edi;
  b = BB_U32(0x10020e68u);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10014936;
L_10014923:;
  eax = BB_U32(edi);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001492b;
  SAMPLE_CALL(eax, 0, 0x1001492bu); /* 10014929 indirect */
L_1001492b:;
  edi = (edi - 0x4u);
  a = edi;
  b = BB_U32(0x10020e68u);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10014923;
L_10014936:;
  args[--sp] = 0x1001e028u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e024u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014980u, 0, 0x10014945u); /* 10014940 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10014948:;
  args[--sp] = 0x1001e030u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e02cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014980u, 0, 0x10014957u); /* 10014952 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001496f;
  BB_U32(0x1001fe8cu) = 0x1u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021210u), 4, 0x1001496fu); /* 10014969 indirect */
L_1001496f:;
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
bb_qmix_exit_dispatch_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exit_dispatch_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_initializer_range_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(esp + 0x10u);
  esi = BB_U32(esp + 0xcu);
  a = edi;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_1001499d;
L_1001498e:;
  eax = BB_U32(esi);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014996;
  SAMPLE_CALL(eax, 0, 0x10014996u); /* 10014994 indirect */
L_10014996:;
  esi = (esi + 0x4u);
  a = edi;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1001498e;
L_1001499d:;
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
bb_qmix_initializer_range_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_initializer_range_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_io_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x10020d60u;
L_10014b86:;
  eax = BB_U32(esi);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014b95;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100117c0u, 0, 0x10014b92u); /* 10014b8d */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10014b95:;
  esi = (esi + 0x4u);
  a = esi;
  b = 0x10020e60u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10014b86;
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
uint32_t __attribute__((naked)) bb_qmix_io_cleanup_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_io_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_runtime_mode_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32(esp + 0x4u);
  BB_U32(0x1001f4a4u) = eax;
  input[0] = edi;
  input[1] = esi;
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
bb_qmix_runtime_mode_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_runtime_mode_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
#endif
