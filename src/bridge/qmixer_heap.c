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
/* C forms of QMIXER CRT malloc/free and small-block page allocation.
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

void bb_qmix_crt_malloc_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32(0x1001f4b0u);
  ecx = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011730u, 0, 0x10011720u); /* 1001171b */
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
bb_qmix_crt_malloc_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_malloc_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_crt_malloc_retry_c(uint32_t *input) {
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
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = 0xffffffe0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10011740;
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
L_10011740:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10011749;
  esi = 0x1u;
L_10011749:;
  edi = BB_U32(esp + 0x10u);
L_1001174d:;
  eax = 0x0u;
  a = esi;
  b = 0xffffffe0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10011760;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011780u, 0, 0x1001175du); /* 10011758 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10011760:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10011777;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10011777;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012160u, 0, 0x1001176eu); /* 10011769 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001174d;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_10011777:;
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
bb_qmix_crt_malloc_retry_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_malloc_retry_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_crt_malloc_block_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x8u);
  esi = (esi + 0xfu);
  esi = (esi & 0xfffffff0u);
  a = esi;
  b = BB_U32(0x1001fcd4u);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100117a5;
  eax = esi;
  a = eax;
  result = (a >> 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 1);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012570u, 0, 0x1001179eu); /* 10011799 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100117b4;
L_100117a5:;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x10020e6cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212ecu), 12, 0x100117b4u); /* 100117ae indirect */
L_100117b4:;
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
bb_qmix_crt_malloc_block_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_malloc_block_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_crt_free_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x10u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1001180a;
  eax = (esp + 0x4u);
  ecx = (esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100124c0u, 0, 0x100117dcu); /* 100117d7 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100117fb;
  ecx = BB_U32(esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012520u, 0, 0x100117f3u); /* 100117ee */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
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
L_100117fb:;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x10020e6cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212e4u), 12, 0x1001180au); /* 10011804 indirect */
L_1001180a:;
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
bb_qmix_crt_free_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_free_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_grow_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = BB_U32(0x1001fcc8u);
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
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if (!(flags & 0x40u))
    goto L_10012289;
  esi = 0x1001f4b8u;
L_10012222:;
  args[--sp] = 0x4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(0x10021214u);
  args[--sp] = 0x2000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x400000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x10012238u); /* 10012236 indirect */
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012362;
  args[--sp] = 0x4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x10012251u); /* 1001224f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012354;
  a = esi;
  b = 0x1001f4b8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100122ad;
  a = BB_U32(0x1001f4b8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10012274;
  BB_U32(0x1001f4b8u) = 0x1001f4b8u;
L_10012274:;
  a = BB_U32(0x1001f4bcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100122c6;
  BB_U32(0x1001f4bcu) = 0x1001f4b8u;
  goto L_100122c6;
L_10012289:;
  args[--sp] = 0x814u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x10020e6cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212ecu), 12, 0x1001229cu); /* 10012296 indirect */
  esi = eax;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10012222;
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
L_100122ad:;
  BB_U32(esi) = 0x1001f4b8u;
  eax = BB_U32(0x1001f4bcu);
  BB_U32(esi + 0x4u) = eax;
  BB_U32(0x1001f4bcu) = esi;
  eax = BB_U32(esi + 0x4u);
  BB_U32(eax) = esi;
L_100122c6:;
  ebp = (ebp ^ ebp);
  eax = 0xf0u;
  ecx = 0xffu;
  edx = 0xf1u;
  BB_U32(esi + 0x810u) = ebx;
  BB_U32(esi + 0x8u) = ebp;
  BB_U32(esi + 0xcu) = 0x10u;
L_100122e7:;
  edi = (esi + ebp);
  a = ebp;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_100122f4;
  BYTE(edi + 0x10u) = ((uint8_t)(eax));
  goto L_100122f7;
L_100122f4:;
  BYTE(edi + 0x10u) = ((uint8_t)(ecx));
L_100122f7:;
  ebp = (ebp + 1u);
  BYTE(edi + 0x410u) = ((uint8_t)(edx));
  a = ebp;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100122e7;
  edi = ebx;
  eax = (eax ^ eax);
  ecx = 0x4000u;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  eax = BB_U32(esi + 0x810u);
  eax = (eax + 0x10000u);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_1001234d;
  edx = 0xf0u;
  ecx = 0xffu;
L_1001232a:;
  eax = (ebx + 0x8u);
  BB_U32(ebx) = eax;
  BB_U32(ebx + 0x4u) = edx;
  BYTE(ebx + 0xf8u) = ((uint8_t)(ecx));
  ebx = (ebx + 0x1000u);
  eax = BB_U32(esi + 0x810u);
  eax = (eax + 0x10000u);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1001232a;
L_1001234d:;
  eax = esi;
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
L_10012354:;
  args[--sp] = 0x8000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021224u), 12, 0x10012362u); /* 1001235c indirect */
L_10012362:;
  a = esi;
  b = 0x1001f4b8u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10012379;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x10020e6cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212e4u), 12, 0x10012379u); /* 10012373 indirect */
L_10012379:;
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_small_heap_grow_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_grow_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_release_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x8000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0xcu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x810u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021224u), 12, 0x10012399u); /* 10012393 indirect */
  a = BB_U32(0x1001fcccu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100123a9;
  eax = BB_U32(esi + 0x4u);
  BB_U32(0x1001fcccu) = eax;
L_100123a9:;
  a = esi;
  b = 0x1001f4b8u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100123d2;
  eax = BB_U32(esi);
  ecx = BB_U32(esi + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = eax;
  eax = BB_U32(esi);
  edx = BB_U32(esi + 0x4u);
  BB_U32(eax + 0x4u) = edx;
  ecx = BB_U32(0x10020e6cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212e4u), 12, 0x100123d0u); /* 100123ca indirect */
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
L_100123d2:;
  BB_U32(0x1001fcc8u) = 0x0u;
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
bb_qmix_small_heap_release_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_release_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_trim_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  a = esp;
  b = 0x4u;
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
  esi = BB_U32(0x1001f4bcu);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
L_100123ed:;
  a = BB_U32(esi + 0x810u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1001249c;
  ebp = 0x3ffu;
  ebx = (esi + 0x40fu);
  BB_U32(esp + 0x10u) = 0x0u;
  args[sp + 4u] = BB_U32(esp + 16u);
  edi = 0x3ff000u;
L_10012412:;
  a = BYTE(ebx);
  b = 0xf0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10012456;
  args[--sp] = 0x4000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x810u);
  args[--sp] = 0x1000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = eax;
  b = edi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021224u), 12, 0x10012430u); /* 1001242a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10012456;
  BYTE(ebx) = 0xffu;
  BB_U32(0x1001fcd0u) = (BB_U32(0x1001fcd0u) - 1u);
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10012449;
  a = ebp;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1001244c;
L_10012449:;
  BB_U32(esi + 0xcu) = ebp;
L_1001244c:;
  BB_U32(esp + 0x10u) = (BB_U32(esp + 0x10u) + 1u);
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esp + 0x18u);
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  BB_U32(esp + 0x18u) = result;
  args[sp + 6u] = BB_U32(esp + 24u);
  if (flags & 0x40u)
    goto L_10012462;
L_10012456:;
  edi = (edi - 0x1000u);
  ebp = (ebp - 1u);
  ebx = (ebx - 1u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10012412;
L_10012462:;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  eax = esi;
  esi = BB_U32(esi + 0x4u);
  if (flags & 0x40u)
    goto L_1001249c;
  a = BYTE(eax + 0x10u);
  b = 0xffu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_1001249c;
  edx = 0x1u;
  ecx = (eax + 0x11u);
L_1001247c:;
  a = BYTE(ecx);
  b = 0xffu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_1001248b;
  edx = (edx + 1u);
  ecx = (ecx + 1u);
  a = edx;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1001247c;
L_1001248b:;
  a = edx;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001249c;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012380u, 0, 0x10012499u); /* 10012494 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_1001249c:;
  a = esi;
  b = BB_U32(0x1001f4bcu);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100124af;
  a = BB_U32(esp + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_100123ed;
L_100124af:;
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
uint32_t __attribute__((naked))
bb_qmix_small_heap_trim_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_trim_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_find_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  ecx = 0x1001f4b8u;
  edx = BB_U32(esp + 0x4u);
L_100124c9:;
  eax = BB_U32(ecx + 0x810u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100124e0;
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_100124e0;
  eax = (eax + 0x400000u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100124ed;
L_100124e0:;
  ecx = BB_U32(ecx);
  a = ecx;
  b = 0x1001f4b8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100124c9;
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
L_100124ed:;
  eax = BB_U32(esp + 0x8u);
  BB_U32(eax) = ecx;
  ecx = edx;
  ecx = (ecx & 0xfffff000u);
  eax = BB_U32(esp + 0xcu);
  edx = (edx - ecx);
  BB_U32(eax) = ecx;
  edx = (edx - 0x100u);
  a = edx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  edx = result;
  eax = (edx + ecx + 0x8u);
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
bb_qmix_small_heap_find_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_find_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_free_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  ecx = BB_U32(esp + 0x8u);
  eax = BB_U32(esp + 0x4u);
  ecx = (ecx - BB_U32(eax + 0x810u));
  ecx = ((uint32_t)((int32_t)(ecx) >> 12));
  ecx = (ecx + eax);
  eax = BB_U32(esp + 0xcu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(eax)) & 0xffu) << 0);
  BYTE(ecx + 0x10u) = (BYTE(ecx + 0x10u) + ((uint8_t)(edx)));
  BYTE(eax) = 0x0u;
  BYTE(ecx + 0x410u) = 0xf1u;
  a = BYTE(ecx + 0x10u);
  b = 0xf0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10012565;
  BB_U32(0x1001fcd0u) = (BB_U32(0x1001fcd0u) + 1u);
  a = BB_U32(0x1001fcd0u);
  b = 0x20u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10012565;
  args[--sp] = 0x10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100123e0u, 0, 0x10012562u); /* 1001255d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10012565:;
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
bb_qmix_small_heap_free_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_free_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_allocate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(0x1001fcccu);
L_1001257e:;
  a = BB_U32(edi + 0x810u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1001263f;
  esi = BB_U32(edi + 0x8u);
  a = esi;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_100125ea;
  ebp = esi;
  ebp = (ebp << 12);
L_1001259b:;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(edi + esi + 0x10u)) & 0xffu) << 0);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_100125db;
  a = ((uint8_t)(ecx));
  b = 0xffu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_100125db;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(edi + esi + 0x410u)) & 0xffu) << 0);
  a = ecx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_100125db;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(edi + 0x810u);
  a = eax;
  b = ebp;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100127f0u, 0, 0x100125c9u); /* 100125c4 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100126b0;
  BYTE(edi + esi + 0x410u) = ((uint8_t)(ebx));
L_100125db:;
  ebp = (ebp + 0x1000u);
  esi = (esi + 1u);
  a = ebp;
  b = 0x400000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1001259b;
L_100125ea:;
  ebp = (ebp ^ ebp);
  esi = (esi ^ esi);
  a = BB_U32(edi + 0x8u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001263f;
L_100125f3:;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(edi + esi + 0x10u)) & 0xffu) << 0);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10012633;
  a = ((uint8_t)(ecx));
  b = 0xffu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10012633;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(edi + esi + 0x410u)) & 0xffu) << 0);
  a = ecx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10012633;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(edi + 0x810u);
  a = eax;
  b = ebp;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100127f0u, 0, 0x10012621u); /* 1001261c */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100126c2;
  BYTE(edi + esi + 0x410u) = ((uint8_t)(ebx));
L_10012633:;
  ebp = (ebp + 0x1000u);
  esi = (esi + 1u);
  a = BB_U32(edi + 0x8u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_100125f3;
L_1001263f:;
  edi = BB_U32(edi);
  a = BB_U32(0x1001fcccu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001257e;
  esi = 0x1001f4b8u;
  eax = (eax ^ eax);
  ecx = 0xffffffffu;
L_10012659:;
  a = BB_U32(esi + 0x810u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10012666;
  a = BB_U32(esi + 0xcu);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100126d4;
L_10012666:;
  esi = BB_U32(esi);
  a = esi;
  b = 0x1001f4b8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10012659;
  SAMPLE_CALL(0x10012210u, 0, 0x10012675u); /* 10012670 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100127e4;
  edx = BB_U32(eax + 0x810u);
  ecx = (ebx + edx);
  BYTE(edx + 0x8u) = ((uint8_t)(ebx));
  ecx = (ecx + 0x8u);
  BB_U32(0x1001fcccu) = eax;
  BB_U32(edx) = ecx;
  ecx = 0xf0u;
  ecx = (ecx - ebx);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(edx + 0x4u) = ecx;
  esi = args[sp++];
  esp += 4u;
  BYTE(eax + 0x10u) = (BYTE(eax + 0x10u) - ((uint8_t)(ebx)));
  eax = BB_U32(eax + 0x810u);
  ebx = args[sp++];
  esp += 4u;
  a = eax;
  b = 0x100u;
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
L_100126b0:;
  ebp = args[sp++];
  esp += 4u;
  BB_U32(0x1001fcccu) = edi;
  a = BYTE(edi + esi + 0x10u);
  b = ((uint8_t)(ebx));
  result = (a - b) & 0xffu;
  flags = sub_flags(flags, a, b, 8);
  BYTE(edi + esi + 0x10u) = result;
  BB_U32(edi + 0x8u) = esi;
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
L_100126c2:;
  ebp = args[sp++];
  esp += 4u;
  BB_U32(0x1001fcccu) = edi;
  a = BYTE(edi + esi + 0x10u);
  b = ((uint8_t)(ebx));
  result = (a - b) & 0xffu;
  flags = sub_flags(flags, a, b, 8);
  BYTE(edi + esi + 0x10u) = result;
  BB_U32(edi + 0x8u) = esi;
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
L_100126d4:;
  edx = BB_U32(esi + 0xcu);
  edi = edx;
  ecx = (edx + 0x10u);
  a = ecx;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100126e9;
  ecx = 0x400u;
L_100126e9:;
  edi = (edi + 1u);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100126fe;
  eax = 0xffu;
L_100126f3:;
  a = BYTE(esi + edi + 0x10u);
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_100126fe;
  edi = (edi + 1u);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_100126f3;
L_100126fe:;
  ebp = edx;
  args[--sp] = 0x4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = (ebp << 12);
  args[--sp] = 0x1000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = edi;
  eax = (eax - edx);
  eax = (eax << 12);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x810u);
  a = eax;
  b = ebp;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021214u), 16, 0x10012721u); /* 1001271b indirect */
  ebp = (ebp + BB_U32(esi + 0x810u));
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100127dd;
  ecx = BB_U32(esi + 0xcu);
  ebp = ecx;
  ebp = (ebp << 12);
  ebp = (ebp + BB_U32(esi + 0x810u));
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10012772;
  eax = 0xf0u;
L_10012746:;
  edx = (ebp + 0x8u);
  ecx = (ecx + 1u);
  BB_U32(ebp) = edx;
  ebp = (ebp + 0x1000u);
  edx = (esi + ecx + 0xffffffffu);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(ebp + 0xfffff004u) = eax;
  BYTE(ebp + 0xfffff0f8u) = 0xffu;
  BYTE(edx + 0x10u) = ((uint8_t)(eax));
  BYTE(edx + 0x410u) = 0xf1u;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10012746;
L_10012772:;
  BB_U32(0x1001fcccu) = esi;
  a = edi;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10012794;
  eax = 0xffu;
L_10012785:;
  a = BYTE(esi + edi + 0x10u);
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10012794;
  edi = (edi + 1u);
  a = edi;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10012785;
L_10012794:;
  ecx = BB_U32(esi + 0xcu);
  a = edi;
  b = 0x400u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(esi + 0xcu) = 0xffffffffu;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_100127a9;
  BB_U32(esi + 0xcu) = edi;
L_100127a9:;
  edx = ecx;
  eax = BB_U32(esi + 0x810u);
  edx = (edx << 12);
  eax = (eax + edx);
  BYTE(eax + 0x8u) = ((uint8_t)(ebx));
  BB_U32(esi + 0x8u) = ecx;
  BYTE(esi + ecx + 0x10u) = (BYTE(esi + ecx + 0x10u) - ((uint8_t)(ebx)));
  ecx = (ebx + eax);
  ecx = (ecx + 0x8u);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(eax) = ecx;
  a = BB_U32(eax + 0x4u);
  b = ebx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(eax + 0x4u) = result;
  eax = BB_U32(esi + 0x810u);
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  eax = (eax + edx + 0x100u);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_100127dd:;
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
L_100127e4:;
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
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_small_heap_allocate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_allocate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_page_allocate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  eax = BB_U32(esp + 0x4u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(esp + 0x10u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax + 0x4u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(eax);
  ebp = edi;
  a = edx;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1001283a;
  ecx = (eax + 0xf8u);
  esi = (edx + edi);
  BYTE(edi) = ((uint8_t)(edx));
  a = esi;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_1001281d;
  BB_U32(eax) = (BB_U32(eax) + edx);
  BB_U32(eax + 0x4u) = (BB_U32(eax + 0x4u) - edx);
  goto L_10012829;
L_1001281d:;
  BB_U32(eax + 0x4u) = 0x0u;
  ecx = (eax + 0x8u);
  BB_U32(eax) = ecx;
L_10012829:;
  edi = (edi - eax);
  ebp = args[sp++];
  esp += 4u;
  a = edi;
  result = (a << 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 0);
  edi = result;
  eax = (edi + eax + 0x80u);
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
L_1001283a:;
  ecx = (ecx + edi);
  a = BYTE(ecx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10012843;
  ebp = ecx;
L_10012843:;
  esi = (edx + ebp);
  ecx = (eax + 0xf8u);
  a = esi;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  esi = BB_U32(esp + 0x18u);
  if (!(flags & 1u))
    goto L_100128de;
L_10012858:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1001288c;
  ecx = (ebp + 0x1u);
  ebx = 0x1u;
  a = BYTE(ecx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10012873;
L_1001286c:;
  ecx = (ecx + 1u);
  ebx = (ebx + 1u);
  a = BYTE(ecx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_1001286c;
L_10012873:;
  a = ebx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_100128a8;
  a = edi;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10012882;
  ebp = ecx;
  BB_U32(eax + 0x4u) = ebx;
  goto L_10012892;
L_10012882:;
  esi = (esi - ebx);
  a = edx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100128a1;
  ebp = ecx;
  goto L_10012892;
L_1001288c:;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  ebp = (ebp + ebx);
L_10012892:;
  ebx = (edx + ebp);
  ecx = (eax + 0xf8u);
  a = ebx;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10012858;
  goto L_100128de;
L_100128a1:;
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
L_100128a8:;
  esi = (edx + ebp);
  ecx = (eax + 0xf8u);
  a = esi;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_100128be;
  ebx = (ebx - edx);
  BB_U32(eax) = esi;
  BB_U32(eax + 0x4u) = ebx;
  goto L_100128ca;
L_100128be:;
  BB_U32(eax + 0x4u) = 0x0u;
  ecx = (eax + 0x8u);
  BB_U32(eax) = ecx;
L_100128ca:;
  BYTE(ebp) = ((uint8_t)(edx));
  ebp = (ebp - eax);
  a = ebp;
  result = (a << 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 0);
  ebp = result;
  eax = (ebp + eax + 0x80u);
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
L_100128de:;
  ebp = (eax + 0x8u);
  ebx = (eax + 0x8u);
  a = edi;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10012928;
L_100128e8:;
  ebx = (edx + ebp);
  ecx = (eax + 0xf7u);
  a = ebx;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10012928;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1001291e;
  ecx = (ebp + 0x1u);
  ebx = 0x1u;
  a = BYTE(ecx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10012910;
L_10012909:;
  ecx = (ecx + 1u);
  ebx = (ebx + 1u);
  a = BYTE(ecx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10012909;
L_10012910:;
  a = ebx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10012936;
  esi = (esi - ebx);
  a = edx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1001292f;
  ebp = ecx;
  goto L_10012924;
L_1001291e:;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  ebp = (ebp + ebx);
L_10012924:;
  a = edi;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100128e8;
L_10012928:;
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
L_1001292f:;
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
L_10012936:;
  esi = (edx + ebp);
  ecx = (eax + 0xf8u);
  a = esi;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_1001294c;
  ebx = (ebx - edx);
  BB_U32(eax) = esi;
  BB_U32(eax + 0x4u) = ebx;
  goto L_10012958;
L_1001294c:;
  BB_U32(eax + 0x4u) = 0x0u;
  ecx = (eax + 0x8u);
  BB_U32(eax) = ecx;
L_10012958:;
  BYTE(ebp) = ((uint8_t)(edx));
  ebp = (ebp - eax);
  a = ebp;
  result = (a << 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 0);
  ebp = result;
  eax = (ebp + eax + 0x80u);
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
bb_qmix_small_page_allocate_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_page_allocate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_small_heap_resize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (eax ^ eax);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = (edx ^ edx);
  ebx = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ebx)) & 0xffu) << 0);
  a = ecx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_100129bc;
  eax = BB_U32(esp + 0x14u);
  esi = BB_U32(esp + 0x18u);
  edi = esi;
  edx = (edx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(edx)) - ((uint8_t)(ecx)))) & 0xffu) << 0);
  BYTE(ebx) = ((uint8_t)(ecx));
  edi = (edi - BB_U32(eax + 0x810u));
  ebp = args[sp++];
  esp += 4u;
  edi = ((uint32_t)((int32_t)(edi) >> 12));
  BYTE(edi + eax + 0x10u) = (BYTE(edi + eax + 0x10u) + ((uint8_t)(edx)));
  edi = args[sp++];
  esp += 4u;
  esi = (esi - BB_U32(eax + 0x810u));
  a = esi;
  result = ((uint32_t)((int32_t)a >> 12)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 12, 2);
  esi = result;
  BYTE(esi + eax + 0x410u) = 0xf1u;
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
L_100129bc:;
  if (flags & 0x41u)
    goto L_10012a33;
  edi = (ebx + ecx);
  esi = BB_U32(esp + 0x18u);
  ebp = (esi + 0xf8u);
  a = edi;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10012a33;
  ebp = (edx + ebx);
  a = ebp;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_100129e3;
L_100129d6:;
  a = BYTE(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_100129e1;
  ebp = (ebp + 1u);
  a = ebp;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_100129d6;
L_100129e1:;
  a = ebp;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
L_100129e3:;
  if (!(flags & 0x40u))
    goto L_10012a33;
  BYTE(ebx) = ((uint8_t)(ecx));
  eax = BB_U32(esi);
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10012a1b;
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10012a1b;
  ebp = (esi + 0xf8u);
  a = ebp;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10012a0f;
  eax = (eax ^ eax);
  BB_U32(esi) = edi;
  a = BYTE(edi);
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10012a0a;
L_10012a03:;
  eax = (eax + 1u);
  edi = (edi + 1u);
  a = BYTE(edi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10012a03;
L_10012a0a:;
  BB_U32(esi + 0x4u) = eax;
  goto L_10012a1b;
L_10012a0f:;
  BB_U32(esi + 0x4u) = 0x0u;
  eax = (esi + 0x8u);
  BB_U32(esi) = eax;
L_10012a1b:;
  eax = BB_U32(esp + 0x14u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(edx)) - ((uint8_t)(ecx)))) & 0xffu) << 0);
  esi = (esi - BB_U32(eax + 0x810u));
  esi = ((uint32_t)((int32_t)(esi) >> 12));
  a = BYTE(esi + eax + 0x10u);
  b = ((uint8_t)(edx));
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  BYTE(esi + eax + 0x10u) = result;
  eax = 0x1u;
L_10012a33:;
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
bb_qmix_small_heap_resize_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused)),
                                uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_small_heap_resize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $0");
}
#endif
