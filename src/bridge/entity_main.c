/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "entity_calls.h"
#include "world_controls.h"
#include <signal.h>

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
/* First C form of the complete entity tick. Temporary values and labels retain
 * their original provenance for auditing the kind/subtype state machine. */
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
static uint32_t shift_flags(uint32_t flags, uint32_t before, uint32_t after,
                            unsigned bits, unsigned count, unsigned right) {
  count &= 31u;
  if (!count)
    return flags;
  uint32_t carry = right ? ((before >> (count - 1u)) & 1u)
                         : ((before >> (bits - count)) & 1u);
  uint32_t overflow = right ? (before >> (bits - 1u)) & 1u
                            : ((after >> (bits - 1u)) & 1u) ^ carry;
  return logic_flags(flags, after, bits) | carry | (overflow ? 0x800u : 0u);
}
static void __attribute__((noreturn)) division_fault(void) {
  raise(SIGFPE);
  __builtin_unreachable();
}
#define MAIN_CALL(target, consumed)                                            \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                      \
    BB_ENTITY_CALL(&call,target,args+sp,esp,edi,esi,ebp,ebx);                          \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

static const uint8_t map_44f0dc[] = {
    0,  80, 80, 80, 80, 1,  80, 80, 80, 80, 80, 80, 80, 80, 80,  80, 80, 80,
    80, 80, 80, 80, 2,  3,  4,  80, 5,  80, 80, 80, 80, 6,  7,   80, 8,  9,
    10, 80, 80, 11, 12, 13, 14, 80, 80, 80, 80, 80, 15, 16, 80,  17, 18, 19,
    20, 21, 22, 80, 80, 23, 80, 80, 80, 80, 80, 24, 25, 26, 27,  28, 80, 80,
    80, 80, 80, 80, 29, 80, 30, 31, 80, 80, 32, 33, 80, 80, 80,  80, 80, 80,
    34, 35, 80, 80, 80, 80, 36, 80, 80, 80, 80, 37, 38, 80, 80,  80, 80, 80,
    39, 80, 80, 80, 40, 41, 42, 80, 80, 80, 80, 80, 80, 80, 80,  80, 80, 80,
    80, 80, 80, 80, 43, 80, 80, 80, 80, 80, 80, 80, 80, 44, 45,  46, 80, 80,
    80, 47, 48, 49, 80, 80, 80, 80, 80, 80, 50, 80, 80, 51, 51,  80, 80, 80,
    52, 80, 80, 80, 80, 53, 80, 80, 54, 55, 56, 80, 57, 58, 58,  80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 59, 59, 60, 61, 62,  80, 63, 64,
    65, 80, 66, 67, 68, 69, 80, 80, 80, 70, 71, 72, 72, 80, 80,  80, 80, 80,
    80, 80, 80, 73, 74, 75, 76, 80, 80, 77, 80, 80, 78, 79, 139, 255};
static const uint8_t map_44f1f0[] = {
    0, 1, 6, 6, 2, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,   6,  6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,   6,  6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,   6,  6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, 6, 6, 4, 5, 141, 73, 0};
static const uint8_t map_44f26c[] = {
    0, 6, 6, 6, 6, 6, 6, 6, 1, 6, 6, 6, 6, 6, 6, 6, 2, 6, 6, 6,   6,  6,
    6, 6, 3, 6, 6, 6, 6, 6, 6, 6, 4, 6, 6, 6, 6, 6, 6, 6, 5, 141, 73, 0};
static const uint8_t map_44f2ac[] = {
    0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 1, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   2,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 141, 73, 0};
static const uint8_t map_44f3c8[] = {
    0,  16, 16, 1,  2,  3,  4,  5,  16, 6,  7,   8,  9,  10, 11, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,  16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,  16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,  16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,  16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 12, 16, 16, 16, 16, 16, 16, 16,  16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 13, 14, 15, 139, 255};
static const uint8_t map_44f460[] = {0, 0, 7, 7, 7, 1, 2, 3, 4, 7,
                                     7, 7, 7, 7, 7, 5, 7, 7, 6, 144};
static const uint8_t map_44f490[] = {0, 6, 6, 6, 1, 6, 6, 6, 2, 6,   6,  6,
                                     3, 6, 6, 6, 4, 6, 6, 6, 5, 141, 73, 0};
static const uint8_t map_44f4c4[] = {0, 6, 1, 6, 2, 6, 3, 6, 4, 6, 5, 144};
static const uint8_t map_44f4ec[] = {0, 6, 6, 6, 1, 6, 6, 6, 2, 6,   6,  6,
                                     3, 6, 6, 6, 4, 6, 6, 6, 5, 141, 73, 0};
static const uint8_t map_44f528[] = {0, 8, 1, 8, 2, 8, 3, 8,
                                     4, 8, 5, 8, 6, 8, 7, 144};
static const uint8_t map_44f554[] = {0, 6, 1, 6, 2, 6, 3, 6, 4, 6, 5, 144};
static const uint8_t map_44f574[] = {0, 4, 4, 4, 1, 4,   4,  4,
                                     2, 4, 4, 4, 3, 141, 73, 0};
static const uint8_t map_44f5fc[] = {0, 4, 4, 4, 1, 4,   4,  4,
                                     2, 4, 4, 4, 3, 141, 73, 0};
static const uint8_t map_44f620[] = {0, 4, 1, 4, 2, 4, 4, 4, 3, 141, 73, 0};
static const uint8_t map_44f648[] = {0, 6, 1, 6, 2, 6, 3, 6, 4, 6, 5, 144};
static const uint8_t map_44f668[] = {0, 4, 1, 4, 2, 4, 4, 4, 3, 141, 73, 0};
static const uint8_t map_44f690[] = {0, 6, 1, 6, 2, 6,   3,  6,
                                     4, 6, 6, 6, 5, 141, 73, 0};
static const uint8_t map_44f6c4[] = {0, 8, 1, 8, 2, 8, 3, 8,
                                     4, 8, 5, 8, 6, 8, 7, 144};
static const uint8_t map_44f6fc[] = {0, 9, 1, 9, 2, 9, 3, 9,   4,  9,
                                     5, 9, 6, 9, 7, 9, 8, 141, 73, 0};
static const uint8_t map_44f74c[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 14, 14, 14, 14, 14,  14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 11, 12, 13, 141, 73, 0};
static const uint8_t map_44f784[] = {
    0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,  4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 4, 2, 3, 139, 255};
static const uint8_t map_44f808[] = {
    0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,   4,   4,   4,   4,   4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,   4,   4,   4,   4,   4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,   4,   4,   4,   4,   4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,   4,   4,   4,   4,   4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   4,   4,   4,   4,   4,   4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 2, 3, 204, 204, 204, 204, 204, 204, 204};
void bb_entity_main_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[40] = {0}, sp = 32, a, b, result;
  uint64_t wide;

  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0x104u)) & 0xffu) << 0);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  esi = ecx;
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_449c6f;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0x104u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_449c6f;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449c6a */
L_449c6f:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x105u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_449c9b;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0x105u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_449c9b;
  eax = BB_U32(esi + 0x9cu);
  ecx = esi;
  a = eax;
  b = 0xbfffffffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(esi + 0x9cu) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449c96 */
L_449c9b:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x138u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_449cad;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0x138u) = ((uint8_t)(eax));
L_449cad:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x114u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_449cbf;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0x114u) = ((uint8_t)(eax));
L_449cbf:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x60u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_449cdc;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0x60u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_449cdc;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(esi + 0x5cu);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 00449cd7 */
L_449cdc:;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_449cf0;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449ceb */
L_449cf0:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11eu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_449d01;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449cfc */
L_449d01:;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx << 6);
  ebx = (ebx ^ ebx);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  edx = (ecx + ecx * 2u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b65u)) & 0xffu) << 0);
  ebx = (ebx << 16);
  a = ((uint8_t)(eax));
  b = 0x14u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_449d36;
  a = ((uint8_t)(eax));
  b = 0x15u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_449d3d;
L_449d36:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449d38 */
L_449d3d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_449e11;
  eax = BB_U32(esi + 0x6eu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449d8a;
  eax = BB_U32(esi + 0x72u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449d8a;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449d8a;
  eax = BB_U32(esi + 0x6au);
  edi = (esi + 0x6au);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_449d7f;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_449d7b;
  eax = (eax + 0x800u);
  goto L_449dc2;
L_449d7b:;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_449dc5;
  goto L_449dbd;
L_449d7f:;
  BB_U32(esi + 0x76u) = 0x0u;
  BB_U32(edi) = ebx;
  goto L_449dc5;
L_449d8a:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0xcu;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  edi = (esi + 0x6au);
  a = eax;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  eax = result;
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
  ecx = esi;
  BYTE(esi + 0xeau) = ((uint8_t)(edx));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449da6 */
  a = BB_U32(edi);
  b = 0x140000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_449dc5;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = 0x8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_449dc5;
L_449dbd:;
  eax = (eax + 0x2000u);
L_449dc2:;
  BB_U32(esi + 0x76u) = eax;
L_449dc5:;
  eax = BB_U32(esi + 0x6eu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449de9;
  eax = BB_U32(esi + 0x72u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449de9;
  eax = BB_U32(esi + 0x9cu);
  a = ((uint8_t)(eax >> 8));
  b = 0x40u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_449de9;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_449e98;
L_449de9:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_release_tiles), 0); /* 00449deb */
  eax = BB_U32(esi + 0x6eu);
  ecx = BB_U32(esi + 0x62u);
  ecx = (ecx + eax);
  edx = BB_U32(esi + 0x72u);
  ebp = BB_U32(esi + 0x66u);
  eax = BB_U32(esi + 0x76u);
  ebp = (ebp + edx);
  BB_U32(esi + 0x62u) = ecx;
  ecx = BB_U32(edi);
  BB_U32(esi + 0x66u) = ebp;
  a = ecx;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(edi) = ecx;
  goto L_449e78;
L_449e11:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x103u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_449eb8;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x108u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_449e98;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x40000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449e98;
  ecx = BB_U32(esi + 0x6eu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449e50;
  ecx = BB_U32(esi + 0x72u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449e50;
  a = ((uint8_t)(eax >> 8));
  b = 0x40u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_449e50;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_449e98;
L_449e50:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_release_tiles), 0); /* 00449e52 */
  eax = BB_U32(esi + 0x6eu);
  ecx = BB_U32(esi + 0x62u);
  ecx = (ecx + eax);
  edx = BB_U32(esi + 0x72u);
  ebp = BB_U32(esi + 0x66u);
  eax = BB_U32(esi + 0x76u);
  ebp = (ebp + edx);
  edi = BB_U32(esi + 0x6au);
  a = edi;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  BB_U32(esi + 0x62u) = ecx;
  BB_U32(esi + 0x66u) = ebp;
  BB_U32(esi + 0x6au) = edi;
L_449e78:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_update_tile), 0); /* 00449e7a */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_register_tiles), 0); /* 00449e81 */
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sort_entry), 4); /* 00449e8c */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_force_dirty_entry), 0); /* 00449e93 */
L_449e98:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x103u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_449eb8;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x18u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_449eb8;
  a = ((uint8_t)(eax));
  b = 0x1cu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_449eb8;
  a = ((uint8_t)(eax));
  b = 0x19u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_449eb8;
  a = ((uint8_t)(eax));
  b = 0x1au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_449ebf;
L_449eb8:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00449eba */
L_449ebf:;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  ecx = (ecx ^ ecx);
  edx = (eax + eax * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b60u)) & 0xffu) << 0);
  ebp = BB_U32(ecx * 8u + 0x6a9092u);
  a = ebp;
  b = 0x10u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_449f2c;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  edx = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - edx);
  ebp = (edx + ecx * 8u);
  edx = (edx + ebp * 8u);
  edi = BB_U32(edx * 2u + 0x663592u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449f2c;
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_449f21;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_idle_sound), 0); /* 00449f1a */
  goto L_449f2c;
L_449f21:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x1u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00449f27 */
L_449f2c:;
  edx = BB_U32(esi + 0x7au);
  ecx = BB_U32(esi + 0x6eu);
  eax = (edx + 0xffffff00u);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_449f47;
  ecx = (ecx + 0x100u);
  BB_U32(esi + 0x6eu) = ecx;
  goto L_449f5f;
L_449f47:;
  eax = (edx + 0x100u);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_449f5c;
  ecx = (ecx - 0x100u);
  BB_U32(esi + 0x6eu) = ecx;
  goto L_449f5f;
L_449f5c:;
  BB_U32(esi + 0x6eu) = edx;
L_449f5f:;
  ecx = BB_U32(esi + 0x7eu);
  edx = BB_U32(esi + 0x72u);
  eax = (ecx + 0xffffff00u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_449f7a;
  edx = (edx + 0x100u);
  BB_U32(esi + 0x72u) = edx;
  goto L_449f92;
L_449f7a:;
  eax = (ecx + 0x100u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_449f8f;
  edx = (edx - 0x100u);
  BB_U32(esi + 0x72u) = edx;
  goto L_449f92;
L_449f8f:;
  BB_U32(esi + 0x72u) = ecx;
L_449f92:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  eax = (eax + edx * 8u);
  ebp = BB_U32(eax * 2u + 0x66358au);
  edx = (eax * 2u);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_449fe8;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  ebp = (eax + eax * 2u);
  edi = BB_U32(ebp * 4u + 0x559b68u);
  a = edi;
  b = 0x4u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_449fe8;
  ebx = 0x10000u;
L_449fe8:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x4au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_449ff5;
  BB_U32(esi + 0x6au) = ebx;
L_449ff5:;
  ecx = BB_U32(esi + 0x9cu);
  ecx = (ecx & 0xfffeffffu);
  BB_U32(esi + 0x9cu) = ecx;
  eax = BB_U32(edx + 0x66357cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44a069;
  edi = BB_U32(esi + 0x6au);
  eax = (ebx + 0x1000u);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44a02b;
  eax = BB_U32(esi + 0x76u);
  eax = (eax - 0x1000u);
  BB_U32(esi + 0x76u) = eax;
  goto L_44a069;
L_44a02b:;
  eax = (ebx + 0xfffff000u);
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44a042;
  eax = BB_U32(esi + 0x76u);
  eax = (eax + 0x1000u);
  BB_U32(esi + 0x76u) = eax;
  goto L_44a069;
L_44a042:;
  ecx = (ecx | 0x10000u);
  eax = BB_U32(esi + 0x76u);
  BB_U32(esi + 0x6au) = ebx;
  BB_U32(esi + 0x9cu) = ecx;
  a = eax;
  b = 0x1000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44a069;
  a = eax;
  b = 0xfffff000u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44a069;
  BB_U32(esi + 0x76u) = 0x0u;
L_44a069:;
  eax = BB_U32(esi + 0x6au);
  a = eax;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44a187;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44a187;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx << 6);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + eax);
  eax = (eax ^ eax);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx << 2);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0x559b60u)) & 0xffu) << 0);
  ebp = BB_U32(eax * 8u + 0x6a9092u);
  a = ebp;
  b = 0x200u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a0e3;
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(edx + 0x663584u)) & 0xffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a0e3;
  eax = BB_U32(ecx + 0x559b68u);
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a0e3;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044a0d1 */
  eax = BB_U32(0x484370u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044a0de */
L_44a0e3:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x37u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a148;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44a141;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044a11e */
  BB_U32(esi + 0x7au) = 0x0u;
  BB_U32(esi + 0x7eu) = 0x0u;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  goto L_44a148;
L_44a141:;
  BB_U32(esi + 0x76u) = 0x0u;
L_44a148:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  eax = (eax + edx * 8u);
  ebp = BB_U32(eax * 2u + 0x663580u);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a180;
  args[--sp] = 0x8000u;
  esp -= 4u;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044a173 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x76u) = eax;
  goto L_44a187;
L_44a180:;
  BB_U32(esi + 0x76u) = 0x0u;
L_44a187:;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a1b7;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x1u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044a19a */
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xb2u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a1ad;
  a = ((uint8_t)(eax));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a1b7;
L_44a1ad:;
  BB_U32(esi + 0x9cu) = (BB_U32(esi + 0x9cu) & 0xfff7ffffu);
L_44a1b7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xfbu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xeu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a1c5;
  a = ((uint8_t)(eax));
  b = 0xdu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a1e2;
L_44a1c5:;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a1db;
  eax = BB_U32(esi + 0x66u);
  edx = BB_U32(esi + 0x62u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_place), 8); /* 0044a1d4 */
  goto L_44a1e2;
L_44a1db:;
  BYTE(esi + 0xfbu) = 0x0u;
L_44a1e2:;
  eax = BB_U32(esi + 0x10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a219;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0xc5u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a219;
  ecx = BB_U32(esi + 0x62u);
  BB_U32(eax + 0x62u) = ecx;
  eax = BB_U32(esi + 0x66u);
  edx = BB_U32(esi + 0x10u);
  BB_U32(edx + 0x66u) = eax;
  ecx = BB_U32(esi + 0x10u);
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044a206 */
  ecx = BB_U32(esi + 0x10u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sort_entry), 4); /* 0044a214 */
L_44a219:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44a240;
  ecx = BB_U32(esi + 0x20u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a239;
  eax = BB_U32(esi + 0x66u);
  edx = BB_U32(esi + 0x62u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_place), 8); /* 0044a232 */
  goto L_44a240;
L_44a239:;
  BYTE(esi + 0x11au) = 0x0u;
L_44a240:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xfdu)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = (ecx ^ ecx);
  eax = (eax - edx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  eax = (eax & 0xffu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xfdu) = ((uint8_t)(eax));
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  eax = (eax + edx * 8u);
  ebp = BB_U32(eax * 2u + 0x6635a2u);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a310;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x106u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a2e2;
  eax = BB_U32(esi + 0x9cu);
  a = ((uint8_t)(eax >> 8));
  b = 0x40u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a29f;
  BYTE(esi + 0xe1u) = 0x48u;
  goto L_44a2e2;
L_44a29f:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe2u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44a2e2;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe1u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a2da;
  a = ((uint8_t)(ecx));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xdeu)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xddu)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  BYTE(esi + 0xe2u) = ((uint8_t)(ecx));
  BYTE(esi + 0xe1u) = 0x48u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_move_escape), 8); /* 0044a2d3 */
  goto L_44a2e2;
L_44a2da:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe1u) = ((uint8_t)(eax));
L_44a2e2:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11du)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a2f5;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_path_step), 0); /* 0044a2ee */
  goto L_44a310;
L_44a2f5:;
  a = ((uint8_t)(eax));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a302;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_destination_step), 0); /* 0044a2fb */
  goto L_44a310;
L_44a302:;
  BB_U32(esi + 0x7au) = 0x0u;
  BB_U32(esi + 0x7eu) = 0x0u;
L_44a310:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a322;
  a = ((uint8_t)(eax));
  b = 0x68u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a48d;
L_44a322:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a48d;
  ecx = BB_U32(esi + 0x9cu);
  edx = BB_U32(esi + 0x6eu);
  ecx = (ecx & 0xfffbffffu);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  BB_U32(esi + 0x9cu) = ecx;
  if (!(flags & 0x40u))
    goto L_44a365;
  ecx = BB_U32(esi + 0x72u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44a365;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x18u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a365;
  a = ((uint8_t)(ecx));
  b = 0x1cu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a365;
  a = ((uint8_t)(ecx));
  b = 0x19u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a36b;
L_44a365:;
  BYTE(esi + 0xeau) = (BYTE(esi + 0xeau) + 1u);
L_44a36b:;
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a381;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(0xcu) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax & 0xffff00ffu) |
        (((uint32_t)((((uint8_t)(eax >> 8)) - ((uint8_t)(eax >> 8)))) & 0xffu)
         << 8);
  b = ((uint8_t)(ecx));
  a = (uint16_t)eax;
  if (!b || a / b > 255u)
    division_fault();
  result = (a / b) | ((a % b) << 8);
  eax = (eax & 0xffff0000u) | result;
  BYTE(esi + 0xeau) = ((uint8_t)(eax >> 8));
L_44a381:;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a48d;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44a48d;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_position), 0); /* 0044a39c */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_range_entry), 0); /* 0044a3a3 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a410;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11du)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a3ed;
  eax = BB_U32(esi + 0xcu);
  eax = BB_U32(eax + 0x28u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a3d1;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_entry), 12); /* 0044a3c7 */
  goto L_44a48d;
L_44a3d1:;
  args[--sp] = 0x7u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044a3d5 */
  BYTE(esi + 0xf9u) = 0x0u;
  BYTE(esi + 0xeau) = 0x0u;
  goto L_44a47c;
L_44a3ed:;
  eax = BB_U32(esi + 0x9cu);
  a = ((uint8_t)(eax >> 8));
  b = 0x20u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a48d;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_reset_move), 0); /* 0044a3fe */
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x2000u);
  goto L_44a487;
L_44a410:;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a48d;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_target_moved_entry), 0); /* 0044a419 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44a42c;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11du)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a48d;
L_44a42c:;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x8000001u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44a448;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_entry), 12); /* 0044a441 */
  goto L_44a47c;
L_44a448:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_position), 0); /* 0044a44a */
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(esi + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_approach_entry), 8); /* 0044a457 */
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf0u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a47c;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(0x4843f0u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a47c;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_target_building), 0); /* 0044a477 */
L_44a47c:;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax & 0xffffdfffu);
L_44a487:;
  BB_U32(esi + 0x9cu) = eax;
L_44a48d:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  eax = (eax + edx * 8u);
  ebp = BB_U32(eax * 2u + 0x663578u);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a4ee;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44a4c1;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  goto L_44a4ee;
L_44a4c1:;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x20000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a4ee;
  a = eax;
  b = 0xfffdffffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(esi + 0x9cu) = eax;
  ecx = BB_U32(0x484324u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044a4e2 */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044a4e9 */
L_44a4ee:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  edx = (ecx + 0xfffffffeu);
  a = edx;
  b = 0xe5u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(map_44f0dc[edx]) & 0xffu) << 0);
  switch (eax) {
  case 0:
    goto L_44a514;
  case 1:
    goto L_44a527;
  case 2:
    goto L_44a5c5;
  case 3:
    goto L_44a5e5;
  case 4:
    goto L_44a63b;
  case 5:
    goto L_44a6ab;
  case 6:
    goto L_44a742;
  case 7:
    goto L_44a7f9;
  case 8:
    goto L_44a850;
  case 9:
    goto L_44a8bc;
  case 10:
    goto L_44a904;
  case 11:
    goto L_44a949;
  case 12:
    goto L_44a9f5;
  case 13:
    goto L_44aafe;
  case 14:
    goto L_44ac6f;
  case 15:
    goto L_44ac9f;
  case 16:
    goto L_44acbf;
  case 17:
    goto L_44ef23;
  case 18:
    goto L_44ae52;
  case 19:
    goto L_44aeb2;
  case 20:
    goto L_44afde;
  case 21:
    goto L_44b142;
  case 22:
    goto L_44b1b8;
  case 23:
    goto L_44b33e;
  case 24:
    goto L_44b3e8;
  case 25:
    goto L_44b564;
  case 26:
    goto L_44b5ba;
  case 27:
    goto L_44b649;
  case 28:
    goto L_44b684;
  case 29:
    goto L_44b6c9;
  case 30:
    goto L_44ef23;
  case 31:
    goto L_44b736;
  case 32:
    goto L_44b8f3;
  case 33:
    goto L_44bb6a;
  case 34:
    goto L_44bb91;
  case 35:
    goto L_44bc7e;
  case 36:
    goto L_44ef23;
  case 37:
    goto L_44bcff;
  case 38:
    goto L_44cdb4;
  case 39:
    goto L_44d313;
  case 40:
    goto L_44d3c7;
  case 41:
    goto L_44d3f9;
  case 42:
    goto L_44d42b;
  case 43:
    goto L_44d45d;
  case 44:
    goto L_44d486;
  case 45:
    goto L_44def6;
  case 46:
    goto L_44e00a;
  case 47:
    goto L_44e02b;
  case 48:
    goto L_44e1fe;
  case 49:
    goto L_44e2e7;
  case 50:
    goto L_44e3ba;
  case 51:
    goto L_44ef23;
  case 52:
    goto L_44e1fe;
  case 53:
    goto L_44e48d;
  case 54:
    goto L_44ef23;
  case 55:
    goto L_44ef23;
  case 56:
    goto L_44ef23;
  case 57:
    goto L_44ef23;
  case 58:
    goto L_44ef23;
  case 59:
    goto L_44ef23;
  case 60:
    goto L_44e570;
  case 61:
    goto L_44e589;
  case 62:
    goto L_44e5e4;
  case 63:
    goto L_44ef23;
  case 64:
    goto L_44e708;
  case 65:
    goto L_44ef23;
  case 66:
    goto L_44e84b;
  case 67:
    goto L_44e874;
  case 68:
    goto L_44e89c;
  case 69:
    goto L_44e8e1;
  case 70:
    goto L_44e9bc;
  case 71:
    goto L_44ea8f;
  case 72:
    goto L_44ef23;
  case 73:
    goto L_44eb53;
  case 74:
    goto L_44ec42;
  case 75:
    goto L_44edea;
  case 76:
    goto L_44eeb0;
  case 77:
    goto L_44e570;
  case 78:
    goto L_44eec4;
  case 79:
    goto L_44eee6;
  case 80:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44a514:;
  eax = BB_U32(0x484768u);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  goto L_44ef23;
L_44a527:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a56a;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044a565 */
L_44a56a:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044a56c */
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x800000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44a585;
  WORD(esi + 0xa0u) = (WORD(esi + 0xa0u) - 1u);
L_44a585:;
  a = WORD(esi + 0xa0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef2a;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0xccu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044a5b0 */
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044a5bb */
  goto L_44ef2a;
L_44a5c5:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44a5e5:;
  eax = (eax ^ eax);
  ecx = BB_U32(0x6a904cu);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = 0x10u;
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044a631 */
  goto L_44ef23;
L_44a63b:;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x10000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef2a;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a684;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44a684;
  BYTE(esi + 0xf9u) = 0x1u;
L_44a684:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44a69b;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44a69b:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044a6a1 */
  goto L_44ef2a;
L_44a6ab:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a6bc;
  BYTE(esi + 0xf5u) = 0xffu;
L_44a6bc:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_44a6df;
  eax = (eax & 0xffffff00u) | (((uint32_t)(0x1u) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = 0xffu;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - ((uint8_t)(ecx)))) & 0xffu) << 0);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  goto L_44a6e7;
L_44a6df:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
L_44a6e7:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a6fd;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44a718;
  goto L_44ef2a;
L_44a6fd:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44a718:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x18u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44a72f;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44a72f:;
  if (!(flags & 1u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44a742:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44a76f;
  a = eax;
  b = 0x19u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44a79a;
  a = eax;
  b = 0x1au;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44a7ed;
  goto L_44ef23;
L_44a76f:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x19u;
  esp -= 4u;
  ecx = esi;
  BYTE(esi + 0xf9u) = 0x0u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044a790 */
  goto L_44ef23;
L_44a79a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x24u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x1au;
  esp -= 4u;
  ecx = esi;
  BYTE(esi + 0xf9u) = 0x0u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044a7bb */
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x62u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi + 0x66u);
  a = eax;
  b = 0x10000u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x67u;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x4u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044a7da */
  args[--sp] = 0x12u;
  esp -= 4u;
  ecx = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_profession), 4); /* 0044a7e3 */
  goto L_44ef23;
L_44a7ed:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44a7f9:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = 0xau;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44a83d;
  args[--sp] = 0x78u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044a838 */
L_44a83d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44a850:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x12u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0xfu;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a8a2;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044a870 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x14u;
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
  edx = (edx + 0xau);
  eax = (eax ^ eax);
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x553f90u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044a89d */
L_44a8a2:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044a8b2 */
  goto L_44ef23;
L_44a8bc:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x3fu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x3cu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44a8df;
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a8e6;
L_44a8df:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044a8e1 */
L_44a8e6:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x18u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x1u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044a8fa */
  goto L_44ef2a;
L_44a904:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44a92b;
  BYTE(esi + 0xf9u) = 0x10u;
L_44a92b:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(eax)) - 1u)) & 0xffffu) << 0);
  WORD(esi + 0xa0u) = ((uint16_t)(eax));
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
  goto L_44ef18;
L_44a949:;
  BB_U32(esi + 0x7au) = 0x0u;
  BB_U32(esi + 0x7eu) = 0x0u;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44ef23;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044a968 */
  args[--sp] = 0xc8u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044a98e */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044a9b0 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044a9cf */
  ecx = BB_U32(0x4843a4u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044a9dd */
  ecx = BB_U32(0x484330u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044a9eb */
  goto L_44ef23;
L_44a9f5:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44aa14;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44aa54;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44aae5;
  goto L_44ef23;
L_44aa14:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44aa2b;
  eax = BB_U32(0x4843c8u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044aa26 */
L_44aa2b:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  BYTE(esi + 0xf9u) = 0x0u;
  BYTE(esi + 0xeau) = 0x1u;
  goto L_44ef23;
L_44aa54:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x5u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44aac4;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044aa80 */
  ecx = BB_U32(0x484330u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044aa8e */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);       /* 0044aa93 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x64u;
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
  edx = (edx + ecx);
  eax = (eax ^ eax);
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x553f90u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044aabf */
L_44aac4:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xau;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  BYTE(esi + 0xf9u) = 0xfu;
  BYTE(esi + 0xeau) = 0x2u;
  goto L_44ef23;
L_44aae5:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44aafe:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ab1d;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ab4d;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ac56;
  goto L_44ef2a;
L_44ab1d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  BYTE(esi + 0xf9u) = 0x0u;
  BYTE(esi + 0xeau) = 0x1u;
  BYTE(esi + 0xf5u) = 0x14u;
  goto L_44ef23;
L_44ab4d:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x17u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef23;
  BYTE(esi + 0xf9u) = 0x0u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44aba0;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044ab8d */
  BYTE(esi + 0xeau) = 0x2u;
  BYTE(esi + 0xf9u) = 0xfu;
L_44aba0:;
  edi = (edi ^ edi);
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = 0x553f90u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044abbb */
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ac3c;
L_44abc6:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44abfa;
  eax = BB_U32(ebx + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44abfa;
  a = eax;
  b = 0xfff7ffffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = ebx;
  edi = 0x1u;
  BB_U32(ebx + 0x9cu) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044abf5 */
L_44abfa:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x7u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ac17;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = ebx;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  edi = 0x1u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044ac12 */
L_44ac17:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044ac31 */
  ebx = eax;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44abc6;
L_44ac3c:;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef23;
  eax = BB_U32(0x484318u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044ac4c */
  goto L_44ef23;
L_44ac56:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44ac6f:;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ac6f */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x3u;
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
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ac84 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(0x4846b4u) = eax;
  goto L_44ef2a;
L_44ac9f:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  a = ecx;
  b = ecx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  goto L_44ef23;
L_44acbf:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax - 1u);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  switch (eax) {
  case 0:
    goto L_44acd8;
  case 1:
    goto L_44ad6e;
  case 2:
    goto L_44ade0;
  case 3:
    goto L_44ae19;
  default:
    __builtin_unreachable();
  }
L_44acd8:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44acf6;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44acf6:;
  a = ((uint8_t)(eax));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ad06;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44ef23;
L_44ad06:;
  a = ((uint8_t)(eax));
  b = 0x30u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ad16;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44ef23;
L_44ad16:;
  a = ((uint8_t)(eax));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ad2c;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) & 0x4u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 2)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x3u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef23;
L_44ad2c:;
  a = ((uint8_t)(eax));
  b = 0xc4u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ad42;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) & 0x4u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 2)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x5u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef23;
L_44ad42:;
  a = ((uint8_t)(eax));
  b = 0xd2u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ad52;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44ef23;
L_44ad52:;
  a = ((uint8_t)(eax));
  b = 0xfau;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ad62;
  BYTE(esi + 0xf9u) = 0x8u;
  goto L_44ef23;
L_44ad62:;
  BYTE(esi + 0xeau) = 0x0u;
  goto L_44ef23;
L_44ad6e:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ad9e;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ad80 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044ad99 */
L_44ad9e:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x24u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44adc6;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ada8 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044adc1 */
L_44adc6:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x50u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  BYTE(esi + 0xeau) = 0x0u;
  goto L_44ef23;
L_44ade0:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x32u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44adff;
  eax = BB_U32(0x484334u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044adfa */
L_44adff:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x64u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef2a;
  BYTE(esi + 0xeau) = 0x0u;
  goto L_44ef23;
L_44ae19:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x32u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ae38;
  eax = BB_U32(0x6aff5cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044ae33 */
L_44ae38:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x64u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef2a;
  BYTE(esi + 0xeau) = 0x0u;
  goto L_44ef23;
L_44ae52:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x64u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ae77;
  eax = BB_U32(0x4843ccu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044ae72 */
L_44ae77:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 2)) & 0xffu) << 0);
  BYTE(esi + 0xe5u) = ((uint8_t)(ecx));
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x5u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  BYTE(esi + 0xf9u) = 0x0u;
  BYTE(esi + 0xe5u) = 0x0u;
  BYTE(esi + 0xf2u) = 0x0u;
  goto L_44ef23;
L_44aeb2:;
  eax = BB_U32(esi + 0x7au);
  ecx = BB_U32(esi + 0x7eu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  BB_U32(esi + 0x6eu) = eax;
  BB_U32(esi + 0x72u) = ecx;
  a = ((uint8_t)(edx));
  b = 0x46u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44af61;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_44af61;
  BB_U32(esi + 0x7au) = 0x0u;
  BB_U32(esi + 0x7eu) = 0x0u;
  BB_U32(esi + 0x6eu) = 0x0u;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx << 6);
  BB_U32(esi + 0x76u) = 0x0u;
  ecx = (ecx + ecx * 2u);
  args[--sp] = 0x5bu;
  esp -= 4u;
  ecx = (ecx + eax);
  eax = (eax ^ eax);
  BB_U32(esi + 0x72u) = 0x0u;
  edx = (ecx + ecx * 2u);
  ecx = esi;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b65u)) & 0xffu) << 0);
  a = eax;
  result = (a << 16) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 16, 0);
  eax = result;
  BB_U32(esi + 0x6au) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044af29 */
  BYTE(esi + 0xf5u) = 0x48u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044af5c */
L_44af61:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x5bu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44af90;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044af86 */
  goto L_44ef23;
L_44af90:;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = 0xffffc180u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_44ef23;
  a = eax;
  b = 0xfffff000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44afaf;
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44afd0;
L_44afaf:;
  a = eax;
  b = 0x1000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44afc0;
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44afd0;
L_44afc0:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44ef23;
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
L_44afd0:;
  args[--sp] = 0x46u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044afd4 */
  goto L_44ef23;
L_44afde:;
  eax = BB_U32(esi + 0x7au);
  ecx = BB_U32(esi + 0x7eu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  BB_U32(esi + 0x6eu) = eax;
  BB_U32(esi + 0x72u) = ecx;
  a = ((uint8_t)(edx));
  b = 0x46u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44b0af;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44b0af;
  BB_U32(esi + 0x7au) = 0x0u;
  BB_U32(esi + 0x7eu) = 0x0u;
  BB_U32(esi + 0x6eu) = 0x0u;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx << 6);
  BB_U32(esi + 0x76u) = 0x0u;
  ecx = (ecx + ecx * 2u);
  args[--sp] = 0x5bu;
  esp -= 4u;
  ecx = (ecx + eax);
  eax = (eax ^ eax);
  BB_U32(esi + 0x72u) = 0x0u;
  edx = (ecx + ecx * 2u);
  ecx = esi;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b65u)) & 0xffu) << 0);
  a = eax;
  result = (a << 16) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 16, 0);
  eax = result;
  BB_U32(esi + 0x6au) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b055 */
  BYTE(esi + 0xf5u) = 0x48u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044b088 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044b0aa */
L_44b0af:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x5bu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44b0de;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044b0d4 */
  goto L_44ef23;
L_44b0de:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax + 0x10u);
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x1fu);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 5));
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x7u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = 0xffffc180u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44b11a;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44b11a:;
  a = eax;
  b = 0xfffff000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44ef23;
  BYTE(esi + 0xf9u) = 0x0u;
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x46u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b138 */
  goto L_44ef23;
L_44b142:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = 0x37u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44b159;
  a = eax;
  b = 0x38u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44b19f;
  goto L_44ef2a;
L_44b159:;
  ecx = BB_U32(esi + 0x58u);
  eax = (eax ^ eax);
  ecx = (ecx + 0x4cu);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (edx - edx);
  edi = BB_U32(ecx);
  edi = (edi << 2);
  b = edi;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = BB_U32(ecx);
  ecx = esi;
  a = ((uint8_t)(eax));
  result = (a << 2) & 0xffu;
  flags = shift_flags(flags, a, result, 8, 2, 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044b18e */
  BYTE(esi + 0xf5u) = 0x40u;
  goto L_44ef23;
L_44b19f:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44b1b8:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  eax = (eax - 0x1bu);
  a = eax;
  b = 0x5cu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f1f0[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44b1db;
  case 1:
    goto L_44b1f6;
  case 2:
    goto L_44b211;
  case 3:
    goto L_44b263;
  case 4:
    goto L_44b29a;
  case 5:
    goto L_44b2ec;
  case 6:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44b1db:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x13u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ef2a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef23;
L_44b1f6:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x27u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ef2a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef23;
L_44b211:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = BB_U32(0x66352cu);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x4cu);
  eax = (eax << 2);
  eax = (eax - 1u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_44b23f;
  BYTE(esi + 0xf5u) = 0xffu;
  a = ((uint8_t)(ecx));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  goto L_44ef23;
L_44b23f:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044b259 */
  goto L_44ef2a;
L_44b263:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = BB_U32(0x602274u);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x4cu);
  eax = (eax << 2);
  eax = (eax - 1u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_44b28a;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  goto L_44ef2a;
L_44b28a:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044b290 */
  goto L_44ef2a;
L_44b29a:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = BB_U32(0x5d1138u);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x4cu);
  eax = (eax << 2);
  eax = (eax - 1u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_44b2c8;
  BYTE(esi + 0xf5u) = 0xffu;
  a = ((uint8_t)(ecx));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  goto L_44ef23;
L_44b2c8:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044b2e2 */
  goto L_44ef2a;
L_44b2ec:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = BB_U32(0x63f050u);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x4cu);
  eax = (eax << 2);
  eax = (eax - 1u);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_44b31a;
  BYTE(esi + 0xf5u) = 0xffu;
  a = ((uint8_t)(ecx));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  goto L_44ef23;
L_44b31a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044b334 */
  goto L_44ef2a;
L_44b33e:;
  eax = BB_U32(esi + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044b389 */
  ecx = BB_U32(esi + 0x6au);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044b392 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = BB_U32(esi + 0x66u);
  eax = (eax - edx);
  eax = (eax & 0xffffu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  ecx = (ecx + eax);
  a = ecx;
  b = 0x8000u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ecx = result;
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044b3b1 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = BB_U32(esi + 0x62u);
  eax = (eax - edx);
  eax = (eax & 0xffffu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  ecx = (ecx + eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ecx;
  b = 0x7fffu;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ecx = result;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044b3de */
  goto L_44ef2a;
L_44b3e8:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44b43d;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44b44d;
  eax = BB_U32(esi + 0x6au);
  eax = (eax + 0x7fffu);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi + 0x66u);
  eax = (eax + 0x400u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  a = eax;
  b = 0x400u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x38u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044b425 */
  ecx = esi;
  BB_U32(esi + 0x10u) = eax;
  BYTE(esi + 0xf9u) = 0x0u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044b436 */
  goto L_44b44d;
L_44b43d:;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44b44d;
  args[--sp] = 0x3au;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b448 */
L_44b44d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x3au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44b528;
  eax = BB_U32(esi + 0x10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44b528;
  ecx = BB_U32(esi + 0xcu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44b528;
  edx = (edx ^ edx);
  ecx = (ecx ^ ecx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  edx = (edx - ecx);
  ecx = (ecx ^ ecx);
  edx = (edx << 6);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  edx = (edx - ecx);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  ecx = (ecx + edx);
  edx =
      (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0x5fda98u)) & 0xffu) << 0);
  BYTE(esi + 0xe5u) = ((uint8_t)(edx));
  edi = BB_U32(ecx * 4u + 0x5fb2f0u);
  BB_U32(eax + 0x76u) = 0x20000u;
  args[--sp] = edi;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  ecx = BB_U32(eax * 4u + 0x5d1150u);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044b4db */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(ecx + 0x7au) = eax;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  ecx = BB_U32(eax * 4u + 0x5d1558u);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044b4fa */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x10u);
  BB_U32(ecx + 0x7eu) = eax;
  eax = BB_U32(esi + 0x10u);
  edx = BB_U32(eax + 0x7au);
  BB_U32(eax + 0x6eu) = edx;
  eax = BB_U32(esi + 0x10u);
  ecx = BB_U32(eax + 0x7eu);
  BB_U32(eax + 0x72u) = ecx;
  ecx = esi;
  edx = BB_U32(0x484388u);
  args[--sp] = edx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044b523 */
L_44b528:;
  eax = BB_U32(esi + 0x58u);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = BB_U32(eax + 0x4cu);
  eax = (eax + eax);
  eax = (eax - ecx);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_44ef23;
  BYTE(esi + 0xf5u) = 0xffu;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044b54c */
  BYTE(esi + 0xe3u) = 0x0u;
  BYTE(esi + 0xe4u) = 0x0u;
  goto L_44ef23;
L_44b564:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx + ecx);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44b5a7;
  args[--sp] = 0xau;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044b5a2 */
L_44b5a7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44b5ba:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x58u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  ecx = (ecx - 1u);
  a = ecx;
  b = 0x28u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(map_44f26c[ecx]) & 0xffu) << 0);
  switch (eax) {
  case 0:
    goto L_44b5f3;
  case 1:
    goto L_44b5ff;
  case 2:
    goto L_44b60b;
  case 3:
    goto L_44b617;
  case 4:
    goto L_44b623;
  case 5:
    goto L_44b62f;
  case 6:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44b5f3:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44b5ff:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44ef23;
L_44b60b:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44ef23;
L_44b617:;
  BYTE(esi + 0xf9u) = 0x3u;
  goto L_44ef23;
L_44b623:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44b62f:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044b631 */
  BYTE(esi + 0xf5u) = 0x0u;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44ef23;
L_44b649:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x15u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = BB_U32(esi + 0x10u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44b676;
  ecx = 0x4098u;
  args[--sp] = 0x17u;
  esp -= 4u;
  BB_U32(eax + 0x7eu) = ecx;
  eax = BB_U32(esi + 0x10u);
  BB_U32(eax + 0x72u) = ecx;
  ecx = BB_U32(esi + 0x10u);
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b671 */
L_44b676:;
  args[--sp] = 0x16u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b67a */
  goto L_44ef23;
L_44b684:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x17u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044b694 */
  eax = BB_U32(esi + 0x6au);
  eax = (eax - 0x800u);
  BB_U32(esi + 0x6au) = eax;
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_44ef2a;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044b6b2 */
  eax = BB_U32(0x48434cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044b6bf */
  goto L_44ef2a;
L_44b6c9:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44b6e5;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44b711;
  a = eax;
  b = 0x5u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44b72a;
  goto L_44ef2a;
L_44b6e5:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(0x5u) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(0x24u) & 0xffu) << 0);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  eax = (eax & 0xffff00ffu) |
        (((uint32_t)((((uint8_t)(eax >> 8)) - ((uint8_t)(eax >> 8)))) & 0xffu)
         << 8);
  b = ((uint8_t)(edx));
  a = (uint16_t)eax;
  if (!b || a / b > 255u)
    division_fault();
  result = (a / b) | ((a % b) << 8);
  eax = (eax & 0xffff0000u) | result;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) - ((uint8_t)(eax)))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  a = ((uint8_t)(ecx));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ef23;
  goto L_44ef18;
L_44b711:;
  eax = BB_U32(esi + 0x2cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b720 */
  goto L_44ef23;
L_44b72a:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef2a;
L_44b736:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44b75a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44b777;
  BYTE(esi + 0xf9u) = 0x0u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044b753 */
  goto L_44b777;
L_44b75a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44b76e;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44b777;
L_44b76e:;
  args[--sp] = 0x3bu;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044b772 */
L_44b777:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44b791;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  ebp = (esi + 0xe4u);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44b7c4;
L_44b791:;
  ebp = (esi + 0xe4u);
  ecx = (ecx ^ ecx);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  ecx = (ecx - edx);
  edx = (edx ^ edx);
  ecx = (ecx << 6);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx - edx);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + edx + 0x5fda98u)) & 0xffu) << 0);
  BYTE(esi + 0xe7u) = ((uint8_t)(eax));
L_44b7c4:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44b7eb;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_direction_step_entry), 0); /* 0044b7d6 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = esi;
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044b7e6 */
L_44b7eb:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x3bu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx - eax);
  eax = (eax ^ eax);
  ecx = (ecx << 6);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx - eax);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  ecx = (ecx + eax);
  eax = BB_U32(esi + 0x6au);
  eax = (eax + 0x40000u);
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(ecx * 4u + 0x5fb2f0u);
  ebx = (ebx << 2);
  eax = BB_U32(esi + 0x66u);
  eax = (eax + 0x8000u);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  a = eax;
  b = 0x8000u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x37u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044b87c */
  edi = eax;
  args[--sp] = ebx;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  ecx = BB_U32(eax * 4u + 0x5d1150u);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044b894 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x7au) = eax;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(eax * 4u + 0x5d1558u);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044b8b0 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x7eu) = eax;
  ecx = BB_U32(edi + 0x7au);
  BB_U32(edi + 0x6eu) = ecx;
  ecx = esi;
  eax = BB_U32(edi + 0x7eu);
  BB_U32(edi + 0x72u) = eax;
  edx = BB_U32(0x48438cu);
  args[--sp] = edx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044b8d0 */
  BYTE(esi + 0xf5u) = 0x24u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044b8de */
  BYTE(esi + 0xe3u) = 0x0u;
  BYTE(ebp) = 0x0u;
  goto L_44ef23;
L_44b8f3:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bb5d;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 1)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx & 0xfffffffcu);
  ebx = BB_U32(ecx + 0x484580u);
  eax = BB_U32(ecx + 0x484600u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) + BYTE(esi + 0xd8u))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + BYTE(esi + 0xd9u))) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  edx = (edx ^ edx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  ecx = (ecx << 6);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + edx);
  ebp = (ecx + ecx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44b9aa;
L_44b94c:;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax + 0x40u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xffu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  edx = (edx ^ edx);
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 1)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx & 0xfffffffcu);
  ebx = BB_U32(ecx + 0x484580u);
  eax = BB_U32(ecx + 0x484600u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ebx)) + BYTE(esi + 0xd8u))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + BYTE(esi + 0xd9u))) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx << 6);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + edx);
  ebp = (ecx + ecx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44b94c;
L_44b9aa:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = esi;
  args[--sp] = ebx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_place_tile), 8); /* 0044b9ae */
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x55u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044b9cd */
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44b9e5;
  BYTE(eax + 0xf9u) = 0x2u;
  goto L_44ba0d;
L_44b9e5:;
  a = ((uint8_t)(ecx));
  b = 0x40u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44b9f3;
  BYTE(eax + 0xf9u) = 0x3u;
  goto L_44ba0d;
L_44b9f3:;
  a = ((uint8_t)(ecx));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ba01;
  BYTE(eax + 0xf9u) = 0x0u;
  goto L_44ba0d;
L_44ba01:;
  a = ((uint8_t)(ecx));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ba0d;
  BYTE(eax + 0xf9u) = 0x1u;
L_44ba0d:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ecx;
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44bb1c;
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(map_44f2ac[ecx]) & 0xffu) << 0);
  switch (edx) {
  case 0:
    goto L_44ba30;
  case 1:
    goto L_44ba7d;
  case 2:
    goto L_44baad;
  case 3:
    goto L_44baea;
  case 4:
    goto L_44bb1c;
  default:
    __builtin_unreachable();
  }
L_44ba30:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ba46;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44bb1c;
L_44ba46:;
  a = ((uint8_t)(ecx));
  b = 0x40u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ba57;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44bb1c;
L_44ba57:;
  a = ((uint8_t)(ecx));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ba68;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44bb1c;
L_44ba68:;
  a = ((uint8_t)(ecx));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bb1c;
  BYTE(esi + 0xf9u) = 0x8u;
  goto L_44bb1c;
L_44ba7d:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ba93;
  BYTE(esi + 0xf9u) = 0x9u;
  goto L_44bb1c;
L_44ba93:;
  a = ((uint8_t)(ecx));
  b = 0x40u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44bb15;
  a = ((uint8_t)(ecx));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44baa6;
  BYTE(esi + 0xf9u) = 0x8u;
  goto L_44bb1c;
L_44baa6:;
  a = ((uint8_t)(ecx));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bb1c;
  goto L_44bb15;
L_44baad:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bac0;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44bb1c;
L_44bac0:;
  a = ((uint8_t)(ecx));
  b = 0x40u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bace;
  BYTE(esi + 0xf9u) = 0x6u;
  goto L_44bb1c;
L_44bace:;
  a = ((uint8_t)(ecx));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44badc;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44bb1c;
L_44badc:;
  a = ((uint8_t)(ecx));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bb1c;
  BYTE(esi + 0xf9u) = 0x9u;
  goto L_44bb1c;
L_44baea:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bafd;
  BYTE(esi + 0xf9u) = 0x6u;
  goto L_44bb1c;
L_44bafd:;
  a = ((uint8_t)(ecx));
  b = 0x40u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44bb15;
  a = ((uint8_t)(ecx));
  b = 0x80u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bb10;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44bb1c;
L_44bb10:;
  a = ((uint8_t)(ecx));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bb1c;
L_44bb15:;
  BYTE(esi + 0xf9u) = 0x5u;
L_44bb1c:;
  ecx = BB_U32(esi + 0x10u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44bb2f;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  BYTE(ecx + 0xf9u) = ((uint8_t)(edx));
L_44bb2f:;
  BYTE(esi + 0xf5u) = 0x4u;
  BB_U32(esi + 0x10u) = eax;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xe9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef2a;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044bb53 */
  goto L_44ef2a;
L_44bb5d:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44bb6a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bb84;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044bb7a */
  goto L_44ef2a;
L_44bb84:;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  goto L_44ef2a;
L_44bb91:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  eax = (eax - 0x2du);
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44bc72;
  switch (eax) {
  case 0:
    goto L_44bbac;
  case 1:
    goto L_44bbdd;
  case 2:
    goto L_44bbfa;
  case 3:
    goto L_44bc1b;
  case 4:
    goto L_44bc45;
  default:
    __builtin_unreachable();
  }
L_44bbac:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x7u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bc6b;
  args[--sp] = 0x2eu;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044bbd3 */
  goto L_44bc6b;
L_44bbdd:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x7u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44bc6b;
L_44bbfa:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x7u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bc6b;
  goto L_44bc64;
L_44bc1b:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bc6b;
  args[--sp] = 0x31u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044bc3e */
  goto L_44bc6b;
L_44bc45:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x7fu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bc6b;
L_44bc64:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044bc66 */
L_44bc6b:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044bc6d */
L_44bc72:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_path_step), 0); /* 0044bc74 */
  goto L_44ef2a;
L_44bc7e:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x14u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = BB_U32(0x60463cu);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x4cu);
  eax = (eax + eax);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_44bcec;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  a = ((uint8_t)(ecx));
  b = 0xau;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044bcb5 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x28u;
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
  edx = (edx + 0x3cu);
  eax = (eax ^ eax);
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x553f90u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044bce2 */
  goto L_44ef23;
L_44bcec:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044bcee */
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44bcff:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = 0x75u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f3c8[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44bd1f;
  case 1:
    goto L_44bd5d;
  case 2:
    goto L_44be50;
  case 3:
    goto L_44bf34;
  case 4:
    goto L_44bfc7;
  case 5:
    goto L_44c04e;
  case 6:
    goto L_44c49b;
  case 7:
    goto L_44c592;
  case 8:
    goto L_44c655;
  case 9:
    goto L_44c774;
  case 10:
    goto L_44c7e8;
  case 11:
    goto L_44c866;
  case 12:
    goto L_44c8ae;
  case 13:
    goto L_44c940;
  case 14:
    goto L_44cc17;
  case 15:
    goto L_44cd36;
  case 16:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44bd1f:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(ecx));
  eax = edx;
  edx = (edx << 3);
  edx = (edx - eax);
  a = BYTE(edx + edx * 4u + 0x6aff88u);
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  BYTE(esi + 0xf5u) = 0x0u;
  goto L_44ef2a;
L_44bd5d:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  ecx = BB_U32(0x604628u);
  eax = (eax + 1u);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bdad;
  eax = BB_U32(0x48436cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044bda8 */
L_44bdad:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044bdbd */
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x3au;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044bddc */
  args[--sp] = 0x1eu;
  esp -= 4u;
  ecx = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044bde5 */
  ecx = BB_U32(0x5c78e4u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044bdf3 */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);               /* 0044bdf8 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_100_entry), 4); /* 0044be0f */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);                  /* 0044be14 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_101_entry), 4); /* 0044be28 */
  args[--sp] = 0x0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044be2f */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044be46 */
  goto L_44ef23;
L_44be50:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  ecx = BB_U32(0x5133f4u);
  eax = (eax + 1u);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = 0x8u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44bea0;
  eax = BB_U32(0x69e368u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044be9b */
L_44bea0:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44beb7;
  eax = BB_U32(0x48436cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044beb2 */
L_44beb7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044bec7 */
  a = BB_U32(esi + 0x9cu);
  b = 0xffffbfffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  BB_U32(esi + 0x9cu) = result;
  ecx = BB_U32(0x5c78e4u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044bedf */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);               /* 0044bee4 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_100_entry), 4); /* 0044befb */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);                  /* 0044bf00 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0xcu;
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
  args[--sp] = edx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_101_entry), 4); /* 0044bf10 */
  args[--sp] = 0x0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044bf17 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0xcu;
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
  a = edx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edx = result;
  ecx = esi;
  args[--sp] = edx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044bf2a */
  goto L_44ef23;
L_44bf34:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  ecx = BB_U32(0x5e4780u);
  eax = (eax + 1u);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(0x14u) & 0xffu) << 0);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(edx))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  eax = (eax & 0xffff00ffu) |
        (((uint32_t)((((uint8_t)(eax >> 8)) - ((uint8_t)(eax >> 8)))) & 0xffu)
         << 8);
  b = ((uint8_t)(ebx));
  a = (uint16_t)eax;
  if (!b || a / b > 255u)
    division_fault();
  result = (a / b) | ((a % b) << 8);
  eax = (eax & 0xffff0000u) | result;
  a = ((uint8_t)(eax >> 8));
  b = ((uint8_t)(eax >> 8));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44bf8b;
  eax = BB_U32(0x484374u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044bf86 */
L_44bf8b:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044bf9b */
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x83u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044bfbd */
  goto L_44ef23;
L_44bfc7:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  ecx = BB_U32(0x6ad1a4u);
  eax = (eax + 1u);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44c02e;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44c02e;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c01b */
  ecx = BB_U32(esi + 0x2cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c02e;
  args[--sp] = 0x5u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044c029 */
L_44c02e:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = BB_U32(0x484378u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c044 */
  goto L_44ef23;
L_44c04e:;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c486;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44c06a;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_begin_entry), 0); /* 0044c065 */
L_44c06a:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  eax = (eax - 1u);
  a = eax;
  b = 0x12u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f460[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c08b;
  case 1:
    goto L_44c0fa;
  case 2:
    goto L_44c1c3;
  case 3:
    goto L_44c226;
  case 4:
    goto L_44c312;
  case 5:
    goto L_44c340;
  case 6:
    goto L_44c40b;
  case 7:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c08b:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0x14u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f490[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c0ab;
  case 1:
    goto L_44c0b7;
  case 2:
    goto L_44c0c3;
  case 3:
    goto L_44c0cf;
  case 4:
    goto L_44c0e2;
  case 5:
    goto L_44c0ee;
  case 6:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c0ab:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c0b7:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44c450;
L_44c0c3:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44c450;
L_44c0cf:;
  BYTE(esi + 0xf9u) = 0x3u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c0d8 */
  goto L_44c450;
L_44c0e2:;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44c450;
L_44c0ee:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c0fa:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xau;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f4c4[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c11a;
  case 1:
    goto L_44c126;
  case 2:
    goto L_44c155;
  case 3:
    goto L_44c168;
  case 4:
    goto L_44c194;
  case 5:
    goto L_44c1a0;
  case 6:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c11a:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c126:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x1u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x72u) = eax;
  goto L_44c450;
L_44c155:;
  BYTE(esi + 0xf9u) = 0x2u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c15e */
  goto L_44c450;
L_44c168:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44c449;
L_44c194:;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44c450;
L_44c1a0:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  BYTE(esi + 0xf9u) = 0x5u;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044c1b9 */
  goto L_44c450;
L_44c1c3:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0x14u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f4ec[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c1e3;
  case 1:
    goto L_44c1ef;
  case 2:
    goto L_44c1fb;
  case 3:
    goto L_44c449;
  case 4:
    goto L_44c207;
  case 5:
    goto L_44c21a;
  case 6:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c1e3:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c1ef:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44c450;
L_44c1fb:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44c450;
L_44c207:;
  BYTE(esi + 0xf9u) = 0x4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c210 */
  goto L_44c450;
L_44c21a:;
  BYTE(esi + 0xf9u) = 0x5u;
  goto L_44c450;
L_44c226:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f528[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c246;
  case 1:
    goto L_44c25f;
  case 2:
    goto L_44c26b;
  case 3:
    goto L_44c277;
  case 4:
    goto L_44c2a6;
  case 5:
    goto L_44c2b9;
  case 6:
    goto L_44c2ec;
  case 7:
    goto L_44c2f8;
  case 8:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c246:;
  BYTE(esi + 0xf9u) = 0x0u;
  eax = BB_U32(0x484384u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c255 */
  goto L_44c450;
L_44c25f:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44c450;
L_44c26b:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44c450;
L_44c277:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x3u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 3);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 3);
  BB_U32(esi + 0x72u) = eax;
  goto L_44c450;
L_44c2a6:;
  BYTE(esi + 0xf9u) = 0x4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c2af */
  goto L_44c450;
L_44c2b9:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x5u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 3);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 3);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44c450;
L_44c2ec:;
  BYTE(esi + 0xf9u) = 0x6u;
  goto L_44c450;
L_44c2f8:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44c450;
L_44c312:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c328;
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44c334;
  goto L_44c450;
L_44c328:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c334:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c336 */
  goto L_44c450;
L_44c340:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xau;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f554[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c360;
  case 1:
    goto L_44c379;
  case 2:
    goto L_44c3a8;
  case 3:
    goto L_44c3bb;
  case 4:
    goto L_44c3eb;
  case 5:
    goto L_44c3f4;
  case 6:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c360:;
  eax = BB_U32(0x484384u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c368 */
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c379:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x1u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 3);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 3);
  BB_U32(esi + 0x72u) = eax;
  goto L_44c450;
L_44c3a8:;
  BYTE(esi + 0xf9u) = 0x2u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c3b1 */
  goto L_44c450;
L_44c3bb:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x3u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 3);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 3);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44c450;
L_44c3eb:;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44c450;
L_44c3f4:;
  BYTE(esi + 0xf9u) = 0x5u;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  goto L_44c450;
L_44c40b:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44c450;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f574[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44c427;
  case 1:
    goto L_44c430;
  case 2:
    goto L_44c439;
  case 3:
    goto L_44c449;
  case 4:
    goto L_44c450;
  default:
    __builtin_unreachable();
  }
L_44c427:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44c450;
L_44c430:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44c450;
L_44c439:;
  BYTE(esi + 0xf9u) = 0x2u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044c442 */
  goto L_44c450;
L_44c449:;
  BYTE(esi + 0xf9u) = 0x3u;
L_44c450:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  edx = (edx ^ edx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  eax = edx;
  BYTE(esi + 0xf5u) = ((uint8_t)(ecx));
  edx = (edx << 3);
  edx = (edx - eax);
  a = BYTE(edx + edx * 4u + 0x6aff88u);
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_44ef23;
  BYTE(esi + 0xf5u) = 0x0u;
  goto L_44ef23;
L_44c486:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044c48a */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_enemy_command), 0); /* 0044c491 */
  goto L_44ef23;
L_44c49b:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = ((uint8_t)(ecx));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44c4f9;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (ecx + 0x1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
L_44c4f9:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44c578;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c519 */
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef23;
  args[--sp] = 0x3du;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044c52b */
  eax = BB_U32(esi + 0x14u);
  eax = BB_U32(eax + 0x1cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c56c;
  BYTE(eax + 0xeau) = (BYTE(eax + 0xeau) - 1u);
  eax = BB_U32(esi + 0x14u);
  eax = BB_U32(eax + 0x1cu);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xeau)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44c56c;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x7au;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44c56c;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x554fa8u);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 0044c567 */
L_44c56c:;
  BB_U32(esi + 0x14u) = 0x0u;
  goto L_44ef23;
L_44c578:;
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = BB_U32(0x484360u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c588 */
  goto L_44ef23;
L_44c592:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = ((uint8_t)(ecx));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44c5f0;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (ecx + 0x1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
L_44c5f0:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44c643;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c610 */
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef23;
  args[--sp] = 0x3fu;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044c622 */
  eax = BB_U32(esi + 0x14u);
  eax = BB_U32(eax + 0x1cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c637;
  a = BYTE(eax + 0xeau);
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  BYTE(eax + 0xeau) = result;
L_44c637:;
  BB_U32(esi + 0x14u) = 0x0u;
  goto L_44ef23;
L_44c643:;
  eax = BB_U32(0x484380u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c64b */
  goto L_44ef23;
L_44c655:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = ((uint8_t)(ecx));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44c6b3;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (ecx + 0x1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
L_44c6b3:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44c75a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c6d7 */
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef23;
  args[--sp] = 0x42u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044c6e9 */
  eax = BB_U32(esi + 0x14u);
  eax = BB_U32(eax + 0x1cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c74e;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xeau)) & 0xffu) << 0);
  edx = (edx ^ edx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) - 1u)) & 0xffu) << 0);
  BYTE(eax + 0xeau) = ((uint8_t)(ecx));
  eax = BB_U32(esi + 0x14u);
  ecx = BB_U32(eax + 0x1cu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd4u)) & 0xffu) << 0);
  eax = edx;
  edx = (edx << 3);
  edx = (edx - eax);
  ebx = (eax + edx * 8u);
  ebp = (eax + ebx * 8u);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd9u)) & 0xffu) << 0);
  edx = BB_U32(ebp * 2u + 0x6638b2u);
  eax = (ebp * 2u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(edx)) + ((uint8_t)(ebx)))) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(eax + 0x6638aeu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd8u)) & 0xffu) << 0);
  ecx = esi;
  a = ((uint8_t)(eax));
  b = ((uint8_t)(edx));
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_move_escape), 8); /* 0044c749 */
L_44c74e:;
  BB_U32(esi + 0x14u) = 0x0u;
  goto L_44ef23;
L_44c75a:;
  a = ((uint8_t)(eax));
  b = 0x8u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = BB_U32(0x48437cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c76a */
  goto L_44ef23;
L_44c774:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  ecx = BB_U32(0x5c78e0u);
  eax = (eax + 1u);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44c7d6;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c7cc */
  goto L_44ef23;
L_44c7d6:;
  eax = BB_U32(0x484360u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c7de */
  goto L_44ef23;
L_44c7e8:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44c84b;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef23;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0xa2u)) & 0xffffu) << 0);
  a = WORD(ecx + 0xa0u);
  b = ((uint16_t)(eax));
  flags = sub_flags(flags, a, b, 16);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44ef23;
  args[--sp] = 0x4u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_heal), 4); /* 0044c83a */
  BYTE(esi + 0xf9u) = 0x10u;
  goto L_44ef23;
L_44c84b:;
  a = ((uint8_t)(eax));
  b = 0x2cu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c855 */
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44c866:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x20u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c89d */
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef23;
L_44c8ae:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  ecx = BB_U32(0x602488u);
  eax = (eax + 1u);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44c925;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0xcu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  BYTE(esi + 0xe8u) = 0x0u;
  ecx = BB_U32(esi + 0x2cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44c919;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c914 */
L_44c919:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c91b */
  goto L_44ef23;
L_44c925:;
  a = ((uint8_t)(edx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = BB_U32(0x4843d0u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044c936 */
  goto L_44ef23;
L_44c940:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x24u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef2a;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x107u)) & 0xffu) << 0);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x115u)) & 0xffu) << 0);
  BYTE(esi + 0x107u) = ((uint8_t)(ecx));
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  BYTE(esi + 0xe9u) = 0x0u;
  if (!(flags & 0x41u))
    goto L_44ef2a;
  switch (eax) {
  case 0:
    goto L_44c983;
  case 1:
    goto L_44c9e0;
  case 2:
    goto L_44ca86;
  case 3:
    goto L_44cb6a;
  default:
    __builtin_unreachable();
  }
L_44c983:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44c996;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44c9c8;
  goto L_44ef2a;
L_44c996:;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044c996 */
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
  if (flags & 0x80u)
    goto L_44ef2a;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef2a;
  eax = BB_U32(0x6034dcu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044c9be */
  goto L_44ef2a;
L_44c9c8:;
  args[--sp] = 0xfau;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_treatment_fee), 4); /* 0044c9cf */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044c9d6 */
  goto L_44ef2a;
L_44c9e0:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44c9fc;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ca0e;
  a = eax;
  b = 0x14u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ca7a;
  goto L_44ef2a;
L_44c9fc:;
  eax = BB_U32(0x6a8f10u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044ca04 */
  goto L_44ef2a;
L_44ca0e:;
  eax = BB_U32(0x60362cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44ca1c;
  eax = 0x48481cu;
L_44ca1c:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x5eaca8u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_notification_entity_entry), 8); /* 0044ca23 */
  args[--sp] = 0x0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ca2a */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_10d_entry), 8); /* 0044ca41 */
  args[--sp] = 0x0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ca48 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0x1fu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044ca5c */
  ecx = BB_U32(esi + 0x30u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0xffffffe0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044ca70 */
  goto L_44ef2a;
L_44ca7a:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044ca7c */
  goto L_44ef2a;
L_44ca86:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = (eax - 0x4u);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  switch (eax) {
  case 0:
    goto L_44ca9d;
  case 1:
    goto L_44cadc;
  case 2:
    goto L_44cb01;
  case 3:
    goto L_44cb1f;
  default:
    __builtin_unreachable();
  }
L_44ca9d:;
  eax = BB_U32(0x6038a0u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44caab;
  eax = 0x48481cu;
L_44caab:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x5eaca8u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_notification_entity_entry), 8); /* 0044cab2 */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044cab7 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0x1u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  eax = BB_U32(eax * 4u + 0x662c88u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044cad2 */
  goto L_44ef2a;
L_44cadc:;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044cadc */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0x1u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  eax = BB_U32(eax * 4u + 0x662c88u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044caf7 */
  goto L_44ef2a;
L_44cb01:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + 0xa0u));
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax - edx);
  a = eax;
  result = ((uint32_t)((int32_t)a >> 1)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 1, 1);
  eax = result;
  eax = (uint32_t)(int32_t)(int16_t)(((uint16_t)(eax)));
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044cb15 */
  goto L_44ef2a;
L_44cb1f:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x63f048u);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 0044cb2b */
  args[--sp] = 0x0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044cb32 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = esi;
  eax = (eax - edx);
  eax = (eax & 0x1fu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x30u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_10d_entry), 8); /* 0044cb49 */
  ecx = BB_U32(esi + 0x30u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44cb5e;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0xffffffc0u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044cb59 */
L_44cb5e:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044cb60 */
  goto L_44ef2a;
L_44cb6a:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44cb82;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44cb94;
  a = eax;
  b = 0x7u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44cbb5;
  goto L_44ef2a;
L_44cb82:;
  eax = BB_U32(0x600030u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044cb8a */
  goto L_44ef2a;
L_44cb94:;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044cb94 */
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
  ecx = esi;
  eax = BB_U32(edx * 4u + 0x5e46f0u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044cbab */
  goto L_44ef2a;
L_44cbb5:;
  eax = BB_U32(0x6038a4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44cbc3;
  eax = 0x48481cu;
L_44cbc3:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x5eaca8u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_notification_entity_entry), 8); /* 0044cbca */
  eax = BB_U32(0x484384u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044cbd7 */
  eax = BB_U32(0x4843c0u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044cbe4 */
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044cbf2 */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044cbf9 */
  ecx = BB_U32(esi + 0x30u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0xffffff9cu;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044cc0d */
  goto L_44ef2a;
L_44cc17:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44cd2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x14u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_health_entry), 8); /* 0044cc68 */
  ecx = BB_U32(esi + 0x14u);
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044cc70 */
  eax = BB_U32(esi + 0x14u);
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(eax + 0xa2u)) & 0xffffu) << 0);
  a = WORD(eax + 0xa0u);
  b = ((uint16_t)(ecx));
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_44cd18;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044cc8e */
  eax = BB_U32(0x48437cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044cc9b */
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x554fa8u);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 0044ccac */
  ecx = BB_U32(esi + 0x14u);
  eax = BB_U32(ecx + 0x9cu);
  ecx = (ecx + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ccce;
  eax = (eax & 0xfff7ffffu);
  BB_U32(ecx) = eax;
L_44ccce:;
  ecx = BB_U32(esi + 0x14u);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd4u)) & 0xffu) << 0);
  eax = edx;
  edx = (edx << 3);
  edx = (edx - eax);
  ebx = (eax + edx * 8u);
  ebp = (eax + ebx * 8u);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd9u)) & 0xffu) << 0);
  edx = BB_U32(ebp * 2u + 0x6638b2u);
  eax = (ebp * 2u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(edx)) + ((uint8_t)(ebx)))) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  eax = BB_U32(eax + 0x6638aeu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd8u)) & 0xffu) << 0);
  ecx = esi;
  a = ((uint8_t)(eax));
  b = ((uint8_t)(edx));
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_move_escape), 8); /* 0044cd0e */
  goto L_44ef23;
L_44cd18:;
  eax = BB_U32(0x48437cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044cd20 */
  goto L_44ef23;
L_44cd2a:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044cd2c */
  goto L_44ef23;
L_44cd36:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  eax = BB_U32(esi + 0x9cu);
  eax = (eax | 0x40000u);
  BB_U32(esi + 0x9cu) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44cda8;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x1eu;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044cd83 */
  eax = BB_U32(esi + 0x14u);
  ecx = BB_U32(eax + 0x9cu);
  a = ((uint8_t)(ecx >> 8));
  b = 0x10u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44cda8;
  eax = BB_U32(0x484360u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044cd9e */
  goto L_44ef23;
L_44cda8:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044cdaa */
  goto L_44ef23;
L_44cdb4:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44cdca;
  a = eax;
  b = 0x7u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ce08;
  goto L_44ef2a;
L_44cdca:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(ecx));
  eax = edx;
  edx = (edx << 3);
  edx = (edx - eax);
  a = BYTE(edx + edx * 4u + 0x6aff88u);
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  BYTE(esi + 0xf5u) = 0x0u;
  goto L_44ef2a;
L_44ce08:;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44ce2e;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x14u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ce2e;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044ce1d */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_enemy_command), 0); /* 0044ce24 */
  goto L_44ef23;
L_44ce2e:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ce3f;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_begin_entry), 0); /* 0044ce3a */
L_44ce3f:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  eax = (eax - 0xfu);
  a = eax;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  switch (eax) {
  case 0:
    goto L_44ce5a;
  case 1:
    goto L_44d2dd;
  case 2:
    goto L_44d2dd;
  case 3:
    goto L_44cea0;
  case 4:
    goto L_44d2dd;
  case 5:
    goto L_44d2dd;
  case 6:
    goto L_44d2dd;
  case 7:
    goto L_44ce5a;
  case 8:
    goto L_44cef7;
  case 9:
    goto L_44cf9f;
  case 10:
    goto L_44d066;
  case 11:
    goto L_44d10e;
  case 12:
    goto L_44d12a;
  case 13:
    goto L_44cf9f;
  case 14:
    goto L_44cef7;
  case 15:
    goto L_44d1ea;
  case 16:
    goto L_44d26b;
  default:
    __builtin_unreachable();
  }
L_44ce5a:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44ce75;
  a = eax;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ce81;
  a = eax;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44ce94;
  goto L_44d2dd;
L_44ce75:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44ce81:;
  BYTE(esi + 0xf9u) = 0x1u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044ce8a */
  goto L_44d2dd;
L_44ce94:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44cea0:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f5fc[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44cec0;
  case 1:
    goto L_44cecc;
  case 2:
    goto L_44ced8;
  case 3:
    goto L_44ceeb;
  case 4:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44cec0:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44cecc:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44d2dd;
L_44ced8:;
  BYTE(esi + 0xf9u) = 0x2u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044cee1 */
  goto L_44d2dd;
L_44ceeb:;
  BYTE(esi + 0xf9u) = 0x3u;
  goto L_44d2dd;
L_44cef7:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f620[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44cf17;
  case 1:
    goto L_44cf46;
  case 2:
    goto L_44cf59;
  case 3:
    goto L_44cf85;
  case 4:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44cf17:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x0u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 3);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 3);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44cf46:;
  BYTE(esi + 0xf9u) = 0x1u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044cf4f */
  goto L_44d2dd;
L_44cf59:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 3);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 3);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44cf85:;
  BYTE(esi + 0xf9u) = 0x0u;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  goto L_44d2dd;
L_44cf9f:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xau;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f648[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44cfbf;
  case 1:
    goto L_44cfcb;
  case 2:
    goto L_44cffa;
  case 3:
    goto L_44d00d;
  case 4:
    goto L_44d040;
  case 5:
    goto L_44d04c;
  case 6:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44cfbf:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44cfcb:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x1u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44cffa:;
  BYTE(esi + 0xf9u) = 0x2u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044d003 */
  goto L_44d2dd;
L_44d00d:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x3u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44d040:;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44d2dd;
L_44d04c:;
  BYTE(esi + 0xf9u) = 0x5u;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  goto L_44d2dd;
L_44d066:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f668[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44d086;
  case 1:
    goto L_44d0b5;
  case 2:
    goto L_44d0c8;
  case 3:
    goto L_44d0fb;
  case 4:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44d086:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x0u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44d0b5:;
  BYTE(esi + 0xf9u) = 0x1u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044d0be */
  goto L_44d2dd;
L_44d0c8:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x0u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44d0fb:;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  goto L_44d2dd;
L_44d10e:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44d2dd;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044d120 */
  goto L_44d2dd;
L_44d12a:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xcu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f690[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44d14a;
  case 1:
    goto L_44d156;
  case 2:
    goto L_44d162;
  case 3:
    goto L_44d191;
  case 4:
    goto L_44d1a4;
  case 5:
    goto L_44d1d0;
  case 6:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44d14a:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44d156:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44d2dd;
L_44d162:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  BYTE(esi + 0xf9u) = 0x2u;
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44d191:;
  BYTE(esi + 0xf9u) = 0x3u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044d19a */
  goto L_44d2dd;
L_44d1a4:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  ecx = (0u - ecx);
  BB_U32(esi + 0x6eu) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  eax = (0u - eax);
  BB_U32(esi + 0x72u) = eax;
  goto L_44d2dd;
L_44d1d0:;
  BYTE(esi + 0xf9u) = 0x4u;
  BB_U32(esi + 0x6eu) = 0x0u;
  BB_U32(esi + 0x72u) = 0x0u;
  goto L_44d2dd;
L_44d1ea:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0xeu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f6c4[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44d20a;
  case 1:
    goto L_44d216;
  case 2:
    goto L_44d222;
  case 3:
    goto L_44d22e;
  case 4:
    goto L_44d23a;
  case 5:
    goto L_44d246;
  case 6:
    goto L_44d259;
  case 7:
    goto L_44d262;
  case 8:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44d20a:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44d216:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44d2dd;
L_44d222:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44d2dd;
L_44d22e:;
  BYTE(esi + 0xf9u) = 0x3u;
  goto L_44d2dd;
L_44d23a:;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44d2dd;
L_44d246:;
  BYTE(esi + 0xf9u) = 0x5u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044d24f */
  goto L_44d2dd;
L_44d259:;
  BYTE(esi + 0xf9u) = 0x6u;
  goto L_44d2dd;
L_44d262:;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44d2dd;
L_44d26b:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = eax;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44d2dd;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f6fc[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44d287;
  case 1:
    goto L_44d290;
  case 2:
    goto L_44d299;
  case 3:
    goto L_44d2a2;
  case 4:
    goto L_44d2ab;
  case 5:
    goto L_44d2b4;
  case 6:
    goto L_44d2c4;
  case 7:
    goto L_44d2cd;
  case 8:
    goto L_44d2d6;
  case 9:
    goto L_44d2dd;
  default:
    __builtin_unreachable();
  }
L_44d287:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44d2dd;
L_44d290:;
  BYTE(esi + 0xf9u) = 0x1u;
  goto L_44d2dd;
L_44d299:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44d2dd;
L_44d2a2:;
  BYTE(esi + 0xf9u) = 0x3u;
  goto L_44d2dd;
L_44d2ab:;
  BYTE(esi + 0xf9u) = 0x4u;
  goto L_44d2dd;
L_44d2b4:;
  BYTE(esi + 0xf9u) = 0x5u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_attack_tick_entry), 0); /* 0044d2bd */
  goto L_44d2dd;
L_44d2c4:;
  BYTE(esi + 0xf9u) = 0x6u;
  goto L_44d2dd;
L_44d2cd:;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44d2dd;
L_44d2d6:;
  BYTE(esi + 0xf9u) = 0x8u;
L_44d2dd:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  edx = (edx ^ edx);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + 1u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  eax = edx;
  BYTE(esi + 0xf5u) = ((uint8_t)(ecx));
  edx = (edx << 3);
  edx = (edx - eax);
  a = BYTE(edx + edx * 4u + 0x6aff88u);
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_44ef23;
  BYTE(esi + 0xf5u) = 0x0u;
  goto L_44ef23;
L_44d313:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x35u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d346;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x34u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44d33f;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  BYTE(esi + 0xe8u) = 0x0u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d33a */
L_44d33f:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044d341 */
L_44d346:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x36u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d36e;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  args[--sp] = 0x0u;
  esp -= 4u;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  ecx = esi;
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d362 */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044d369 */
L_44d36e:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x5eu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d39d;
  eax = BB_U32(esi + 0x2cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d394;
  ecx = BB_U32(eax + 0x2cu);
  a = ecx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_44d38d;
  BB_U32(esi + 0x2cu) = 0x0u;
L_44d38d:;
  eax = BB_U32(esi + 0x2cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44d39d;
L_44d394:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d398 */
L_44d39d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11du)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0xeu;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax + 1u);
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
  BYTE(esi + 0xeau) = ((uint8_t)(edx));
  goto L_44ef2a;
L_44d3c7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x3cu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0xc8u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d3ef */
  goto L_44ef23;
L_44d3f9:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x3eu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0xc8u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d421 */
  goto L_44ef23;
L_44d42b:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x41u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef2a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0xc8u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef2a;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d453 */
  goto L_44ef23;
L_44d45d:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44d486:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  eax = (eax - 0x47u);
  a = eax;
  b = 0x20u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f74c[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44d4a9;
  case 1:
    goto L_44d58c;
  case 2:
    goto L_44d690;
  case 3:
    goto L_44d736;
  case 4:
    goto L_44d7da;
  case 5:
    goto L_44d89b;
  case 6:
    goto L_44d8e7;
  case 7:
    goto L_44d92d;
  case 8:
    goto L_44d9e0;
  case 9:
    goto L_44db14;
  case 10:
    goto L_44dbee;
  case 11:
    goto L_44dce5;
  case 12:
    goto L_44dd6e;
  case 13:
    goto L_44de23;
  case 14:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44d4a9:;
  eax = BB_U32(esi + 0x14u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d57c;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0xe6u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d4d1 */
  ecx = BB_U32(0x4843a0u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d4df */
  args[--sp] = 0x8u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044d4fd */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d57c;
L_44d508:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44d557;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = eax;
  b = 0x24u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_44d557;
  a = eax;
  b = 0x25u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44d52f;
  a = eax;
  b = 0x5du;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_44d557;
L_44d52f:;
  eax = BB_U32(edi + 0x6au);
  edx = BB_U32(edi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  ebx = BB_U32(edi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = 0xc2u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d546 */
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  BYTE(eax + 0xeau) = ((uint8_t)(ecx));
L_44d557:;
  args[--sp] = 0x8u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044d571 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44d508;
L_44d57c:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d582 */
  goto L_44ef2a;
L_44d58c:;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d673;
  eax = (eax ^ eax);
  edx = (edx ^ edx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd9u)) & 0xffu) << 0);
  eax = (eax - edx);
  edx = (edx ^ edx);
  eax = (eax << 6);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd8u)) & 0xffu) << 0);
  eax = (eax - edx);
  a = edx;
  b = edx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edx = result;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(eax + edx + 0x5fda98u)) & 0xffu) << 0);
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  eax = BB_U32(ecx + 0x62u);
  ebx = BB_U32(ecx + 0x6au);
  edx = BB_U32(ecx + 0x66u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xc4u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d5e8 */
  edi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = (eax ^ eax);
  BYTE(edi + 0xe5u) = ((uint8_t)(ecx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044d612 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044d634 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x7eu) = eax;
  ecx = BB_U32(edi + 0x7au);
  BB_U32(edi + 0x6eu) = ecx;
  eax = BB_U32(edi + 0x7eu);
  BB_U32(edi + 0x72u) = eax;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  BYTE(edi + 0xe3u) = ((uint8_t)(edx));
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  BYTE(edi + 0xe4u) = ((uint8_t)(ecx));
  ecx = esi;
  BB_U32(edi + 0x14u) = esi;
  eax = BB_U32(0x484390u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d66e */
L_44d673:;
  eax = BB_U32(0x6a9938u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0044d67b */
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d686 */
  goto L_44ef2a;
L_44d690:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044d6a9 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d70b;
  ebx = 0x67u;
L_44d6b9:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d6e6;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44d6e6;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044d6d3 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x3u;
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
  BYTE(edi + 0x11eu) = ((uint8_t)(edx));
L_44d6e6:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044d700 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44d6b9;
L_44d70b:;
  eax = BB_U32(0x484394u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d713 */
  eax = BB_U32(0x5f9150u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_sound_play_entry), 0); /* 0044d71e */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = 0x553f90u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d72c */
  goto L_44ef2a;
L_44d736:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044d74f */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d7bd;
  ebx = 0x67u;
L_44d75f:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d798;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d798;
  eax = BB_U32(edi + 0x9cu);
  ecx = edi;
  a = eax;
  b = 0x40000000u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(edi + 0x9cu) = eax;
  BYTE(edi + 0x105u) = 0xffu;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044d793 */
L_44d798:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044d7b2 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44d75f;
L_44d7bd:;
  eax = BB_U32(0x484398u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d7c5 */
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d7d0 */
  goto L_44ef2a;
L_44d7da:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44d7e8;
  a = ((uint8_t)(eax));
  b = 0x28u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44d7f5;
L_44d7e8:;
  eax = BB_U32(0x484398u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d7f0 */
L_44d7f5:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x24u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44d80f;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 1)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(0x6u) & 0xffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = ((uint8_t)(eax >> 8));
  result = (a - b) & 0xffu;
  flags = sub_flags(flags, a, b, 8);
  eax = (eax & 0xffff00ffu) | (((uint32_t)(result) & 0xffu) << 8);
  b = ((uint8_t)(ecx));
  a = (uint16_t)eax;
  if (!b || a / b > 255u)
    division_fault();
  result = (a / b) | ((a % b) << 8);
  eax = (eax & 0xffff0000u) | result;
  BYTE(esi + 0xf9u) = ((uint8_t)(eax >> 8));
  goto L_44d889;
L_44d80f:;
  a = ((uint8_t)(eax));
  b = 0x26u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44d81c;
  BYTE(esi + 0xf9u) = 0x6u;
  goto L_44d889;
L_44d81c:;
  a = ((uint8_t)(eax));
  b = 0x28u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44d829;
  BYTE(esi + 0xf9u) = 0x7u;
  goto L_44d889;
L_44d829:;
  a = ((uint8_t)(eax));
  b = 0x2au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44d836;
  BYTE(esi + 0xf9u) = 0x8u;
  goto L_44d889;
L_44d836:;
  a = ((uint8_t)(eax));
  b = 0x3au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44d848;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) & 0x2u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) >> 1)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x8u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44d889;
L_44d848:;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x68u;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x4u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d85d */
  ecx = eax;
  edi = eax;
  args[--sp] = 0x12u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_profession), 4); /* 0044d868 */
  eax = BB_U32(edi + 0x9cu);
  args[--sp] = esi;
  esp -= 4u;
  a = eax;
  b = 0x80000000u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x553f90u;
  BB_U32(edi + 0x9cu) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d884 */
L_44d889:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044d88b */
  BYTE(esi + 0xeau) = (BYTE(esi + 0xeau) + 1u);
  goto L_44ef2a;
L_44d89b:;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x2au;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d8b5 */
  ecx = BB_U32(0x484398u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d8c3 */
  ecx = BB_U32(0x5f9158u);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_sound_play_entry), 0); /* 0044d8cf */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = 0x553f90u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d8dd */
  goto L_44ef2a;
L_44d8e7:;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x2bu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d901 */
  args[--sp] = 0x11u;
  esp -= 4u;
  ecx = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044d90a */
  ecx = BB_U32(0x484338u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d918 */
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d923 */
  goto L_44ef2a;
L_44d92d:;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0xe7u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044d94a */
  ecx = BB_U32(0x484338u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d958 */
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044d976 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44d9d0;
  ebx = 0x24u;
L_44d986:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0x11eu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44d9ab;
  BYTE(edi + 0x11eu) = 0x0u;
  ecx = edi;
  BYTE(edi + 0x105u) = ((uint8_t)(ebx));
  BYTE(edi + 0x12fu) = 0x1u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044d9a6 */
L_44d9ab:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044d9c5 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44d986;
L_44d9d0:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044d9d6 */
  goto L_44ef2a;
L_44d9e0:;
  eax = BB_U32(0x484338u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044d9e8 */
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044da06 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44db04;
  ebx = 0x1u;
L_44da1a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44dadb;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x4eu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44da63;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44da4a;
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44dadb;
L_44da4a:;
  args[--sp] = 0x2u;
  esp -= 4u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044da4e */
  BYTE(edi + 0xf9u) = 0x2u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044da5c */
  goto L_44dab9;
L_44da63:;
  a = ((uint8_t)(eax));
  b = 0x5eu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44daab;
  args[--sp] = edi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044da6d */
  eax = BB_U32(edi + 0x6au);
  ecx = BB_U32(edi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(edi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x4fu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044da8c */
  ecx = BB_U32(edi + 0x6au);
  edx = BB_U32(edi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(edi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xe3u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  goto L_44dad1;
L_44daab:;
  a = ((uint8_t)(eax));
  b = 0x6eu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44dab3;
  a = ((uint8_t)(eax));
  b = 0x6fu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44dadb;
L_44dab3:;
  BYTE(edi + 0xf5u) = ((uint8_t)(ebx));
L_44dab9:;
  eax = BB_U32(edi + 0x6au);
  ecx = BB_U32(edi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(edi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0xe3u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
L_44dad1:;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044dad6 */
L_44dadb:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044daf5 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44da1a;
L_44db04:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044db0a */
  goto L_44ef2a;
L_44db14:;
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0xe6u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044db31 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xe0u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044db53 */
  ecx = BB_U32(0x48439cu);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044db61 */
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044db7f */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44dbde;
L_44db8a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44dbb9;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = eax;
  b = 0x67u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_44dbb9;
  a = eax;
  b = 0x68u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44dbb1;
  a = eax;
  b = 0x6eu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_44dbb9;
L_44dbb1:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_evade_entry), 4); /* 0044dbb4 */
L_44dbb9:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044dbd3 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44db8a;
L_44dbde:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044dbe4 */
  goto L_44ef2a;
L_44dbee:;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44dcd5;
  edx = (edx ^ edx);
  eax = (eax ^ eax);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd9u)) & 0xffu) << 0);
  edx = (edx - eax);
  eax = (eax ^ eax);
  edx = (edx << 6);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xd8u)) & 0xffu) << 0);
  edx = (edx - eax);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx + eax + 0x5fda98u)) & 0xffu) << 0);
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  eax = BB_U32(ecx + 0x62u);
  edx = BB_U32(ecx + 0x6au);
  ebx = BB_U32(ecx + 0x66u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xdfu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044dc4a */
  edi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = (eax ^ eax);
  BYTE(edi + 0xe5u) = ((uint8_t)(ecx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044dc74 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044dc96 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0x7eu) = eax;
  ecx = BB_U32(edi + 0x7au);
  BB_U32(edi + 0x6eu) = ecx;
  eax = BB_U32(edi + 0x7eu);
  BB_U32(edi + 0x72u) = eax;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  BYTE(edi + 0xe3u) = ((uint8_t)(edx));
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  BYTE(edi + 0xe4u) = ((uint8_t)(ecx));
  ecx = esi;
  BB_U32(edi + 0x14u) = esi;
  eax = BB_U32(0x484390u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044dcd0 */
L_44dcd5:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044dcdb */
  goto L_44ef2a;
L_44dce5:;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44dd5e;
  eax = BB_U32(ecx + 0x6au);
  edx = BB_U32(ecx + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ecx + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0xe6u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044dd09 */
  ecx = BB_U32(0x484348u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044dd17 */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044dd1e */
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = BB_U32(esi + 0x14u);
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_place_tile), 8); /* 0044dd34 */
  ecx = BB_U32(esi + 0x14u);
  edx = BB_U32(ecx + 0x6au);
  eax = BB_U32(ecx + 0x66u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = BB_U32(ecx + 0x62u);
  args[--sp] = eax;
  esp -= 4u;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0xe6u;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044dd59 */
L_44dd5e:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044dd64 */
  goto L_44ef2a;
L_44dd6e:;
  eax = BB_U32(0x484338u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044dd76 */
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044dd94 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44de13;
  ebx = 0x67u;
L_44dda4:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ddee;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ddee;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xa2u)) & 0xffffu) << 0);
  a = WORD(edi + 0xa0u);
  b = ((uint16_t)(eax));
  flags = sub_flags(flags, a, b, 16);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44ddee;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = 0x64u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_health_entry), 8); /* 0044ddd4 */
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044dddb */
  BYTE(edi + 0x105u) = 0x24u;
  BYTE(edi + 0x12fu) = 0x1u;
L_44ddee:;
  args[--sp] = 0x5u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044de08 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44dda4;
L_44de13:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044de19 */
  goto L_44ef2a;
L_44de23:;
  eax = BB_U32(0x484338u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044de2b */
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near), 12); /* 0044de49 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44dee6;
  ebx = 0x67u;
L_44de5d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44debd;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44debd;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = 0x40u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_health_entry), 8); /* 0044de7d */
  args[--sp] = 0x20u;
  esp -= 4u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_100_entry), 4); /* 0044de86 */
  args[--sp] = 0x20u;
  esp -= 4u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_101_entry), 4); /* 0044de8f */
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = 0x40u;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 0044de9a */
  args[--sp] = 0x20u;
  esp -= 4u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_adjust_11b_entry), 4); /* 0044dea3 */
  BYTE(edi + 0x105u) = 0x24u;
  BYTE(edi + 0x12fu) = 0x2u;
  ecx = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044deb8 */
L_44debd:;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_near_next), 16); /* 0044ded7 */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44de5d;
L_44dee6:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044deec */
  goto L_44ef2a;
L_44def6:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  edx = BB_U32(eax + 0x5d1150u);
  edx = (edx << 2);
  BB_U32(esi + 0x7au) = edx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 2);
  BB_U32(esi + 0x6eu) = edx;
  edx = (edx & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  edx = (edx & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edx)) - 1u)) & 0xffffu) << 0);
  BB_U32(esi + 0x7eu) = eax;
  WORD(esi + 0xa0u) = ((uint16_t)(edx));
  BB_U32(esi + 0x72u) = eax;
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44df63;
  eax = (ecx * 8u);
  eax = (eax - ecx);
  ebx = (ecx + eax * 8u);
  ecx = (ecx + ebx * 8u);
  eax = (uint32_t)(int32_t)(int16_t)(WORD(ecx * 2u + 0x6635aeu));
  ecx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(edx)));
  eax = (eax - 0x10u);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44df63;
  args[--sp] = 0x45u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044df5e */
L_44df63:;
  a = WORD(esi + 0xa0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44dfa5;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef23;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x45u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
L_44dfa5:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044dfab */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);        /* 0044dfb0 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x28u;
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
  edx = (edx + ecx);
  eax = (eax ^ eax);
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = BB_U32(esi + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044dfde */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044e000 */
  goto L_44ef23;
L_44e00a:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  a = ecx;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  ecx = result;
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  goto L_44ef23;
L_44e02b:;
  eax = BB_U32(esi + 0x7au);
  ecx = BB_U32(esi + 0x7eu);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  BB_U32(esi + 0x6eu) = eax;
  BB_U32(esi + 0x72u) = ecx;
  a = ((uint8_t)(edx));
  b = 0x46u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e0da;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44e0da;
  BB_U32(esi + 0x7au) = 0x0u;
  BB_U32(esi + 0x7eu) = 0x0u;
  BB_U32(esi + 0x6eu) = 0x0u;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx << 6);
  BB_U32(esi + 0x76u) = 0x0u;
  ecx = (ecx + ecx * 2u);
  args[--sp] = 0x5bu;
  esp -= 4u;
  ecx = (ecx + eax);
  eax = (eax ^ eax);
  BB_U32(esi + 0x72u) = 0x0u;
  edx = (ecx + ecx * 2u);
  ecx = esi;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b65u)) & 0xffu) << 0);
  a = eax;
  result = (a << 16) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 16, 0);
  eax = result;
  BB_U32(esi + 0x6au) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e0a2 */
  BYTE(esi + 0xf5u) = 0x48u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044e0d5 */
L_44e0da:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x5bu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e109;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044e0ff */
  goto L_44ef23;
L_44e109:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax + 0x10u);
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x1fu);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 5));
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x7u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  edx = BB_U32(esi + 0x76u);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  a = edx;
  b = 0xffffc180u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44e14a;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + ecx * 4u + 0x484684u)) & 0xffu) << 0);
  goto L_44e1f3;
L_44e14a:;
  a = edx;
  b = 0xfffff000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44e179;
  edx = (edx ^ edx);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx + edx * 4u + 0x484683u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x46u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e16f */
  goto L_44ef23;
L_44e179:;
  a = edx;
  b = 0x1000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44e1a8;
  edx = (edx ^ edx);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx + edx * 4u + 0x484682u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x46u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e19e */
  goto L_44ef23;
L_44e1a8:;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44e1d3;
  edx = (edx ^ edx);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(edx + edx * 4u + 0x484682u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x46u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e1c9 */
  goto L_44ef23;
L_44e1d3:;
  a = edx;
  b = 0x3e80u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44e1e8;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + ecx * 4u + 0x484681u)) & 0xffu) << 0);
  goto L_44e1f3;
L_44e1e8:;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + ecx * 4u + 0x484680u)) & 0xffu) << 0);
L_44e1f3:;
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  goto L_44ef23;
L_44e1fe:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x7au) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x6eu) = ecx;
  BB_U32(esi + 0x7eu) = eax;
  BB_U32(esi + 0x72u) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax ^ edx);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edi)) - 1u)) & 0xffffu) << 0);
  eax = (eax - edx);
  WORD(esi + 0xa0u) = ((uint16_t)(edi));
  eax = (eax & 0xfu);
  eax = (eax ^ edx);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax - edx);
  ecx = (ecx ^ ecx);
  edx = (edx ^ edx);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx << 6);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + edx);
  ebp = (ecx + ecx * 2u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e299;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e290;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44e299;
L_44e290:;
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edi));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
L_44e299:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044e29f */
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x94u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44e2b6;
  a = ((uint8_t)(eax));
  b = 0xa4u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
L_44e2b6:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044e2dd */
  goto L_44ef23;
L_44e2e7:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x7au) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = 0xcu;
  BB_U32(esi + 0x7eu) = eax;
  BB_U32(esi + 0x72u) = eax;
  eax = (eax ^ eax);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edi)) - 1u)) & 0xffffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  WORD(esi + 0xa0u) = ((uint16_t)(edi));
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
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
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  ecx = (ecx << 6);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + edx);
  ebp = (ecx + ecx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e37e;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e375;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44e37e;
L_44e375:;
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edi));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
L_44e37e:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044e384 */
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044e3b0 */
  goto L_44ef23;
L_44e3ba:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x7au) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = 0xcu;
  BB_U32(esi + 0x7eu) = eax;
  BB_U32(esi + 0x72u) = eax;
  eax = (eax ^ eax);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edi)) - 1u)) & 0xffffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  WORD(esi + 0xa0u) = ((uint16_t)(edi));
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
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
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  edx = (edx << 6);
  edx = (edx + edx * 2u);
  edx = (edx + ecx);
  ebp = (edx + edx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e451;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e448;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44e451;
L_44e448:;
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edi));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
L_44e451:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044e457 */
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044e483 */
  goto L_44ef23;
L_44e48d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x61u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e4ef;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44e4ab;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  goto L_44e4ef;
L_44e4ab:;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx << 6);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + eax);
  edx = (ecx + ecx * 2u);
  ebx = BB_U32(edx * 4u + 0x559b68u);
  a = ((uint8_t)(ebx));
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e4ef;
  args[--sp] = 0x5fu;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e4d6 */
  eax = BB_U32(0x484324u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044e4e3 */
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044e4ea */
L_44e4ef:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x5fu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e555;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44e530;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44e530;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  BYTE(edx * 4u + 0x559b66u) = 0x0u;
L_44e530:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x5fu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e555;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  goto L_44ef23;
L_44e555:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x2u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44ef2a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  goto L_44ef23;
L_44e570:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44e589:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x18u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e5d1;
  ecx = BB_U32(esi + 0x14u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44e5d1;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd7u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd6u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_place_tile), 8); /* 0044e5b0 */
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd7u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  ecx = BB_U32(esi + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd6u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_walk_entry), 12); /* 0044e5cc */
L_44e5d1:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x30u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44e5e4:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x7au) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = 0xcu;
  BB_U32(esi + 0x7eu) = eax;
  BB_U32(esi + 0x72u) = eax;
  eax = (eax ^ eax);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edi)) - 1u)) & 0xffffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  WORD(esi + 0xa0u) = ((uint16_t)(edi));
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
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
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  ecx = (ecx << 6);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + edx);
  ebp = (ecx + ecx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e67b;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e672;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44e67b;
L_44e672:;
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edi));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
L_44e67b:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044e681 */
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0);        /* 0044e686 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x3cu;
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
  edx = (edx + ecx);
  eax = (eax ^ eax);
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = BB_U32(esi + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044e6b4 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044e6d6 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044e6f5 */
  WORD(eax + 0xa0u) = 0xau;
  goto L_44ef23;
L_44e708:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = 0x6du;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f784[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44e728;
  case 1:
    goto L_44e734;
  case 2:
    goto L_44e740;
  case 3:
    goto L_44e7c9;
  case 4:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44e728:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef2a;
L_44e734:;
  BYTE(esi + 0xf9u) = 0x5u;
  goto L_44ef2a;
L_44e740:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef2a;
  BYTE(esi + 0xeau) = 0x0u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e77c;
  eax = BB_U32(0x4842dcu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044e777 */
L_44e77c:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x5u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef23;
  args[--sp] = 0x6au;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e78e */
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef23;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_release_tiles), 0); /* 0044e7bf */
  goto L_44ef23;
L_44e7c9:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xeau)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xeau) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef2a;
  BYTE(esi + 0xeau) = 0x0u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44e7f8;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
L_44e7f8:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44e82f;
  eax = (eax ^ eax);
  ecx = (ecx ^ ecx);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(edx * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e82f;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_register_tiles), 0); /* 0044e82a */
L_44e82f:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044e841 */
  goto L_44ef23;
L_44e84b:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44e874:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx + ecx);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44e89c:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e8c3;
  BYTE(esi + 0xf9u) = 0x10u;
L_44e8c3:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(eax)) - 1u)) & 0xffffu) << 0);
  WORD(esi + 0xa0u) = ((uint16_t)(eax));
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
  goto L_44ef18;
L_44e8e1:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  eax = BB_U32(eax + 0x5d1558u);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd6u)) & 0xffu) << 0);
  eax = (eax << 4);
  BB_U32(esi + 0x72u) = eax;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(eax)) - 1u)) & 0xffffu) << 0);
  a = ((uint8_t)(edx));
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  WORD(esi + 0xa0u) = ((uint16_t)(eax));
  if (!(flags & 0x40u))
    goto L_44e934;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd7u)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44e939;
L_44e934:;
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44e999;
L_44e939:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044e93f */
  args[--sp] = 0x28u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(esi + 0x14u);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044e964 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcau;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044e986 */
  ecx = BB_U32(0x4843a4u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044e994 */
L_44e999:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44e9af;
  BYTE(esi + 0xf5u) = 0x4u;
  goto L_44ef23;
L_44e9af:;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  goto L_44ef23;
L_44e9bc:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x7au) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = 0xcu;
  BB_U32(esi + 0x7eu) = eax;
  BB_U32(esi + 0x72u) = eax;
  eax = (eax ^ eax);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edi)) - 1u)) & 0xffffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  WORD(esi + 0xa0u) = ((uint16_t)(edi));
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
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
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  edx = (edx << 6);
  edx = (edx + edx * 2u);
  edx = (edx + ecx);
  ebp = (edx + edx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ea53;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ea4a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44ea53;
L_44ea4a:;
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edi));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
L_44ea53:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044ea59 */
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044ea85 */
  goto L_44ef23;
L_44ea8f:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf2u)) & 0xffu) << 0);
  a = eax;
  b = 0x70u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_44ef2a;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_44f808[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_44eaaf;
  case 1:
    goto L_44eabb;
  case 2:
    goto L_44eac7;
  case 3:
    goto L_44eb08;
  case 4:
    goto L_44ef2a;
  default:
    __builtin_unreachable();
  }
L_44eaaf:;
  BYTE(esi + 0xf9u) = 0x0u;
  goto L_44ef2a;
L_44eabb:;
  BYTE(esi + 0xf9u) = 0x2u;
  goto L_44ef2a;
L_44eac7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef2a;
  BYTE(esi + 0xe5u) = 0x0u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x2u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef23;
  args[--sp] = 0x6eu;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044eafe */
  goto L_44ef23;
L_44eb08:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44ef2a;
  BYTE(esi + 0xe5u) = 0x0u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44eb45;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) - 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
L_44eb45:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 0044eb49 */
  goto L_44ef23;
L_44eb53:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe6u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_44eb87;
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  edx = (edx - ecx);
  a = edx;
  b = 0x80u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44eb81;
  a = ((uint8_t)(eax));
  b = 0xfcu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_44eb7d;
  a = ((uint8_t)(eax));
  b = 0x4u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  goto L_44ebab;
L_44eb7d:;
  a = ((uint8_t)(eax));
  b = 0x4u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  goto L_44ebab;
L_44eb81:;
  a = ((uint8_t)(eax));
  b = 0x3u;
  a = ((uint8_t)(eax));
  b = 0x4u;
  result = (a - b) & 0xffu;
  flags = sub_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  goto L_44ebab;
L_44eb87:;
  if (flags & 0x41u)
    goto L_44ebb8;
  edx = (edx ^ edx);
  ebx = (ebx ^ ebx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  edx = (edx - ebx);
  a = edx;
  b = 0x80u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_44eba7;
  a = ((uint8_t)(eax));
  b = 0x3u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x41u))
    goto L_44eba3;
  a = ((uint8_t)(eax));
  b = 0x4u;
  result = (a - b) & 0xffu;
  flags = sub_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  goto L_44ebab;
L_44eba3:;
  a = ((uint8_t)(eax));
  b = 0x4u;
  result = (a - b) & 0xffu;
  flags = sub_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  goto L_44ebab;
L_44eba7:;
  a = ((uint8_t)(eax));
  b = 0xfcu;
  a = ((uint8_t)(eax));
  b = 0x4u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
L_44ebab:;
  ecx = esi;
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044ebb3 */
L_44ebb8:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe6u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(ecx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ebd6;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) & 0xdfu)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  result = (a >> 5) & 0xffu;
  flags = shift_flags(flags, a, result, 8, 5, 1);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(ecx));
  goto L_44ebfc;
L_44ebd6:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  eax = (eax + 0x20u);
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x3fu);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 6));
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(eax));
L_44ebfc:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044ebfe */
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44ef2a;
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(0x4843f0u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = BB_U32(0x484410u);
  a = eax;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  eax = result;
  BB_U32(0x484410u) = eax;
  goto L_44ef23;
L_44ec42:;
  eax = BB_U32(esi + 0x76u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44ef2a;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x3u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(esi + 0x14u);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0044ec70 */
  ecx = BB_U32(esi + 0x6au);
  edx = BB_U32(esi + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(esi + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ebp = 0x3u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044ec97 */
L_44ec9c:;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ec9c */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  edi = (eax + 0x1u);
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ecb0 */
  edi = (edi << 16);
  ecx = BB_U32(esi + 0x66u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0xffu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  edx = BB_U32(esi + 0x62u);
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  eax = BB_U32(esi + 0x6au);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = 0x38u;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044ece6 */
  ebx = eax;
  args[--sp] = edi;
  esp -= 4u;
  eax = (eax ^ eax);
  BB_U32(ebx + 0x76u) = 0x10000u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  BYTE(ebx + 0xe5u) = ((uint8_t)(ecx));
  BYTE(ebx + 0xf9u) = 0x1u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044ed1a */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebx + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044ed38 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebx + 0x7eu) = eax;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ed43 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  eax = (eax & 0x7ffu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  edi = eax;
  ecx = BB_U32(ebx + 0x7au);
  args[--sp] = ecx;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044ed61 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  edi = (edi + eax);
  a = edi;
  b = 0x400u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edi = result;
  BB_U32(ebx + 0x7au) = edi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ed74 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  eax = (eax & 0x7ffu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  edi = eax;
  eax = BB_U32(ebx + 0x7eu);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044ed92 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  edi = (edi + eax);
  a = edi;
  b = 0x400u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edi = result;
  a = ebp;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebp = result;
  BB_U32(ebx + 0x7eu) = edi;
  eax = BB_U32(ebx + 0x7au);
  BB_U32(ebx + 0x6eu) = eax;
  edi = BB_U32(ebx + 0x7eu);
  BB_U32(ebx + 0x72u) = edi;
  BB_U32(ebx + 0x14u) = esi;
  BYTE(ebx + 0xe9u) = 0xffu;
  if (!(flags & 0x40u))
    goto L_44ec9c;
  eax = BB_U32(0x484390u);
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_sample_play_entry), 0); /* 0044edc8 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = 0x553f90u;
  BB_U32(0x4846b4u) = 0x20u;
  args[--sp] = esi;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044ede0 */
  goto L_44ef2a;
L_44edea:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe5u)) & 0xffu) << 0);
  eax = (eax << 2);
  ecx = BB_U32(eax + 0x5d1150u);
  ecx = (ecx << 4);
  BB_U32(esi + 0x7au) = ecx;
  eax = BB_U32(eax + 0x5d1558u);
  eax = (eax << 4);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = 0xcu;
  BB_U32(esi + 0x7eu) = eax;
  BB_U32(esi + 0x72u) = eax;
  eax = (eax ^ eax);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xa0u)) & 0xffffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edi)) - 1u)) & 0xffffu) << 0);
  eax = (eax + 1u);
  edx = (uint32_t)((int32_t)eax >> 31);
  WORD(esi + 0xa0u) = ((uint16_t)(edi));
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
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
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(((uint8_t)(eax))) & 0xffu) << 0);
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  ecx = (ecx << 6);
  edx = (edx ^ edx);
  edx = (edx & 0xffffff00u) | (((uint32_t)(((uint8_t)(ebx))) & 0xffu) << 0);
  ecx = (ecx + ecx * 2u);
  ecx = (ecx + edx);
  ebp = (ecx + ecx * 2u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebp * 4u + 0x559b66u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ee81;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_44ee78;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44ee81;
L_44ee78:;
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edi));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_44ef23;
L_44ee81:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044ee87 */
  eax = BB_U32(esi + 0x6au);
  ecx = BB_U32(esi + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esi + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0xe6u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044eea9 */
  goto L_44ef23;
L_44eeb0:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf5u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xf5u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x78u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef23;
  goto L_44ef18;
L_44eec4:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  goto L_44ef18;
L_44eee6:;
  eax = (eax ^ eax);
  ecx = BB_U32(esi + 0x58u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf9u)) & 0xffu) << 0);
  edx = (edx - edx);
  ecx = BB_U32(ecx + 0x4cu);
  eax = (eax + 1u);
  ecx = (ecx << 2);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || wide / b > 0xffffffffu)
    division_fault();
  result = (uint32_t)(wide / b);
  edx = (uint32_t)(wide % b);
  eax = result;
  BYTE(esi + 0xf9u) = ((uint8_t)(edx));
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_44ef23;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe8u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) + 1u)) & 0xffu) << 0);
  BYTE(esi + 0xe8u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_44ef23;
L_44ef18:;
  args[--sp] = esi;
  esp -= 4u;
  ecx = 0x553f90u;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0044ef1e */
L_44ef23:;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 0044ef25 */
L_44ef2a:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x12bu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  BYTE(esi + 0x12bu) = ((uint8_t)(eax));
  if (!(flags & 0x40u))
    goto L_44ef48;
  ecx = esi;
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_kind_tick_entry), 0); /* 0044ef3c */
  BYTE(esi + 0x12bu) = 0xffu;
L_44ef48:;
  a = BYTE(esi + 0xfau);
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  BYTE(esi + 0xfau) = result;
  if (!(flags & 0x40u))
    goto L_44ef91;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44ef74;
  a = ((uint8_t)(eax));
  b = 0x68u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44ef74;
  a = ((uint8_t)(eax));
  b = 0x6eu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_44ef74;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ef62 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x28u;
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
  a = ((uint8_t)(edx));
  b = 0x50u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  goto L_44ef84;
L_44ef74:;
  MAIN_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ef74 */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = 0x14u;
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
  a = ((uint8_t)(edx));
  b = 0x28u;
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
L_44ef84:;
  ecx = esi;
  BYTE(esi + 0xfau) = ((uint8_t)(edx));
  MAIN_CALL(BB_ENTITY_TARGET(bb_entity_decision_tick), 0); /* 0044ef8c */
L_44ef91:;
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
void __attribute__((naked)) bb_entity_main_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_main_c) ";leal 4(%esp),%esp;popal;popfl;ret");
}
#endif
