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
/* C forms of QMIXER session factories, device setup and playback pumps.
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
static void directsound_create_body(uint32_t *input) {
#else
void bb_qmix_directsound_create_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10004477u;
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
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x130u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = 0x0u;
  SAMPLE_CALL(0x10001020u, 0, 0x1000440du); /* 10004408 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffff0u) = eax;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004427;
  ecx = eax;
  SAMPLE_CALL(0x100045a0u, 0, 0x10004425u); /* 10004420 */
  edi = eax;
L_10004427:;
  ebx = BB_U32(ebp + 0xcu);
  ecx = edi;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  esi = BB_U32(edi);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(esi + 0x40u), 4, 0x10004439u); /* 10004436 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004458;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = edi;
  SAMPLE_CALL(BB_U32(esi + 0x3cu), 8, 0x10004447u); /* 10004444 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004458;
  eax = BB_U32(ebp + 0x8u);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(esi + 0x2cu), 4, 0x10004454u); /* 10004451 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10004464;
L_10004458:;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004462;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = edi;
  SAMPLE_CALL(BB_U32(esi), 4, 0x10004462u); /* 10004460 indirect */
L_10004462:;
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
L_10004464:;
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
bb_qmix_directsound_create_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused)),
                                 uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_directsound_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_free_failed_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000448au); /* 10004485 */
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
uint32_t __attribute__((naked)) bb_qmix_directsound_free_failed_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_directsound_free_failed_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_directsound_configure_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = ecx;
  esi = BB_U32(esp + 0x14u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005450u, 8, 0x10004775u); /* 10004770 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10004782;
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
L_10004782:;
  a = ebx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e1b0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x10004792u); /* 1000478d */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0xf4u) = eax;
  eax = BB_U32(esi + 0x2u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) & 0x60u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x60u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_100047b7;
  a = WORD(esi + 0x10u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_100047b7;
  eax = (eax ^ eax);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0x12u)) & 0xffffu) << 0);
  BB_U32(edi + 0xf4u) = eax;
L_100047b7:;
  a = BB_U32(edi + 0xf4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000480d;
  eax = BB_U32(edi + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e1a4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x100047d6u); /* 100047d1 */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ebp = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e194u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x100047e8u); /* 100047e3 */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ebx = BB_U32(0x100212b4u);
  a = ebp;
  b = eax;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  flags = (flags & ~0x801u) |
          (((int64_t)wide != (int64_t)(int32_t)result) ? 0x801u : 0u);
  ebp = result;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 12, 0x100047f7u); /* 100047f5 indirect */
  BB_U32(edi + 0xf4u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10004813;
  BB_U32(edi + 0xf4u) = 0x32u;
  goto L_10004813;
L_1000480d:;
  ebx = BB_U32(0x100212b4u);
L_10004813:;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(edi + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(edi + 0xf4u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 12, 0x10004825u); /* 10004823 indirect */
  eax = (eax & 0xfffffff8u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  BB_U32(edi + 0xf0u) = eax;
  ecx = (eax * 2u);
  edx = (eax + eax * 2u);
  BB_U32(edi + 0xf8u) = ecx;
  BB_U32(edi + 0x100u) = edx;
  if (flags & 0x40u)
    goto L_10004879;
  a = BYTE(esi + 0x2u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10004859;
  eax = BB_U32(esi + 0xau);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10004740u, 4, 0x10004859u); /* 10004854 */
L_10004859:;
  a = BYTE(esi + 0x2u);
  b = 0x8u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10004869;
  BB_U32(edi + 0x110u) = 0x1u;
L_10004869:;
  a = BYTE(esi + 0x2u);
  b = 0x80u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10004879;
  BB_U32(edi + 0x114u) = 0x0u;
L_10004879:;
  eax = 0x1u;
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
bb_qmix_directsound_configure_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_directsound_configure_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "2048(%esp),%esp;ret $8");
}
#endif
void bb_qmix_directsound_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x184u;
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
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10002950u, 4, 0x100048e6u); /* 100048e1 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004bb3;
  eax = (esp + 0x30u);
  ecx = (esp + 0x94u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e1ccu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d8u), 24, 0x1000490fu); /* 10004909 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004ba9;
  args[--sp] = 0x1001e1ccu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100211f4u), 4, 0x10004922u); /* 1000491c indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004ba9;
  a = BB_U32(esi + 0x11cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10004a74;
  args[--sp] = 0x1001e1b8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100211f0u), 8, 0x10004943u); /* 1000493d indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000494e;
  args[--sp] = 0x14u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_1000494e:;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (esi + 0x11cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(eax, 12, 0x1000495bu); /* 10004959 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004966;
  args[--sp] = 0x15u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004966:;
  SAMPLE_CALL(BB_U32(0x100212d4u), 0, 0x1000496cu); /* 10004966 indirect */
  edi = eax;
  SAMPLE_CALL(BB_U32(0x10021320u), 0, 0x10004974u); /* 1000496e indirect */
  BB_U32(esi + 0x118u) = eax;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax + 1u);
  BB_U32(0x1001e17cu) = eax;
  eax = BB_U32(esi + 0x118u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100049a1;
  ecx = (esp + 0x2cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002131cu), 8, 0x1000499bu); /* 10004995 indirect */
  a = BB_U32(esp + 0x2cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100049c7;
L_100049a1:;
  BB_U32(esp + 0x28u) = 0x0u;
  args[sp + 10u] = BB_U32(esp + 40u);
  BB_U32(esp + 0x24u) = edi;
  args[sp + 9u] = BB_U32(esp + 36u);
  edi = (esp + 0x24u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10004890u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021324u), 8, 0x100049bdu); /* 100049b7 indirect */
  eax = BB_U32(esp + 0x28u);
  BB_U32(esi + 0x118u) = eax;
L_100049c7:;
  args[--sp] = 0x10004490u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x118u);
  args[--sp] = 0xfffffffcu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = (esi + 0x110u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002132cu), 12, 0x100049e1u); /* 100049db indirect */
  BB_U32(0x1001e178u) = eax;
  ecx = BB_U32(esi + 0x11cu);
  edx = BB_U32(esi + 0x118u);
  a = BB_U32(ebx);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  ebp = BB_U32(ecx);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0xfffffffeu);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebp + 0x18u), 12, 0x10004a05u); /* 10004a02 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004a10;
  args[--sp] = 0x16u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004a10:;
  eax = (esp + 0x10u);
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  BB_U32(eax) = ecx;
  BB_U32(eax + 0x4u) = ecx;
  edi = (esi + 0x120u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(eax + 0x8u) = ecx;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(eax + 0xcu) = ecx;
  BB_U32(eax + 0x10u) = ecx;
  eax = BB_U32(esi + 0x11cu);
  BB_U32(esp + 0x18u) = 0x14u;
  args[sp + 6u] = BB_U32(esp + 24u);
  BB_U32(esp + 0x1cu) = 0x1u;
  args[sp + 7u] = BB_U32(esp + 28u);
  ecx = (esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 16, 0x10004a4du); /* 10004a4a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004a58;
  args[--sp] = 0x18u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004a58:;
  eax = BB_U32(edi);
  ecx = (esi + 0x28u);
  ebp = BB_U32(eax);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebp + 0x38u), 8, 0x10004a64u); /* 10004a61 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004a80;
  a = BB_U32(ebx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004a80;
  args[--sp] = 0x1au;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004a74:;
  ebx = (esi + 0x110u);
  BB_U32(ebx) = 0x0u;
L_10004a80:;
  edi = (esp + 0x34u);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x18u;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(esp + 0x34u) = 0x60u;
  args[sp + 13u] = BB_U32(esp + 52u);
  eax = BB_U32(esi + 0x11cu);
  ecx = (esp + 0x34u);
  ebp = BB_U32(eax);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebp + 0x10u), 8, 0x10004aa6u); /* 10004aa3 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004ab1;
  args[--sp] = 0x17u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004ab1:;
  a = BB_U32(ebx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004b12;
  eax = BB_U32(esi + 0x120u);
  ecx = (esp + 0x10u);
  BB_U32(esp + 0x10u) = 0x14u;
  args[sp + 4u] = BB_U32(esp + 16u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0xcu), 8, 0x10004acfu); /* 10004acc indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004ada;
  args[--sp] = 0x19u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004ada:;
  eax = BB_U32(esp + 0x18u);
  BB_U32(esi + 0x100u) = eax;
  eax = BB_U32(esp + 0x18u);
  a = BB_U32(esi + 0xf8u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10004afc;
  eax = (eax - BB_U32(esi + 0xf0u));
  BB_U32(esi + 0xf8u) = eax;
L_10004afc:;
  eax = BB_U32(esi + 0x120u);
  BB_U32(esi + 0x128u) = eax;
  eax = 0x1u;
  goto L_10004bb5;
L_10004b12:;
  eax = (esp + 0x10u);
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  BB_U32(eax) = edi;
  ecx = BB_U32(esi + 0x100u);
  BB_U32(eax + 0x4u) = edi;
  BB_U32(eax + 0x8u) = edi;
  BB_U32(eax + 0xcu) = edi;
  BB_U32(eax + 0x10u) = edi;
  eax = (esi + 0x28u);
  ebx = (esi + 0x124u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esp + 0x14u) = 0x14u;
  args[sp + 5u] = BB_U32(esp + 20u);
  BB_U32(esp + 0x18u) = 0x1c008u;
  args[sp + 6u] = BB_U32(esp + 24u);
  BB_U32(esp + 0x1cu) = ecx;
  args[sp + 7u] = BB_U32(esp + 28u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (esp + 0x18u);
  BB_U32(esp + 0x28u) = eax;
  args[sp + 10u] = BB_U32(esp + 40u);
  eax = BB_U32(esi + 0x11cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 16, 0x10004b60u); /* 10004b5d indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004b96;
  ebp = 0x1001e180u;
L_10004b69:;
  a = BB_U32(0x1001e18cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004ba5;
  eax = BB_U32(ebp);
  ebp = (ebp + 0x4u);
  edi = (edi + 1u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (esp + 0x14u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = ~(eax);
  a = BB_U32(esp + 0x1cu);
  b = eax;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  BB_U32(esp + 0x1cu) = result;
  args[sp + 7u] = BB_U32(esp + 28u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x11cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 16, 0x10004b92u); /* 10004b8f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10004b69;
L_10004b96:;
  eax = BB_U32(ebx);
  BB_U32(esi + 0x128u) = eax;
  eax = 0x1u;
  goto L_10004bb5;
L_10004ba5:;
  args[--sp] = 0x1bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_10004bab;
L_10004ba9:;
  args[--sp] = 0x13u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
L_10004bab:;
  SAMPLE_CALL(0x10005040u, 0, 0x10004bb0u); /* 10004bab */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10004bb3:;
  eax = (eax ^ eax);
L_10004bb5:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x184u;
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
bb_qmix_directsound_initialize_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_directsound_initialize_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "2048(%esp),%esp;ret $4");
}
#endif
void bb_qmix_directsound_pump_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x2cu;
  esp -= b;
  sp -= b / 4u;
  a = BB_U32(0x1001e190u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
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
  if (!(flags & 0x40u))
    goto L_10004fba;
  a = BB_U32(esi + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004fba;
  BB_U32(0x1001e190u) = 0x1u;
  a = BB_U32(0x1001e17cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10004cb8;
  eax = BB_U32(esi + 0x128u);
  a = BB_U32(esi + 0x120u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10004cb8;
  SAMPLE_CALL(0x10004520u, 0, 0x10004ca6u); /* 10004ca1 */
  BB_U32(0x1001e190u) = 0x0u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
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
L_10004cb8:;
  a = BB_U32(esi + 0xccu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004ce6;
  eax = (esp + 0x30u);
  edi = BB_U32(0x100212c4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 4, 0x10004cceu); /* 10004ccc indirect */
  edx = BB_U32(esp + 0x30u);
  ecx = BB_U32(esp + 0x34u);
  BB_U32(esp + 0x18u) = edx;
  args[sp + 6u] = BB_U32(esp + 24u);
  a = BB_U32(esi + 0xd0u);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(esi + 0xd0u) = result;
  BB_U32(esp + 0x1cu) = ecx;
  args[sp + 7u] = BB_U32(esp + 28u);
  goto L_10004cec;
L_10004ce6:;
  edi = BB_U32(0x100212c4u);
L_10004cec:;
  eax = BB_U32(esi + 0x128u);
  ecx = (esp + 0x28u);
  ebx = BB_U32(eax);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0x24u), 8, 0x10004cfdu); /* 10004cfa indirect */
  a = BYTE(esp + 0x28u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10004d30;
  eax = BB_U32(esi + 0x128u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(eax);
  SAMPLE_CALL(BB_U32(ebx + 0x50u), 4, 0x10004d10u); /* 10004d0d indirect */
  a = eax;
  b = 0x88780096u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10004d30;
  ecx = esi;
  SAMPLE_CALL(0x10004520u, 0, 0x10004d1eu); /* 10004d19 */
  BB_U32(0x1001e190u) = 0x0u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
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
L_10004d30:;
  a = BB_U32(esi + 0x10cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004d72;
  a = BYTE(esp + 0x28u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10004d72;
  eax = BB_U32(esi + 0x128u);
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(eax);
  SAMPLE_CALL(BB_U32(ebx + 0x30u), 16, 0x10004d52u); /* 10004d4f indirect */
  a = eax;
  b = 0x88780096u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10004d72;
  ecx = esi;
  SAMPLE_CALL(0x10004520u, 0, 0x10004d60u); /* 10004d5b */
  BB_U32(0x1001e190u) = 0x0u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
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
L_10004d72:;
  ecx = BB_U32(esi + 0x128u);
  eax = (esp + 0x38u);
  edx = (esp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(ecx);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0x10u), 12, 0x10004d88u); /* 10004d85 indirect */
  a = BB_U32(esi + 0xccu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004da4;
  eax = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10004fd0u, 0, 0x10004d9bu); /* 10004d96 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0xe4u) = (BB_U32(esi + 0xe4u) + eax);
L_10004da4:;
  a = BB_U32(esi + 0x10cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10004db7;
  eax = BB_U32(esp + 0x2cu);
  BB_U32(esi + 0x104u) = eax;
L_10004db7:;
  eax = BB_U32(esi + 0xf0u);
  edx = BB_U32(esi + 0xf8u);
  eax = (eax + eax);
  edx = (edx + BB_U32(esp + 0x2cu));
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esi + 0x100u);
  a = edx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10004dd9;
  edx = (edx - eax);
L_10004dd9:;
  ecx = BB_U32(esi + 0x104u);
  a = edx;
  b = ecx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edx = result;
  if (flags & 0x80u)
    goto L_10004de9;
  BB_U32(esp + 0x10u) = edx;
  args[sp + 4u] = BB_U32(esp + 16u);
  goto L_10004def;
L_10004de9:;
  eax = (eax + edx);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
L_10004def:;
  eax = BB_U32(esp + 0x14u);
  a = BB_U32(esp + 0x10u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x14u);
  if (!(flags & 0x41u))
    goto L_10004e01;
  eax = (esp + 0x10u);
L_10004e01:;
  ebx = BB_U32(eax);
  a = ebx;
  b = 0xfffffff8u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  if (flags & 0x40u)
    goto L_10004f92;
  edx = BB_U32(esi + 0x128u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x14u);
  ebp = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x2cu);
  ebp = (esp + 0x30u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(edx);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x2cu), 32, 0x10004e30u); /* 10004e2d indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004e46;
  BB_U32(0x1001e190u) = 0x0u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
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
L_10004e46:;
  a = BB_U32(esi + 0xccu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004e62;
  eax = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10004fd0u, 0, 0x10004e59u); /* 10004e54 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0xe4u) = (BB_U32(esi + 0xe4u) + eax);
L_10004e62:;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  eax = BB_U32(esp + 0x20u);
  if (!(flags & 0x40u))
    goto L_10004e86;
  a = eax;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  eax = result;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x10002b90u, 8, 0x10004e7du); /* 10004e78 */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = ebp;
  b = ebp;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  ebp = result;
  ebp = (0u - ebp);
  goto L_10004eba;
L_10004e86:;
  a = eax;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  eax = result;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x10002b90u, 8, 0x10004e96u); /* 10004e91 */
  ecx = BB_U32(esp + 0x14u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = ebp;
  b = ebp;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  ebp = result;
  eax = BB_U32(esp + 0x10u);
  ebp = (0u - ebp);
  a = eax;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x10002b90u, 8, 0x10004eb1u); /* 10004eac */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (0u - eax);
  ebp = (ebp & eax);
L_10004eba:;
  ecx = BB_U32(esi + 0x104u);
  eax = BB_U32(esi + 0x100u);
  ecx = (ecx + ebx);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10004ece;
  ecx = (ecx - eax);
L_10004ece:;
  a = BB_U32(esi + 0xccu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esi + 0x104u) = ecx;
  if (flags & 0x40u)
    goto L_10004ef0;
  eax = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10004fd0u, 0, 0x10004ee7u); /* 10004ee2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esi + 0xe0u);
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(esi + 0xe0u) = result;
L_10004ef0:;
  ecx = BB_U32(esi + 0x128u);
  eax = BB_U32(esp + 0x10u);
  edx = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x24u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(esp + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x4cu), 20, 0x10004f10u); /* 10004f0d indirect */
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004f49;
  eax = BB_U32(esi + 0xfcu);
  eax = (eax + ebx);
  BB_U32(esi + 0xfcu) = eax;
  a = BB_U32(esi + 0x100u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10004f76;
  eax = BB_U32(esi + 0x128u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(eax);
  SAMPLE_CALL(BB_U32(ebx + 0x48u), 4, 0x10004f36u); /* 10004f33 indirect */
  BB_U32(esi + 0x10cu) = 0x0u;
  ecx = esi;
  SAMPLE_CALL(0x10004560u, 0, 0x10004f47u); /* 10004f42 */
  goto L_10004f76;
L_10004f49:;
  BB_U32(esi + 0xfcu) = 0x0u;
  ecx = esi;
  SAMPLE_CALL(0x10004520u, 0, 0x10004f5au); /* 10004f55 */
  eax = BB_U32(esi + 0x128u);
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esi + 0x10cu) = 0x1u;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(eax);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0x30u), 16, 0x10004f76u); /* 10004f73 indirect */
L_10004f76:;
  a = BB_U32(esi + 0xccu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004fb0;
  eax = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10004fd0u, 0, 0x10004f89u); /* 10004f84 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0xe4u) = (BB_U32(esi + 0xe4u) + eax);
L_10004f92:;
  a = BB_U32(esi + 0xccu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10004fb0;
  eax = (esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 4, 0x10004fa2u); /* 10004fa0 indirect */
  eax = BB_U32(esp + 0x18u);
  eax = (eax - BB_U32(esp + 0x30u));
  BB_U32(esi + 0xe8u) = (BB_U32(esi + 0xe8u) + eax);
L_10004fb0:;
  BB_U32(0x1001e190u) = 0x0u;
L_10004fba:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_directsound_pump_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_directsound_pump_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_create_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0xbcu;
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
  SAMPLE_CALL(BB_U32(0x10021340u), 0, 0x10005260u); /* 1000525a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000527b;
  args[--sp] = 0x6u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x1000526bu); /* 10005266 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xbcu;
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
L_1000527b:;
  args[--sp] = 0x104u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x100209e0u);
  args[--sp] = 0x100209e8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212ccu), 12, 0x10005291u); /* 1000528b indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100052ac;
  args[--sp] = 0x7u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x1000529cu); /* 10005297 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xbcu;
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
L_100052ac:;
  a = ebx;
  b = ebx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  args[--sp] = 0x100209e8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(0x10021280u);
  SAMPLE_CALL(esi, 8, 0x100052c0u); /* 100052be indirect */
  args[--sp] = 0x5cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011e10u, 0, 0x100052ccu); /* 100052c7 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100052fc;
  args[--sp] = 0x1001e278u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(esi, 8, 0x100052dbu); /* 100052d9 indirect */
  eax = (esp + 0x44u);
  args[--sp] = 0x4000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BYTE(esp + 0x48u) = 0x88u;
  args[sp + 18u] = BB_U32(esp + 72u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c8u), 12, 0x100052f5u); /* 100052ef indirect */
  eax = (eax + 1u);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = ebx;
  b = ebx;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  ebx = result;
  a = ebx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ebx = result;
L_100052fc:;
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e268u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e168u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c0u), 16, 0x10005313u); /* 1000530d indirect */
  edi = eax;
  args[--sp] = 0x34u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x14u);
  ebp = BB_U32(0x10021344u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebp, 12, 0x10005325u); /* 10005323 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005335;
  eax = (esp + 0x18u);
  args[--sp] = 0x1001e258u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(esi, 8, 0x10005335u); /* 10005333 indirect */
L_10005335:;
  esi = BB_U32(esp + 0xd0u);
  a = BYTE(esi + 0x2u);
  b = 0x20u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005382;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0x10u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 16);
  if (flags & 1u)
    goto L_10005363;
  args[--sp] = 0x8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10005353u); /* 1000534e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = (eax ^ eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xbcu;
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
L_10005363:;
  ecx = (esp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = ((uint16_t)(eax));
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax * 8u + 0x1001e20cu), 0,
              0x10005374u); /* 1000536d indirect */
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
  b = 0xbcu;
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
L_10005382:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100053d5;
  a = BB_U32(0x1001e220u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005402;
  ebx = 0x1001e220u;
  a = ebp;
  b = ebp;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebp = result;
L_10005396:;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = (esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x100053a5u); /* 100053a0 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100053b2;
  BB_U32(ebx + 0x8u) = ebp;
  goto L_100053ce;
L_100053b2:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100053c7;
  eax = (esp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0x4u), 0, 0x100053c0u); /* 100053bd indirect */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10005404;
L_100053c7:;
  BB_U32(ebx + 0x8u) = 0x1u;
L_100053ce:;
  ebx = (ebx + 0xcu);
  a = BB_U32(ebx);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005396;
L_100053d5:;
  a = BB_U32(0x1001e220u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005402;
  ebx = 0x1001e228u;
  ebp = (ebp ^ ebp);
L_100053e5:;
  a = BB_U32(ebx);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100053fa;
  eax = (esp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0xfffffffcu), 0,
              0x100053f3u); /* 100053f0 indirect */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10005404;
L_100053fa:;
  ebx = (ebx + 0xcu);
  a = BB_U32(ebx + 0xfffffff8u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100053e5;
L_10005402:;
  eax = (eax ^ eax);
L_10005404:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xbcu;
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
bb_qmix_session_create_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_session_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "2048(%esp),%esp;ret $0");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void waveout_create_body(uint32_t *input) {
#else
void bb_qmix_waveout_create_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10009bf7u;
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
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x140u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x0u;
  SAMPLE_CALL(0x10001020u, 0, 0x10009b8du); /* 10009b88 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffff0u) = eax;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009ba7;
  ecx = eax;
  SAMPLE_CALL(0x10009c70u, 0, 0x10009ba5u); /* 10009ba0 */
  esi = eax;
L_10009ba7:;
  ebx = BB_U32(ebp + 0xcu);
  ecx = esi;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  edi = BB_U32(esi);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edi + 0x40u), 4, 0x10009bb9u); /* 10009bb6 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009bd8;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(BB_U32(edi + 0x3cu), 8, 0x10009bc7u); /* 10009bc4 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009bd8;
  eax = BB_U32(ebp + 0x8u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edi + 0x2cu), 4, 0x10009bd4u); /* 10009bd1 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10009be4;
L_10009bd8:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10009be2;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(BB_U32(edi), 4, 0x10009be2u); /* 10009be0 indirect */
L_10009be2:;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
L_10009be4:;
  eax = esi;
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
bb_qmix_waveout_create_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_free_failed_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10009c0au); /* 10009c05 */
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
uint32_t __attribute__((naked)) bb_qmix_waveout_free_failed_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_free_failed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_configure_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
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
  edi = BB_U32(esp + 0x14u);
  ebx = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005450u, 8, 0x10009ff4u); /* 10009fef */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000a000;
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
L_1000a000:;
  a = edi;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e1b0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x1000a010u); /* 1000a00b */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebx + 0x2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) & 0x60u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x60u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_1000a02b;
  a = WORD(ebx + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_1000a02b;
  eax = (eax ^ eax);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ebx + 0x12u)) & 0xffffu) << 0);
L_1000a02b:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000a075;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e1a4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x1000a03cu); /* 1000a037 */
  eax = ((uint16_t)(eax));
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x104u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e194u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x1000a055u); /* 1000a050 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(esi + 0x34u)) & 0xffffu) << 0);
  edi = args[sp++];
  esp += 4u;
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
  eax = ((uint16_t)(eax));
  BB_U32(esi + 0x108u) = eax;
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
L_1000a075:;
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x3u);
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (eax + edx);
  a = eax;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  eax = result;
  ebx = eax;
  eax = BB_U32(esi + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x1000a090u); /* 1000a08a indirect */
  ecx = ((uint16_t)(eax));
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esi + 0x108u) = ecx;
  args[--sp] = 0x1001e2b8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x1000a0a6u); /* 1000a0a1 */
  edx = (uint32_t)((int32_t)eax >> 31);
  b = ebx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  BB_U32(esi + 0x104u) = eax;
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
uint32_t __attribute__((naked))
bb_qmix_waveout_configure_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_configure_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $8");
}
#endif
void bb_qmix_waveout_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  a = esp;
  b = 0x50u;
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
  edi = BB_U32(esp + 0x60u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10002950u, 4, 0x1000a0d3u); /* 1000a0ce */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000a0e3;
  eax = (eax ^ eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x50u;
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
L_1000a0e3:;
  eax = BB_U32(esi + 0x104u);
  a = eax;
  b = BB_U32(esi + 0x108u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000a149;
  eax = (esi + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000a290u, 0, 0x1000a0feu); /* 1000a0f9 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x108u) = eax;
  BB_U32(esi + 0x104u) = 0x4u;
  eax = BB_U32(esi + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x3e8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x108u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x1000a12au); /* 1000a124 indirect */
  ecx = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e2c0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021300u), 0, 0x1000a13bu); /* 1000a135 indirect */
  ecx = (esp + 0x1cu);
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212e8u), 4, 0x1000a149u); /* 1000a143 indirect */
L_1000a149:;
  eax = BB_U32(esi + 0x104u);
  BB_U32(esi + 0xf0u) = edi;
  a = eax;
  result = (a << 5) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 5, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000a15eu); /* 1000a159 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x104u);
  ecx = (ecx << 5);
  edi = eax;
  ecx = (ecx >> 2);
  ebx = (ebx ^ ebx);
  BB_U32(esi + 0x10cu) = eax;
  eax = (eax ^ eax);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  a = BB_U32(esi + 0x104u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_1000a277;
  edi = (edi ^ edi);
L_1000a189:;
  eax = BB_U32(esi + 0x108u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000a198u); /* 1000a193 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x10cu);
  BB_U32(ecx + edi) = eax;
  eax = BB_U32(esi + 0x108u);
  eax = (eax << 2);
  ecx = BB_U32(esi + 0x10cu);
  BB_U32(ecx + edi + 0x4u) = eax;
  ecx = BB_U32(esi + 0x10cu);
  BB_U32(ecx + edi + 0xcu) = esi;
  ecx = BB_U32(esi + 0x10cu);
  ecx = (ecx + edi);
  a = BB_U32(esi + 0x120u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esp + 0x10u) = ecx;
  args[sp + 4u] = BB_U32(esp + 16u);
  if (!(flags & 0x40u))
    goto L_1000a232;
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
  SAMPLE_CALL(0x10001020u, 0, 0x1000a1e8u); /* 1000a1e3 */
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
    goto L_1000a21e;
  ecx = eax;
L_1000a209:;
  ebp = (ecx + 0x8u);
  ecx = (ecx + 0x8u);
  edx = (edx + 1u);
  BB_U32(ecx + 0xfffffff8u) = ebp;
  ebp = BB_U32(esi + 0x11cu);
  ebp = (ebp - 1u);
  a = ebp;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1000a209;
L_1000a21e:;
  ecx = BB_U32(esi + 0x11cu);
  BB_U32(eax + ecx * 8u + 0xfffffff8u) = 0x0u;
  BB_U32(esi + 0x120u) = eax;
L_1000a232:;
  ecx = BB_U32(esi + 0x120u);
  eax = BB_U32(ecx);
  BB_U32(esi + 0x120u) = eax;
  edx = BB_U32(esp + 0x10u);
  BB_U32(ecx + 0x4u) = edx;
  BB_U32(ecx) = 0x0u;
  eax = BB_U32(esi + 0x118u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000a25b;
  BB_U32(eax) = ecx;
  goto L_1000a261;
L_1000a25b:;
  BB_U32(esi + 0x114u) = ecx;
L_1000a261:;
  edi = (edi + 0x20u);
  ebx = (ebx + 1u);
  BB_U32(esi + 0x118u) = ecx;
  a = BB_U32(esi + 0x104u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1000a189;
L_1000a277:;
  eax = 0x1u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x50u;
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
bb_qmix_waveout_initialize_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "2048(%esp),%esp;ret $4");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void waveout_measure_latency_body(uint32_t *input) {
#else
void bb_qmix_waveout_measure_latency_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000a487u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  eax = BB_U32(ebp + 0xcu);
  a = esp;
  b = 0x2cu;
  esp -= b;
  sp -= b / 4u;
  eax = (eax + 0xcu);
  ecx = 0x3e8u;
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = (edx - edx);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = esp;
  eax = BB_U32(eax + 0x4u);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  ecx = BB_U32(ebp + 0xffffffd8u);
  edx = (edx ^ edx);
  edx = (edx & 0xffff0000u) | (((uint32_t)(WORD(ecx)) & 0xffffu) << 0);
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0x1f40u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffffe4u) = esi;
  a = eax;
  b = edx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  flags = (flags & ~0x801u) |
          (((int64_t)wide != (int64_t)(int32_t)result) ? 0x801u : 0u);
  eax = result;
  BB_U32(ebp + 0xffffffe0u) = eax;
  SAMPLE_CALL(0x10001020u, 0, 0x1000a2e6u); /* 1000a2e1 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ebx = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffecu) = eax;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000a2f5u); /* 1000a2f0 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffdcu) = eax;
  eax = BB_U32(ebp + 0xcu);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(eax + 0xeu)) & 0xffffu) << 0);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(eax)) - 0x8u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 16);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax & 0x80u);
  edi = BB_U32(ebp + 0xffffffdcu);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx & 0xffff00ffu) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 8);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = ecx;
  eax = (eax << 16);
  eax = (eax & 0xffff0000u) | (((uint32_t)(((uint16_t)(ecx))) & 0xffffu) << 0);
  ecx = edx;
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
  edi = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x7d0u;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = (ebp + 0xffffffc8u);
  SAMPLE_CALL(0x100041e0u, 0, 0x1000a347u); /* 1000a342 */
  BB_U32(ebp + 0xfffffffcu) = esi;
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffc8u) = eax;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ebp + 0xffffffc8u);
  SAMPLE_CALL(0x10004200u, 16, 0x1000a363u); /* 1000a35e */
  a = esi;
  b = 0xfau;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000a390;
  edi = esi;
  edi = (edi << 5);
  a = edi;
  b = BB_U32(ebp + 0xffffffecu);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
L_1000a373:;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(edi) = eax;
  BB_U32(edi + 0x4u) = ebx;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ebp + 0xffffffc8u);
  SAMPLE_CALL(0x10004250u, 4, 0x1000a384u); /* 1000a37f */
  edi = (edi + 0x20u);
  esi = (esi + 1u);
  a = esi;
  b = 0xfau;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000a373;
L_1000a390:;
  ecx = (ebp + 0xffffffc8u);
  SAMPLE_CALL(0x10004340u, 0, 0x1000a398u); /* 1000a393 */
  esi = (esi ^ esi);
  a = esi;
  b = 0xfau;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000a3bf;
  edi = esi;
  edi = (edi << 5);
  a = edi;
  b = BB_U32(ebp + 0xffffffecu);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
L_1000a3aa:;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ebp + 0xffffffc8u);
  SAMPLE_CALL(0x10004290u, 4, 0x1000a3b3u); /* 1000a3ae */
  edi = (edi + 0x20u);
  esi = (esi + 1u);
  a = esi;
  b = 0xfau;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000a3aa;
L_1000a3bf:;
  ecx = (ebp + 0xffffffc8u);
  SAMPLE_CALL(0x10004370u, 0, 0x1000a3c7u); /* 1000a3c2 */
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x10u);
L_1000a3cd:;
  a = BYTE(eax);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000a3cd;
  esi = (esi ^ esi);
  a = esi;
  b = 0xfau;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000a3fc;
  eax = esi;
  eax = (eax << 5);
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = (eax + ecx + 0x10u);
L_1000a3e8:;
  a = BYTE(eax);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000a3f0;
  BB_U32(ebp + 0xffffffe4u) = (BB_U32(ebp + 0xffffffe4u) + 1u);
L_1000a3f0:;
  eax = (eax + 0x20u);
  esi = (esi + 1u);
  a = esi;
  b = 0xfau;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000a3e8;
L_1000a3fc:;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  ecx = (ebp + 0xffffffc8u);
  esi = 0xfau;
  SAMPLE_CALL(0x10004310u, 0, 0x1000a410u); /* 1000a40b */
  edi = BB_U32(ebp + 0xffffffecu);
L_1000a413:;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ebp + 0xffffffc8u);
  a = edi;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  SAMPLE_CALL(0x100042d0u, 4, 0x1000a41fu); /* 1000a41a */
  a = esi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  esi = result;
  if (!(flags & 0x40u))
    goto L_1000a413;
  eax = BB_U32(ebp + 0xffffffecu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000a42bu); /* 1000a426 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xffffffdcu);
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000a439u); /* 1000a434 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = BB_U32(ebp + 0xffffffe4u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  flags = (flags & ~0x801u) |
          (((int64_t)wide != (int64_t)(int32_t)result) ? 0x801u : 0u);
  eax = result;
  ecx = BB_U32(ebp + 0xffffffd8u);
  edx = (uint32_t)((int32_t)eax >> 31);
  esi = (esi & 0xffff0000u) | (((uint32_t)(WORD(ecx)) & 0xffffu) << 0);
  b = esi;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  BB_U32(ebp + 0xffffffd4u) = eax;
  SAMPLE_CALL(0x1000a491u, 0, 0x1000a45bu); /* 1000a456 */
  eax = BB_U32(ebp + 0xffffffd4u);
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
#ifdef BB_MODEL_RUNTIME
/* The original protected range is stage 1 and catches WaveError only. The
 * saved logical owner survives native recovery; nested calls have already
 * released their own resources before this catch resumes the probe cleanup. */
void bb_qmix_waveout_measure_latency_c(uint32_t *input) {
  uint32_t owner = input[3] + 2036u;
  struct bb_qmix_model_error error;
  if (bb_qmix_model_try_body(waveout_measure_latency_body, input, owner, &error))
    return;
  if (error.information != 0x1001ae90u || BB_I32(owner + 8u) != 1)
    bb_qmix_model_raise_error(error);
  uint32_t saved[4] = {input[0], input[1], input[2], input[4]};
  input[2] = owner + 12u;
  bb_qmix_waveout_latency_catch_c(input);
  bb_qmix_waveout_latency_resume_c(input);
  input[0] = saved[0];
  input[1] = saved[1];
  input[2] = saved[2];
  input[4] = saved[3];
}
#else
uint32_t __attribute__((naked))
bb_qmix_waveout_measure_latency_entry(uint32_t arg0 __attribute__((unused)),
                                      uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_measure_latency_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_waveout_measure_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  ecx = (ebp + 0xffffffc8u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100041f0u, 0, BB_U32(esp - 4u)); /* tail call 1000a494 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_waveout_measure_cleanup_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_measure_cleanup_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "2048(%esp),%esp;ret $0");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void waveout_activate_body(uint32_t *input) {
#else
void bb_qmix_waveout_activate_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000a608u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xffffffecu) = ecx;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xfffffff0u) = esp;
  a = BB_U32(ecx + 0x8u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a560;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  SAMPLE_CALL(0x10005670u, 4, 0x1000a4d8u); /* 1000a4d3 */
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000a58b;
  BB_U32(ebp + 0xfffffffcu) = esi;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0xf0u);
  BB_U32(ebp + 0xffffffe8u) = eax;
  args[--sp] = 0x10000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(eax + 0x100u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x28u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffe8u);
  SAMPLE_CALL(0x10004200u, 16, 0x1000a50du); /* 1000a508 */
  edi = BB_U32(ebp + 0xffffffecu);
  edi = (edi + 0x104u);
  a = BB_U32(edi);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_1000a542;
  ebx = esi;
  ebx = (ebx << 5);
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x10cu);
  BB_U32(ebp + 0xffffffd8u) = eax;
L_1000a52a:;
  eax = BB_U32(ebp + 0xffffffd8u);
  eax = BB_U32(eax);
  a = eax;
  b = ebx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffe8u);
  SAMPLE_CALL(0x10004250u, 4, 0x1000a53au); /* 1000a535 */
  ebx = (ebx + 0x20u);
  esi = (esi + 1u);
  a = BB_U32(edi);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1000a52a;
L_1000a542:;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x110u) = 0x0u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  eax = BB_U32(ebp + 0xffffffecu);
  ecx = eax;
  ebx = BB_U32(eax);
  SAMPLE_CALL(BB_U32(ebx + 0x8u), 0, 0x1000a560u); /* 1000a55d indirect */
L_1000a560:;
  eax = BB_U32(ebp + 0xfffffff4u);
  edi = args[sp++];
  esp += 4u;
  exception_link(eax);
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
L_1000a58b:;
  BB_U32(ebp + 0xfffffffcu) = 0x2u;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0xf0u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  BB_U32(ebp + 0xffffffe8u) = eax;
  ecx = eax;
  SAMPLE_CALL(0x10004310u, 0, 0x1000a5a4u); /* 1000a59f */
  ebx = (ebx ^ ebx);
  edi = BB_U32(ebp + 0xffffffecu);
  edi = (edi + 0x104u);
  a = BB_U32(edi);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_1000a5db;
  esi = ebx;
  esi = (esi << 5);
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x10cu);
  BB_U32(ebp + 0xffffffe0u) = eax;
L_1000a5c3:;
  eax = BB_U32(ebp + 0xffffffe0u);
  eax = BB_U32(eax);
  a = eax;
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffe8u);
  SAMPLE_CALL(0x100042d0u, 4, 0x1000a5d3u); /* 1000a5ce */
  esi = (esi + 0x20u);
  ebx = (ebx + 1u);
  a = BB_U32(edi);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1000a5c3;
L_1000a5db:;
  ecx = BB_U32(ebp + 0xffffffe8u);
  SAMPLE_CALL(0x100043a0u, 0, 0x1000a5e3u); /* 1000a5de */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  goto L_1000a560;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_waveout_activate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_activate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $4");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void waveout_pump_body(uint32_t *input) {
#else
void bb_qmix_waveout_pump_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
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
  args[--sp] = 0x1000aad0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0xa4u;
  esp -= b;
  sp -= b / 4u;
  a = BB_U32(ecx + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffffecu) = ecx;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = esp;
  if (flags & 0x40u)
    goto L_1000aaa7;
  eax = (ecx + 0xccu);
  BB_U32(ebp + 0xffffffe8u) = eax;
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a7d5;
  eax = (ebp + 0xffffff60u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000a7ccu); /* 1000a7c6 indirect */
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0xd0u) = (BB_U32(eax + 0xd0u) + 1u);
L_1000a7d5:;
  eax = (eax ^ eax);
  BB_U32(ebp + 0xfffffffcu) = eax;
  BB_U32(ebp + 0xffffffc8u) = eax;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x114u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  esi = BB_U32(ebp + 0xffffff88u);
  edi = BB_U32(ebp + 0xffffff84u);
  ebx = BB_U32(ebp + 0xffffff80u);
L_1000a7f1:;
  eax = BB_U32(ebp + 0xffffffd8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a803;
  eax = BB_U32(eax);
  eax = BB_U32(eax + 0x4u);
  BB_U32(ebp + 0xffffffd4u) = eax;
  goto L_1000a80a;
L_1000a803:;
  BB_U32(ebp + 0xffffffd4u) = 0x0u;
L_1000a80a:;
  a = BB_U32(ebp + 0xffffffd4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000aa23;
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a829;
  eax = (ebp + 0xffffff50u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000a829u); /* 1000a823 indirect */
L_1000a829:;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(eax + 0x108u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xffffffd4u);
  eax = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10002b90u, 8, 0x1000a841u); /* 1000a83c */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000aa23;
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a873;
  eax = (ebp + 0xffffff68u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000a85eu); /* 1000a858 indirect */
  eax = BB_U32(ebp + 0xffffffecu);
  ecx = BB_U32(ebp + 0xffffff68u);
  ecx = (ecx - BB_U32(ebp + 0xffffff50u));
  BB_U32(eax + 0xe0u) = (BB_U32(eax + 0xe0u) + ecx);
L_1000a873:;
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a893;
  eax = BB_U32(ebp + 0xffffff68u);
  ecx = BB_U32(ebp + 0xffffff6cu);
  BB_U32(ebp + 0xffffff78u) = eax;
  BB_U32(ebp + 0xffffff7cu) = ecx;
L_1000a893:;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x110u);
  BB_U32(ebp + 0xffffffa4u) = eax;
  eax = BB_U32(eax);
  BB_U32(ebp + 0xffffff90u) = eax;
  ecx = (eax + 0x1u);
  eax = BB_U32(ebp + 0xffffffa4u);
  BB_U32(eax) = ecx;
  a = BB_U32(ebp + 0xffffff90u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a8c6;
  ecx = BB_U32(ebp + 0xffffffecu);
  a = ecx;
  b = 0xf0u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  SAMPLE_CALL(0x10004340u, 0, 0x1000a8bfu); /* 1000a8ba */
  BB_U32(ebp + 0xffffffc8u) = 0x1u;
L_1000a8c6:;
  eax = BB_U32(ebp + 0xffffffd4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  a = ecx;
  b = 0xf0u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  SAMPLE_CALL(0x10004290u, 4, 0x1000a8d8u); /* 1000a8d3 */
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000a902;
  eax = (ebp + 0xffffff70u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000a8edu); /* 1000a8e7 indirect */
  eax = BB_U32(ebp + 0xffffffecu);
  ecx = BB_U32(ebp + 0xffffff70u);
  ecx = (ecx - BB_U32(ebp + 0xffffff78u));
  BB_U32(eax + 0xe4u) = (BB_U32(eax + 0xe4u) + ecx);
L_1000a902:;
  eax = BB_U32(ebp + 0xffffffd8u);
  eax = BB_U32(eax);
  BB_U32(ebp + 0xffffffa8u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000a917;
  BB_U32(ebp + 0xffffffbcu) = 0x0u;
  goto L_1000a95c;
L_1000a917:;
  ecx = BB_U32(ebp + 0xffffffa8u);
  BB_U32(ebp + 0xffffffe0u) = ecx;
  eax = BB_U32(ecx + 0x4u);
  BB_U32(ebp + 0xffffff98u) = eax;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x118u);
  a = BB_U32(eax);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a935;
  BB_U32(eax) = 0x0u;
L_1000a935:;
  ecx = BB_U32(ebp + 0xffffffe0u);
  ecx = BB_U32(ecx);
  eax = BB_U32(ebp + 0xffffffd8u);
  BB_U32(eax) = ecx;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x120u);
  BB_U32(ebp + 0xffffffb0u) = eax;
  eax = BB_U32(eax);
  ecx = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ecx) = eax;
  eax = BB_U32(ebp + 0xffffffb0u);
  BB_U32(eax) = ecx;
  eax = BB_U32(ebp + 0xffffff98u);
  BB_U32(ebp + 0xffffffbcu) = eax;
L_1000a95c:;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x134u);
  BB_U32(ebp + 0xffffffdcu) = eax;
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000a9d4;
  esi = BB_U32(ebp + 0xffffffecu);
  esi = (esi + 0x130u);
  eax = BB_U32(esi);
  eax = (eax << 3);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000a983u); /* 1000a97e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffccu) = eax;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x138u);
  BB_U32(ebp + 0xffffffb4u) = eax;
  eax = BB_U32(eax);
  ecx = BB_U32(ebp + 0xffffffccu);
  BB_U32(ecx) = eax;
  eax = BB_U32(ebp + 0xffffffb4u);
  BB_U32(eax) = ecx;
  ecx = (ecx + 0x4u);
  BB_U32(ebp + 0xffffffd0u) = ecx;
  edi = (edi ^ edi);
  eax = BB_U32(esi);
  eax = (eax - 1u);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_1000a9c2;
  ebx = (ecx + edi * 8u);
L_1000a9b2:;
  eax = (ebx + 0x8u);
  BB_U32(ebx) = eax;
  ebx = (ebx + 0x8u);
  edi = (edi + 1u);
  eax = BB_U32(esi);
  eax = (eax - 1u);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1000a9b2;
L_1000a9c2:;
  eax = BB_U32(esi);
  ecx = BB_U32(ebp + 0xffffffd0u);
  BB_U32(ecx + eax * 8u + 0xfffffff8u) = 0x0u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(eax) = ecx;
L_1000a9d4:;
  eax = BB_U32(ebp + 0xffffffdcu);
  eax = BB_U32(eax);
  BB_U32(ebp + 0xffffff9cu) = eax;
  ecx = BB_U32(eax);
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(eax) = ecx;
  ecx = BB_U32(ebp + 0xffffff9cu);
  BB_U32(ebp + 0xffffffe4u) = ecx;
  eax = BB_U32(ebp + 0xffffffbcu);
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx) = 0x0u;
  eax = BB_U32(ebp + 0xffffffecu);
  eax = (eax + 0x12cu);
  BB_U32(ebp + 0xffffffc4u) = eax;
  eax = BB_U32(eax);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000aa0a;
  BB_U32(eax) = ecx;
  goto L_1000aa16;
L_1000aa0a:;
  eax = BB_U32(ebp + 0xffffffecu);
  ecx = BB_U32(ebp + 0xffffffe4u);
  BB_U32(eax + 0x128u) = ecx;
L_1000aa16:;
  eax = BB_U32(ebp + 0xffffffc4u);
  ecx = BB_U32(ebp + 0xffffffe4u);
  BB_U32(eax) = ecx;
  goto L_1000a7f1;
L_1000aa23:;
  a = BB_U32(ebp + 0xffffffc8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000aa76;
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000aa3e;
  eax = (ebp + 0xffffff78u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000aa3eu); /* 1000aa38 indirect */
L_1000aa3e:;
  ecx = BB_U32(ebp + 0xffffffecu);
  a = ecx;
  b = 0xf0u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  SAMPLE_CALL(0x10004370u, 0, 0x1000aa4cu); /* 1000aa47 */
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000aa76;
  eax = (ebp + 0xffffff70u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000aa61u); /* 1000aa5b indirect */
  eax = BB_U32(ebp + 0xffffffecu);
  ecx = BB_U32(ebp + 0xffffff70u);
  ecx = (ecx - BB_U32(ebp + 0xffffff78u));
  BB_U32(eax + 0xe4u) = (BB_U32(eax + 0xe4u) + ecx);
L_1000aa76:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  eax = BB_U32(ebp + 0xffffffe8u);
  a = BB_U32(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000aaa7;
  eax = (ebp + 0xffffff58u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c4u), 4, 0x1000aa92u); /* 1000aa8c indirect */
  eax = BB_U32(ebp + 0xffffffecu);
  ecx = BB_U32(ebp + 0xffffff58u);
  ecx = (ecx - BB_U32(ebp + 0xffffff60u));
  a = BB_U32(eax + 0xe8u);
  b = ecx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(eax + 0xe8u) = result;
L_1000aaa7:;
  eax = BB_U32(ebp + 0xfffffff4u);
  edi = args[sp++];
  esp += 4u;
  exception_link(eax);
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
uint32_t __attribute__((naked)) bb_qmix_waveout_pump_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_waveout_pump_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
#endif

#ifdef BB_MODEL_RUNTIME
static void factory_create(uint32_t *input, void (*body)(uint32_t *)) {
  uint32_t owner = input[3] + 2036u;
  BB_U32(owner - 4u) = 0;
  struct bb_qmix_model_error error;
  if (bb_qmix_model_try_body(body, input, owner, &error))
    return;
  /* Stage zero still belongs to the unfinished constructor's cleanup map.
   * After construction this factory owns the session until it returns it. */
  uint32_t self = BB_U32(owner - 4u);
  if (BB_I32(owner + 8u) == -1 && self) {
    uint32_t arguments[] = {1};
    struct bb_call_state call = {0, self, 0, 0x202};
    bb_model_invoke(&call, BB_U32(BB_U32(self)), arguments, 1);
  }
  bb_qmix_model_raise_error(error);
}
void bb_qmix_directsound_create_c(uint32_t *input) {
  factory_create(input, directsound_create_body);
}
void bb_qmix_waveout_create_c(uint32_t *input) {
  factory_create(input, waveout_create_body);
}
void bb_qmix_waveout_activate_c(uint32_t *input) {
  uint32_t owner = input[3] + 2036u;
  struct bb_qmix_model_error error;
  if (bb_qmix_model_try_body(waveout_activate_body, input, owner, &error))
    return;
  int32_t stage = BB_I32(owner + 8u);
  if (error.information != 0x1001ae90u || (stage != 0 && stage != 2))
    bb_qmix_model_raise_error(error);
  input[2] = owner + 12u;
  bb_qmix_model_catch_body(stage == 0 ? bb_qmix_waveout_open_catch_c
                                    : bb_qmix_waveout_close_catch_c,
                           input, error);
  abort(); /* Both device catches rethrow. */
}
void bb_qmix_waveout_pump_c(uint32_t *input) {
  uint32_t owner = input[3] + 2036u;
  struct bb_qmix_model_error error;
  if (bb_qmix_model_try_body(waveout_pump_body, input, owner, &error))
    return;
  if (error.information != 0x1001ae90u || BB_I32(owner + 8u) != 0)
    bb_qmix_model_raise_error(error);
  input[2] = owner + 12u;
  bb_qmix_model_catch_body(bb_qmix_waveout_pump_catch_c, input, error);
  abort(); /* The pump catch rethrows. */
}
#endif
