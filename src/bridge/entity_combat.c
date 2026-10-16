/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "entity_calls.h"
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
#ifdef BB_MODEL_RUNTIME
uint32_t BB_THISCALL bb_entity_area_damage_entry(void *object,uint32_t exclude,
 uint32_t attacker,uint32_t x,uint32_t y,uint32_t radius,uint32_t amount) {
 return BB_GAME_CALL(0x434e60,bb_address_encode(object),exclude,attacker,x,y,radius,amount);
}
#endif
/* First C forms of combat dispatch and damage. Address-derived labels and
 * temporary values retain provenance for auditing callback and death ordering.
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
#define COMBAT_CALL(target, consumed)                                          \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                    \
    BB_ENTITY_CALL(&call,target,args+sp,esp,edi,esi,ebp,ebx);                        \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

/* Keep the x87 truncation boundary, including invalid/overflow results and
 * control-word restoration. Floating calculations above it are C expressions.
 */
#ifdef BB_MODEL_RUNTIME
static void combat_float_to_int(struct bb_call_state *state,const long double *value) {
 uint64_t result=bb_entity_integer(*value);state->eax=result;state->edx=result>>32;
 state->flags=logic_flags(state->flags,15,8);
}
#else
static void combat_float_to_int(struct bb_call_state *state,
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
#endif
void bb_entity_area_damage_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,28u),28u);
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  eax = (eax << 16);
  BB_U32((uintptr_t)(args + sp) + 0u) = eax;
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 28u);
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax << 6);
  args[--sp] = edi;
  esp -= 4u;
  edx = BB_U32((uintptr_t)(args + sp) + 32u);
  args[--sp] = ebp;
  esp -= 4u;
  eax = (eax + eax * 2u);
  eax = (eax + edx);
  edx = (edx ^ edx);
  esi = BB_U32(ecx + 0x1008u);
  ebx = (eax + eax * 2u);
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE(ebx * 4u + 0x559b65u)) & 0xffu) << 0);
  edx = (edx + 0x3u);
  edx = (edx << 16);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  BB_U32((uintptr_t)(args + sp) + 20u) = edx;
  if (flags & 0x40u)
    goto L_435063;
  edi = BB_U32((uintptr_t)(args + sp) + 28u);
L_434eaa:;
  ebx = (ebx ^ ebx);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  ecx = eax;
  eax = (eax << 3);
  eax = (eax - ecx);
  edx = (ecx + eax * 8u);
  ecx = (ecx + edx * 8u);
  ebp = BB_U32(ecx * 2u + 0x663578u);
  edx = (ecx * 2u);
  a = ebp;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_434f1c;
  ecx = BB_U32((uintptr_t)(args + sp) + 36u);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ebp = BB_U32((uintptr_t)(args + sp) + 40u);
  ecx = (ecx - eax);
  eax = (eax ^ eax);
  ecx = (ecx + 0x5u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ebp = (ebp - eax);
  ebp = (ebp + 0x5u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_434f58;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_434f58;
  a = ecx;
  b = 0xbu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_434f58;
  a = ebp;
  b = 0xbu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_434f58;
  eax = (ebp + ebp * 4u);
  eax = (ebp + eax * 2u);
  eax = (eax + ecx);
  a = BB_U32(edx + eax * 4u + 0x663688u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_434f58;
  goto L_434f53;
L_434f1c:;
  eax = (eax ^ eax);
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  edx = BB_U32((uintptr_t)(args + sp) + 36u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32((uintptr_t)(args + sp) + 48u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_distance), 0); /* 00434f38 */
  ecx = BB_U32((uintptr_t)(args + sp) + 32u);
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_434f58;
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  ecx = BB_U32(esi + 0x6au);
  a = ecx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_434f58;
L_434f53:;
  ebx = 0x1u;
L_434f58:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_435058;
  a = edi;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_435058;
  edx = (edx ^ edx);
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  ebx = (eax + ecx * 8u);
  eax = (eax + ebx * 8u);
  a = BB_U32(eax * 2u + 0x663596u);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_434f96;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xb2u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_434f96;
  a = ((uint8_t)(eax));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_434fad;
L_434f96:;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_434fa8;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x7u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_434fa8;
  a = ((uint8_t)(eax));
  b = 0x5du;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_434fad;
L_434fa8:;
  edx = 0x1u;
L_434fad:;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_435058;
  eax = BB_U32((uintptr_t)(args + sp) + 32u);
  ecx = BB_U32((uintptr_t)(args + sp) + 48u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00434fc1 */
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_43504e;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x93u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_435008;
  a = ((uint8_t)(eax));
  b = 0x94u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_434fe3;
  eax = BB_U32(0x48432cu);
  goto L_43500d;
L_434fe3:;
  a = ((uint8_t)(eax));
  b = 0xa4u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_434fee;
  eax = BB_U32(0x48432cu);
  goto L_43500d;
L_434fee:;
  a = ((uint8_t)(eax));
  b = 0x95u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_434ff9;
  eax = BB_U32(0x48432cu);
  goto L_43500d;
L_434ff9:;
  a = ((uint8_t)(eax));
  b = 0xd1u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_435004;
  eax = BB_U32(0x48432cu);
  goto L_43500d;
L_435004:;
  a = ((uint8_t)(eax));
  b = 0x37u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_435015;
L_435008:;
  eax = BB_U32(0x484328u);
L_43500d:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00435010 */
L_435015:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x7u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_435027;
  a = ((uint8_t)(eax));
  b = 0x3du;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_435027;
  a = ((uint8_t)(eax));
  b = 0x5du;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_435031;
L_435027:;
  BB_U32(esi + 0x9cu) = (BB_U32(esi + 0x9cu) | 0x80000u);
L_435031:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_435058;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x19u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_435058;
  BYTE(esi + 0x11eu) = 0x3u;
  goto L_435058;
L_43504e:;
  BB_U32(esi + 0x9cu) = (BB_U32(esi + 0x9cu) | 0x80000u);
L_435058:;
  esi = BB_U32(esi + 0x4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_434eaa;
L_435063:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
uint32_t __attribute__((naked, thiscall))
bb_entity_area_damage_entry(void *object __attribute__((unused)),
                            uint32_t exclude __attribute__((unused)),
                            uint32_t attacker __attribute__((unused)),
                            uint32_t x __attribute__((unused)),
                            uint32_t y __attribute__((unused)),
                            uint32_t radius __attribute__((unused)),
                            uint32_t amount __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_area_damage_c) ";leal 4(%esp),%esp;popal;popfl;ret $24");
}
#endif
void bb_entity_attack_begin_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,4u),4u);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0xcu);
  esi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_44f8ef;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44f8ef;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_position), 0); /* 0044f894 */
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_attack_range_entry), 0); /* 0044f89b */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44f8ad;
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044f8a6 */
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
L_44f8ad:;
  ecx = (ecx ^ ecx);
  eax = (eax ^ eax);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe4u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx - eax);
  eax = (eax ^ eax);
  ecx = (ecx << 6);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd8u)) & 0xffu) << 0);
  ecx = (ecx - eax);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xe3u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + eax + 0x5fda98u)) & 0xffu) << 0);
  BYTE(esi + 0xe7u) = ((uint8_t)(eax));
  BYTE(esi + 0xe5u) = ((uint8_t)(eax));
  BYTE(esi + 0xe6u) = ((uint8_t)(eax));
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
L_44f8ef:;
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044f8f1 */
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
void __attribute__((naked)) bb_entity_attack_begin_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_attack_begin_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
static const uint8_t map_4503a4[] = {0, 1, 2,   3,   6,   6,   6,   6,  6, 6,
                                     4, 6, 6,   6,   6,   6,   6,   6,  6, 6,
                                     6, 5, 204, 204, 204, 204, 204, 204};
void bb_entity_attack_tick_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,4u),4u);
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(ecx + 0xcu);
  edi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450338;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_450338;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_position), 0); /* 0044f922 */
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_attack_range_entry), 0); /* 0044f929 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_44f941;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype_clear_entry), 0); /* 0044f934 */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_44f941:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe3u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe4u)) & 0xffu) << 0);
  BYTE((uintptr_t)(args + sp) + 20u) = ((uint8_t)(eax));
  BYTE((uintptr_t)(args + sp) + 19u) = ((uint8_t)(ecx));
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 27u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 32u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_distance), 0); /* 0044f975 */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  esi = eax;
  ecx = (ecx ^ ecx);
  ebx = (ebx ^ ebx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf3u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  BYTE(edi + 0xe2u) = 0x0u;
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + ecx * 4u + 0x6aff89u)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE(ecx + ecx * 4u + 0x6aff8au)) & 0xffu) << 0);
  ebp = (ecx + ecx * 4u);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_44f9be;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044f9ac */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = (ecx ^ ecx);
  ecx =
      (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x6aff8au)) & 0xffu) << 0);
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
  ebx = (ebx + edx);
L_44f9be:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf3u)) & 0xffu) << 0);
  eax = (eax - 0x6u);
  a = eax;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_45027a;
  switch (eax) {
  case 0:
    goto L_44f9d9;
  case 1:
    goto L_44fb23;
  case 2:
    goto L_44fc24;
  case 3:
    goto L_44fc33;
  case 4:
    goto L_44fce2;
  case 5:
    goto L_44fd91;
  case 6:
    goto L_44fe40;
  case 7:
    goto L_450000;
  case 8:
    goto L_45001c;
  case 9:
    goto L_45027a;
  case 10:
    goto L_450038;
  case 11:
    goto L_45006c;
  case 12:
    goto L_45011b;
  case 13:
    goto L_45016d;
  case 14:
    goto L_450199;
  case 15:
    goto L_4501a8;
  case 16:
    goto L_450200;
  case 17:
    goto L_45020c;
  default:
    __builtin_unreachable();
  }
L_44f9d9:;
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
  args[--sp] = 0x93u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044f9f6 */
  ebp = eax;
  a = esi;
  b = 0x30000u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44fa3e;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax ^ eax);
  BB_U32(ebp + 0x76u) = 0x10000u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fa1f */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  goto L_44fa82;
L_44fa3e:;
  eax = BB_U32(ebp + 0x6au);
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax + 0x8000u);
  BB_U32(ebp + 0x6au) = eax;
  eax = (eax ^ eax);
  BB_U32(ebp + 0x76u) = 0x2710u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fa64 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
L_44fa82:;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fa83 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7eu) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044fa8e */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  ecx = BB_U32(ebp + 0x7au);
  eax = (eax & 0x3ffu);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  esi = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044faac */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x200u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  BB_U32(ebp + 0x7au) = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044fabf */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  eax = (eax & 0x3ffu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  esi = eax;
  eax = BB_U32(ebp + 0x7eu);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044fadd */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x200u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  BB_U32(ebp + 0x7eu) = esi;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  BYTE(ebp + 0xe5u) = ((uint8_t)(eax));
  ecx = BB_U32(ebp + 0x7au);
  BB_U32(ebp + 0x6eu) = ecx;
  ecx = edi;
  edx = BB_U32(ebp + 0x7eu);
  BB_U32(ebp + 0x72u) = edx;
  BB_U32(ebp + 0x14u) = edi;
  BYTE(ebp + 0xe9u) = ((uint8_t)(ebx));
  eax = BB_U32(0x4843a8u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044fb19 */
  goto L_45027a;
L_44fb23:;
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
  args[--sp] = 0x38u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044fb3d */
  ebp = eax;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax ^ eax);
  BB_U32(ebp + 0x76u) = 0x10000u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  BYTE(ebp + 0xe5u) = ((uint8_t)(ecx));
  BYTE(ebp + 0xf9u) = 0x1u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fb71 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fb8f */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7eu) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044fb9a */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  ecx = BB_U32(ebp + 0x7au);
  eax = (eax & 0x7ffu);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  esi = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044fbb8 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x400u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  BB_U32(ebp + 0x7au) = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044fbcb */
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
  esi = eax;
  eax = BB_U32(ebp + 0x7eu);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044fbe9 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x400u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  BB_U32(ebp + 0x7eu) = esi;
  eax = BB_U32(ebp + 0x7au);
  BB_U32(ebp + 0x6eu) = eax;
  esi = BB_U32(ebp + 0x7eu);
  BB_U32(ebp + 0x72u) = esi;
  BB_U32(ebp + 0x14u) = edi;
  BYTE(ebp + 0xe9u) = ((uint8_t)(ebx));
  ecx = BB_U32(0x484388u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044fc1a */
  goto L_45027a;
L_44fc24:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0044fc29 */
  goto L_45027a;
L_44fc33:;
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
  args[--sp] = 0x94u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044fc50 */
  esi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x60000u;
  esp -= 4u;
  eax = (eax ^ eax);
  BYTE(esi + 0xe5u) = ((uint8_t)(ecx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fc7a */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x60000u;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fc9c */
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7eu) = eax;
  ecx = BB_U32(esi + 0x7au);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 19u)) & 0xffu) << 0);
  eax = BB_U32(esi + 0x7eu);
  BB_U32(esi + 0x72u) = eax;
  BYTE(esi + 0xe3u) = ((uint8_t)(edx));
  BYTE(esi + 0xe4u) = ((uint8_t)(ecx));
  ecx = edi;
  BB_U32(esi + 0x14u) = edi;
  BYTE(esi + 0xe9u) = ((uint8_t)(ebx));
  eax = BB_U32(0x4843acu);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044fcd8 */
  goto L_45027a;
L_44fce2:;
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
  args[--sp] = 0x95u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044fcff */
  esi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = (eax ^ eax);
  BYTE(esi + 0xe5u) = ((uint8_t)(ecx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fd29 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
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
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fd4b */
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7eu) = eax;
  ecx = BB_U32(esi + 0x7au);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 19u)) & 0xffu) << 0);
  eax = BB_U32(esi + 0x7eu);
  BB_U32(esi + 0x72u) = eax;
  BYTE(esi + 0xe3u) = ((uint8_t)(edx));
  BYTE(esi + 0xe4u) = ((uint8_t)(ecx));
  ecx = edi;
  BB_U32(esi + 0x14u) = edi;
  BYTE(esi + 0xe9u) = ((uint8_t)(ebx));
  eax = BB_U32(0x4843b0u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044fd87 */
  goto L_45027a;
L_44fd91:;
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
  args[--sp] = 0x9cu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044fdae */
  esi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = (eax ^ eax);
  BYTE(esi + 0xe5u) = ((uint8_t)(ecx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fdd8 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
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
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fdfa */
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7eu) = eax;
  ecx = BB_U32(esi + 0x7au);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 19u)) & 0xffu) << 0);
  eax = BB_U32(esi + 0x7eu);
  BB_U32(esi + 0x72u) = eax;
  BYTE(esi + 0xe3u) = ((uint8_t)(edx));
  BYTE(esi + 0xe4u) = ((uint8_t)(ecx));
  ecx = edi;
  BB_U32(esi + 0x14u) = edi;
  eax = BB_U32(0x4843b0u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044fe30 */
  BYTE(esi + 0xe9u) = ((uint8_t)(ebx));
  goto L_45027a;
L_44fe40:;
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
  args[--sp] = 0x37u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0044fe5a */
  BB_U32(eax + 0x76u) = 0x10000u;
  BYTE(eax + 0xf9u) = 0x1u;
  ebp = eax;
  a = esi;
  b = 0x30000u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_44ff1b;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044fe8e */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044feac */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7eu) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044feb7 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  ecx = BB_U32(ebp + 0x7au);
  eax = (eax & 0x7ffu);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  esi = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044fed5 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x400u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  BB_U32(ebp + 0x7au) = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044fee8 */
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
  esi = eax;
  eax = BB_U32(ebp + 0x7eu);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044ff06 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x400u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  goto L_44ffca;
L_44ff1b:;
  eax = BB_U32(ebp + 0x6au);
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax + 0x8000u);
  BB_U32(ebp + 0x6au) = eax;
  eax = (eax ^ eax);
  BB_U32(ebp + 0x76u) = 0x2710u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044ff41 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(eax * 4u + 0x5d1558u);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 0044ff60 */
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0x7eu) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ff6b */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  ecx = BB_U32(ebp + 0x7au);
  eax = (eax & 0x3ffu);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  esi = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044ff89 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x200u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  BB_U32(ebp + 0x7au) = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0044ff9c */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  args[--sp] = 0x10000u;
  esp -= 4u;
  eax = (eax - edx);
  eax = (eax & 0x3ffu);
  eax = (eax ^ edx);
  a = eax;
  b = edx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  esi = eax;
  eax = BB_U32(ebp + 0x7eu);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_divide_entry), 0); /* 0044ffba */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  esi = (esi + eax);
  a = esi;
  b = 0x200u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
L_44ffca:;
  BB_U32(ebp + 0x7eu) = esi;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  BYTE(ebp + 0xe5u) = ((uint8_t)(eax));
  ecx = BB_U32(ebp + 0x7au);
  BB_U32(ebp + 0x6eu) = ecx;
  ecx = edi;
  edx = BB_U32(ebp + 0x7eu);
  BB_U32(ebp + 0x72u) = edx;
  BB_U32(ebp + 0x14u) = edi;
  BYTE(ebp + 0xe9u) = ((uint8_t)(ebx));
  eax = BB_U32(0x4843a8u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0044fff6 */
  goto L_45027a;
L_450000:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00450005 */
  eax = BB_U32(0x6a991cu);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 00450012 */
  goto L_45027a;
L_45001c:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00450021 */
  eax = BB_U32(0x4843b4u);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045002e */
  goto L_45027a;
L_450038:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0045003d */
  eax = BB_U32(0x4843a4u);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045004a */
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0x128u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_45027a;
  BB_U32(0x4846b4u) = 0x10u;
  goto L_45027a;
L_45006c:;
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
  args[--sp] = 0xd1u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00450089 */
  esi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  args[--sp] = 0x40000u;
  esp -= 4u;
  eax = (eax ^ eax);
  BYTE(esi + 0xe5u) = ((uint8_t)(ecx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
  eax = BB_U32(eax * 4u + 0x5d1150u);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 004500b3 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7au) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xe5u)) & 0xffu) << 0);
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
  COMBAT_CALL(BB_ENTITY_TARGET(bb_fixed_multiply_entry), 0); /* 004500d5 */
  edx = (edx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 28u)) & 0xffu) << 0);
  a = esp;
  b = 0x8u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7eu) = eax;
  ecx = BB_U32(esi + 0x7au);
  BB_U32(esi + 0x6eu) = ecx;
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 19u)) & 0xffu) << 0);
  eax = BB_U32(esi + 0x7eu);
  BB_U32(esi + 0x72u) = eax;
  BYTE(esi + 0xe3u) = ((uint8_t)(edx));
  BYTE(esi + 0xe4u) = ((uint8_t)(ecx));
  ecx = edi;
  BB_U32(esi + 0x14u) = edi;
  eax = BB_U32(0x4843b0u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045010b */
  BYTE(esi + 0xe9u) = ((uint8_t)(ebx));
  goto L_45027a;
L_45011b:;
  eax = BB_U32(edi + 0x20u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_45027a;
  eax = BB_U32(0x4843b0u);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045012e */
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
  args[--sp] = 0x68u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0045014d */
  ecx = eax;
  esi = eax;
  args[--sp] = 0x19u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_profession), 4); /* 00450158 */
  args[--sp] = edi;
  esp -= 4u;
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_secondary_entry), 4); /* 00450160 */
  BB_U32(edi + 0x20u) = esi;
  goto L_45027a;
L_45016d:;
  args[--sp] = 0x190u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0045018f */
  goto L_45027a;
L_450199:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 0045019e */
  goto L_45027a;
L_4501a8:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 004501ad */
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0);              /* 004501b2 */
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
  if (!(flags & 0x40u))
    goto L_45027a;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(0x5e5cc0u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x4u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_45027a;
  esi = BB_U32(edi + 0xcu);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_45027a;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 004501e7 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  BYTE(esi + 0x11eu) = ((uint8_t)(eax));
  goto L_45027a;
L_450200:;
  args[--sp] = edi;
  esp -= 4u;
  ecx = BB_U32(edi + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00450205 */
  goto L_45027a;
L_45020c:;
  eax = BB_U32(edi + 0x6au);
  a = eax;
  b = 0x10000u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_45027a;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0x100u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_45027a;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(edi + 0x62u);
  eax = BB_U32(edi + 0x66u);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0xdeu;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = edx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0045023b */
  BB_U32(eax + 0x14u) = edi;
  BYTE(eax + 0xe9u) = ((uint8_t)(ebx));
  ecx = BB_U32(0x4843a8u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450252 */
  ecx = BB_U32(0x5f9144u);
  args[--sp] = ecx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_sound_play_entry), 0); /* 0045025e */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = 0x553f90u;
  BYTE(edi + 0x100u) = 0x1u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_transfer_entry), 8); /* 00450275 */
L_45027a:;
  eax = BB_U32(edi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450338;
  a = WORD(eax + 0xa0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_450338;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf4u)) & 0xffu) << 0);
  eax = (eax - 0x6u);
  a = eax;
  b = 0x15u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_450338;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_4503a4[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_4502b6;
  case 1:
    goto L_4502cd;
  case 2:
    goto L_4502e4;
  case 3:
    goto L_4502fb;
  case 4:
    goto L_450312;
  case 5:
    goto L_450329;
  case 6:
    goto L_450338;
  default:
    __builtin_unreachable();
  }
L_4502b6:;
  args[--sp] = 0x18u;
  esp -= 4u;
  eax = BB_U32(0x63f028u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_delay), 8); /* 004502c0 */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_4502cd:;
  args[--sp] = 0x18u;
  esp -= 4u;
  eax = BB_U32(0x559114u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_delay), 8); /* 004502d7 */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_4502e4:;
  args[--sp] = 0x18u;
  esp -= 4u;
  eax = BB_U32(0x6a9930u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_delay), 8); /* 004502ee */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_4502fb:;
  args[--sp] = 0x18u;
  esp -= 4u;
  eax = BB_U32(0x6ad1f0u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_delay), 8); /* 00450305 */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_450312:;
  args[--sp] = 0x18u;
  esp -= 4u;
  eax = BB_U32(0x5c7790u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_delay), 8); /* 0045031c */
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
L_450329:;
  args[--sp] = 0x18u;
  esp -= 4u;
  eax = BB_U32(0x63f0b8u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = edi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_delay), 8); /* 00450333 */
L_450338:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
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
void __attribute__((naked)) bb_entity_attack_tick_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_attack_tick_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
static const uint8_t map_451904[] = {
    0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 3, 3, 3, 3, 2};
static const uint8_t map_45198c[] = {0, 1, 6, 6, 6, 6, 6, 6, 6, 6,   2,  2,
                                     2, 2, 6, 6, 3, 6, 6, 6, 6, 6,   6,  6,
                                     6, 6, 6, 6, 6, 6, 6, 4, 5, 141, 73, 0};
static const uint8_t map_451a74[] = {
    0,  1,  2,  39, 39, 3,  3,  39, 39, 39, 39, 39, 39, 39, 39, 39, 4,  39, 39,
    39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 5,  39, 39, 39, 39, 39, 39, 39, 6,
    39, 7,  39, 39, 39, 39, 39, 39, 8,  9,  10, 11, 39, 39, 39, 39, 39, 39, 12,
    39, 39, 39, 13, 14, 39, 39, 39, 39, 15, 39, 39, 39, 39, 39, 39, 39, 16, 17,
    18, 39, 19, 39, 39, 39, 20, 21, 39, 39, 22, 39, 39, 39, 39, 39, 39, 39, 39,
    39, 39, 39, 23, 39, 39, 39, 39, 24, 24, 39, 39, 39, 39, 39, 25, 26, 39, 39,
    27, 27, 27, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 28, 39, 39, 39,
    39, 39, 29, 30, 39, 39, 39, 39, 39, 39, 39, 31, 39, 39, 39, 39, 39, 39, 39,
    39, 39, 39, 39, 39, 32, 39, 39, 39, 39, 33, 33, 39, 39, 39, 39, 39, 34, 39,
    39, 39, 35, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39,
    39, 36, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39,
    39, 39, 39, 39, 39, 39, 37, 39, 39, 38, 144};
void bb_entity_damage_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,12u),12u);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0xf4u)) & 0xffu) << 0);
  ebp = ecx;
  BYTE((uintptr_t)(args + sp) + 24u) = ((uint8_t)(eax));
  a = ((uint8_t)(eax));
  b = 0x1au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_4508cc;
  eax = BB_U32(0x6a9938u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 004508bd */
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
L_4508cc:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x5eu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4518e8;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11bu)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0x60u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_450918;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ebx = BB_U32((uintptr_t)(args + sp) + 32u);
  fp0 = (long double)(int32_t)(BB_U32((uintptr_t)(args + sp) + 32u));
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
  BB_U32((uintptr_t)(args + sp) + 20u) = 0x0u;
  fp1 = (long double)(int64_t)(QWORD((uintptr_t)(args + sp) + 16u));
  fp1 = (long double)BB_DOUBLE(0x4822b0u) - fp1;
  fp0 *= fp1;
  fp0 *= (long double)BB_DOUBLE(0x4822b8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    combat_float_to_int(&converted, &fp0);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ebx = (ebx + eax);
  goto L_45095e;
L_450918:;
  a = ((uint8_t)(ecx));
  b = 0x84u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x41u)
    goto L_45095a;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ebx = BB_U32((uintptr_t)(args + sp) + 32u);
  fp0 = (long double)(int32_t)(BB_U32((uintptr_t)(args + sp) + 32u));
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  BB_U32((uintptr_t)(args + sp) + 16u) = eax;
  BB_U32((uintptr_t)(args + sp) + 20u) = 0x0u;
  fp1 = (long double)(int64_t)(QWORD((uintptr_t)(args + sp) + 16u));
  fp1 = fp1 - (long double)BB_DOUBLE(0x4822c0u);
  fp0 *= fp1;
  fp0 *= (long double)BB_DOUBLE(0x4822c8u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    combat_float_to_int(&converted, &fp0);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ebx = (ebx - eax);
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_45095e;
  ebx = 0x1u;
  goto L_45095e;
L_45095a:;
  ebx = BB_U32((uintptr_t)(args + sp) + 32u);
L_45095e:;
  esi = BB_U32((uintptr_t)(args + sp) + 36u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450ae7;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450985;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_4518e8;
L_450985:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xd4u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450992;
  ebx = (ebx << 2);
L_450992:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x67u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4509a0;
  a = ((uint8_t)(eax));
  b = 0x68u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_4509e2;
L_4509a0:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)(BYTE((uintptr_t)(args + sp) + 24u)) & 0xffu) << 0);
  ecx = eax;
  edx = (eax + eax * 8u);
  eax = (ecx + edx * 8u);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)(BYTE(eax * 4u + 0x5e5f02u)) & 0xffu) << 0);
  edi = (eax * 4u);
  eax = (eax ^ eax);
  eax =
      (eax & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0x5e5f01u)) & 0xffu) << 0);
  ebx = (ebx - eax);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(ecx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_4509dc;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 004509ca */
  edx = (uint32_t)((int32_t)eax >> 31);
  ecx = (ecx ^ ecx);
  ecx =
      (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0x5e5f02u)) & 0xffu) << 0);
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
  ebx = (ebx - edx);
L_4509dc:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_4509e2;
  ebx = (ebx ^ ebx);
L_4509e2:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  ecx = eax;
  eax = (eax << 3);
  eax = (eax - ecx);
  edx = (ecx + eax * 8u);
  ecx = (ecx + edx * 8u);
  edi = BB_U32(ecx * 2u + 0x663578u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450a1b;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11cu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_450a1b;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_450a1b;
  args[--sp] = esi;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_recruit_entry), 4); /* 00450a16 */
L_450a1b:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf2u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x10u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450a35;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 00450a29 */
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00450a30 */
L_450a35:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  a = eax;
  b = 0x67u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_450a94;
  a = eax;
  b = 0x68u;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_450a4e;
  a = eax;
  b = 0x6eu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_450a72;
  goto L_450a94;
L_450a4e:;
  eax = BB_U32(ebp + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_450a72;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11du)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_450a72;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_450a72;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = ebp;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_entry), 12); /* 00450a6d */
L_450a72:;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x84u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00450a8f */
L_450a94:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x8u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450aab;
  eax = BB_U32(0x4843b8u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450aa6 */
L_450aab:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf3u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x14u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450ac2;
  eax = BB_U32(0x4843b8u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450abd */
L_450ac2:;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_450ae7;
  args[--sp] = esi;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = ebp;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  edx = (eax + eax * 2u);
  eax = (eax + edx * 4u);
  a = eax;
  result = (a << 8) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 8, 0);
  eax = result;
  ecx = (eax + 0x554fbcu);
  COMBAT_CALL(BB_ENTITY_TARGET(bb_faction_attack_alert_entry), 8); /* 00450ae2 */
L_450ae7:;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = 0xbfu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450b28;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4518e8;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xaeu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_450b28;
  eax = BB_U32(0x6a9938u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 00450b0c */
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = esi;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_entry), 12); /* 00450b19 */
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
L_450b28:;
  a = ((uint8_t)(ecx));
  b = 0x68u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450b93;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1fu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450b93;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450b93;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xc1u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_450b93;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x102u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_450b63;
  eax = BB_U32(0x6a9918u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 00450b57 */
  BYTE(ebp + 0x102u) = 0x2u;
L_450b63:;
  eax = BB_U32(esi + 0x9cu);
  args[--sp] = 0x1u;
  esp -= 4u;
  a = eax;
  b = 0x80000u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = esi;
  BB_U32(esi + 0x9cu) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_entry), 12); /* 00450b7c */
  args[--sp] = ebp;
  esp -= 4u;
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_evade_entry), 4); /* 00450b84 */
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
L_450b93:;
  edi = BB_U32(ebp + 0x9cu);
  a = edi;
  b = 0x800000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450bb7;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4518e8;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1au;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_4518e8;
L_450bb7:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11au)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0xc0u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_450bc9;
  a = edi;
  b = 0x40000000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450bdb;
L_450bc9:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450bdb;
  eax = ebx;
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x3u);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 2));
  ebx = (eax + 0x1u);
L_450bdb:;
  edx = (edx & 0xffff0000u) | (((uint32_t)(WORD(ebp + 0xa0u)) & 0xffffu) << 0);
  edx = (edx & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(edx)) - ((uint16_t)(ebx)))) & 0xffffu) << 0);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  WORD(ebp + 0xa0u) = ((uint16_t)(edx));
  if (flags & 0x40u)
    goto L_450c10;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11cu)) & 0xffu) << 0);
  eax = (eax + ebx);
  a = eax;
  b = 0xffu;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_450c09;
  BYTE(ebp + 0x11cu) = (BYTE(ebp + 0x11cu) + ((uint8_t)(ebx)));
  goto L_450c10;
L_450c09:;
  BYTE(ebp + 0x11cu) = 0xffu;
L_450c10:;
  a = edi;
  b = 0x100000u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_450caa;
  a = ((uint16_t)(edx));
  b = ((uint16_t)(edx));
  flags = logic_flags(flags, a & b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_450caa;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(((uint8_t)(ecx))) & 0xffu) << 0);
  ecx = (eax * 8u);
  ecx = (ecx - eax);
  edi = (eax + ecx * 8u);
  ecx = (eax + edi * 8u);
  ecx = (ecx + ecx);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)(WORD(ecx + 0x6635aeu)) & 0xffffu) << 0);
  edi = (edi & 0xffff0000u) |
        (((uint32_t)(((uint32_t)((int16_t)(((uint16_t)(edi))) >> 1))) & 0xffffu)
         << 0);
  a = ((uint16_t)(edi));
  b = ((uint16_t)(edx));
  flags = sub_flags(flags, a, b, 16);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_450caa;
  edx = BB_U32(ecx + 0x663578u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450c80;
  eax = BB_U32(0x4843a4u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450c5b */
  eax = BB_U32(0x4843bcu);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450c68 */
  eax = BB_U32(ebp + 0x9cu);
  eax = (eax | 0x80000u);
  BB_U32(ebp + 0x9cu) = eax;
  goto L_450c92;
L_450c80:;
  a = eax;
  b = 0x6eu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_450c92;
  eax = BB_U32(0x484334u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450c8d */
L_450c92:;
  eax = BB_U32(ebp + 0x9cu);
  ecx = ebp;
  a = eax;
  b = 0x100000u;
  result = (a | b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(ebp + 0x9cu) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00450ca5 */
L_450caa:;
  eax = (eax ^ eax);
  edx = 0x1u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x12du)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(0x4843f0u)) & 0xffu) << 0);
  edx = (edx << (((uint8_t)(ecx)) & 31u));
  a = edx;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450cca;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_dirty_entry), 0); /* 00450cc5 */
L_450cca:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  eax = (eax - 1u);
  a = eax;
  b = 0x6bu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_450f11;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_451904[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_450ceb;
  case 1:
    goto L_450d19;
  case 2:
    goto L_450f01;
  case 3:
    goto L_450f11;
  default:
    __builtin_unreachable();
  }
L_450ceb:;
  a = WORD(ebp + 0xa0u);
  b = 0x4bu;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_450f11;
  a = WORD(ebp + 0xa4u);
  b = 0x4bu;
  flags = sub_flags(flags, a, b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_450f11;
  eax = BB_U32(0x4843bcu);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00450d0f */
  goto L_450f11;
L_450d19:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf4u)) & 0xffu) << 0);
  eax = (eax - 1u);
  a = eax;
  b = 0x20u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_450d36;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_45198c[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_450d7e;
  case 1:
    goto L_450ded;
  case 2:
    goto L_450f11;
  case 3:
    goto L_450ecf;
  case 4:
    goto L_450d7e;
  case 5:
    goto L_450ded;
  case 6:
    goto L_450d36;
  default:
    __builtin_unreachable();
  }
L_450d36:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450f11;
  a = ebx;
  b = 0x5u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_450f11;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11eu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_450f11;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 00450d59 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = ebp;
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
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 00450d74 */
  goto L_450f11;
L_450d7e:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450ddd;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450db4;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x102u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_450db4;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x5fe3a0u);
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 00450da8 */
  BYTE(ebp + 0x102u) = 0x10u;
L_450db4:;
  a = ebx;
  b = 0x5u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_450ddd;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 00450dbd */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = ebp;
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
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 00450dd8 */
L_450ddd:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = ebp;
  args[--sp] = 0xfffffff0u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 00450de3 */
  goto L_450f11;
L_450ded:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450ec2;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xf7u)) & 0xffu) << 0);
  a = ((uint8_t)(ecx));
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_450e67;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x102u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_450e67;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 00450e0f */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  eax = (eax & 0x3u);
  eax = (eax ^ edx);
  eax = (eax - edx);
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_450e5e;
  switch (eax) {
  case 0:
    goto L_450e2c;
  case 1:
    goto L_450e37;
  case 2:
    goto L_450e42;
  case 3:
    goto L_450e4d;
  default:
    __builtin_unreachable();
  }
L_450e2c:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x604620u);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450e56;
L_450e37:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x6ad1f4u);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450e56;
L_450e42:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x5116bcu);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450e56;
L_450e4d:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x6a9914u);
  args[--sp] = 0x0u;
  esp -= 4u;
L_450e56:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 00450e59 */
L_450e5e:;
  BYTE(ebp + 0x102u) = 0x10u;
  goto L_450ec2;
L_450e67:;
  a = ebx;
  b = 0x5u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_450ec2;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 00450e6c */
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
  a = edx;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_450ec2;
  switch (edx) {
  case 0:
    goto L_450e85;
  case 1:
    goto L_450e90;
  case 2:
    goto L_450e9b;
  case 3:
    goto L_450ea6;
  case 4:
    goto L_450eb1;
  default:
    __builtin_unreachable();
  }
L_450e85:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x5f9130u);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450eba;
L_450e90:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x5f9134u);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450eba;
L_450e9b:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x5116bcu);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450eba;
L_450ea6:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x5f9128u);
  args[--sp] = 0x0u;
  esp -= 4u;
  goto L_450eba;
L_450eb1:;
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(0x6a9914u);
  args[--sp] = 0x0u;
  esp -= 4u;
L_450eba:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_feedback_entry), 12); /* 00450ebd */
L_450ec2:;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = ebp;
  args[--sp] = 0xfffffff0u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_adjust_111_entry), 8); /* 00450ec8 */
  goto L_450f11;
L_450ecf:;
  a = WORD(ebp + 0xa4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_450f11;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  WORD(ebp + 0xa0u) = 0xffffu;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x4u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00450efa */
  goto L_450f11;
L_450f01:;
  eax = BB_U32(ebp + 0x6eu);
  eax = (0u - eax);
  BB_U32(ebp + 0x6eu) = eax;
  eax = BB_U32(ebp + 0x72u);
  eax = (0u - eax);
  BB_U32(ebp + 0x72u) = eax;
L_450f11:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ebp + 0xa0u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_4518da;
  a = WORD(ebp + 0xa4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_4518da;
  WORD(ebp + 0xa4u) = ((uint16_t)(eax));
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  eax = (eax + edx * 8u);
  ebx = (ebx & 0xffffff00u) |
        (((uint32_t)(BYTE(eax * 2u + 0x6635bcu)) & 0xffu) << 0);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(ebx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_450fda;
  edi = BB_U32(0x554f98u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450f8f;
  esi = 0x1000u;
L_450f69:;
  eax = BB_U32(edi + 0x1cu);
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_450f88;
  eax = BB_U32(edi + 0x9cu);
  a = esi;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_450f88;
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = edi;
  args[--sp] = 0x2710u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00450f83 */
L_450f88:;
  edi = BB_U32(edi + 0x4u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_450f69;
L_450f8f:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  ebx = (eax + edx * 8u);
  esi = BB_U32(ebx * 2u + 0x6638bau);
  eax = (ebx * 2u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_450fda;
  ecx = BB_U32(eax + 0x6638aau);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + ((uint8_t)(edx)))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x6638a6u);
  args[--sp] = ecx;
  esp -= 4u;
  a = ((uint8_t)(eax));
  b = BYTE(ebp + 0xd8u);
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_road_prune_near_entry), 0); /* 00450fd2 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_450fda:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  a = eax;
  b = 0xdau;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_451624;
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(map_451a74[eax]) & 0xffu) << 0);
  switch (ecx) {
  case 0:
    goto L_450ffc;
  case 1:
    goto L_451624;
  case 2:
    goto L_451624;
  case 3:
    goto L_450ffc;
  case 4:
    goto L_451016;
  case 5:
    goto L_45119c;
  case 6:
    goto L_4511c3;
  case 7:
    goto L_451624;
  case 8:
    goto L_451244;
  case 9:
    goto L_451256;
  case 10:
    goto L_451624;
  case 11:
    goto L_45162f;
  case 12:
    goto L_45127d;
  case 13:
    goto L_451624;
  case 14:
    goto L_4512fc;
  case 15:
    goto L_451624;
  case 16:
    goto L_451324;
  case 17:
    goto L_451624;
  case 18:
    goto L_451624;
  case 19:
    goto L_451624;
  case 20:
    goto L_451368;
  case 21:
    goto L_451624;
  case 22:
    goto L_451624;
  case 23:
    goto L_45162f;
  case 24:
    goto L_45138f;
  case 25:
    goto L_4514da;
  case 26:
    goto L_451547;
  case 27:
    goto L_45155a;
  case 28:
    goto L_451624;
  case 29:
    goto L_451624;
  case 30:
    goto L_451624;
  case 31:
    goto L_45162f;
  case 32:
    goto L_451624;
  case 33:
    goto L_45162f;
  case 34:
    goto L_451624;
  case 35:
    goto L_45162f;
  case 36:
    goto L_4515bc;
  case 37:
    goto L_450ffc;
  case 38:
    goto L_450ffc;
  case 39:
    goto L_451624;
  default:
    __builtin_unreachable();
  }
L_450ffc:;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x31u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  goto L_45161a;
L_451016:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0045101c */
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  esi = 0x10000u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451040 */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  a = eax;
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451061 */
  ecx = BB_U32(ebp + 0x6au);
  eax = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  a = eax;
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = 0x7u;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = edx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451082 */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  a = eax;
  b = 0x10000u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004510a6 */
  ecx = BB_U32(ebp + 0x6au);
  eax = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  a = eax;
  b = 0x10000u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004510ca */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xc9u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004510ec */
  BB_U32(eax + 0x76u) = esi;
  BYTE(eax + 0xe5u) = 0x0u;
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xc9u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451118 */
  BB_U32(eax + 0x76u) = esi;
  BYTE(eax + 0xe5u) = 0x40u;
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xc9u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451144 */
  BB_U32(eax + 0x76u) = esi;
  BYTE(eax + 0xe5u) = 0x80u;
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xc9u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451170 */
  BB_U32(eax + 0x76u) = esi;
  BYTE(eax + 0xe5u) = 0xc0u;
  ecx = BB_U32(0x484330u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00451188 */
  BB_U32(0x4846b4u) = 0x32u;
  goto L_45162f;
L_45119c:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 004511a2 */
  eax = BB_U32(0x484330u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 004511af */
  BB_U32(0x4846b4u) = 0xau;
  goto L_45162f;
L_4511c3:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 004511c9 */
  args[--sp] = 0x190u;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 004511ef */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451211 */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 00451230 */
  BB_U32(0x4846b4u) = 0x32u;
  goto L_45162f;
L_451244:;
  eax = BB_U32(0x484330u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045124c */
  goto L_451624;
L_451256:;
  eax = BB_U32(0x484330u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045125e */
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 00451269 */
  BB_U32(0x4846b4u) = 0x18u;
  goto L_45162f;
L_45127d:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xe9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax ^ eax);
  ecx = BB_U32(ebp + 0x14u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 004512a4 */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004512c6 */
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0);         /* 004512cb */
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
  if (!(flags & 0x40u))
    goto L_451624;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  goto L_45161a;
L_4512fc:;
  args[--sp] = 0xau;
  esp -= 4u;
  eax = (eax ^ eax);
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ecx = 0x553f90u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_area_damage_entry), 24); /* 0045131a */
  goto L_451624;
L_451324:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0045132a */
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  args[--sp] = 0xau;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x5e470cu;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_terrain_set_entry), 16); /* 00451346 */
  ecx = BB_U32(0x484330u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00451354 */
  BB_U32(0x4846b4u) = 0x10u;
  goto L_45162f;
L_451368:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0045136e */
  eax = BB_U32(0x484330u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045137b */
  BB_U32(0x4846b4u) = 0xau;
  goto L_45162f;
L_45138f:;
  eax = BB_U32(ebp + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4513d6;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 004513a4 */
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  ecx = BB_U32(ebp + 0x62u);
  eax = (eax - edx);
  eax = (eax & 0x7ffu);
  eax = (eax ^ edx);
  eax = (eax - edx);
  ecx = (ecx + eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  a = ecx;
  b = 0x400u;
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
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004513d1 */
L_4513d6:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf4u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x1u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4513f4;
  a = ((uint8_t)(eax));
  b = 0x2u;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4513f4;
  a = ((uint8_t)(eax));
  b = 0xdu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4513f4;
  a = ((uint8_t)(eax));
  b = 0xeu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4513f4;
  a = ((uint8_t)(eax));
  b = 0xbu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_4513f4;
  a = ((uint8_t)(eax));
  b = 0xcu;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_451405;
L_4513f4:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xceu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x58u;
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 1u))
    goto L_451405;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_morale_shock), 0); /* 00451400 */
L_451405:;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x3au;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0045141f */
  esi = eax;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf4u)) & 0xffu) << 0);
  args[--sp] = 0x1bu;
  esp -= 4u;
  BYTE(esi + 0xeau) = ((uint8_t)(ecx));
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 00451437 */
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11eu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_451455;
  eax = BB_U32(0x6024c8u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 0045144e */
  goto L_45145e;
L_451455:;
  args[--sp] = 0x3u;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00451459 */
L_45145e:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf4u)) & 0xffu) << 0);
  eax = (eax - 0x12u);
  a = eax;
  b = 0xdu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x41u))
    goto L_45149a;
  switch (eax) {
  case 0:
    goto L_451475;
  case 1:
    goto L_45149a;
  case 2:
    goto L_45149a;
  case 3:
    goto L_45149a;
  case 4:
    goto L_45149a;
  case 5:
    goto L_451479;
  case 6:
    goto L_45147d;
  case 7:
    goto L_451481;
  case 8:
    goto L_451485;
  case 9:
    goto L_451489;
  case 10:
    goto L_45147d;
  case 11:
    goto L_451479;
  case 12:
    goto L_45148d;
  case 13:
    goto L_451491;
  default:
    __builtin_unreachable();
  }
L_451475:;
  args[--sp] = 0x1du;
  esp -= 4u;
  goto L_451493;
L_451479:;
  args[--sp] = 0x62u;
  esp -= 4u;
  goto L_451493;
L_45147d:;
  args[--sp] = 0x63u;
  esp -= 4u;
  goto L_451493;
L_451481:;
  args[--sp] = 0x68u;
  esp -= 4u;
  goto L_451493;
L_451485:;
  args[--sp] = 0x68u;
  esp -= 4u;
  goto L_451493;
L_451489:;
  args[--sp] = 0x69u;
  esp -= 4u;
  goto L_451493;
L_45148d:;
  args[--sp] = 0x71u;
  esp -= 4u;
  goto L_451493;
L_451491:;
  args[--sp] = 0x72u;
  esp -= 4u;
L_451493:;
  ecx = esi;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 00451495 */
L_45149a:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 004514a0 */
  eax = BB_U32(ebp + 0x9cu);
  a = eax;
  b = 0x80000u;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_45162f;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x7u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004514d0 */
  goto L_45162f;
L_4514da:;
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0x3au;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004514f4 */
  ecx = eax;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0x11eu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_451520;
  args[--sp] = 0x1fu;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 00451507 */
  eax = BB_U32(0x4843c0u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00451514 */
  eax = BB_U32(0x48436cu);
  goto L_45153a;
L_451520:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xceu)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 0x3cu;
  flags = sub_flags(flags, a, b, 8);
  if (flags & 1u)
    goto L_45152e;
  args[--sp] = 0x77u;
  esp -= 4u;
  goto L_451530;
L_45152e:;
  args[--sp] = 0x76u;
  esp -= 4u;
L_451530:;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_subtype), 4); /* 00451530 */
  eax = BB_U32(0x4843c0u);
L_45153a:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = ebp;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 0045153d */
  goto L_451624;
L_451547:;
  eax = BB_U32(0x69e368u);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_sound_play_entry), 0); /* 0045154d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_451624;
L_45155a:;
  ecx = BB_U32(ebp + 0x1cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_451578;
  eax = BB_U32(ecx + 0x9cu);
  a = ((uint8_t)(eax >> 8));
  b = 0x10u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_451578;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = 0x2710u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_damage_entry), 8); /* 00451573 */
L_451578:;
  eax = BB_U32(0x6ba530u);
  ecx = BB_U32(0x6ba528u);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  ebx = BB_U32(edx * 4u + 0x559b68u);
  a = ebx;
  b = 0xffdfffffu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  BB_U32(edx * 4u + 0x559b68u) = ebx;
  edx = BB_U32(ebp + 0x62u);
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = 0xcau;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  goto L_45161a;
L_4515bc:;
  eax = BB_U32(0x484330u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 004515c4 */
  eax = BB_U32(0x6afbe0u);
  ecx = ebp;
  BB_U32(0x4846b4u) = 0x20u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_command_sound), 4); /* 004515db */
  eax = BB_U32(ebp + 0x6au);
  ecx = BB_U32(ebp + 0x66u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x62u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = edx;
  esp -= 4u;
  ecx = 0x553f90u;
  args[--sp] = 0xccu;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004515fd */
  ecx = BB_U32(ebp + 0x6au);
  edx = BB_U32(ebp + 0x66u);
  args[--sp] = ecx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x62u);
  args[--sp] = edx;
  esp -= 4u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
L_45161a:;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0045161f */
L_451624:;
  args[--sp] = ebp;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_remove), 4); /* 0045162a */
L_45162f:;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  ebx = (eax + edx * 8u);
  esi = BB_U32(ebx * 2u + 0x663578u);
  eax = (ebx * 2u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4517e9;
  ecx = BB_U32(eax + 0x6638bau);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_451688;
  ecx = BB_U32(eax + 0x6638aau);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(ecx)) + ((uint8_t)(edx)))) & 0xffu) << 0);
  eax = BB_U32(eax + 0x6638a6u);
  args[--sp] = ecx;
  esp -= 4u;
  a = ((uint8_t)(eax));
  b = BYTE(ebp + 0xd8u);
  result = (a + b) & 0xffu;
  flags = add_flags(flags, a, b, 8);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_road_prune_near_entry), 0); /* 00451680 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
L_451688:;
  eax = BB_U32(0x484330u);
  ecx = ebp;
  args[--sp] = eax;
  esp -= 4u;
  a = esi;
  b = esi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  esi = result;
  edi = 0xbu;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_sound), 4); /* 00451697 */
  BB_U32(0x4846b4u) = 0x20u;
  BB_U32(0x6ba524u) = esi;
L_4516ac:;
  ebx = 0xffdfffffu;
  BB_U32(0x6ba52cu) = esi;
L_4516b7:;
  ecx = BB_U32(0x6ba524u);
  eax = BB_U32(0x6ba52cu);
  edx = (ecx + ecx * 4u);
  ecx = (ecx + edx * 2u);
  ecx = (ecx + eax);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  edx = eax;
  eax = (eax << 3);
  eax = (eax - edx);
  eax = (edx + eax * 8u);
  eax = (edx + eax * 8u);
  eax = (eax + eax);
  edx = BB_U32(eax + ecx * 4u + 0x663688u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4517be;
  eax = (eax ^ eax);
  ecx = BB_U32(0x6ba52cu);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd8u)) & 0xffu) << 0);
  edx = BB_U32(0x6ba524u);
  eax = (eax + ecx);
  eax = (eax - 0x5u);
  BB_U32(0x6ba528u) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd9u)) & 0xffu) << 0);
  eax = (eax + edx);
  eax = (eax - 0x5u);
  BB_U32(0x6ba530u) = eax;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  ecx = eax;
  eax = (eax << 3);
  eax = (eax - ecx);
  edx = (ecx + eax * 8u);
  eax = (ecx + edx * 8u);
  edx = BB_U32(0x6ba52cu);
  eax = (eax + eax);
  ecx = BB_U32(eax + 0x66389eu);
  ecx = (ecx - edx);
  a = ecx;
  b = 0xfffffffbu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4517a1;
  eax = BB_U32(eax + 0x6638a2u);
  ecx = BB_U32(0x6ba524u);
  eax = (eax - ecx);
  a = eax;
  b = 0xfffffffbu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4517a1;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0045175d */
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
    goto L_4517a1;
  eax = BB_U32(ebp + 0x6au);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6ba530u);
  eax = (eax << 16);
  eax = (eax + 0x8000u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6ba528u);
  eax = (eax << 16);
  a = eax;
  b = 0x8000u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcbu;
  esp -= 4u;
  args[--sp] = 0x4u;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 0045179c */
L_4517a1:;
  eax = BB_U32(0x6ba530u);
  ecx = BB_U32(0x6ba528u);
  eax = (eax << 6);
  eax = (eax + eax * 2u);
  eax = (eax + ecx);
  edx = (eax + eax * 2u);
  BB_U32(edx * 4u + 0x559b68u) = (BB_U32(edx * 4u + 0x559b68u) & ebx);
L_4517be:;
  eax = BB_U32(0x6ba52cu);
  eax = (eax + 1u);
  BB_U32(0x6ba52cu) = eax;
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4516b7;
  eax = BB_U32(0x6ba524u);
  eax = (eax + 1u);
  BB_U32(0x6ba524u) = eax;
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_4516ac;
  goto L_4518da;
L_4517e9:;
  esi = (esi ^ esi);
  ecx = BB_U32(eax + 0x663586u);
  a = ecx;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_4518da;
  edi = 0xbu;
  BB_U32(0x6ba524u) = esi;
L_451804:;
  BB_U32(0x6ba52cu) = esi;
L_45180a:;
  edx = BB_U32(0x6ba524u);
  eax = BB_U32(0x6ba52cu);
  ecx = (edx + edx * 4u);
  edx = (edx + ecx * 2u);
  edx = (edx + eax);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xd4u)) & 0xffu) << 0);
  ecx = eax;
  eax = (eax << 3);
  eax = (eax - ecx);
  ebx = (ecx + eax * 8u);
  eax = (ecx + ebx * 8u);
  eax = (eax + eax);
  ecx = BB_U32(eax + edx * 4u + 0x663688u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4518b4;
  eax = BB_U32(ebp + 0x62u);
  ecx = BB_U32(0x6ba52cu);
  eax = ((uint32_t)((int32_t)(eax) >> 16));
  edx = BB_U32(0x6ba524u);
  eax = (eax + ecx);
  eax = (eax - 0x5u);
  BB_U32(0x6ba528u) = eax;
  eax = BB_U32(ebp + 0x66u);
  eax = ((uint32_t)((int32_t)(eax) >> 16));
  eax = (eax + edx);
  a = eax;
  b = 0x5u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(0x6ba530u) = eax;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_random), 0); /* 0045186b */
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
    goto L_4518b4;
  eax = BB_U32(ebp + 0x6au);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebp + 0xf7u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6ba530u);
  eax = (eax << 16);
  eax = (eax + 0x8000u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(0x6ba528u);
  eax = (eax << 16);
  a = eax;
  b = 0x8000u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xcau;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_create), 20); /* 004518af */
L_4518b4:;
  eax = BB_U32(0x6ba52cu);
  eax = (eax + 1u);
  BB_U32(0x6ba52cu) = eax;
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_45180a;
  eax = BB_U32(0x6ba524u);
  eax = (eax + 1u);
  BB_U32(0x6ba524u) = eax;
  a = eax;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_451804;
L_4518da:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ebp + 0xa0u)) & 0xffffu) << 0);
  WORD(ebp + 0xa4u) = ((uint16_t)(eax));
L_4518e8:;
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
bb_entity_damage_entry(void *object __attribute__((unused)),
                       uint32_t amount __attribute__((unused)),
                       uint32_t attacker __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_damage_c) ";leal 4(%esp),%esp;popal;popfl;ret $8");
}
#endif
void bb_entity_subtype_clear_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,4u),4u);
  BYTE(ecx + 0xf2u) = 0x0u;
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
void __attribute__((naked)) bb_entity_subtype_clear_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_subtype_clear_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_entity_attack_range_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,4u),4u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32(ecx + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  ebx = ecx;
  edi = (edi ^ edi);
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0xd4u)) & 0xffu) << 0);
  ecx = eax;
  eax = (eax << 3);
  eax = (eax - ecx);
  edx = (ecx + eax * 8u);
  ecx = (ecx + edx * 8u);
  a = BB_U32(ecx * 2u + 0x663578u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_455cf6;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0x11du)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_455d3c;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_outer_at), 8); /* 00455c60 */
  a = eax;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455c75;
  edi = 0x1u;
  ebp = args[sp++];
  esp += 4u;
  eax = edi;
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
L_455c75:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd9u)) & 0xffu) << 0);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x553f90u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_outer_at), 8); /* 00455c8a */
  a = eax;
  b = BB_U32(ebx + 0xcu);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455ca0;
  edi = 0x1u;
  ebp = args[sp++];
  esp += 4u;
  eax = edi;
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
L_455ca0:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a - b) & 0xffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_outer_at), 8); /* 00455cb5 */
  a = eax;
  b = BB_U32(ebx + 0xcu);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455ccb;
  edi = 0x1u;
  ebp = args[sp++];
  esp += 4u;
  eax = edi;
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
L_455ccb:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd9u)) & 0xffu) << 0);
  ecx = 0x553f90u;
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd8u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = 1u;
  result = (a + b) & 0xffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 8) & ~1u);
  eax = (eax & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_outer_at), 8); /* 00455ce0 */
  a = eax;
  b = BB_U32(ebx + 0xcu);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_455d3c;
  edi = 0x1u;
  ebp = args[sp++];
  esp += 4u;
  eax = edi;
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
L_455cf6:;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xe4u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xe3u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax ^ eax);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd9u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0xd8u)) & 0xffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  COMBAT_CALL(BB_ENTITY_TARGET(bb_entity_distance), 0); /* 00455d1a */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(ebx + 0x12au)) & 0xffu) << 0);
  ecx = (ecx << 16);
  ecx = (ecx + 0x8000u);
  a = eax;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_455d3c;
  edi = 0x1u;
L_455d3c:;
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
void __attribute__((naked)) bb_entity_attack_range_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_attack_range_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
void bb_entity_target_moved_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  long double fp0 __attribute__((unused)), fp1 __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp,4u),4u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(ecx + 0xcu);
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0x130u)) & 0xffu) << 0);
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xd8u)) & 0xffu) << 0);
  a = ((uint8_t)(ebx));
  b = ((uint8_t)(edx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_455d76;
  ebx = (ebx & 0xffffff00u) | (((uint32_t)(BYTE(ecx + 0x131u)) & 0xffu) << 0);
  a = BYTE(eax + 0xd9u);
  b = ((uint8_t)(ebx));
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_455d76;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
L_455d76:;
  ebx = args[sp++];
  esp += 4u;
  BYTE(ecx + 0x130u) = ((uint8_t)(edx));
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xd9u)) & 0xffu) << 0);
  BYTE(ecx + 0x131u) = ((uint8_t)(eax));
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
void __attribute__((naked)) bb_entity_target_moved_entry(void) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_entity_target_moved_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#endif
