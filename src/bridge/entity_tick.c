/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_zero_fn(id,a0) BB_GAME_CALL(id,EA_WORD(a0))
#else
#define EA_zero_fn(id,...) CALL(id,zero_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
typedef uint32_t(BB_THISCALL * zero_fn)(void *);
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef uint32_t(BB_THISCALL *
                 four_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
static uint32_t low_byte(uint32_t eax, uint32_t byte) {
  return (eax & 0xffffff00u) | (uint8_t)byte;
}
static int32_t random_value(void) { return bb_random(); }
static uint32_t dirty(void *object, uint32_t eax) {
  return bb_entity_dirty_c(object, eax, 0);
}
static uint32_t damage(void *object, uint32_t amount) {
  return
#ifdef BB_MODEL_RUNTIME
BB_GAME_CALL(0x4508a0,bb_address_encode(object), amount, 0)
#else
((two_fn)(uintptr_t)bb_entity_damage_entry)(object, amount, 0)
#endif
;
}
static uint32_t feedback(void *object, uintptr_t slot) {
  uint32_t sample = BB_U32(slot);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sample, 0, 0, sample);
}
static uint32_t remove_node(void *object) {
  return bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
}
static uint32_t replace(void *object, uint32_t kind) {
  uint32_t node = bb_address_encode(object);
  remove_node(object);
  return (uintptr_t)bb_entity_create((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7),
                                     kind, BB_U32(node + 0x62),
                                     BB_U32(node + 0x66), BB_U32(node + 0x6a));
}
/* Copy the three bytes in order. The parent and child can alias, and calls
 * before these reads can change the parent or the optional destination.
 */
static uint32_t child_order(uint32_t parent, uint32_t child, uint32_t eax) {
  eax = BB_U32(parent + 0x9c);
  if (eax & 0x4000000)
    eax = bb_entity_secondary_c((void *)(uintptr_t)child, BB_POINTER(parent + 0x24));
  else {
    uint32_t other = BB_U32(parent + 0x24);
    eax = bb_entity_command_c((void *)(uintptr_t)child, (void *)(uintptr_t)other, 1, 1,
                              other);
    BB_U32(child + 0x9c) |= 0x8000000;
  }
  eax = low_byte(eax, BYTE(parent + 0x92));
  BYTE(child + 0x92) = eax;
  BYTE(child + 0x93) = BYTE(parent + 0x93);
  BYTE(child + 0x94) = BYTE(parent + 0x94);
  eax = low_byte(eax, BYTE(parent + 0xd6));
  if ((uint8_t)eax && BYTE(parent + 0xd7))
    eax =
        bb_entity_move_escape((void *)(uintptr_t)child, (uint8_t)eax, BYTE(parent + 0xd7));
  return eax;
}

/* 0x00442f40: complete kind-specific periodic simulation. A strain outside
 * 1..3 consumes the original uninitialized four-byte local; the entry adapter
 * exposes that stack input explicitly rather than inventing a damage value.
 */
uint32_t bb_entity_kind_tick_c(void *object, uint32_t stack_damage) {
  uint32_t node = bb_address_encode(object);
  uint32_t timer = BYTE(node + 0x11c);
  if (timer)
    BYTE(node + 0x11c) = timer < 50 ? 0 : timer - 50;
  if (BYTE(node + 0x102))
    --BYTE(node + 0x102);
  uint32_t kind = BYTE(node + 0xd4), eax = kind - 0x31u;
  switch (kind) {
  case 0x31:
  case 0x83:
    eax = low_byte(eax, BYTE(node + 0xf5) + 1u);
    BYTE(node + 0xf5) = eax;
    if ((uint8_t)eax != 4)
      return eax;
    if (kind == 0x83)
      return replace(object, 0x31);
    remove_node(object);
    return bb_terrain_set_entry((void *)(uintptr_t)0x005e470c, BYTE(node + 0xd8),
                                BYTE(node + 0xd9), 0, 1);
  case 0x3a:
    eax = random_value() % 2;
    if ((int32_t)eax)
      return eax;
    eax = low_byte(eax, BYTE(0x005e5cc1));
    if ((uint8_t)eax) {
      eax = low_byte(eax, BYTE(0x004843f0));
      if (BYTE(node + 0xf7) == (uint8_t)eax) {
        int32_t chance = random_value() % 100;
        if (chance < BYTE(0x005e5cc1))
          BYTE(node + 0x11e) = random_value() % 4;
      }
    }
    int32_t value = random_value();
    eax = value / 6;
    if (value % 6)
      return eax;
    eax = BYTE(node + 0xf2) - 0x1bu;
    switch (BYTE(node + 0xf2)) {
    case 0x1b:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
      eax = bb_entity_subtype(object, 0x25);
      return dirty(object, eax);
    case 0x1d:
    case 0x1e:
    case 0x25:
    case 0x62:
    case 0x63:
    case 0x68:
    case 0x69:
    case 0x71:
    case 0x72:
      return remove_node(object);
    default:
      return eax;
    }
  case 0x4e:
    eax = BB_U32(0x00484768);
    if (eax == 1 && !BYTE(node + 0xf2)) {
      dirty(object, 0);
      eax = bb_entity_subtype(object, 1);
      BYTE(node + 0xf9) = 1;
    } else if (BB_U32(0x00484768) == 2 && BYTE(node + 0xf2) == 1) {
      dirty(object, 1);
      eax = bb_entity_subtype(object, 2);
      BYTE(node + 0xf9) = 2;
    }
    eax = BB_U32(0x00484768);
    if (eax == 3 && BYTE(node + 0xf2) < 4) {
      eax = low_byte(eax, BYTE(node + 0xf2));
      if ((uint8_t)eax == 0)
        BYTE(node + 0xea) = 0x48;
      else if ((uint8_t)eax == 1)
        BYTE(node + 0xea) = 0x24;
      else if ((uint8_t)eax == 2)
        BYTE(node + 0xea) = 0;
      dirty(object, eax);
      eax = bb_entity_subtype(object, 3);
    }
    eax = low_byte(eax, BYTE(node + 0xf2));
    return (uint8_t)eax == 5 ? remove_node(object) : eax;
  case 0x5e:
  case 0x5f:
  case 0x60:
  case 0x86:
    eax = low_byte(eax, BYTE(node + 0xf5) - 1u);
    BYTE(node + 0xf5) = eax;
    if (kind != 0x86 && (uint8_t)eax == 3) {
      BYTE(node + 0xf9) = 1;
      return dirty(object, eax);
    }
    if ((uint8_t)eax)
      return eax;
    return replace(object, kind == 0x5e   ? 0x4f
                           : kind == 0x5f ? 0x6f
                           : kind == 0x60 ? 0x6e
                                          : 6);
  case 0x67: {
    uint32_t strain = BYTE(node + 0x11e);
    if (strain) {
      uint32_t part = BYTE(node + 0xf4);
      int female = part == 2 || part == 14 || part == 12;
      uint32_t loss = stack_damage;
      if (strain >= 1 && strain <= 3) {
        if (strain == 1) {
          const uintptr_t male[] = {0x005117d4, 0x00663514, 0x005ecf8c};
          const uintptr_t woman[] = {0x00602494, 0x005e4750, 0x00662c7c};
          const uintptr_t *slots = female ? woman : male;
          for (unsigned i = 0; i < 3; i++)
            if (random_value() % 2 == 0) {
              feedback(object, slots[i]);
              break;
            }
        } else if (strain == 2) {
          uintptr_t slot = female ? 0x00663508 : 0x006afce8;
          if (random_value() % 2) {
            slot = female ? 0x005db170 : 0x006aff3c;
            if (random_value() % 2 == 0)
              slot = female ? 0x005ecfa4 : 0x006ad258;
          }
          feedback(object, slot);
        } else {
          if (random_value() % 2 == 0)
            feedback(object, 0x006b0828);
          else if (random_value() % 2 == 0)
            feedback(object, female ? 0x005db03c : 0x006afcf0);
        }
        int32_t interval = strain == 1 ? 10 : strain == 2 ? 20 : 5;
        loss = random_value() % interval + interval;
        uint32_t resistance = BYTE(node + 0x11b);
        if (resistance > 96 &&
            random_value() % (interval * 20) < (int32_t)(resistance - 96) / 2)
          BYTE(node + 0x11e) = 0;
      }
      eax = damage(object, loss);
    }
    if (BYTE(node + 0x100) < 16) {
      damage(object, 16u - BYTE(node + 0x100));
      eax = bb_entity_adjust(object, 0x111,
                             (16 - (int32_t)BYTE(node + 0x100)) / 2, 0);
    }
    if (BYTE(node + 0x101) < 16) {
      damage(object, 16u - BYTE(node + 0x101));
      eax = bb_entity_adjust(object, 0x111,
                             (16 - (int32_t)BYTE(node + 0x101)) / 2, 0);
    }
    bb_entity_adjust(object, 0x100, -2u, 0);
    bb_entity_adjust(object, 0x101, -2u, 0);
    eax = bb_entity_adjust(object, 0x11b, -2u, 0);
    if (!BB_U32(node + 0x1c))
      eax = bb_entity_adjust(object, 0x11b, -3u, 0);
    eax = BB_U32(node + 0x9c);
    if (eax & 0x200000) {
      BYTE(node + 0x100) = 128;
      BYTE(node + 0x101) = 128;
      if (BYTE(node + 0xce) > 12)
        --BYTE(node + 0xce);
    }
    if (BYTE(node + 0x116))
      --BYTE(node + 0x116);
    if (BYTE(node + 0x117))
      --BYTE(node + 0x117);
    bb_entity_adjust(object, 0x113, 4, 0);
    eax = bb_entity_adjust(object, 0x109, 4, 0);
    if (BYTE(node + 0x100) > 64)
      eax = bb_entity_adjust(object, 0x111, 4, 0);
    if (BYTE(node + 0x101) > 64)
      eax = bb_entity_adjust(object, 0x111, 4, 0);
    if (BYTE(node + 0x100) < 32)
      eax = bb_entity_adjust(object, 0x111, -4u, 0);
    if (BYTE(node + 0x101) < 32)
      eax = bb_entity_adjust(object, 0x111, -4u, 0);
    eax = bb_entity_adjust(object, 0x111, -2u, 0);
    if (BYTE(node + 0x111) < 64) {
      if (BYTE(0x005e5cc0) >= 4)
        bb_entity_adjust(object, 0x10d, -4u, 0);
      eax = bb_entity_adjust(object, 0x10f, -4u, 0);
      if (BYTE(node + 0x111) < 16)
        eax = bb_entity_adjust(object, 0x10d, -4u, 0);
    }
    if (BYTE(0x005e5cc0) >= 3)
      eax = bb_entity_adjust(object, 0x10d, -1u, 0);
    if (BYTE(node + 0x111) > 192) {
      bb_entity_adjust(object, 0x10d, 4, 0);
      eax = bb_entity_adjust(object, 0x10f, 4, 0);
    }
    eax = (eax & 0xffff0000u) | WORD(node + 0xa2);
    if (SHORT(node + 0xa0) < (int16_t)eax &&
        (BYTE(node + 0xf4) == 1 || BYTE(node + 0xf4) == 2))
      eax = bb_entity_adjust(object, 0xa0, 2, 0);
    uint32_t resistance = BYTE(node + 0x11b);
    eax = low_byte(eax, BYTE(node + 0xce) + 1u);
    BYTE(node + 0xce) = eax;
    if (resistance > 128) {
      eax = random_value() % 256;
      if ((int32_t)eax < (int32_t)(resistance - 128))
        --BYTE(node + 0xce);
    }
    eax = low_byte(eax, BYTE(node + 0xf6));
    if ((uint8_t)eax)
      return eax;
    eax = low_byte(eax, BYTE(node + 0xce));
    if ((uint8_t)eax >= 12 && (uint8_t)eax < 88) {
      eax = low_byte(eax, BYTE(node + 0xf4));
      if ((uint8_t)eax == 13 || (uint8_t)eax == 14) {
        bb_entity_profession(object, (uint8_t)eax == 13 ? 1 : 2);
        bb_entity_adjust(object, 0xa0, 0, 255u - (int32_t)SHORT(node + 0xa2));
        eax = bb_entity_adjust(object, 0xa0, 255, 0);
        eax = dirty(object, eax);
      }
    }
    uint32_t age = BYTE(node + 0xce), part = BYTE(node + 0xf4);
    if (age >= 88 && age < 96 && part != 12 && part != 11) {
      uint32_t owner = BYTE(node + 0xf7);
      eax = low_byte(eax, owner);
      if (owner == BYTE(0x004843f0) ||
          WORD((part == 2 ? 0x005559c0 : 0x005559be) + owner * 0xd00u) >= 2) {
        eax = bb_entity_profession(object, part == 2 ? 12 : 11);
        ++BB_U32(0x00555c1c + BYTE(node + 0xf7) * 0xd00u);
        eax = dirty(object, eax);
      } else
        BYTE(node + 0xce) = age - 30;
    }
    eax = low_byte(eax, BYTE(node + 0xce));
    if ((uint8_t)eax < 96)
      return eax;
    eax = low_byte(eax, BYTE(node + 0xf4));
    if ((uint8_t)eax == 11 || (uint8_t)eax == 12)
      if (random_value() % 4 == 0 && BYTE(node + 0xf4) == 2)
        feedback(object, 0x00663518);
    bb_entity_adjust(object, 0xa0, 0, -20u);
    return damage(object, random_value() % 5);
  }
  case 0x68:
    eax = BYTE(node + 0xf4);
    if (eax != 26)
      return eax;
    eax = BYTE(node + 0x100);
    return eax == 1 ? replace(object, 0xe6) : eax;
  case 0x6e:
    if (BYTE(node + 0xf5))
      --BYTE(node + 0xf5);
    eax = BB_U32(node + 0x9c);
    if (eax & 0x200000)
      return eax;
    eax = low_byte(eax, BYTE(node + 0xce) + 1u);
    BYTE(node + 0xce) = eax;
    return (uint8_t)eax >= 60 ? damage(object, random_value() % 10) : eax;
  case 0x6f:
    eax = low_byte(eax, BYTE(node + 0xf5));
    if (!(uint8_t)eax)
      return eax;
    eax = low_byte(eax, (uint8_t)eax - 1u);
    BYTE(node + 0xf5) = eax;
    if ((uint8_t)eax)
      return eax;
    bb_entity_subtype(object, 0x5c);
    eax = bb_entity_sound(object, BB_U32(0x00484344));
    return dirty(object, eax);
  case 0x90:
    eax = low_byte(eax, BYTE(node + 0x101));
    if ((uint8_t)eax < 3) {
      uint32_t previous = (uint8_t)eax;
      eax = low_byte(eax, previous + 1);
      BYTE(node + 0x101) = eax;
      eax = bb_terrain_set_entry((void *)(uintptr_t)0x005e470c, BYTE(node + 0xd8),
                                 BYTE(node + 0xd9), 0x2e + previous, 1);
      BYTE(node + 0xea) = 0;
      BYTE(node + 0xf5) = 5;
      return eax;
    }
    eax = low_byte(eax, BYTE(node + 0xea));
    if (!(uint8_t)eax)
      return eax;
    eax = low_byte(eax, BYTE(node + 0xf5) - 1u);
    BYTE(node + 0xf5) = eax;
    if (!(uint8_t)eax)
      BYTE(node + 0xea) = 0;
    return eax;
  case 0xab:
  case 0xe2:
    eax = low_byte(eax, BYTE(node + 0xf2));
    if ((uint8_t)eax)
      return eax;
    for (;;) {
      eax = low_byte(eax, BYTE(node + 0xe8));
      if (BYTE(node + 0xea) >= (uint8_t)eax && (uint8_t)eax)
        return eax;
      eax = low_byte(eax, BYTE(node + 0xf5) + 1u);
      BYTE(node + 0xf5) = eax;
      if (BYTE(node + 0xe5) > (uint8_t)eax)
        return eax;
      eax = low_byte(eax, BYTE(node + 0xe7));
      uint32_t spawn_kind = (uint8_t)eax, owner = BYTE(node + 0xf7), population;
      if (spawn_kind == 0x6e)
        population = WORD(0x0055503a + owner * 0xd00u + 0x6e * 2u) +
                     WORD(0x005550fa + owner * 0xd00u) +
                     (owner ? WORD(0x00555116) + WORD(0x005550fa) : 0);
      else if (spawn_kind == 0x67 || spawn_kind == 0x68)
        population = WORD(0x005559bc + owner * 0xd00u + BYTE(node + 0xf4) * 2u);
      else
        population = WORD(0x0055503a + owner * 0xd00u + spawn_kind * 2u);
      if (population >= BYTE(node + 0xe6)) {
        BYTE(node + 0xf5) = 0;
        return eax;
      }
      uint32_t child = (uintptr_t)bb_entity_create(
          (void *)(uintptr_t)0x00553f90, owner, spawn_kind, BB_U32(node + 0x62),
          BB_U32(node + 0x66), BB_U32(node + 0x6a));
      eax = low_byte(child, BYTE(node + 0xe7));
      if (((uint8_t)eax == 0x67 || (uint8_t)eax == 0x68) && child) {
        BYTE(child + 0x111) = 128;
        BYTE(child + 0x112) = 255;
        BYTE(child + 0x10d) = 128;
        BYTE(child + 0x10e) = 255;
        eax = bb_entity_profession((void *)(uintptr_t)child, BYTE(node + 0xf4));
        BYTE(child + 0x100) = 128;
        BYTE(child + 0x101) = 128;
        eax = child_order(node, child, eax);
      }
      eax = low_byte(eax, BYTE(node + 0xea) + 1u);
      BYTE(node + 0xea) = eax;
      BYTE(node + 0xf5) = 0;
    }
  case 0xbf:
    eax = BB_U32(0x00484288);
    if (!eax)
      return eax;
    eax = low_byte(eax, BYTE(node + 0xf5) + 1u);
    BYTE(node + 0xf5) = eax;
    if (BYTE(node + 0xe5) > (uint8_t)eax)
      return eax;
    uint32_t population = 0;
    for (unsigned faction = 0; faction < 5; faction++)
      population += WORD(0x005559e0 + faction * 0xd00u);
    eax = BYTE(node + 0xe6);
    if (population < eax) {
      uint32_t child = (uintptr_t)bb_entity_create(
          (void *)(uintptr_t)0x00553f90, 4, 0x68, BB_U32(node + 0x62), BB_U32(node + 0x66),
          BB_U32(node + 0x6a));
      eax = child;
      if (child) {
        eax = bb_entity_profession((void *)(uintptr_t)child, 18);
        eax = child_order(node, child, eax);
      }
    }
    BYTE(node + 0xf5) = 0;
    return eax;
  default:
    return eax;
  }
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_kind_tick_entry(void) {
  __asm__ volatile("pushl -4(%esp);pushl %ecx;call " BB_ASM(
      bb_entity_kind_tick_c) ";addl $8,%esp;ret");
}
#endif

/* 0x004347b0: visit all 5,000 slots in order, rereading the live pool base
 * after every entity tick, then drain pending removals through C.
 */
uint32_t BB_THISCALL bb_entity_pool_tick(void *manager) {
  uint32_t self = bb_address_encode(manager);
  for (unsigned offset = 0; offset < 5000u * 314u; offset += 314u) {
    uint32_t node = BB_U32(self) + offset;
    if (BB_U32(node))
      EA_zero_fn(0x00449c50,(void *)(uintptr_t)node);
  }
  return bb_entity_remove_pending(manager);
}
