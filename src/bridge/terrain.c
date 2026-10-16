/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "terrain_models.h"
#endif
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
/* First C forms of terrain transitions and road cleanup. Address-derived labels
 * retain provenance for auditing recursive neighborhood and callback ordering.
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
#ifdef BB_MODEL_RUNTIME
#define TERRAIN_DISPATCH(target)                                               \
  bb_model_call_at(&call, BB_MODEL_ID_##target, args + sp, 6, esp, edi, esi,   \
                   ebp, ebx)
#else
#define TERRAIN_DISPATCH(target)                                               \
  bb_call_words6(&call, BB_CODE(target), args + sp)
#endif
#define TERRAIN_CALL(target, consumed)                                         \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                        \
    TERRAIN_DISPATCH(target);                                                  \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

static uint32_t borrow_flags(uint32_t flags, uint32_t a, uint32_t b,
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
void bb_road_shape_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  ecx = (ecx << 6);
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  eax = (eax & 0xffu);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + eax);
  edx = (ecx + ecx * 2u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b61u)) & 0xffu) << 0);
  esi = (edx * 4u);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_41846a;
  ebx = (ebx ^ ebx);
  ecx = (ecx ^ ecx);
  ebx =
      (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559b55u)) & 0xffu) << 0);
  ecx =
      (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559261u)) & 0xffu) << 0);
  eax = (eax ^ eax);
  edx = (edx ^ edx);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x55a461u)) & 0xffu) << 0);
  edx =
      (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559b6du)) & 0xffu) << 0);
  a = ebx;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_418416;
  ebx = (ebx ^ ebx);
L_418416:;
  a = ecx;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_41841d;
  ecx = (ecx ^ ecx);
L_41841d:;
  a = eax;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_418424;
  eax = (eax ^ eax);
L_418424:;
  a = edx;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_41842b;
  edx = (edx ^ edx);
L_41842b:;
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = edi;
  b = edi;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  edi = result;
  a = ecx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = ecx;
  b = ecx;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  ecx = result;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  a = edx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  ebx = (ecx + edi * 2u + 0x3u);
  edi = (eax + ebx * 2u + 0x1u);
  a = ecx;
  b = ecx;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  ecx = result;
  edx = BB_U32((uintptr_t)(args + sp) + 16u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + edi * 2u + 0x484571u)) & 0xffu) << 0);
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x5e470cu;
  BYTE(esi + 0x559b61u) = ((uint8_t)(eax));
  TERRAIN_CALL(bb_terrain_dirty_entry, 8); /* 00418465 */
L_41846a:;
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
uint32_t __attribute__((naked)) bb_road_shape_entry(uint32_t x
                                                    __attribute__((unused)),
                                                    uint32_t y
                                                    __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_road_shape_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_road_prune_near_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  eax = BB_U32((uintptr_t)(args + sp) + 4u);
  args[--sp] = ebx;
  esp -= 4u;
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041ab8b */
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 16u)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041ab9c */
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 16u)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041abad */
  ecx = BB_U32((uintptr_t)(args + sp) + 16u);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) + 1u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041abc1 */
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041abcf */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
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
bb_road_prune_near_entry(uint32_t x __attribute__((unused)),
                         uint32_t y __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_road_prune_near_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_road_prune_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[16] = {0}, sp = 8, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  eax = (eax << 6);
  args[--sp] = esi;
  esp -= 4u;
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 16u)) & 0xffu) << 0);
  args[--sp] = ebp;
  esp -= 4u;
  eax = (eax + eax * 2u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b61u)) & 0xffu) << 0);
  esi = (edx * 4u);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_41ad63;
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559b55u)) & 0xffu) << 0);
  ecx =
      (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559261u)) & 0xffu) << 0);
  edx =
      (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x55a461u)) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 12u) = ((uint8_t)(eax));
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559b6du)) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 13u) = ((uint8_t)(ecx));
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 14u) = ((uint8_t)(edx));
  BYTE((uintptr_t)(args + sp) + 15u) = ((uint8_t)(eax));
  a = ((uint8_t)(ecx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  a = edx;
  b = edx;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  edx = result;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 13u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  a = ebp;
  b = ebp;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  ebp = result;
  ecx = (ebp + edx * 2u + 0x3u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 14u)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 15u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  a = ((uint8_t)(edx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  ecx = (eax + ecx * 2u + 0x1u);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  eax = (eax + ecx * 2u + 0x1u);
  a = eax;
  b = 0x7u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41ad55;
  a = eax;
  b = 0xbu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41ad55;
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41ad55;
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41ad55;
  a = eax;
  b = 0xfu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41ad55;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  ecx = 0x5e470cu;
  BYTE(esi + 0x559b61u) = 0x11u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_dirty_entry, 8); /* 0041acac */
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_41acd8;
  eax = BB_U32(esi + 0x559b5cu);
  a = eax;
  b = 0x200000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41acd8;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041acd0 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_41acd8:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 13u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_41acfd;
  eax = BB_U32(esi + 0x559268u);
  a = eax;
  b = 0x200000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41acfd;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 24u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041acf5 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_41acfd:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 14u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_41ad22;
  eax = BB_U32(esi + 0x55a468u);
  a = eax;
  b = 0x200000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41ad22;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 24u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041ad1a */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_41ad22:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 15u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_41ad47;
  eax = BB_U32(esi + 0x559b74u);
  a = eax;
  b = 0x200000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41ad47;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  a = ((uint8_t)(ebx));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_road_prune_entry, 0); /* 0041ad3f */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_41ad47:;
  BYTE(esi + 0x559b61u) = 0x0u;
  ebp = args[sp++];
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
L_41ad55:;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_road_shape_entry, 0); /* 0041ad5b */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_41ad63:;
  ebp = args[sp++];
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
uint32_t __attribute__((naked)) bb_road_prune_entry(uint32_t x
                                                    __attribute__((unused)),
                                                    uint32_t y
                                                    __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_road_prune_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_terrain_fill_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  BB_U32((uintptr_t)(args + sp) + 4u) = ecx;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 24u)) & 0xffu) << 0);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 36u)) & 0xffu) << 0);
  edi = (edi ^ edi);
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
L_4215ed:;
  esi = (esi ^ esi);
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  eax = (eax - edi);
  BB_U32((uintptr_t)(args + sp) + 24u) = eax;
L_4215f9:;
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  ecx = BB_U32((uintptr_t)(args + sp) + 24u);
  ebp = (uint32_t)(int32_t)(int16_t)(WORD(eax));
  a = ecx;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421649;
  eax = edi;
  ecx = BB_U32((uintptr_t)(args + sp) + 16u);
  eax = (eax - ecx);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421649;
  eax = ebx;
  eax = (eax - esi);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421649;
  eax = esi;
  eax = (eax - ebx);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421649;
  a = ebp;
  result = (a << 16) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 16, 0);
  ebp = result;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  TERRAIN_CALL(bb_entity_distance, 0); /* 0042162b */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421649;
  eax = BB_U32((uintptr_t)(args + sp) + 40u);
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_set_entry, 16); /* 00421644 */
L_421649:;
  esi = (esi + 1u);
  a = esi;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4215f9;
  edi = (edi + 1u);
  a = edi;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4215ed;
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
uint32_t __attribute__((naked, thiscall))
bb_terrain_fill_entry(void *object __attribute__((unused)),
                      uint32_t arg0 __attribute__((unused)),
                      uint32_t arg1 __attribute__((unused)),
                      uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_fill_c) ";leal 4(%esp),%esp;popal;popfl;ret $12");
}
#endif
void bb_terrain_radius_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(WORD((uintptr_t)(args + sp) + 4u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421683;
  a = ((uint16_t)(eax));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 16);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_421683;
  WORD(ecx) = ((uint16_t)(eax));
L_421683:;
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
bb_terrain_radius_entry(void *object __attribute__((unused)),
                        uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_radius_c) ";leal 4(%esp),%esp;popal;popfl;ret $4");
}
#endif
void bb_terrain_open_all_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = (ebx ^ ebx);
  esi = 0x559b68u;
L_42169a:;
  edi = (edi ^ edi);
L_42169c:;
  eax = BB_U32(esi);
  args[--sp] = ebx;
  esp -= 4u;
  eax = (eax & 0xffffefffu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esi) = eax;
  ecx = 0x5e470cu;
  eax = (eax | 0x800u);
  esi = (esi + 0xcu);
  edi = (edi + 1u);
  BB_U32(esi + 0xfffffff4u) = eax;
  a = eax;
  b = 0xffffbfffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(esi + 0xfffffff4u) = eax;
  TERRAIN_CALL(bb_terrain_dirty_entry, 8); /* 004216c1 */
  a = edi;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_42169c;
  ebx = (ebx + 1u);
  a = ebx;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_42169a;
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
void __attribute__((naked)) bb_terrain_open_all_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_open_all_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_terrain_block_all_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = (ebx ^ ebx);
  esi = 0x559b68u;
L_4216ea:;
  edi = (edi ^ edi);
L_4216ec:;
  eax = BB_U32(esi);
  args[--sp] = ebx;
  esp -= 4u;
  eax = (eax & 0xffffefffu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esi) = eax;
  ecx = 0x5e470cu;
  eax = (eax & 0xfffff7ffu);
  esi = (esi + 0xcu);
  edi = (edi + 1u);
  BB_U32(esi + 0xfffffff4u) = eax;
  a = eax;
  b = 0x4000u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(esi + 0xfffffff4u) = eax;
  TERRAIN_CALL(bb_terrain_dirty_entry, 8); /* 00421711 */
  a = edi;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4216ec;
  ebx = (ebx + 1u);
  a = ebx;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4216ea;
  esi = BB_U32(0x554f98u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_42174e;
L_421731:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x12au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_421747;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_entity_reveal_entry, 4); /* 00421742 */
L_421747:;
  esi = BB_U32(esi + 0x4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_421731;
L_42174e:;
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
void __attribute__((naked)) bb_terrain_block_all_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_block_all_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_terrain_blend_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  args[--sp] = 0x35u;
  esp -= 4u;
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 20u)) & 0xffu) << 0);
  args[--sp] = 0x51u;
  esp -= 4u;
  args[--sp] = 0x1000u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 00421778 */
  esi = eax;
  args[--sp] = 0xe3u;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = 0x62u;
  esp -= 4u;
  args[--sp] = 0x4000u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 00421793 */
  a = esi;
  b = eax;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0x83u;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = 0x73u;
  esp -= 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 004217ae */
  a = esi;
  b = eax;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0x4fu;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = 0x3fu;
  esp -= 4u;
  args[--sp] = 0x80000u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 004217c6 */
  a = esi;
  b = eax;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0x6u;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = 0xbu;
  esp -= 4u;
  args[--sp] = 0x200u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 004217de */
  a = esi;
  b = eax;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0xe1u;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  args[--sp] = 0xeau;
  esp -= 4u;
  args[--sp] = 0x200000u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 004217fc */
  a = esi;
  b = eax;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  args[--sp] = 0xc8u;
  esp -= 4u;
  args[--sp] = 0xcau;
  esp -= 4u;
  args[--sp] = 0x800000u;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 32u);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_transition_entry, 20); /* 0042181a */
  a = esi;
  b = eax;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  if (!(flags & 0x40u))
    goto L_421863;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 20u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  ecx = (ecx << 6);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + eax);
  eax = (eax ^ eax);
  edx = (ecx + ecx * 2u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b60u)) & 0xffu) << 0);
  eax = BB_U32(eax * 8u + 0x6a9092u);
  a = eax;
  b = 0x54a808u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421863;
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = 0x5e470cu;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_set_entry, 16); /* 0042185e */
L_421863:;
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
bb_terrain_blend_entry(void *object __attribute__((unused)),
                       uint32_t arg0 __attribute__((unused)),
                       uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_blend_c) ";leal 4(%esp),%esp;popal;popfl;ret $8");
}
#endif
void bb_terrain_blend_near_c(uint32_t input[10]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 8u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  /* The original writes only the low byte of its four-byte local. */
  args[sp - 1] = input[9];
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 8u)) & 0xffu) << 0);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 0u) = ((uint8_t)(eax));
  args[--sp] = ebx;
  esp -= 4u;
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  esi = ecx;
  eax = BB_U32((uintptr_t)(args + sp) + 8u);
  args[--sp] = eax;
  esp -= 4u;
  edx = (ebx + 0xffffffffu);
  args[--sp] = edx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 0042188e */
  ecx = BB_U32((uintptr_t)(args + sp) + 8u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 0042189b */
  ecx = BB_U32((uintptr_t)(args + sp) + 8u);
  edx = (ebx + 0x1u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 004218ab */
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  edx = (ebx + 0xffffffffu);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 004218bb */
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  edx = (ebx + 0x1u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 004218cb */
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 20u)) & 0xffu) << 0);
  ecx = (ebx + 0xffffffffu);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 8u) = ((uint8_t)(eax));
  eax = BB_U32((uintptr_t)(args + sp) + 8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 004218e5 */
  ecx = BB_U32((uintptr_t)(args + sp) + 8u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 004218f2 */
  ecx = BB_U32((uintptr_t)(args + sp) + 8u);
  edx = (ebx + 0x1u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 00421902 */
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  ecx = esi;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 0042190f */
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
uint32_t __attribute__((naked, thiscall))
bb_terrain_blend_near_entry(void *object __attribute__((unused)),
                            uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -4(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_blend_near_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "4(%esp),%esp;ret $8");
}
#endif
void bb_terrain_blend_area_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  a = esp;
  b = 0x1cu;
  esp -= b;
  sp -= b / 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 36u)) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32((uintptr_t)(args + sp) + 28u) = ecx;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 48u)) & 0xffu) << 0);
  BB_U32((uintptr_t)(args + sp) + 28u) = eax;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  eax = (eax << 6);
  edx = BB_U32((uintptr_t)(args + sp) + 28u);
  ecx = (ecx ^ ecx);
  eax = (eax + eax * 2u);
  eax = (eax + edx);
  ebx = (eax + eax * 2u);
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  eax = (eax - 0x2u);
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebx * 4u + 0x559b60u)) & 0xffu) << 0);
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  eax = (eax + 0x2u);
  ebp = BB_U32(ecx * 8u + 0x6a9092u);
  ecx = BB_U32((uintptr_t)(args + sp) + 16u);
  BB_U32((uintptr_t)(args + sp) + 36u) = eax;
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421a33;
  eax = ecx;
  eax = (eax << 6);
  esi = (eax + eax * 2u);
  eax = (edx + 0xfffffffeu);
  BB_U32((uintptr_t)(args + sp) + 32u) = eax;
  eax = (edx + 0x2u);
  BB_U32((uintptr_t)(args + sp) + 20u) = eax;
L_42199a:;
  ebx = BB_U32((uintptr_t)(args + sp) + 32u);
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421a18;
  eax = (esi + ebx);
  edx = (eax + eax * 2u);
  edi = (edx * 4u + 0x559b60u);
L_4219b3:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421a0e;
  a = esi;
  b = 0x9000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_421a0e;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421a0e;
  a = ebx;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_421a0e;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi)) & 0xffu) << 0);
  eax = BB_U32(eax * 8u + 0x6a9092u);
  a = ((uint8_t)(eax >> 8));
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_421a0e;
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_421a0e;
  a = ebp;
  b = 0x8u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4219ee;
  a = eax;
  b = 0x40000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_421a0e;
L_4219ee:;
  a = ebp;
  b = 0x40000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4219fa;
  a = ((uint8_t)(eax));
  b = 0x8u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_421a0e;
L_4219fa:;
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = 0x5e470cu;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_set_entry, 16); /* 00421a09 */
L_421a0e:;
  edi = (edi + 0xcu);
  ebx = (ebx + 1u);
  a = ebx;
  b = BB_U32((uintptr_t)(args + sp) + 20u);
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4219b3;
L_421a18:;
  esi = (esi + 0xc0u);
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  eax = (eax + 1u);
  ecx = BB_U32((uintptr_t)(args + sp) + 36u);
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_42199a;
L_421a33:;
  ebx = BB_U32((uintptr_t)(args + sp) + 24u);
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  ebx = (ebx - 0x3u);
  eax = (eax + 0x3u);
  BB_U32((uintptr_t)(args + sp) + 20u) = eax;
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_421a76;
  edi = BB_U32((uintptr_t)(args + sp) + 28u);
  esi = BB_U32((uintptr_t)(args + sp) + 28u);
  edi = (edi - 0x3u);
  esi = (esi + 0x3u);
L_421a57:;
  ebp = edi;
  a = esi;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421a6d;
L_421a5d:;
  ecx = BB_U32((uintptr_t)(args + sp) + 40u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  TERRAIN_CALL(bb_terrain_blend_entry, 8); /* 00421a63 */
  ebp = (ebp + 1u);
  a = ebp;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_421a5d;
L_421a6d:;
  ebx = (ebx + 1u);
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_421a57;
L_421a76:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x1cu;
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
bb_terrain_blend_area_entry(void *object __attribute__((unused)),
                            uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_blend_area_c) ";leal 4(%esp),%esp;popal;popfl;ret $8");
}
#endif
void bb_terrain_set_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 12u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax << 6);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  edi = ecx;
  eax = (eax + eax * 2u);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 20u)) & 0xffu) << 0);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b60u)) & 0xffu) << 0);
  ebp = (edx * 4u);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_421b83;
  eax = (eax ^ eax);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  esi = BB_U32(eax * 8u + 0x6a9092u);
  if (flags & 0x40u)
    goto L_421acb;
  a = ((uint8_t)(ebx));
  b = 0x35u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421ada;
L_421acb:;
  TERRAIN_CALL(bb_random, 0); /* 00421acb */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x6u;
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
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) + ((uint8_t)(edx)))) & 0xffu) << 0);
L_421ada:;
  a = ((uint8_t)(ebx));
  b = 0xe1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421af7;
  TERRAIN_CALL(bb_random, 0); /* 00421adf */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x1u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_421af7;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xe2u) & 0xffu) << 0);
L_421af7:;
  a = ((uint8_t)(ebx));
  b = 0xc8u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421b14;
  TERRAIN_CALL(bb_random, 0); /* 00421afc */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x1u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_421b14;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xc9u) & 0xffu) << 0);
L_421b14:;
  a = ((uint8_t)(ebx));
  b = 0xe3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421b28;
  TERRAIN_CALL(bb_random, 0); /* 00421b19 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x5u;
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
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) + ((uint8_t)(edx)))) & 0xffu) << 0);
L_421b28:;
  a = esi;
  b = 0x100u;
  flags = logic_flags(flags, a & b, 32);
  BYTE(ebp + 0x559b60u) = ((uint8_t)(ebx));
  if (flags & 0x40u)
    goto L_421b40;
  a = BB_U32(ebp + 0x559b68u);
  b = 0x200000u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  BB_U32(ebp + 0x559b68u) = result;
L_421b40:;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = edi;
  TERRAIN_CALL(bb_terrain_dirty_entry, 8); /* 00421b4c */
  ecx = BB_U32((uintptr_t)(args + sp) + 32u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421b83;
  a = esi;
  b = 0x400u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421b72;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = edi;
  TERRAIN_CALL(bb_terrain_blend_area_entry, 8); /* 00421b6d */
L_421b72:;
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = edi;
  TERRAIN_CALL(bb_terrain_blend_near_entry, 8); /* 00421b7e */
L_421b83:;
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
uint32_t __attribute__((naked, thiscall))
bb_terrain_set_entry(void *object __attribute__((unused)),
                     uint32_t arg0 __attribute__((unused)),
                     uint32_t arg1 __attribute__((unused)),
                     uint32_t arg2 __attribute__((unused)),
                     uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_set_c) ";leal 4(%esp),%esp;popal;popfl;ret $16");
}
#endif
void bb_terrain_transition_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 4u)) & 0xffu) << 0);
  a = esp;
  b = 0x1cu;
  esp -= b;
  sp -= b / 4u;
  eax = 0xc0u;
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  if (!(flags & 1u))
    goto L_421e7a;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 52u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_421e7a;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  eax = (eax << 6);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 48u)) & 0xffu) << 0);
  edi = BB_U32((uintptr_t)(args + sp) + 56u);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b60u)) & 0xffu) << 0);
  esi = (edx * 4u);
  a = BB_U32(eax * 8u + 0x6a9092u);
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421bf5;
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
  b = 0x1cu;
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
L_421bf5:;
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559260u)) & 0xffu) << 0);
  eax = BB_U32(eax * 8u + 0x6a9092u);
  eax = (eax & edi);
  BB_U32((uintptr_t)(args + sp) + 24u) = eax;
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559b54u)) & 0xffu) << 0);
  eax = BB_U32(eax * 8u + 0x6a9092u);
  eax = (eax & edi);
  BB_U32((uintptr_t)(args + sp) + 20u) = eax;
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559b6cu)) & 0xffu) << 0);
  ebp = BB_U32(eax * 8u + 0x6a9092u);
  eax = (eax ^ eax);
  ebp = (ebp & edi);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x55a460u)) & 0xffu) << 0);
  ebx = (ebx ^ ebx);
  edx = BB_U32((uintptr_t)(args + sp) + 24u);
  eax = BB_U32(eax * 8u + 0x6a9092u);
  eax = (eax & edi);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
  BYTE((uintptr_t)(args + sp) + 28u) = ((uint8_t)(ecx));
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  a = edx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  eax = (eax & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 32u) = ((uint8_t)(eax));
  a = ecx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  edx = (edx & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  BYTE((uintptr_t)(args + sp) + 36u) = ((uint8_t)(edx));
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  eax = (eax ^ eax);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x55926cu)) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 40u) = ((uint8_t)(ecx));
  a = BB_U32(eax * 8u + 0x6a9092u);
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  eax = (eax & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  ecx = (eax + edx * 2u);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 32u)) & 0xffu) << 0);
  ecx = (eax + ecx * 2u);
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x559254u)) & 0xffu) << 0);
  a = BB_U32(eax * 8u + 0x6a9092u);
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 36u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  ecx = (eax + ecx * 2u);
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x55a454u)) & 0xffu) << 0);
  ecx = (edx + ecx * 2u);
  edx = BB_U32(eax * 8u + 0x6a9092u);
  a = edi;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 40u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  ecx = (eax + ecx * 2u);
  ecx = (edx + ecx * 2u);
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x55a46cu)) & 0xffu) << 0);
  a = BB_U32(eax * 8u + 0x6a9092u);
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  edx = (edx & 0xffffff00u) | (((uint32_t)(!(flags & 0x40u)) & 0xffu) << 0);
  eax = (eax ^ eax);
  ecx = (edx + ecx * 2u);
  edx = (edx ^ edx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
L_421cee:;
  esi = 0x1u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(edx))) & 0xffu) << 0);
  esi = (esi << (((uint8_t)(ecx)) & 31u));
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_421cfe;
  ebx = (edx + 0x1u);
L_421cfe:;
  edx = (edx + 1u);
  a = edx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421cee;
  a = ((uint8_t)(ebx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421d0b;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xdu) & 0xffu) << 0);
L_421d0b:;
  a = ((uint8_t)(ebx));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421d12;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xeu) & 0xffu) << 0);
L_421d12:;
  a = ((uint8_t)(ebx));
  b = 0x5u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421d19;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xfu) & 0xffu) << 0);
L_421d19:;
  a = ((uint8_t)(ebx));
  b = 0x7u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421d20;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x10u) & 0xffu) << 0);
L_421d20:;
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_421dba;
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 32u)) & 0xffu) << 0);
  ecx = (eax + edx * 4u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 36u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 40u)) & 0xffu) << 0);
  ecx = (edx + ecx * 4u);
  ecx = (eax + ecx * 4u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + ((uint8_t)(ecx)))) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_421d53;
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
  b = 0x1cu;
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
L_421d53:;
  esi = (esi ^ esi);
  eax = (eax ^ eax);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
L_421d5b:;
  edi = 0x1u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  edi = (edi << (((uint8_t)(ecx)) & 31u));
  a = edx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_421d6b;
  ebx = (eax + 0x1u);
L_421d6b:;
  a = edi;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421d70;
  esi = (esi + 1u);
L_421d70:;
  eax = (eax + 1u);
  a = eax;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_421d5b;
  a = esi;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_421d7f;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x11u) & 0xffu) << 0);
  goto L_421dba;
L_421d7f:;
  if (!(flags & 0x40u))
    goto L_421dba;
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  ecx = BB_U32((uintptr_t)(args + sp) + 20u);
  a = eax;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421d91;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xau) & 0xffu) << 0);
  goto L_421dba;
L_421d91:;
  eax = BB_U32((uintptr_t)(args + sp) + 16u);
  a = eax;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_421d9d;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x9u) & 0xffu) << 0);
  goto L_421dba;
L_421d9d:;
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  ecx = BB_U32((uintptr_t)(args + sp) + 24u);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0xbu) & 0xffu) << 0);
  a = ecx;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_421dba;
  ebp = (ebp & BB_U32((uintptr_t)(args + sp) + 24u));
  a = ebp;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  result = (a - b - (flags & 1u)) & 0xffu;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) & 0x5u)) & 0xffu) << 0);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) + 0xcu)) & 0xffu) << 0);
L_421dba:;
  a = ((uint8_t)(ebx));
  b = 0xdu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_421e19;
  a = ((uint8_t)(ebx));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_421e19;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(0x13u) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 48u)) & 0xffu) << 0);
  eax = (eax & 0xffff00ffu) |
        (((uint32_t)((((uint8_t)(eax >> 8)) - ((uint8_t)(eax >> 8)))) & 0xffu)
         << 8);
  edx = (edx ^ edx);
  b = ((uint8_t)(ecx));
  a = (uint16_t)eax;
  if (!b || a / b > 255u)
    division_fault();
  result = (a / b) | ((a % b) << 8);
  eax = (eax & 0xffff0000u) | result;
  edx =
      (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax >> 8))) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(0x7u) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 52u)) & 0xffu) << 0);
  eax = (eax & 0xffff00ffu) |
        (((uint32_t)((((uint8_t)(eax >> 8)) - ((uint8_t)(eax >> 8)))) & 0xffu)
         << 8);
  ebp = (edx + edx * 4u);
  b = ((uint8_t)(ecx));
  a = (uint16_t)eax;
  if (!b || a / b > 255u)
    division_fault();
  result = (a / b) | ((a % b) << 8);
  eax = (eax & 0xffff0000u) | result;
  ecx = (ecx ^ ecx);
  ecx =
      (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax >> 8))) & 0xffu) << 0);
  edx = (ecx + ecx * 2u);
  eax = (ebp + edx);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x1u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  if (!(flags & 0x40u))
    goto L_421e19;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  eax = (eax - 0xdu);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_421e19;
  switch (eax) {
  case 0:
    goto L_421e0b;
  case 1:
    goto L_421e0f;
  case 2:
    goto L_421e13;
  case 3:
    goto L_421e17;
  default:
    __builtin_unreachable();
  }
L_421e0b:;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x1u) & 0xffu) << 0);
  goto L_421e19;
L_421e0f:;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x3u) & 0xffu) << 0);
  goto L_421e19;
L_421e13:;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x5u) & 0xffu) << 0);
  goto L_421e19;
L_421e17:;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x7u) & 0xffu) << 0);
L_421e19:;
  a = ((uint8_t)(ebx));
  b = 0x11u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_421e48;
  eax = BB_U32((uintptr_t)(args + sp) + 64u);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = BB_U32((uintptr_t)(args + sp) + 56u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32((uintptr_t)(args + sp) + 56u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x5e470cu;
  TERRAIN_CALL(bb_terrain_set_entry, 16); /* 00421e34 */
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
  b = 0x1cu;
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
L_421e48:;
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_421e6b;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 60u)) & 0xffu) << 0);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = BB_U32((uintptr_t)(args + sp) + 56u);
  edx = BB_U32((uintptr_t)(args + sp) + 52u);
  eax = (eax + ebx + 0xffffffffu);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x5e470cu;
  TERRAIN_CALL(bb_terrain_set_entry, 16); /* 00421e66 */
L_421e6b:;
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
  b = 0x1cu;
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
L_421e7a:;
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
  b = 0x1cu;
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
bb_terrain_transition_entry(void *object __attribute__((unused)),
                            uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused)),
                            uint32_t arg3 __attribute__((unused)),
                            uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_terrain_transition_c) ";leal 4(%esp),%esp;popal;popfl;ret $20");
}
#endif
