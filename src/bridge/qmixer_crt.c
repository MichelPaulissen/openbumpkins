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
/* C forms of QMIXER CRT startup, arguments, environment, code pages and exit
 * registration. Address-derived labels retain provenance for auditing phase
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

void bb_qmix_io_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = esp;
  b = 0x44u;
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
  args[--sp] = 0x100u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x100149b1u); /* 100149ac */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = eax;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100149c4;
  args[--sp] = 0x1bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012030u, 0, 0x100149c1u); /* 100149bc */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_100149c4:;
  eax = (esi + 0x100u);
  BB_U32(0x10020d60u) = esi;
  BB_U32(0x10020e60u) = 0x20u;
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10014a03;
  ecx = (ecx ^ ecx);
  edx = 0xau;
L_100149e5:;
  BYTE(esi + 0x4u) = ((uint8_t)(ecx));
  esi = (esi + 0x8u);
  BB_U32(esi + 0xfffffff8u) = 0xffffffffu;
  BYTE(esi + 0xfffffffdu) = ((uint8_t)(edx));
  eax = BB_U32(0x10020d60u);
  eax = (eax + 0x100u);
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_100149e5;
L_10014a03:;
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002127cu), 4, 0x10014a0eu); /* 10014a08 indirect */
  a = WORD(esp + 0x42u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (flags & 0x40u)
    goto L_10014aed;
  a = BB_U32(esp + 0x44u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10014aed;
  eax = BB_U32(esp + 0x44u);
  esi = BB_U32(eax);
  edi = (eax + 0x4u);
  a = esi;
  b = 0x800u;
  flags = sub_flags(flags, a, b, 32);
  ebx = (esi + edi);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10014a3e;
  esi = 0x800u;
L_10014a3e:;
  a = BB_U32(0x10020e60u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10014aa3;
  ebp = 0x10020d64u;
L_10014a4b:;
  args[--sp] = 0x100u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10014a55u); /* 10014a50 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014a9d;
  ecx = (eax + 0x100u);
  BB_U32(ebp) = eax;
  BB_U32(0x10020e60u) = (BB_U32(0x10020e60u) + 0x20u);
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_10014a90;
  ecx = (ecx ^ ecx);
L_10014a72:;
  BYTE(eax + 0x4u) = ((uint8_t)(ecx));
  eax = (eax + 0x8u);
  BB_U32(eax + 0xfffffff8u) = 0xffffffffu;
  BYTE(eax + 0xfffffffdu) = 0xau;
  edx = BB_U32(ebp);
  edx = (edx + 0x100u);
  a = edx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10014a72;
L_10014a90:;
  ebp = (ebp + 0x4u);
  a = BB_U32(0x10020e60u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10014a4b;
  goto L_10014aa3;
L_10014a9d:;
  esi = BB_U32(0x10020e60u);
L_10014aa3:;
  ebp = (ebp ^ ebp);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10014aed;
L_10014aa9:;
  ecx = BB_U32(ebx);
  a = ecx;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10014ae4;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014ae4;
  a = ((uint8_t)(eax));
  b = 0x8u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10014ac5;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002120cu), 4, 0x10014ac1u); /* 10014abb indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014ae4;
L_10014ac5:;
  eax = ebp;
  ecx = ebp;
  eax = (eax & 0xffffffe7u);
  ecx = (ecx & 0x1fu);
  eax = ((uint32_t)((int32_t)(eax) >> 3));
  edx = BB_U32(eax + 0x10020d60u);
  eax = BB_U32(ebx);
  ecx = (edx + ecx * 8u);
  BB_U32(ecx) = eax;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(edi)) & 0xffu) << 0);
  BYTE(ecx + 0x4u) = ((uint8_t)(edx));
L_10014ae4:;
  ebp = (ebp + 1u);
  edi = (edi + 1u);
  ebx = (ebx + 0x4u);
  a = ebp;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10014aa9;
L_10014aed:;
  esi = (esi ^ esi);
  ebx = BB_U32(0x100211fcu);
L_10014af5:;
  edi = (esi * 8u);
  edi = (edi + BB_U32(0x10020d60u));
  a = BB_U32(edi);
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10014b5a;
  eax = 0xfffffff6u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  BYTE(edi + 0x4u) = 0x81u;
  if (flags & 0x40u)
    goto L_10014b22;
  eax = (esi + 0xffffffffu);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  eax = 0xfffffff5u;
  a = eax & 0xffffffffu;
  b = 0xffffffffu & 0xffffffffu;
  wide = (uint64_t)a + b + (flags & 1u);
  result = (uint32_t)wide & 0xffffffffu;
  flags = logic_flags(flags, result, 32) | (wide > 0xffffffffu ? 1u : 0u) |
          (((a ^ b ^ result) & 16u) ? 16u : 0u) |
          ((~(a ^ b) & (a ^ result) & 0x80000000u) ? 0x800u : 0u);
  eax = result;
L_10014b22:;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 4, 0x10014b25u); /* 10014b23 indirect */
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ebp = eax;
  if (flags & 0x40u)
    goto L_10014b54;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002120cu), 4, 0x10014b33u); /* 10014b2d indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014b54;
  eax = (eax & 0xffu);
  BB_U32(edi) = ebp;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10014b49;
  BYTE(edi + 0x4u) = (BYTE(edi + 0x4u) | 0x40u);
  goto L_10014b5e;
L_10014b49:;
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10014b5e;
  BYTE(edi + 0x4u) = (BYTE(edi + 0x4u) | 0x8u);
  goto L_10014b5e;
L_10014b54:;
  BYTE(edi + 0x4u) = (BYTE(edi + 0x4u) | 0x40u);
  goto L_10014b5e;
L_10014b5a:;
  BYTE(edi + 0x4u) = (BYTE(edi + 0x4u) | 0x80u);
L_10014b5e:;
  esi = (esi + 1u);
  a = esi;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10014af5;
  eax = BB_U32(0x10020e60u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021204u), 4, 0x10014b70u); /* 10014b6a indirect */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x44u;
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
uint32_t __attribute__((naked)) bb_qmix_io_initialize_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_io_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_environment_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  edx = BB_U32(0x1001f494u);
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
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = BYTE(edx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014bde;
L_10014bc4:;
  a = BYTE(edx);
  b = 0x3du;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014bca;
  esi = (esi + 1u);
L_10014bca:;
  edi = edx;
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
  edx = (edx + ecx);
  a = BYTE(edx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014bc4;
L_10014bde:;
  eax = (esi * 4u + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10014bebu); /* 10014be6 */
  BB_U32(0x1001fe6cu) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014c03;
  args[--sp] = 0x9u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012030u, 0, 0x10014c00u); /* 10014bfb */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10014c03:;
  ebp = BB_U32(0x1001f494u);
  eax = ebp;
  a = BYTE(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014c6f;
L_10014c11:;
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
  BB_U32(esp + 0x10u) = ecx;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BYTE(ebp);
  b = 0x3du;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014c65;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10014c2eu); /* 10014c29 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebx) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014c41;
  args[--sp] = 0x9u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012030u, 0, 0x10014c3eu); /* 10014c39 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10014c41:;
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
  eax = ecx;
  ecx = (ecx >> 2);
  esi = edi;
  edi = BB_U32(ebx);
  ebx = (ebx + 0x4u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = eax;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
L_10014c65:;
  ebp = (ebp + BB_U32(esp + 0x10u));
  a = BYTE(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014c11;
L_10014c6f:;
  eax = BB_U32(0x1001f494u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100117c0u, 0, 0x10014c7au); /* 10014c75 */
  BB_U32(0x1001f494u) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebx) = 0x0u;
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
uint32_t __attribute__((naked)) bb_qmix_environment_initialize_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_environment_initialize_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_arguments_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x104u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = 0x10020c10u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212ccu), 12, 0x10014cb8u); /* 10014cb2 indirect */
  eax = BB_U32(0x10020e70u);
  BB_U32(0x1001fe7cu) = esi;
  a = BYTE(eax);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014cce;
  esi = BB_U32(0x10020e70u);
L_10014cce:;
  eax = (esp + 0xcu);
  ecx = (esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014d40u, 0, 0x10014ce2u); /* 10014cdd */
  eax = BB_U32(esp + 0x1cu);
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  eax = (eax << 2);
  a = eax;
  b = BB_U32(esp + 0xcu);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10014cf6u); /* 10014cf1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014d09;
  args[--sp] = 0x8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012030u, 0, 0x10014d06u); /* 10014d01 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10014d09:;
  eax = (esp + 0xcu);
  ecx = (esp + 0x8u);
  edx = BB_U32(esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (edi + edx * 4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014d40u, 0, 0x10014d22u); /* 10014d1d */
  eax = BB_U32(esp + 0x1cu);
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
  eax = (eax - 1u);
  BB_U32(0x1001fe64u) = edi;
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x1001fe60u) = eax;
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
uint32_t __attribute__((naked)) bb_qmix_arguments_initialize_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_arguments_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_arguments_parse_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  ecx = BB_U32(esp + 0x14u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(esp + 0x14u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x18u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = BB_U32(esp + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(ecx) = 0x0u;
  BB_U32(edx) = 0x1u;
  if (flags & 0x40u)
    goto L_10014d72;
  edx = BB_U32(esp + 0x18u);
  BB_U32(esp + 0x18u) = (BB_U32(esp + 0x18u) + 0x4u);
  args[sp + 6u] = BB_U32(esp + 24u);
  BB_U32(edx) = eax;
L_10014d72:;
  a = BYTE(esi);
  b = 0x22u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014dbd;
L_10014d77:;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014d82;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  BYTE(eax) = ((uint8_t)(edx));
  eax = (eax + 1u);
L_10014d82:;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  esi = (esi + 1u);
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(edx))) & 0xffu) << 0);
  a = BYTE(ebx + 0x1001fe99u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014d9e;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014d9d;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  BYTE(eax) = ((uint8_t)(ebx));
  eax = (eax + 1u);
L_10014d9d:;
  esi = (esi + 1u);
L_10014d9e:;
  a = ((uint8_t)(edx));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014dac;
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014db0;
  a = ((uint8_t)(edx));
  b = 0x9u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014d77;
L_10014dac:;
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10014db3;
L_10014db0:;
  esi = (esi - 1u);
  goto L_10014e03;
L_10014db3:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014e03;
  BYTE(eax + 0xffffffffu) = 0x0u;
  goto L_10014e03;
L_10014dbd:;
  esi = (esi + 1u);
  a = BYTE(esi);
  b = 0x22u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014df3;
L_10014dc3:;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014df3;
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  a = BYTE(edx + 0x1001fe99u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014de2;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014de2;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  esi = (esi + 1u);
  BYTE(eax) = ((uint8_t)(edx));
  eax = (eax + 1u);
L_10014de2:;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014ded;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  BYTE(eax) = ((uint8_t)(edx));
  eax = (eax + 1u);
L_10014ded:;
  esi = (esi + 1u);
  a = BYTE(esi);
  b = 0x22u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014dc3;
L_10014df3:;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014dfd;
  BYTE(eax) = 0x0u;
  eax = (eax + 1u);
L_10014dfd:;
  a = BYTE(esi);
  b = 0x22u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014e03;
  esi = (esi + 1u);
L_10014e03:;
  edi = (edi ^ edi);
L_10014e05:;
  a = BYTE(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014ef6;
L_10014e0e:;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014e1a;
  a = ((uint8_t)(edx));
  b = 0x9u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014e1d;
L_10014e1a:;
  esi = (esi + 1u);
  goto L_10014e0e;
L_10014e1d:;
  a = BYTE(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014ef6;
  a = BB_U32(esp + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10014e38;
  edx = BB_U32(esp + 0x18u);
  BB_U32(esp + 0x18u) = (BB_U32(esp + 0x18u) + 0x4u);
  args[sp + 6u] = BB_U32(esp + 24u);
  BB_U32(edx) = eax;
L_10014e38:;
  edx = BB_U32(esp + 0x20u);
  BB_U32(edx) = (BB_U32(edx) + 1u);
L_10014e3e:;
  ebx = 0x1u;
  ebp = (ebp ^ ebp);
  a = BYTE(esi);
  b = 0x5cu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014e51;
L_10014e4a:;
  esi = (esi + 1u);
  ebp = (ebp + 1u);
  a = BYTE(esi);
  b = 0x5cu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014e4a;
L_10014e51:;
  a = BYTE(esi);
  b = 0x22u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014e7a;
  a = ebp;
  b = 0x1u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014e77;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014e6e;
  edx = (esi + 0x1u);
  a = BYTE(edx);
  b = 0x22u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014e6e;
  esi = edx;
  goto L_10014e70;
L_10014e6e:;
  ebx = (ebx ^ ebx);
L_10014e70:;
  a = edi;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = edi;
  b = edi;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  edi = result;
  edi = (0u - edi);
L_10014e77:;
  ebp = (ebp >> 1);
L_10014e7a:;
  edx = ebp;
  ebp = (ebp - 1u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014e92;
L_10014e81:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014e89;
  BYTE(eax) = 0x5cu;
  eax = (eax + 1u);
L_10014e89:;
  edx = ebp;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  ebp = (ebp - 1u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014e81;
L_10014e92:;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014ee7;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014ea6;
  a = ((uint8_t)(edx));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014ee7;
  a = ((uint8_t)(edx));
  b = 0x9u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014ee7;
L_10014ea6:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014ee1;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014ecf;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(edx))) & 0xffu) << 0);
  a = BYTE(ebx + 0x1001fe99u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014ec1;
  BYTE(eax) = ((uint8_t)(edx));
  esi = (esi + 1u);
  eax = (eax + 1u);
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
L_10014ec1:;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  eax = (eax + 1u);
  esi = (esi + 1u);
  BYTE(eax + 0xffffffffu) = ((uint8_t)(edx));
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  goto L_10014e3e;
L_10014ecf:;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(edx))) & 0xffu) << 0);
  a = BYTE(ebx + 0x1001fe99u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014edf;
  esi = (esi + 1u);
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
L_10014edf:;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
L_10014ee1:;
  esi = (esi + 1u);
  goto L_10014e3e;
L_10014ee7:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10014eef;
  BYTE(eax) = 0x0u;
  eax = (eax + 1u);
L_10014eef:;
  BB_U32(ecx) = (BB_U32(ecx) + 1u);
  goto L_10014e05;
L_10014ef6:;
  a = BB_U32(esp + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10014f07;
  edx = BB_U32(esp + 0x18u);
  BB_U32(edx) = 0x0u;
L_10014f07:;
  edx = BB_U32(esp + 0x20u);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = BB_U32(edx);
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  BB_U32(edx) = result;
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
bb_qmix_arguments_parse_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused)),
                              uint32_t arg3 __attribute__((unused)),
                              uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_arguments_parse_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_codepage_set_c(uint32_t *input) {
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
  b = 0x18u;
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
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10015100u, 0, 0x10014f31u); /* 10014f2c */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ebp = eax;
  a = ebp;
  b = BB_U32(0x1001ff9cu);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10014f48;
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
  b = 0x18u;
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
L_10014f48:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10014f5b;
  SAMPLE_CALL(0x100151b0u, 0, 0x10014f51u); /* 10014f4c */
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
  b = 0x18u;
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
L_10014f5b:;
  BB_U32(esp + 0x10u) = 0x0u;
  args[sp + 4u] = BB_U32(esp + 16u);
  eax = 0x1001ffc0u;
L_10014f68:;
  a = BB_U32(eax);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1001500b;
  eax = (eax + 0x30u);
  BB_U32(esp + 0x10u) = (BB_U32(esp + 0x10u) + 1u);
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = 0x100200b0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10014f68;
  eax = (esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021278u), 8, 0x10014f8au); /* 10014f84 indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100150d6;
  edi = 0x1001fe98u;
  eax = (eax ^ eax);
  ecx = 0x40u;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BYTE(edi) = (uint8_t)eax;
  edi += (flags & 0x400u) ? 0u - 1u : 1u;
  a = BB_U32(esp + 0x14u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_100150b3;
  esi = (esp + 0x1au);
  a = BYTE(esp + 0x1au);
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10014fe3;
L_10014fb7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x1u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10014fe3;
  ecx = (ecx ^ ecx);
  edx = (edx ^ edx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  a = edx;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10014fdb;
L_10014fca:;
  BYTE(ecx + 0x1001fe99u) = (BYTE(ecx + 0x1001fe99u) | 0x4u);
  ecx = (ecx + 1u);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x1u)) & 0xffu) << 0);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10014fca;
L_10014fdb:;
  esi = (esi + 0x2u);
  a = BYTE(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10014fb7;
L_10014fe3:;
  eax = 0x1u;
L_10014fe8:;
  BYTE(eax + 0x1001fe99u) = (BYTE(eax + 0x1001fe99u) | 0x8u);
  eax = (eax + 1u);
  a = eax;
  b = 0xffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10014fe8;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(0x1001ff9cu) = ebp;
  SAMPLE_CALL(0x10015150u, 0, 0x10015003u); /* 10014ffe */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  goto L_100150ba;
L_1001500b:;
  edi = 0x1001fe98u;
  eax = (eax ^ eax);
  ecx = 0x40u;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BYTE(edi) = (uint8_t)eax;
  edi += (flags & 0x400u) ? 0u - 1u : 1u;
  ecx = BB_U32(esp + 0x10u);
  edx = (ecx + ecx * 2u);
  edi = (edx * 2u);
L_10015028:;
  ecx = (eax + edi);
  esi = (ecx * 8u + 0x1001ffd0u);
  a = BYTE(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10015068;
L_10015037:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x1u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10015068;
  edx = (edx ^ edx);
  ebx = (ebx ^ ebx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  a = ebx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10015060;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + 0x1001ffb8u)) & 0xffu) << 0);
L_10015050:;
  BYTE(edx + 0x1001fe99u) = (BYTE(edx + 0x1001fe99u) | ((uint8_t)(ecx)));
  edx = (edx + 1u);
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x1u)) & 0xffu) << 0);
  a = ebx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10015050;
L_10015060:;
  esi = (esi + 0x2u);
  a = BYTE(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10015037;
L_10015068:;
  eax = (eax + 1u);
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10015028;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(0x1001ff9cu) = ebp;
  SAMPLE_CALL(0x10015150u, 0, 0x1001507au); /* 10015075 */
  BB_U32(0x1001ffa0u) = eax;
  eax = BB_U32(esp + 0x14u);
  eax = (eax << 4);
  ebx = BB_U32(eax + eax * 2u + 0x1001ffc8u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = 0x1001ffa8u;
  ecx = (eax + eax * 2u + 0x1001ffc4u);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  eax = BB_U32(ecx);
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx) = eax;
  eax = (eax ^ eax);
  BB_U32(edx + 0x4u) = ebx;
  ebx = args[sp++];
  esp += 4u;
  BB_U32(edx + 0x8u) = ecx;
  a = esp;
  b = 0x18u;
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
L_100150b3:;
  eax = (eax ^ eax);
  BB_U32(0x1001ff9cu) = eax;
L_100150ba:;
  ecx = 0x1001ffa8u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x1001ffa0u) = eax;
  eax = (eax ^ eax);
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  BB_U32(ecx) = eax;
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx + 0x8u) = eax;
  a = esp;
  b = 0x18u;
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
L_100150d6:;
  a = BB_U32(0x1001ffb4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100150ee;
  SAMPLE_CALL(0x100151b0u, 0, 0x100150e4u); /* 100150df */
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
  b = 0x18u;
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
L_100150ee:;
  eax = 0xffffffffu;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x18u;
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
bb_qmix_codepage_set_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_codepage_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_codepage_resolve_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  BB_U32(0x1001ffb4u) = 0x0u;
  eax = BB_U32(esp + 0x4u);
  a = eax;
  b = 0xfffffffeu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10015123;
  BB_U32(0x1001ffb4u) = 0x1u;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(BB_U32(0x10021270u), 0, BB_U32(esp - 4u));
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10015123:;
  a = eax;
  b = 0xfffffffdu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10015138;
  BB_U32(0x1001ffb4u) = 0x1u;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(BB_U32(0x10021274u), 0, BB_U32(esp - 4u));
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10015138:;
  a = eax;
  b = 0xfffffffcu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001514c;
  BB_U32(0x1001ffb4u) = 0x1u;
  eax = BB_U32(0x100204b8u);
L_1001514c:;
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
bb_qmix_codepage_resolve_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_codepage_resolve_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "2048(%esp),%esp;ret $0");
}
#endif
static const uint8_t map_1001519c[] = {0, 4, 4, 4, 1, 4, 4, 4, 4, 4,
                                       4, 4, 4, 4, 4, 4, 4, 2, 3, 204};
void bb_qmix_codepage_language_c(uint32_t *input) {
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
  eax = (eax - 0x3a4u);
  a = eax;
  b = 0x12u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_1001516d;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_1001519c[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_10015170;
  case 1:
    goto L_10015176;
  case 2:
    goto L_1001517c;
  case 3:
    goto L_10015182;
  case 4:
    goto L_1001516d;
  default:
    __builtin_unreachable();
  }
L_1001516d:;
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
L_10015170:;
  eax = 0x411u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10015176:;
  eax = 0x804u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1001517c:;
  eax = 0x412u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10015182:;
  eax = 0x404u;
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
bb_qmix_codepage_language_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_codepage_language_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_codepage_clear_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  edi = 0x1001fe98u;
  ecx = 0x40u;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BYTE(edi) = (uint8_t)eax;
  edi += (flags & 0x400u) ? 0u - 1u : 1u;
  BB_U32(0x1001ffa8u) = eax;
  ecx = 0x1001ffa8u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(0x1001ff9cu) = eax;
  BB_U32(0x1001ffa0u) = eax;
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx + 0x8u) = eax;
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
uint32_t __attribute__((naked)) bb_qmix_codepage_clear_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_codepage_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_codepage_bootstrap_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = 0xfffffffdu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10014f20u, 0, 0x100151e7u); /* 100151e2 */
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
uint32_t __attribute__((naked)) bb_qmix_codepage_bootstrap_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_codepage_bootstrap_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_environment_capture_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
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
  ebx = (ebx ^ ebx);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = (edi ^ edi);
  esi = BB_U32(0x1002125cu);
  a = BB_U32(0x100200b4u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1001523f;
  SAMPLE_CALL(esi, 0, 0x1001520bu); /* 10015209 indirect */
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  edi = eax;
  if (flags & 0x40u)
    goto L_1001521d;
  BB_U32(0x100200b4u) = 0x1u;
  goto L_1001523f;
L_1001521d:;
  SAMPLE_CALL(BB_U32(0x10021264u), 0, 0x10015223u); /* 1001521d indirect */
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10015235;
  BB_U32(0x100200b4u) = 0x2u;
  goto L_1001523f;
L_10015235:;
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
L_1001523f:;
  a = BB_U32(0x100200b4u);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100152ee;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10015262;
  SAMPLE_CALL(esi, 0, 0x10015252u); /* 10015250 indirect */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10015262;
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
L_10015262:;
  a = WORD(edi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  esi = edi;
  if (flags & 0x40u)
    goto L_1001527c;
L_1001526a:;
  esi = (esi + 0x2u);
  a = WORD(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_1001526a;
  esi = (esi + 0x2u);
  a = WORD(esi);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_1001526a;
L_1001527c:;
  esi = (esi - edi);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  result = ((uint32_t)((int32_t)a >> 1)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 1, 2);
  esi = result;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  esi = result;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021258u), 32, 0x10015296u); /* 10015290 indirect */
  ebp = eax;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100152dd;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x100152a2u); /* 1001529d */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100152dd;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021258u), 32, 0x100152bdu); /* 100152b7 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100152cc;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100117c0u, 0, 0x100152c7u); /* 100152c2 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = ebx;
  b = ebx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
L_100152cc:;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021260u), 4, 0x100152d3u); /* 100152cd indirect */
  eax = ebx;
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
L_100152dd:;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021260u), 4, 0x100152e4u); /* 100152de indirect */
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
L_100152ee:;
  a = BB_U32(0x100200b4u);
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10015372;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10015311;
  SAMPLE_CALL(BB_U32(0x10021264u), 0, 0x10015301u); /* 100152fb indirect */
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10015311;
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
L_10015311:;
  ebp = ebx;
  a = BYTE(ebx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_10015326;
L_10015318:;
  ebp = (ebp + 1u);
  a = BYTE(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10015318;
  ebp = (ebp + 1u);
  a = BYTE(ebp);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_10015318;
L_10015326:;
  a = ebp;
  b = ebx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ebp = result;
  a = ebp;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ebp = result;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x1001532fu); /* 1001532a */
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1001534b;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002126cu), 4, 0x10015341u); /* 1001533b indirect */
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
L_1001534b:;
  edi = BB_U32(esp + 0x10u);
  esi = ebx;
  ecx = ebp;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = ebp;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  SAMPLE_CALL(BB_U32(0x1002126cu), 4, 0x10015366u); /* 10015360 indirect */
  eax = BB_U32(esp + 0x10u);
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
L_10015372:;
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
uint32_t __attribute__((naked)) bb_qmix_environment_capture_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_environment_capture_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exit_append_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = BB_U32(0x10020e68u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10017ea0u, 0, 0x10015a3bu); /* 10015a36 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(0x10020e64u);
  ecx = (ecx - BB_U32(0x10020e68u));
  ecx = (ecx + 0x4u);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10015a95;
  eax = BB_U32(0x10020e68u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10017ea0u, 0, 0x10015a5cu); /* 10015a57 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  ecx = BB_U32(0x10020e68u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10017cf0u, 0, 0x10015a6fu); /* 10015a6a */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10015a79;
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
L_10015a79:;
  ecx = BB_U32(0x10020e64u);
  ecx = (ecx - BB_U32(0x10020e68u));
  ecx = (ecx & 0xfffffffcu);
  BB_U32(0x10020e68u) = eax;
  ecx = (ecx + eax);
  BB_U32(0x10020e64u) = ecx;
L_10015a95:;
  ecx = BB_U32(0x10020e64u);
  eax = BB_U32(esp + 0x4u);
  BB_U32(ecx) = eax;
  a = BB_U32(0x10020e64u);
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  BB_U32(0x10020e64u) = result;
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
bb_qmix_exit_append_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exit_append_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exit_register_c(uint32_t *input) {
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
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10015a30u, 0, 0x10015abau); /* 10015ab5 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
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
bb_qmix_exit_register_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exit_register_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_exit_initialize_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = 0x80u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10015adau); /* 10015ad5 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(0x10020e68u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10015af0;
  args[--sp] = 0x18u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012030u, 0, 0x10015aedu); /* 10015ae8 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10015af0:;
  eax = BB_U32(0x10020e68u);
  BB_U32(eax) = 0x0u;
  eax = BB_U32(0x10020e68u);
  BB_U32(0x10020e64u) = eax;
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
uint32_t __attribute__((naked)) bb_qmix_exit_initialize_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_exit_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_crt_reallocate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[160] = {0}, sp = 144, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double call_float __attribute__((unused)) = 0;
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0x1cu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if (!(flags & 0x40u))
    goto L_10017d13;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011710u, 0, 0x10017d08u); /* 10017d03 */
  a = esp;
  b = 0x4u;
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
L_10017d13:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10017d2e;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100117c0u, 0, 0x10017d21u); /* 10017d1c */
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
L_10017d2e:;
  a = ebx;
  b = 0xffffffe0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10017d44;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10017d3f;
  ebx = (ebx + 0xfu);
  ebx = (ebx & 0xfffffff0u);
  goto L_10017d44;
L_10017d3f:;
  ebx = 0x10u;
L_10017d44:;
  BB_U32(esp + 0x10u) = 0x0u;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = ebx;
  b = 0xffffffe0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_10017e61;
  eax = (esp + 0x14u);
  ecx = (esp + 0x18u);
  edx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100124c0u, 0, 0x10017d69u); /* 10017d64 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ebp = eax;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10017e48;
  a = ebx;
  b = BB_U32(0x1001fcd4u);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_10017ded;
  esi = ebx;
  eax = BB_U32(esp + 0x14u);
  a = esi;
  result = (a >> 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 1);
  esi = result;
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012970u, 0, 0x10017d94u); /* 10017d8f */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10017da5;
  eax = BB_U32(esp + 0x20u);
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  goto L_10017ded;
L_10017da5:;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012570u, 0, 0x10017dabu); /* 10017da6 */
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10017ded;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  eax = (eax << 4);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10017dc4;
  eax = ebx;
L_10017dc4:;
  edi = BB_U32(esp + 0x10u);
  esi = BB_U32(esp + 0x20u);
  ecx = eax;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = eax;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = BB_U32(esp + 0x18u);
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012520u, 0, 0x10017deau); /* 10017de5 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_10017ded:;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10017e8c;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x10020e6cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212ecu), 12, 0x10017e07u); /* 10017e01 indirect */
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10017e61;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  eax = (eax << 4);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_10017e1d;
  eax = ebx;
L_10017e1d:;
  edi = BB_U32(esp + 0x10u);
  esi = BB_U32(esp + 0x20u);
  ecx = eax;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = eax;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = BB_U32(esp + 0x18u);
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012520u, 0, 0x10017e43u); /* 10017e3e */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  goto L_10017e61;
L_10017e48:;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(0x10020e6cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021248u), 16, 0x10017e5du); /* 10017e57 indirect */
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
L_10017e61:;
  a = BB_U32(esp + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10017e8c;
  a = BB_U32(0x1001f4b0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10017e8c;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10012160u, 0, 0x10017e77u); /* 10017e72 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10017d44;
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
L_10017e8c:;
  eax = BB_U32(esp + 0x10u);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
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
bb_qmix_crt_reallocate_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_reallocate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "2048(%esp),%esp;ret $0");
}
#endif
void bb_qmix_crt_allocation_size_c(uint32_t *input) {
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
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  eax = (esp + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x14u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100124c0u, 0, 0x10017eb8u); /* 10017eb3 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10017ecd;
  ecx = (ecx ^ ecx);
  esi = args[sp++];
  esp += 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(eax)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = ecx;
  result = (a << 4) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 0);
  ecx = result;
  eax = ecx;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10017ecd:;
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
  SAMPLE_CALL(BB_U32(0x10021244u), 12, 0x10017edcu); /* 10017ed6 indirect */
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
bb_qmix_crt_allocation_size_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_allocation_size_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "2048(%esp),%esp;ret $0");
}
#endif
