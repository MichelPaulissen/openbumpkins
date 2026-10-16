/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <signal.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include <stdio.h>
/* Preserve fatal reporting and the configured quick-exit callback without
 * routing a terminal failure through the obsolete MSVC reporting machinery. */
static void terminate_call(uint32_t *r, uint32_t target, const uint32_t *args,
                           unsigned count) {
  struct bb_call_state state = {r[7], r[6], r[5], r[8]};
  bb_model_invoke(&state, target, args, count);
  r[7] = state.eax;
  r[6] = state.ecx;
  r[5] = state.edx;
  r[8] = state.flags;
}
void bb_qmix_fatal_error_c(uint32_t *r) {
  fprintf(stderr, "QMixer95 runtime error %u\n", BB_U32(r[3] + 2056u));
  uint32_t code = 255;
  terminate_call(r, BB_U32(0x1001f49c), &code, 1);
}
void bb_qmix_quick_exit_c(uint32_t *r) {
  uint32_t args[] = {BB_U32(r[3] + 2056u), 1, 0};
  terminate_call(r, 0x100148d0u, args, 3);
}
void bb_qmix_pure_virtual_failure_c(uint32_t *r) {
  uint32_t error = 25;
  terminate_call(r, 0x10012030u, &error, 1);
}
#endif
#define QWORD(a) BB_U64(a)
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
/* C forms of QMIXER error reporting, exception boundaries and bounded
 * string/pointer helpers. Address-derived labels retain provenance for auditing
 * phase transitions, arithmetic boundaries and callback ordering.
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
static uint32_t __attribute__((unused))
add_flags(uint32_t flags, uint32_t a, uint32_t b,
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
    bb_qmix_call(&call, (uintptr_t)(target), args + sp, esp, edi, esi, ebp,    \
                 ebx, returned);                                               \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

#ifndef BB_MODEL_RUNTIME
void bb_qmix_exception_raise_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 12u);
  a = esp;
  b = 0x20u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  ecx = 0x8u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x1001a738u;
  edi = (esp + 0x8u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = BB_U32(esp + 0x2cu);
  ecx = BB_U32(esp + 0x30u);
  edx = (esp + 0x1cu);
  BB_U32(esp + 0x20u) = eax;
  args[sp + 8u] = BB_U32(esp + 32u);
  eax = BB_U32(esp + 0x18u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(esp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esp + 0x2cu) = ecx;
  args[sp + 11u] = BB_U32(esp + 44u);
  ecx = BB_U32(esp + 0x14u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212dcu), 16, 0x100116ffu); /* 100116f9 indirect */
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
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
}
uint32_t __attribute__((naked))
bb_qmix_exception_raise_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exception_raise_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $8");
}
#endif
void bb_qmix_string_compare_bounded_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0x10u);
  if (!ecx)
    goto L_10011c31;
  ebx = ecx;
  edi = BB_U32(ebp + 0x8u);
  esi = edi;
  eax = (eax ^ eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = (0u - ecx);
  ecx = (ecx + ebx);
  edi = esi;
  esi = BB_U32(ebp + 0xcu);
  while (ecx) {
    a = BYTE(esi);
    b = BYTE(edi);
    uint32_t step = (flags & 0x400u) ? 0xffffffffu : 1u;
    esi += step;
    edi += step;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (!(flags & 0x40u))
      break;
  }
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(esi + 0xffffffffu)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  a = ((uint8_t)(eax));
  b = BYTE(edi + 0xffffffffu);
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_10011c2f;
  if (flags & 0x40u)
    goto L_10011c31;
  ecx = (ecx - 1u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
L_10011c2f:;
  ecx = ~(ecx);
L_10011c31:;
  eax = ecx;
  ebx = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  edi = args[sp++];
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
bb_qmix_string_compare_bounded_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_string_compare_bounded_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "2048(%esp),%esp;ret $0");
}
void bb_qmix_pure_virtual_failure_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 4u);
  args[--sp] = 0x19u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012030u, 0, 0x10011c47u); /* 10011c42 */
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
uint32_t __attribute__((naked)) bb_qmix_pure_virtual_failure_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_pure_virtual_failure_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_string_find_last_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(ebp + 0x8u);
  eax = (eax ^ eax);
  ecx = (ecx | 0xffffffffu);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = (ecx + 1u);
  ecx = (0u - ecx);
  edi = (edi - 1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xcu)) & 0xffu) << 0);
  flags |= 0x400u;
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  edi = (edi + 1u);
  a = BYTE(edi);
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10011e31;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_10011e33;
L_10011e31:;
  eax = edi;
L_10011e33:;
  flags &= ~0x400u;
  edi = args[sp++];
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
bb_qmix_string_find_last_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_string_find_last_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $0");
}
void bb_qmix_fatal_error_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 8u);
  eax = BB_U32(0x1001f4a0u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10012047;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001204c;
  a = BB_U32(0x1001f4a4u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001204c;
L_10012047:;
  SAMPLE_CALL(0x10015390u, 0, 0x1001204cu); /* 10012047 */
L_1001204c:;
  eax = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100153d0u, 0, 0x10012056u); /* 10012051 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = 0xffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1001f49cu), 0, 0x10012064u); /* 1001205e indirect */
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
uint32_t __attribute__((naked))
bb_qmix_fatal_error_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_fatal_error_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "2048(%esp),%esp;ret $0");
}
void bb_qmix_exception_filter_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x8u);
  eax = BB_U32(esi);
  a = BB_U32(eax);
  b = 0xe06d7363u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001211c;
  a = BB_U32(eax + 0x10u);
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001211c;
  a = BB_U32(eax + 0x14u);
  b = 0x19930520u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001211c;
  SAMPLE_CALL(0x100134f0u, 0, 0x10012113u); /* 1001210e */
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
L_1001211c:;
  a = BB_U32(0x10020c00u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10012142;
  eax = BB_U32(0x10020c00u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10015b50u, 0, 0x10012130u); /* 1001212b */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012142;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10020c00u), 4, 0x1001213eu); /* 10012138 indirect */
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
L_10012142:;
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
uint32_t __attribute__((naked))
bb_qmix_exception_filter_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exception_filter_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $4");
}
void bb_qmix_quick_exit_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 8u);
  eax = BB_U32(esp + 0x4u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100148d0u, 0, 0x100148aeu); /* 100148a9 */
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
uint32_t __attribute__((naked))
bb_qmix_quick_exit_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_quick_exit_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "2048(%esp),%esp;ret $0");
}
void bb_qmix_error_banner_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 4u);
  eax = BB_U32(0x1001f4a0u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100153a7;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100153cc;
  a = BB_U32(0x1001f4a4u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100153cc;
L_100153a7:;
  args[--sp] = 0xfcu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100153d0u, 0, 0x100153b1u); /* 100153ac */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x10020148u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100153bf;
  SAMPLE_CALL(eax, 0, 0x100153bfu); /* 100153bd indirect */
L_100153bf:;
  args[--sp] = 0xffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100153d0u, 0, 0x100153c9u); /* 100153c4 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100153cc:;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
uint32_t __attribute__((naked)) bb_qmix_error_banner_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_error_banner_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
void bb_qmix_error_report_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 8u);
  a = esp;
  b = 0x1a8u;
  esp -= b;
  sp -= b / 4u;
  ecx = (ecx ^ ecx);
  eax = 0x100200b8u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(esp + 0x1b0u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
L_100153e8:;
  a = BB_U32(eax);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100153f7;
  eax = (eax + 0x8u);
  ecx = (ecx + 1u);
  a = eax;
  b = 0x10020148u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_100153e8;
L_100153f7:;
  a = BB_U32(ecx * 8u + 0x100200b8u);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  ebx = (ecx * 8u);
  if (!(flags & 0x40u))
    goto L_100155bb;
  a = BB_U32(0x1001f4a0u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10015577;
  a = BB_U32(0x1001f4a0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001542e;
  a = BB_U32(0x1001f4a4u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10015577;
L_1001542e:;
  a = edx;
  b = 0xfcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100155bb;
  eax = (esp + 0xb4u);
  args[--sp] = 0x104u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = BB_U32(0x100212ccu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebp, 12, 0x10015451u); /* 1001544f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001546b;
  esi = 0x1001abe0u;
  edi = (esp + 0xb4u);
  ecx = 0x5u;
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  WORD(edi) = WORD(esi);
  uint32_t step_10015468 = (flags & 0x400u) ? 0u - 2u : 2u;
  esi += step_10015468;
  edi += step_10015468;
  BYTE(edi) = BYTE(esi);
  uint32_t step_1001546a = (flags & 0x400u) ? 0u - 1u : 1u;
  esi += step_1001546a;
  edi += step_1001546a;
L_1001546b:;
  ebp = (esp + 0xb4u);
  edi = (esp + 0xb4u);
  ecx = 0xffffffffu;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  a = ecx;
  b = 0x3cu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_100154af;
  edi = (esp + 0xb4u);
  ecx = 0xffffffffu;
  eax = (eax - eax);
  args[--sp] = 0x3u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  ebp = (esp + ecx + 0x7cu);
  args[--sp] = 0x1001abdcu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100168a0u, 0, 0x100154acu); /* 100154a7 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_100154af:;
  esi = 0x1001abc0u;
  edi = (esp + 0x14u);
  ecx = 0x6u;
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  WORD(edi) = WORD(esi);
  uint32_t step_100154bf = (flags & 0x400u) ? 0u - 2u : 2u;
  esi += step_100154bf;
  edi += step_100154bf;
  edi = ebp;
  ecx = 0xffffffffu;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  edi = (edi - ecx);
  edx = ecx;
  esi = edi;
  ecx = 0xffffffffu;
  edi = (esp + 0x14u);
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  edi = (edi - 1u);
  ecx = edx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = edx;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  edi = 0x1001abbcu;
  ecx = 0xffffffffu;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  edi = (edi - ecx);
  edx = ecx;
  esi = edi;
  ecx = 0xffffffffu;
  edi = (esp + 0x14u);
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  edi = (edi - 1u);
  ecx = edx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = edx;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  edi = BB_U32(ebx + 0x100200bcu);
  ecx = 0xffffffffu;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  edi = (edi - ecx);
  esi = edi;
  edx = ecx;
  edi = (esp + 0x14u);
  ecx = 0xffffffffu;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  edi = (edi - 1u);
  ecx = edx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = edx;
  args[--sp] = 0x12010u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  args[--sp] = 0x1001ab94u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = (esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10016800u, 0, 0x10015569u); /* 10015564 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x1a8u;
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
L_10015577:;
  a = BB_U32(0x10020d60u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1001558d;
  eax = BB_U32(0x10020d60u);
  esi = BB_U32(eax + 0x10u);
  a = esi;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10015597;
L_1001558d:;
  args[--sp] = 0xfffffff4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100211fcu), 4, 0x10015595u); /* 1001558f indirect */
  esi = eax;
L_10015597:;
  edx = BB_U32(ebx + 0x100200bcu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x14u);
  edi = edx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = 0xffffffffu;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021254u), 20, 0x100155bbu); /* 100155b5 indirect */
L_100155bb:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x1a8u;
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
uint32_t __attribute__((naked))
bb_qmix_error_report_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_error_report_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
void bb_qmix_memory_read_probe_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 12u);
  eax = BB_U32(esp + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x1u;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(esp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021250u), 8, 0x10015b26u); /* 10015b20 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10015b2c;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
L_10015b2c:;
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
uint32_t __attribute__((naked))
bb_qmix_memory_read_probe_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_memory_read_probe_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $0");
}
void bb_qmix_memory_write_probe_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 12u);
  eax = BB_U32(esp + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x1u;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(esp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10015b46u); /* 10015b40 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10015b4c;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
L_10015b4c:;
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
uint32_t __attribute__((naked))
bb_qmix_memory_write_probe_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_memory_write_probe_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "2048(%esp),%esp;ret $0");
}
void bb_qmix_code_probe_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 8u);
  eax = BB_U32(esp + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x1u;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002124cu), 4, 0x10015b61u); /* 10015b5b indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10015b67;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
L_10015b67:;
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
uint32_t __attribute__((naked))
bb_qmix_code_probe_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_code_probe_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "2048(%esp),%esp;ret $0");
}
void bb_qmix_error_message_box_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 16u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = (esi ^ esi);
  a = BB_U32(0x100204bcu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001684f;
  args[--sp] = 0x1001ac60u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100211f4u), 4, 0x10016818u); /* 10016812 indirect */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001688c;
  args[--sp] = 0x1001ac54u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(0x100211f0u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 8, 0x1001682cu); /* 1001682a indirect */
  BB_U32(0x100204bcu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001688c;
  args[--sp] = 0x1001ac44u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 8, 0x1001683du); /* 1001683b indirect */
  args[--sp] = 0x1001ac30u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(0x100204c0u) = eax;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 8, 0x1001684au); /* 10016848 indirect */
  BB_U32(0x100204c4u) = eax;
L_1001684f:;
  eax = BB_U32(0x100204c0u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001685c;
  SAMPLE_CALL(eax, 0, 0x1001685au); /* 10016858 indirect */
  esi = eax;
L_1001685c:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10016872;
  a = BB_U32(0x100204c4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10016872;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100204c4u), 4, 0x10016870u); /* 1001686a indirect */
  esi = eax;
L_10016872:;
  eax = BB_U32(esp + 0x18u);
  ecx = BB_U32(esp + 0x14u);
  edx = BB_U32(esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100204bcu), 16, 0x10016888u); /* 10016882 indirect */
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
L_1001688c:;
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
uint32_t __attribute__((naked))
bb_qmix_error_message_box_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_error_message_box_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $0");
}
void bb_qmix_string_copy_bounded_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, (void *)(uintptr_t)esp, 16u);
  ecx = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10016923;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = ecx;
  esi = BB_U32(esp + 0x14u);
  a = esi;
  b = 0x3u;
  flags = logic_flags(flags, a & b, 32);
  edi = BB_U32(esp + 0x10u);
  if (!(flags & 0x40u))
    goto L_100168c4;
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_10016931;
  goto L_100168e5;
L_100168c4:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  esi = (esi + 1u);
  BYTE(edi) = ((uint8_t)(eax));
  edi = (edi + 1u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_100168f2;
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100168fa;
  a = esi;
  b = 0x3u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100168c4;
  ebx = ecx;
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_10016931;
L_100168e0:;
  a = ebx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  if (flags & 0x40u)
    goto L_100168f2;
L_100168e5:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  esi = (esi + 1u);
  BYTE(edi) = ((uint8_t)(eax));
  edi = (edi + 1u);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1001691e;
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  if (!(flags & 0x40u))
    goto L_100168e5;
L_100168f2:;
  eax = BB_U32(esp + 0x10u);
  ebx = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
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
L_100168fa:;
  a = edi;
  b = 0x3u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10016914;
L_10016902:;
  BYTE(edi) = ((uint8_t)(eax));
  edi = (edi + 1u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_10016996;
  a = edi;
  b = 0x3u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10016902;
L_10016914:;
  ebx = ecx;
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_10016987;
L_1001691b:;
  BYTE(edi) = ((uint8_t)(eax));
  edi = (edi + 1u);
L_1001691e:;
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  if (!(flags & 0x40u))
    goto L_1001691b;
  ebx = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
L_10016923:;
  eax = BB_U32(esp + 0x8u);
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
L_10016929:;
  BB_U32(edi) = edx;
  edi = (edi + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_100168e0;
L_10016931:;
  edx = 0x7efefeffu;
  eax = BB_U32(esi);
  edx = (edx + eax);
  eax = (eax ^ 0xffffffffu);
  eax = (eax ^ edx);
  edx = BB_U32(esi);
  esi = (esi + 0x4u);
  a = eax;
  b = 0x81010100u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10016929;
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1001697b;
  a = ((uint8_t)(edx >> 8));
  b = ((uint8_t)(edx >> 8));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10016971;
  a = edx;
  b = 0xff0000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10016967;
  a = edx;
  b = 0xff000000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10016929;
  BB_U32(edi) = edx;
  goto L_1001697f;
L_10016967:;
  edx = (edx & 0xffffu);
  BB_U32(edi) = edx;
  goto L_1001697f;
L_10016971:;
  edx = (edx & 0xffu);
  BB_U32(edi) = edx;
  goto L_1001697f;
L_1001697b:;
  edx = (edx ^ edx);
  BB_U32(edi) = edx;
L_1001697f:;
  edi = (edi + 0x4u);
  eax = (eax ^ eax);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (flags & 0x40u)
    goto L_10016991;
L_10016987:;
  eax = (eax ^ eax);
L_10016989:;
  BB_U32(edi) = eax;
  edi = (edi + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_10016989;
L_10016991:;
  a = ebx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  if (!(flags & 0x40u))
    goto L_1001691b;
L_10016996:;
  eax = BB_U32(esp + 0x10u);
  ebx = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked))
bb_qmix_string_copy_bounded_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_string_copy_bounded_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
