/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_format_fn(id,out,fmt,...) EA_FORMAT(out,fmt,__VA_ARGS__)
#else
#define EA_format_fn(id,...) CALL(id,format_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int (*format_fn)(char *, const char *, ...);
typedef uint32_t(BB_THISCALL * notice_fn)(void *, uint32_t,
                                                        uint32_t);
typedef uint32_t (*sound_fn)(uint32_t);
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00u) | (uint8_t)byte;
}

/* 0x0043f710: clear the command before installing byte attack coordinates.
 * The command's return supplies the upper bytes of the final EAX value.
 */
uint32_t bb_entity_attack_tile_c(void *object, uint32_t x, uint32_t y,
                                 uint32_t eax) {
  eax = bb_entity_command_c(object, 0, 1, 1, eax);
  BYTE(bb_address_encode(object) + 0xe3) = x;
  BYTE(bb_address_encode(object) + 0xe4) = y;
  return low_byte(eax, x);
}

/* 0x00453ba0: complete script/command stat dispatcher, including its table.
 * Stat keys wrap to bytes; absolute health/need changes use live fields and
 * preserve signed word conversion, wrapping deltas and partial returns.
 */
uint32_t BB_THISCALL bb_entity_stat(void *object, uint32_t key,
                                                  uint32_t value) {
  uint32_t node = bb_address_encode(object);
  uint32_t eax = (uint8_t)key - 1u;
  switch ((uint8_t)key) {
  case 1:
    return bb_entity_profession(object, value);
  case 2:
    return bb_entity_set_ability(object, value);
  case 3:
    return bb_entity_adjust(object, 0xa0,
                            value - (uint32_t)(int32_t)SHORT(node + 0xa0), 0);
  case 4:
    return bb_entity_adjust(object, 0xa0, 0,
                            value - (uint32_t)(int32_t)SHORT(node + 0xa2));
  case 5:
    return bb_entity_adjust(object, 0x113, value - BYTE(node + 0x113), 0);
  case 7: {
    uint32_t offset = value * 314u;
    eax = BB_U32(0x00553f90) + offset;
    if (!BB_U32(eax))
      return eax;
    uint8_t kind = BYTE(node + 0xd4);
    if (kind != 0xab && kind != 0xbf && kind != 0xe2)
      return bb_entity_command_c(object, (void *)(uintptr_t)eax, 1, 1, eax);
    BB_U32(node + 0x9c) &= 0xfbffffff;
    eax = BB_U32(0x00553f90) + offset;
    BB_U32(node + 0x24) = eax;
    return eax;
  }
  case 8:
    BYTE(node + 0x92) = value;
    break;
  case 9:
    BYTE(node + 0x93) = value;
    break;
  case 10:
    BYTE(node + 0x94) = value;
    break;
  case 11:
    BYTE(node + 0xe5) = value;
    break;
  case 12:
    BYTE(node + 0xe7) = value;
    break;
  case 13:
    BYTE(node + 0xe6) = value;
    break;
  case 14:
  case 15:
  case 20:
  case 24: {
    uint32_t mask = (uint8_t)key == 14   ? 0x200000
                    : (uint8_t)key == 15 ? 0x400000
                    : (uint8_t)key == 20 ? 0x800000
                                         : 0x10000000;
    eax = BB_U32(node + 0x9c);
    eax = value ? eax | mask : eax & ~mask;
    BB_U32(node + 0x9c) = eax;
    return eax;
  }
  case 16:
    BYTE(node + 0xea) = value;
    return bb_entity_dirty_c(object, eax, 0);
  case 17:
    eax = bb_entity_transfer_c((void *)(uintptr_t)0x00553f90, value, node, eax);
    return bb_entity_dirty_c(object, eax, 0);
  case 18:
    BYTE(node + 0xd6) = value;
    break;
  case 19:
    BYTE(node + 0xd7) = value;
    break;
  case 21:
    eax = bb_entity_subtype(object, value);
    eax = low_byte(eax, BYTE(node + 0xd4));
    if ((uint8_t)eax == 0xab || (uint8_t)eax == 0xe2)
      BYTE(node + 0x12b) = 1;
    break;
  case 22:
    BYTE(node + 0xe8) = value;
    break;
  case 23:
    BB_U32(node + 0x9c) |= 0x4000000;
    eax = BB_U32(0x00553f90) + value * 314u;
    BB_U32(node + 0x24) = eax;
    break;
  case 25:
    BYTE(node + 0x10b) = value;
    return bb_entity_speed_c(object);
  }
  return eax;
}

/* 0x00453140: complete spell request. Player mana failure notifies and
 * plays feedback; an enemy's failed request is silent. Successful requests
 * create the effect before changing the caster and re-read its live mana.
 */
uint32_t BB_THISCALL bb_entity_cast(void *object,
                                                  uint32_t requested_spell,
                                                  uint32_t requested_x,
                                                  uint32_t requested_y) {
  uint32_t node = bb_address_encode(object);
  uint8_t spell = requested_spell;
  uint32_t cost_slot = 0x00640f20 + spell;
  if (BYTE(node + 0xd4) == 0x67 && BYTE(cost_slot) > BYTE(node + 0x109)) {
    uint8_t player = BYTE(0x004843f0);
    if (BYTE(node + 0xf7) != player)
      return player;
    uint32_t format = BB_U32(0x00603948);
    EA_format_fn(0x0046bae0,(char *)(uintptr_t)0x006033d8,(const char *)(uintptr_t)(format ? format : 0x0048481c));
    bb_message_append_entry((void *)(uintptr_t)0x005ecf38, 0x006033d8, 1);
    return bb_sample_play_entry(BB_U32(0x004842e0));
  }
  uint8_t x = requested_x, y = requested_y;
  uint32_t z = BB_U32(node + 0x6a);
  uint32_t world_y =
      !x && !y ? BB_U32(node + 0x66) : ((uint32_t)y << 16) + 0x8000;
  uint32_t world_x =
      !x && !y ? BB_U32(node + 0x62) : ((uint32_t)x << 16) + 0x8000;
  void *effect = bb_entity_create((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0x8d,
                                  world_x, world_y, z);
  uint32_t eax = bb_entity_subtype(effect, requested_spell);
  BB_SET_POINTER((uintptr_t)effect + 0x14, object);
  eax = low_byte(eax, BYTE(node + 0xd4));
  if ((uint8_t)eax != 0x67)
    return eax;
  eax = bb_entity_subtype(object, 14);
  uint8_t current = BYTE(node + 0x109);
  BYTE(node + 0xf9) = 0;
  uint8_t cost = BYTE(cost_slot);
  eax = low_byte(eax, cost);
  if (current >= cost)
    eax = bb_entity_adjust(object, 0x109, 0u - cost, 0);
  uint32_t index = (uint32_t)spell - 0x47;
  if (index > 32)
    return eax;
  static const uintptr_t feedback[14] = {
      0x005f9120, 0x005f91b0, 0x005f9170, 0x005f9114, 0x005fe27c,
      0x005f91b8, 0x005f917c, 0x005f91a0, 0x005f9178, 0x005f910c,
      0x005fe284, 0x005f9190, 0x005f9184, 0x005f91a8};
  if (index > 10 && index < 30)
    return 14;
  index = index <= 10 ? index : index - 19;
  uint32_t sound = BB_U32(feedback[index]);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sound, 0, 0, sound);
}

#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_attack_tile_entry(void) {
  __asm__ volatile(
      "pushl %eax;pushl 12(%esp);pushl 12(%esp);pushl %ecx;call " BB_ASM(
          bb_entity_attack_tile_c) ";addl $16,%esp;ret $8");
}
#endif
