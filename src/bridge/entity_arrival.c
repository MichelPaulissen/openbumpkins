/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_plain_one_fn(id,a0) BB_GAME_CALL(id,0,EA_WORD(a0))
#else
#define EA_plain_one_fn(id,...) CALL(id,plain_one_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef uint32_t(BB_THISCALL *
                 four_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * six_fn)(void *, uint32_t, uint32_t,
                                                     uint32_t, uint32_t,
                                                     uint32_t, uint32_t);
typedef uint32_t (*plain_one_fn)(void *);
typedef int32_t (*random_fn)(void);

static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00) | (uint8_t)byte;
}
static uint32_t feedback(void *object, uintptr_t slot) {
  uint32_t sound = BB_U32(slot);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sound, 0, 0, sound);
}
static uint32_t create_at(uint32_t node, uint32_t kind) {
  uint32_t z = BB_U32(node + 0x6a), y = BB_U32(node + 0x66);
  uint32_t x = BB_U32(node + 0x62), faction = BYTE(node + 0xf7);
  return (uintptr_t)bb_entity_create((void *)(uintptr_t)0x00553f90, faction, kind, x, y,
                                     z);
}
static uint32_t remove_target(uint32_t node) {
  return bb_entity_remove((void *)(uintptr_t)0x00553f90, BB_POINTER(node + 0x2c));
}

/* 0x0043fe30..0x004412ed: the complete arrival-action dispatcher.
 * Distance is captured before callbacks; target/partner slots and byte state
 * stay live wherever the executable reads them again. EAX's partial byte
 * writes are observable on failed gates and through the dirty helper.
 * Faction/UI notifications and scripted acquisition
 * routines called below remain bridges; no original dispatcher path survives.
 */
uint32_t BB_THISCALL bb_entity_arrive(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t eax = BB_U32(node + 0x18);
  BB_U32(node + 0x2c) = eax;
  BB_U32(node + 0x18) = 0;
  if (!eax)
    return eax;
  eax = bb_entity_distance(BYTE(eax + 0xd8), BYTE(eax + 0xd9),
                           BYTE(node + 0xd8), BYTE(node + 0xd9));
  int near = (int32_t)eax <= 0x18000;
  uint8_t state = BYTE(node + 0xf6);
  if (state == 14 || state == 15) {
    eax = BB_U32(node + 0x28);
    if (eax && eax == BB_U32(node + 0x2c)) {
      bb_entity_subtype(object, state == 14 ? 0x74 : 0x75);
      eax = BB_U32(node + 0x2c);
      BB_U32(node + 0x14) = eax;
    }
    goto clear_activity;
  }
  if (state != 16) {
    eax = BB_U32(node + 0x9c);
    if (eax & 0x04000000) {
      eax = BB_U32(node + 0x2c);
      if (BB_U32(node + 0x24) == eax)
        goto secondary;
    }
    if (state == 17) {
      eax = low_byte(eax, BYTE(node + 0xf4));
      if ((uint8_t)eax != 6)
        goto clear_activity;
      eax = BB_U32(node + 0x2c);
      switch (BYTE(eax + 0x115)) {
      case 1:
        feedback(object, 0x006a98b4);
        break;
      case 3:
        feedback(object, 0x005e4720);
        break;
      case 4:
        feedback(object, 0x0060014c);
        break;
      case 5:
        feedback(object, 0x00602190);
        break;
      }
      eax = BB_U32(node + 0x2c);
      BB_U32(node + 0x14) = eax;
      BB_U32(eax + 0x9c) |= 0x40000;
      BYTE(node + 0xfb) = 13;
      eax = bb_entity_reset_move(BB_POINTER(node + 0x14));
      eax = bb_entity_command_c(BB_POINTER(node + 0x14), 0, 1, 1, eax);
      bb_entity_set_ability(BB_POINTER(node + 0x14), 0);
      eax = bb_entity_set_ability(object, 0);
      goto clear_activity;
    }
    goto dispatch;
  }
secondary:
  eax = bb_entity_has_ability(object, 0x1a);
  if (eax) {
    eax = BB_U32(node + 0x9c) | 0x04000000;
    BB_U32(node + 0x9c) = eax;
    return eax;
  }
  BYTE(node + 0xf6) = 0;
  BB_U32(node + 0x24) = BB_U32(node + 0x2c);
dispatch:;
  uint32_t target = BB_U32(node + 0x2c);
  uint32_t kind = BYTE(target + 0xd4);
  eax = kind - 1;
  switch (kind) {
  case 1:
    eax = BB_U32(node + 0x28);
    if (eax != target)
      goto clear_activity;
    eax = low_byte(eax, BYTE(node + 0xf6));
    if ((uint8_t)eax == 2) {
      eax = bb_entity_trade_c(object, target, 1, 3, 150, eax);
      goto clear_activity;
    }
    if ((uint8_t)eax == 11) {
      eax = BB_U32(node + 0x30);
      if (!eax)
        goto clear_activity;
      if (BB_U32(eax + 0x2c) != target || BYTE(eax + 0x11d))
        return eax;
      BYTE(0x005e5eff + BYTE(node + 0xf4) * 292) = 2;
      uint8_t role = BYTE(0x005e5eff + BYTE(node + 0xf4) * 292);
      BYTE(node + 0xf6) = 0;
      BYTE(BB_U32(node + 0x30) + 0xf6) = 0;
      return role ? bb_entity_partner_name(object, BB_POINTER(node + 0x30))
                  : bb_entity_partner_name(BB_POINTER(node + 0x30), object);
    }
    eax = low_byte(eax, BYTE(node + 0xf7));
    if (BYTE(target + 0xf7) != (uint8_t)eax)
      return eax;
    eax = bb_entity_can_consume(object, target, 12);
    return eax ? bb_entity_consume(object, BB_U32(node + 0x2c), 12) : eax;
  case 2:
    eax = BB_U32(node + 0x28);
    if (eax != target)
      goto clear_activity;
    eax = low_byte(eax, BYTE(node + 0xf6));
    if ((uint8_t)eax == 3) {
      uint32_t partner = BB_U32(node + 0x30);
      if (!partner)
        BYTE(node + 0xf6) = 0;
      else {
        eax = BB_U32(partner + 0x2c);
        if (eax == target && BYTE(node + 0x114) < 100) {
          bb_entity_command_sound(BYTE(node + 0xf4) == 1 ? object
                                                         : (void *)(uintptr_t)partner,
                                  BB_U32(0x005f925c));
          bb_entity_adjust(object, 0x113, 0u - BYTE(node + 0x113), 0);
          bb_entity_adjust(object, 0x11b, 8, 0);
          partner = BB_U32(node + 0x30);
          uint32_t amount = 0u - BYTE(node + 0x113);
          BYTE(node + 0x103) = 8;
          BYTE(node + 0x114) = 0;
          bb_entity_adjust((void *)(uintptr_t)partner, 0x113, amount, 0);
          bb_entity_adjust(BB_POINTER(node + 0x30), 0x11b, 8, 0);
          BYTE(BB_U32(node + 0x30) + 0x103) = 8;
          BYTE(BB_U32(node + 0x30) + 0x114) = 0;
          if (BYTE(node + 0xf4) == 2)
            BYTE(node + 0x108) = 10;
          else
            BYTE(BB_U32(node + 0x30) + 0x108) = 10;
          eax = BB_U32(node + 0x30);
          BYTE(node + 0xf6) = 0;
          BYTE(eax + 0xf6) = 0;
        } else {
          eax = low_byte(eax, BYTE(node + 0x114));
          if ((uint8_t)eax <= 1) {
            eax = low_byte(eax, BYTE(node + 0xe2) + 1);
            BYTE(node + 0xe2) = eax;
            if ((uint8_t)eax > 2)
              BYTE(node + 0xf6) = 0;
            else
              BYTE(node + 0x114) = 255;
          }
        }
      }
      eax = low_byte(eax, BYTE(node + 0xf4));
      if ((uint8_t)eax == 0x21)
        return bb_entity_profession(object, 2);
      return (uint8_t)eax == 0x20 ? bb_entity_profession(object, 1) : eax;
    }
    if ((uint8_t)eax == 11) {
      BYTE(node + 0xfb) = 0;
      void *found = bb_entity_nearest((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 1,
                                      BYTE(node + 0xd8), BYTE(node + 0xd9));
      if (!found) {
        eax = 0;
        goto clear_activity;
      }
      eax = bb_entity_profession(object, BYTE(node + 0xf4) == 2 ? 0x21 : 0x20);
      return bb_entity_activity_c(object, 11, found, eax);
    }
    eax = low_byte(eax, BYTE(node + 0xf4));
    if ((uint8_t)eax != 0x21 && (uint8_t)eax != 0x20)
      return eax;
    eax = bb_entity_profession(object, (uint8_t)eax == 0x21 ? 2 : 1);
    goto clear_activity;
  case 0x2f:
    eax = BB_U32(node + 0x28);
    if (eax != target)
      goto clear_activity;
    eax = bb_entity_can_consume(object, target, 1);
    if (!eax)
      return eax;
    bb_entity_consume(object, BB_U32(node + 0x2c), 1);
    target = BB_U32(node + 0x2c);
    eax = BB_U32(target + 0x38);
    return WORD(eax) ? eax
                     : (uint32_t)bb_entity_dirty_c((void *)(uintptr_t)target, eax, 0);
  case 0x33:
  case 0x35:
  case 0x36: {
    if (kind != 0x33) {
      eax = BB_U32(node + 0x28);
      if (eax != target)
        goto clear_activity;
    }
    eax = low_byte(eax, BYTE(node + 0xfb));
    uint32_t active = kind == 0x33 ? 19 : kind == 0x35 ? 6 : 10;
    if ((uint8_t)eax == active) {
      eax = bb_entity_adjust((void *)(uintptr_t)target, 0x38, kind == 0x35 ? 3 : 4, 0);
      BYTE(node + 0xfb) = 0;
      if (kind == 0x36)
        eax = bb_entity_subtype(BB_POINTER(node + 0x2c), 100);
      return eax;
    }
    uint32_t resource = kind == 0x33 ? 18 : kind == 0x35 ? 7 : 9;
    eax = bb_entity_can_steal(object, target, resource);
    if (eax)
      return bb_entity_steal(object, BB_U32(node + 0x2c), resource);
    eax = bb_entity_can_consume(object, BB_U32(node + 0x2c), resource);
    return eax ? bb_entity_consume(object, BB_U32(node + 0x2c), resource) : eax;
  }
  case 0x3a:
    eax = low_byte(eax, BYTE(node + 0xf4));
    if ((uint8_t)eax == 3) {
      eax = BB_U32(node + 0x96) + 100;
      BB_U32(node + 0x14) = target;
      BYTE(node + 0xfb) = 14;
      BB_U32(node + 0x96) = eax;
    }
    return eax;
  case 0x42:
  case 0x45:
  case 0x56:
  case 0x91:
  case 0x96:
  case 0x98:
  case 0x9a:
  case 0x9d:
  case 0xcf: {
    eax = BB_U32(node + 0x28);
    if (eax != target)
      goto clear_activity;
    eax = low_byte(eax, BYTE(node + 0xf6));
    if ((uint8_t)eax == 2) {
      uint32_t resource, cost;
      switch (kind) {
      case 0x42:
        resource = 5;
        cost = 300;
        break;
      case 0x45:
        resource = 4;
        cost = 75;
        break;
      case 0x56:
        resource = 9;
        cost = 800;
        break;
      case 0x91:
        resource = 19;
        cost = 200;
        break;
      case 0x96:
        resource = 20;
        cost = 600;
        break;
      case 0x98:
        resource = 8;
        cost = 400;
        break;
      case 0x9a:
        resource = 6;
        cost = 150;
        break;
      case 0x9d:
        resource = 16;
        cost = 600;
        break;
      default:
        resource = 7;
        cost = 150;
        break;
      }
      eax = bb_entity_trade_c(object, target, kind == 0x9d ? 8 : 1, resource,
                              cost, eax);
      goto clear_activity;
    }
    if (kind == 0x9a) {
      eax = low_byte(eax, BYTE(node + 0xf4));
      if ((uint8_t)eax == 6) {
        void *action = BB_POINTER(node + 0x14);
        if (action) {
          eax = bb_entity_action_begin(action);
          BB_U32(node + 0x14) = 0;
        }
        BYTE(node + 0xfb) = 0;
      }
      goto clear_activity;
    }
    return eax;
  }
  case 0x46:
    bb_entity_subtype((void *)(uintptr_t)target, 0);
    target = BB_U32(node + 0x2c);
    eax = (uintptr_t)bb_entity_create(
        (void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0x47,
        ((uint32_t)BYTE(target + 0xd8) << 16) + 0x8000,
        ((uint32_t)BYTE(target + 0xd9) << 16) + 0x8000,
        BB_U32(target + 0x6a) + 0x18000);
    BB_U32(target + 0x10) = eax;
    bb_entity_dirty_c(BB_POINTER(node + 0x2c), eax, 0);
    return bb_entity_command_sound(object, BB_U32(0x006b0838));
  case 0x4e:
  case 0x4f:
    if (!near)
      goto clear_activity;
    eax = low_byte(eax, BYTE(node + 0xce));
    if ((uint8_t)eax >= 88)
      goto clear_activity;
    if (kind == 0x4e) {
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax != 2)
        goto clear_activity;
    }
    eax = low_byte(eax, BYTE(node + 0xf4));
    if ((uint8_t)eax != 1 && (uint8_t)eax != 2)
      goto clear_activity;
    bb_entity_place_tile(object, BYTE(target + 0xd8), BYTE(target + 0xd9));
    if (kind == 0x4e) {
      BYTE(node + 0xfb) = 6;
      bb_entity_subtype(object, 6);
      target = BB_U32(node + 0x2c);
      BYTE(node + 0xe8) = 4;
      BYTE(node + 0xf9) = 0;
      BB_U32(target + 0x2c) = node;
      eax = bb_entity_subtype(BB_POINTER(node + 0x2c), 4);
    } else {
      remove_target(node);
      BYTE(node + 0xfb) = 10;
      eax = bb_entity_subtype(object, 5);
      BYTE(node + 0xf9) = 0;
    }
    goto clear_activity;
  case 0x53:
    if (near) {
      uint32_t reward = BYTE(target + 0xea);
      if (reward <= 6) {
        bb_entity_command_sound(object, BB_U32(0x005c78e4));
        if (reward <= 5) {
          bb_entity_adjust(object, 0x100, 64, 0);
          if (reward <= 1) {
            bb_entity_adjust(object, reward ? 0x109 : 0xa0, 255, 0);
            bb_entity_adjust(object, 0x11b, 16, 0);
            bb_entity_sound(object, BB_U32(0x00484338));
          } else
            bb_entity_adjust(object, 0x11b, 4, 0);
        } else {
          create_at(node, 0x26);
          create_at(node, 0x22);
          bb_entity_sound(object, BB_U32(0x00484318));
        }
      }
      eax = remove_target(node);
    }
    goto clear_activity;
  case 0x67:
    eax = BYTE(node + 0xf6);
    if (eax == 12) {
      bb_entity_subtype(object, 13);
      eax = BB_U32(node + 0x2c);
      BB_U32(node + 0x14) = eax;
      BB_U32(node + 0x96) += 100;
    } else if (eax == 13) {
      feedback(object, 0x005f9100);
      uint32_t amount = bb_random() % 32 + 32;
      eax = bb_entity_adjust(BB_POINTER(node + 0x2c), 0x10d, amount, 0);
    }
    goto clear_activity;
  case 0x6e:
    eax = low_byte(eax, BYTE(node + 0xf6));
    if ((uint8_t)eax == 6) {
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax != 0x5a && (uint8_t)eax != 0x5e) {
        uint32_t part = BYTE(node + 0xf4);
        if (part == 1 || part == 2)
          feedback(object, part == 1 ? 0x00602454 : 0x0050faa4);
        bb_entity_sound(object, BB_U32(0x00484334));
        BB_U32(BB_U32(node + 0x2c) + 0x14) = node;
        bb_entity_subtype(BB_POINTER(node + 0x2c), 0x5a);
        eax = bb_entity_transfer_c((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7),
                                   BB_U32(node + 0x2c), BB_U32(node + 0x2c));
      }
      goto clear_activity;
    }
    if ((uint8_t)eax == 10) {
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax == 0x5a) {
        bb_entity_subtype((void *)(uintptr_t)target, 0);
        BB_U32(BB_U32(node + 0x2c) + 0x14) = 0;
        eax = bb_entity_sound(object, BB_U32(0x00484334));
      }
      goto clear_activity;
    }
    if ((uint8_t)eax != 1)
      return eax;
    if (!near)
      goto clear_activity;
    eax = low_byte(eax, BYTE(target + 0xf5));
    if ((uint8_t)eax)
      goto clear_activity;
    eax = low_byte(eax, BYTE(node + 0xce));
    if ((uint8_t)eax >= 88)
      goto clear_activity;
    bb_entity_place_tile(object, BYTE(target + 0xd8), BYTE(target + 0xd9));
    BYTE(node + 0xfb) = 19;
    bb_entity_subtype(object, 0x5d);
    BYTE(node + 0xf9) = 0;
    eax = bb_entity_subtype(BB_POINTER(node + 0x2c), 0x5e);
    BYTE(BB_U32(node + 0x2c) + 0xf5) = 15;
    BB_U32(BB_U32(node + 0x2c) + 0x2c) = node;
    goto clear_activity;
  case 0x6f:
    eax = low_byte(eax, BYTE(target + 0xf2));
    if ((uint8_t)eax != 0x5c)
      goto clear_activity;
    bb_entity_command_sound(object, BB_U32(0x005c78e4));
    bb_entity_adjust(object, 0x100, bb_random() % 12 + 32, 0);
    eax = bb_entity_subtype(BB_POINTER(node + 0x2c), 0);
    BYTE(BB_U32(node + 0x2c) + 0xf5) = 20;
    bb_entity_dirty_c(BB_POINTER(node + 0x2c), eax, 0);
    uint32_t cost = WORD(BB_U32(0x004844a8) + 0x58);
    bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(node + 0xf7), cost);
    BB_U32(node + 0x96) -= cost;
    if (BYTE(node + 0xf7) == BYTE(0x004843f0))
      bb_notification_number_entry((void *)(uintptr_t)0x005eaca8, node, cost);
    ++BB_U32(0x00555c38 + BYTE(node + 0xf7) * 0xd00);
    eax = BYTE(node + 0xf7);
    BB_U32(0x00555c2c + eax * 0xd00) += cost;
    eax = low_byte(eax, BYTE(node + 0xf4));
    if ((uint8_t)eax == 2) {
      eax = bb_random() % 8;
      if (!eax)
        eax = feedback(object, 0x00559b5c);
    }
    goto clear_activity;
  case 0x72:
  case 0x73:
  case 0x74:
    eax = low_byte(eax, BYTE(target + 0xf2));
    if ((uint8_t)eax)
      goto clear_activity;
    eax = low_byte(eax, BYTE(node + 0xf4));
    if ((uint8_t)eax != 1 && (uint8_t)eax != 4)
      goto clear_activity;
    bb_entity_place_tile(object, BYTE(target + 0xd8), BYTE(target + 0xd9));
    bb_entity_subtype(object, kind - 0x69);
    target = BB_U32(node + 0x2c);
    if (kind == 0x74)
      BYTE(node + 0xe5) = BYTE(target + 0xe5);
    BYTE(node + 0xe8) = 8;
    BYTE(node + 0xf9) = 0;
    bb_entity_subtype((void *)(uintptr_t)target, kind == 0x72   ? 0x3c
                                      : kind == 0x73 ? 0x3e
                                                     : 0x41);
    BYTE(BB_U32(node + 0x2c) + 0xf5) = 0;
    BB_U32(node + 0x14) = BB_U32(node + 0x2c);
    eax = BB_U32(node + 0x96) + 100;
    BB_U32(node + 0x96) = eax;
    goto clear_activity;
  case 0x8c:
    remove_target(node);
    eax =
        BB_U32(0x00559b68 + (BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8)) * 12);
    if (eax & 0x200002)
      return (uintptr_t)bb_entity_child_site(object);
    eax = BB_U32(node + 0x14);
    if (eax) {
      bb_entity_remove((void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)eax);
      eax = create_at(node, 0x39);
    }
    BYTE(node + 0xfb) = 0;
    return eax;
  case 0x8f:
  case 0xc7:
    return eax;
  case 0x90:
    if (!near)
      return eax;
    eax = low_byte(eax, BYTE(target + 0x101));
    if ((uint8_t)eax != 3)
      return eax;
    bb_entity_command_sound(object, BB_U32(0x005e474c));
    BYTE(BB_U32(node + 0x2c) + 0x101) = 0;
    target = BB_U32(node + 0x2c);
    bb_terrain_set_entry((void *)(uintptr_t)0x005e470c, BYTE(target + 0xd8),
                         BYTE(target + 0xd9), 0x2d, 1);
    return bb_entity_adjust(object, 0x101, 48, 0);
  case 0x9f:
  case 0xac:
  case 0xae:
  case 0xb1:
  case 0xb2:
  case 0xc0:
  case 0xc1:
  case 0xc6:
  case 0xc8:
  case 0xd3:
  case 0xd4:
    if (near)
      eax = bb_entity_cargo(object, (void *)(uintptr_t)target);
    goto clear_activity;
  case 0xa0:
    if (!near)
      goto clear_activity;
    eax = BYTE(target + 0xf2);
    if (eax == 0x54 || eax == 0x56) {
      bb_entity_command_sound(object, BB_U32(0x005e474c));
      bb_entity_adjust(object, eax == 0x54 ? 0xa0 : 0x109, 255, 0);
      bb_entity_sound(object, BB_U32(0x004842dc));
    } else if (eax == 0x55) {
      bb_entity_command_sound(object, BB_U32(0x005e474c));
      int32_t reward = bb_random() % 10;
      switch (reward) {
      case 0:
        BB_U32(node + 0x9c) |= 0x80000;
        bb_entity_command_sound(object, BB_U32(0x006a9938));
        break;
      case 1:
        BYTE(node + 0x11e) = bb_random() % 3;
        bb_entity_command_sound(object, BB_U32(0x006a9938));
        break;
      case 2:
        create_at(node, 0x22);
        bb_entity_sound(object, BB_U32(0x00484318));
        break;
      case 3: {
        create_at(node, 0xcb);
        uint32_t amount = bb_random() % 50 + 50;
        ((six_fn)(uintptr_t)bb_entity_area_damage_entry)(
            (void *)(uintptr_t)0x00553f90, node, 0, BYTE(node + 0xd8), BYTE(node + 0xd9),
            1, amount);
        bb_entity_command_sound(object, BB_U32(0x006a9938));
        break;
      }
      case 4:
      case 5:
      case 6:
        bb_entity_cast(BB_POINTER(node + 0x2c),
                       reward == 4   ? 0x4c
                       : reward == 5 ? 0x66
                                     : 0x67,
                       0, 0);
        break;
      case 7:
      case 8:
        bb_entity_command_sound(object, BB_U32(0x005e474c));
        bb_entity_adjust(object, reward == 7 ? 0xa0 : 0x109, 255, 0);
        bb_entity_sound(object, BB_U32(0x004842dc));
        break;
      case 9: {
        int32_t gender = bb_random() % 2;
        if (gender == 0 || gender == 1) {
          target = BB_U32(node + 0x2c);
          void *child = bb_entity_create(
              (void *)(uintptr_t)0x00553f90, 4, 0x68, BB_U32(target + 0x62),
              BB_U32(target + 0x66), BB_U32(target + 0x6a));
          bb_entity_profession(child, gender == 0 ? 23 : 24);
          target = BB_U32(node + 0x2c);
          uint32_t x = low_byte(target, BYTE(target + 0xd8) + 1);
          bb_entity_walk_c(child, x, BYTE(target + 0xd9), 0, x);
        }
        break;
      }
      }
    }
    eax = remove_target(node);
    goto clear_activity;
  case 0xa2: {
    uintptr_t portal = BB_U32(target + 4);
    if (portal)
      eax = 0xa2;
    for (; portal; portal = BB_U32(portal + 4))
      if (BYTE(portal + 0xd4) == 0xa2)
        break;
    if (!portal) {
      portal = BB_U32(0x00554f98);
      if (portal)
        eax = 0xa2;
      for (; portal; portal = BB_U32(portal + 4))
        if (BYTE(portal + 0xd4) == 0xa2)
          break;
    }
    if (!portal)
      return eax;
    eax = create_at(node, 0xc3);
    BB_U32(eax + 0x14) = node;
    BYTE(eax + 0xd6) = BYTE(portal + 0xd8) + BYTE(0x00687b02);
    uint8_t y = BYTE(portal + 0xd9) + BYTE(0x00687b06);
    BYTE(eax + 0xd7) = y;
    uint32_t z = BB_U32(node + 0x6a);
    uint32_t x = ((uint32_t)BYTE(eax + 0xd6) << 16) + 0x8000;
    bb_entity_create((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0xc3, x,
                     ((uint32_t)y << 16) + 0x8000, z);
    bb_entity_sound(object, BB_U32(0x00484348));
    eax = bb_entity_sound((void *)(uintptr_t)portal, BB_U32(0x00484348));
    uint8_t faction = BYTE(0x004843f0);
    eax = low_byte(eax, BYTE(node + 0xf7));
    if ((uint8_t)eax == faction) {
      eax = BYTE(node + 0x12d);
      if (eax & (1u << (faction & 31)))
        eax = EA_plain_one_fn(0x0041ad70,(void *)(uintptr_t)portal);
    }
    eax = bb_entity_dirty_c(object, eax, 0);
    BYTE(node + 0xf2) = 0;
    return eax;
  }
  case 0xb0:
    if (near) {
      bb_faction_flag(BB_POINTER(0x004844a8), BYTE(target + 0xea), 1);
      remove_target(node);
      eax = bb_entity_sound(object, BB_U32(0x004842dc));
    }
    goto clear_activity;
  case 0xd2:
    if (near) {
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax == 0 || (uint8_t)eax == 0x6e) {
        uint32_t sound = (uint8_t)eax ? 0x00484324 : 0x00484320;
        bb_entity_subtype((void *)(uintptr_t)target, (uint8_t)eax ? 0x70 : 0x6f);
        eax = bb_entity_sound(object, BB_U32(sound));
      }
    }
    goto clear_activity;
  case 0xd6:
    if (near) {
      bb_entity_adjust(object, 0xa0, 255, 0);
      bb_entity_adjust(object, 0x109, 255, 0);
      bb_entity_sound(object, BB_U32(0x004842dc));
      bb_entity_command_sound(object, BB_U32(0x00604624));
      BYTE(node + 0xce) = 12;
      eax = BYTE(node + 0xf4);
      if (eax == 11 || eax == 12)
        eax = bb_entity_profession(object, eax == 11 ? 1 : 2);
    }
    eax = bb_entity_dirty_c(object, eax, 0);
    goto clear_activity;
  default:
    goto clear_activity;
  }
clear_activity:
  BYTE(node + 0xf6) = 0;
  return eax;
}
