/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
static uint32_t entity_site(uint32_t id,void *world,uint32_t x,uint32_t y,
 uint8_t *out_x,uint8_t *out_y,uint32_t flags) {
 struct bb_model_buffer bx,by;bb_model_buffer_begin(&bx,out_x,1);bb_model_buffer_begin(&by,out_y,1);
 uint32_t result=BB_GAME_CALL(id,EA_WORD(world),x,y,bx.reference,by.reference,flags);
 bb_model_buffer_end(&by,1);bb_model_buffer_end(&bx,1);return result;
}
#define EA_site_fn entity_site
#define EA_tile_fn(id,a0,a1,a2) BB_GAME_CALL(id,0,EA_WORD(a0),EA_WORD(a1),EA_WORD(a2))
#else
#define EA_site_fn(id,...) CALL(id,site_fn)(__VA_ARGS__)
#define EA_tile_fn(id,...) CALL(id,tile_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef uint32_t(BB_THISCALL * site_fn)(void *, uint32_t,
                                                      uint32_t, uint8_t *,
                                                      uint8_t *, uint32_t);
typedef uint32_t (*tile_fn)(uint32_t, uint32_t, uint32_t);
static uint32_t low_byte(uint32_t eax, uint32_t value) {
  return (eax & 0xffffff00u) | (uint8_t)value;
}
static int32_t random_value(void) { return bb_random(); }
static uint32_t dirty(void *object, uint32_t eax) {
  return bb_entity_dirty_c(object, eax, 0);
}
static uint32_t feedback(void *object, uintptr_t slot) {
  uint32_t sample = BB_U32(slot);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sample, 0, 0, sample);
}
static uint32_t command_sound(void *object, uintptr_t slot) {
  return bb_entity_command_sound(object, BB_U32(slot));
}
static uint32_t command(void *object, uint32_t target, uint32_t eax) {
  return bb_entity_command_c(object, (void *)(uintptr_t)target, 1, 1, eax);
}
static uint32_t approach(void *object, uint32_t target, uint32_t value,
                         uint32_t eax) {
  return bb_entity_approach_c(object, (void *)(uintptr_t)target, value, eax);
}
static uint32_t wander(void *object, unsigned interval, unsigned bias,
                       int walk) {
  uint32_t node = bb_address_encode(object);
  int32_t value = random_value();
  uint32_t y = low_byte(value / (int32_t)interval,
                        BYTE(node + 0xd9) + value % (int32_t)interval - bias);
  value = random_value();
  uint32_t x = low_byte(value / (int32_t)interval,
                        BYTE(node + 0xd8) + value % (int32_t)interval - bias);
  return walk ? bb_entity_walk_c(object, x, y, 0, x)
              : bb_entity_move_escape(object, x, y);
}
static uintptr_t create(void *object, uint32_t kind) {
  uint32_t node = bb_address_encode(object);
  return (uintptr_t)bb_entity_create((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7),
                                     kind, BB_U32(node + 0x62),
                                     BB_U32(node + 0x66), BB_U32(node + 0x6a));
}
static uint32_t remove(void *object) {
  return bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
}
static void remove_dependents(uint32_t node) {
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4))
    if (BB_U32(other + 0x1c) == node)
      remove((void *)(uintptr_t)other);
}
static uintptr_t near(uint32_t node, unsigned radius) {
  return (uintptr_t)bb_entity_near((void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8),
                                   BYTE(node + 0xd9), radius);
}
static uintptr_t near_next(uint32_t node, uint32_t other, unsigned radius) {
  return (uintptr_t)bb_entity_near_next((void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)other,
                                        BYTE(node + 0xd8), BYTE(node + 0xd9),
                                        radius);
}
static int protected_owner(uint32_t node) {
  return ((1u << (BYTE(node + 0xf7) & 31)) & BYTE(node + 0x12d)) != 0;
}

/* 0x00444150: complete decision tick, including the shared preamble, all kind
 * branches, animal breeding and the two building-collapse footprint scans.
 */
uint32_t BB_THISCALL bb_entity_decision_tick(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t eax = BB_U32(node + 0x2c);
  if (eax)
    eax = bb_entity_check_arrival_c(object, eax);
  uint32_t part = BYTE(node + 0xf4);
  if (part == 9 || part == 3) {
    bb_entity_adjust(object, 0x109, 4, 0);
    if (random_value() % 4 == 0 && BYTE(node + 0xf7) != BYTE(0x004843f0) &&
        BYTE(node + 0xf4) == 9)
      bb_entity_mage_offense(object);
    if (random_value() % 4 == 0 && BYTE(node + 0xf7) != BYTE(0x004843f0) &&
        BYTE(node + 0xf4) == 3)
      bb_entity_mage_support(object);
  }
  if (BB_U32(node + 0x9c) & 0x80000)
    if (random_value() % 4 == 0) {
      bb_entity_sound(object, BB_U32(0x00484318));
      BB_U32(node + 0x9c) &= 0xfff7ffffu;
    }
  if (BB_U32(node + 0x9c) & 0x80000000)

#ifdef BB_MODEL_RUNTIME
BB_GAME_CALL(0x4508a0,bb_address_encode(object), 7, 0)
#else
((two_fn)(uintptr_t)bb_entity_damage_entry)(object, 7, 0)
#endif
;
  uint32_t other = BB_U32(node + 0x28);
  if (other && (BB_U32(other + 0x9c) & 0x80000))
    bb_entity_evade_c(object, (void *)(uintptr_t)other, other);
  if (BYTE(node + 0x106)) {
    wander(object, 10, 5, 0);
    --BYTE(node + 0x106);
  }
  other = BB_U32(node + 0xc);
  if (!other) {
    unsigned kind = BYTE(node + 0xd4);
    if ((kind == 0x67 || kind == 0x68) &&
        (!BYTE(node + 0xf3) || (BB_U32(node + 0x9c) & 0x10000000) ||
         SHORT(node + 0xa0) < 20) &&
        !protected_owner(node))
      bb_entity_enemy_evade(object);
  } else if (BYTE(node + 0xf2) == 7) {
    if (!protected_owner(node)) {
      if ((BB_U32(node + 0x9c) & 0x10000000) || SHORT(node + 0xa0) < 20)
        bb_entity_evade_c(object, (void *)(uintptr_t)other, other);
      else if (!BYTE(other + 0x11c) && random_value() % 4 == 0 &&
               BYTE(node + 0xf7) != BYTE(0x004843f0))
        wander(object, 7, 3, 1);
    }
  } else if (BB_U32(node + 0x9c) & 0x4000000) {
    if (!BB_U32(node + 0x24))
      BB_U32(node + 0x9c) &= 0xfbffffffu;
    else if (random_value() % 4 == 0) {
      other = BB_U32(node + 0x24);
      eax = bb_entity_distance(BYTE(node + 0xd8), BYTE(node + 0xd9),
                               BYTE(other + 0xd8), BYTE(other + 0xd9));
      if ((int32_t)eax > 0x80000) {
        command(object, 0, eax);
        approach(object, BB_U32(node + 0x24), 0, BB_U32(node + 0x24));
      }
    }
  }
  if (BYTE(node + 0xf2) == 0x10) {
    if (!BB_U32(0x00484288)) {
      eax = bb_entity_subtype(object, 0);
      dirty(object, eax);
    }
  } else if (BB_U32(0x00484288) && !BYTE(node + 0xf6) && !BB_U32(node + 0x18) &&
             !BYTE(node + 0x11d) && !BB_U32(node + 0xc) &&
             BB_U32(node + 0x2c) && BYTE(BB_U32(node + 0x2c) + 0xd4) == 0x8f) {
    uint32_t tile = (BYTE(node + 0xd9) * 192u + BYTE(node + 0xd8)) * 12u;
    if (BYTE(0x00559b60 + tile) < 45 || BYTE(0x00559b60 + tile) > 48) {
      other = (uintptr_t)bb_entity_at((void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8),
                                      BYTE(node + 0xd9));
      int occupied = other != node;
      while (other) {
        if (other != node)
          occupied = 1;
        other =
            (uintptr_t)bb_entity_at_next((void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)other,
                                         BYTE(node + 0xd8), BYTE(node + 0xd9));
      }
      if (!occupied) {
        eax = bb_entity_subtype(object, 0x10);
        dirty(object, eax);
      }
    }
  }
  unsigned kind = BYTE(node + 0xd4);
  if (kind == 0x67 || kind == 0x6e) {
    unsigned reaction = BYTE(node + 0x12e);
    if (reaction) {
      if (reaction == 1) {
        if (random_value() % 2 == 0)
          bb_entity_idle_action(object);
      } else if (reaction == 2) {
        if (random_value() % 2 == 0) {
          part = BYTE(node + 0xf4);
          command_sound(object, part == 2 || part == 14 || part == 12
                                    ? 0x00662c78
                                    : 0x005ecf90);
        }
      } else if (reaction == 3) {
        if (random_value() % 2 == 0) {
          part = BYTE(node + 0xf4);
          if (part == 1 || part == 2)
            feedback(object, part == 1 ? 0x005f51d8 : 0x005e42bc);
        }
        bb_entity_adjust(object, 0x10d, -32u, 0);
      }
      if (random_value() % 8 == 0)
        BYTE(node + 0x12e) = 0;
    } else {
      int idle = kind == 0x67 && random_value() % 300 == 0;
      if (idle) {
        part = BYTE(node + 0xf4);
        int female = part == 2 || part == 12 || part == 14;
        int32_t selection = random_value() % 4;
        if (selection == 0)
          bb_entity_idle_action(object);
        else if (selection > 0) {
          const uintptr_t male[] = {0, 0x005ecf90, 0x006afcf0, 0x006afce8};
          const uintptr_t woman[] = {0, 0x00662c78, 0x005db03c, 0x00663508};
          feedback(object, female ? woman[selection] : male[selection]);
        }
      } else if (BYTE(node + 0xd4) == 0x6e && random_value() % 200 == 0)
        bb_entity_idle_action(object);
    }
  }
  if (!BB_U32(node + 0xc) && !BYTE(node + 0x11d)) {
    if (BYTE(node + 0xf3) && BYTE(node + 0xd4) == 0x67 &&
        (!protected_owner(node) || BYTE(node + 0xf7) != BYTE(0x004843f0)))
      bb_entity_enemy_command(object);
    kind = BYTE(node + 0xd4);
    if (kind != 0xab && kind != 0xbf && kind != 0xe2 && !BB_U32(node + 0xc)) {
      if (BB_U32(node + 0x9c) & 0x4000000) {
        if (!BB_U32(node + 0x24))
          BB_U32(node + 0x9c) &= 0xfbffffffu;
        else
          approach(object, BB_U32(node + 0x24), 0, BB_U32(node + 0x9c));
      }
      if (BB_U32(node + 0x9c) & 0x8000000) {
        other = BB_U32(node + 0x24);
        if (other && BB_U32(other))
          command(object, other, BB_U32(node + 0x9c));
        else {
          if (other)
            BB_U32(node + 0x24) = 0;
          BB_U32(node + 0x9c) &= 0xf7ffffffu;
        }
      }
    }
  }
  kind = BYTE(node + 0xd4);
  eax = kind - 0x21u;
  switch (kind) {
  case 0x21:
    eax = BYTE(node + 0xf2);
    if (eax == 0x1a) {
      int32_t value = random_value();
      eax = value / 40;
      if (value % 40 == 0)
        return bb_entity_subtype(object, 0);
    }
    return eax;
  case 0x2b:
    eax = random_value() % 2;
    if (!eax) {
      BB_U32(node + 0x7a) = BB_U32(0x004846a8);
      BB_U32(node + 0x7e) = BB_U32(0x004846ac);
      eax = random_value();
      eax = low_byte(eax, BYTE(node + 0xd8));
      if ((uint8_t)eax < 3 || BYTE(node + 0xd9) < 3 || (uint8_t)eax > 188 ||
          BYTE(node + 0xd9) > 188) {
        BYTE(node + 0xea) = 2;
        BYTE(node + 0xf9) = 15;
      }
    }
    return eax;
  case 0x2c: {
    int32_t branch = random_value() % 2;
    other = create(object, branch ? 0x37 : 0x38);
    BB_U32(other + 0x76) = random_value() % 65536 + (branch ? 0 : 65536);
    eax = random_value() % 256;
    BYTE(other + 0xe5) = eax;
    if (!branch) {
      BB_U32(other + 0x7a) = BB_U32(0x005d1150 + BYTE(other + 0xe5) * 4u) << 4;
      eax = BB_U32(0x005d1558 + BYTE(other + 0xe5) * 4u) << 4;
      BB_U32(other + 0x7e) = eax;
    }
    return eax;
  }
  case 0x2f:
    if (BYTE(node + 0xe9) && random_value() % 2 == 0) {
      uintptr_t payload = BB_U32(node + 0x38);
      if (WORD(payload) < 20)
        ++WORD(payload);
      if (--BYTE(node + 0xe9) == 0)
        dirty(object, 0);
    }
    {
      int32_t value = random_value();
      eax = value / 20;
      if (value % 20 == 0) {
        BYTE(node + 0xe9) = 3;
        return dirty(object, eax);
      }
    }
    return eax;
  case 0x33:
    eax = low_byte(eax, BYTE(node + 0xea));
    if (!(uint8_t)eax && !(eax = BB_U32(0x00484288))) {
      int32_t value = random_value();
      eax = value / 5;
      if (value % 5 == 0) {
        eax = random_value() % 4;
        BYTE(node + 0xf9) = BYTE(node + 0xf5) = 0;
        eax = low_byte(eax, eax + 1u);
        BYTE(node + 0xea) = eax;
        return dirty(object, eax);
      }
    }
    return eax;
  case 0x45:
    eax = low_byte(eax, BYTE(node + 0xf2));
    if (!(uint8_t)eax) {
      int32_t value = random_value();
      eax = value / 10;
      if (value % 10 == 0)
        return bb_entity_subtype(object, 0x58);
    }
    return eax;
  case 0x51: {
    int32_t value = random_value();
    eax = value / 5;
    if (value % 5 == 0) {
      eax = random_value() % 256;
      BYTE(node + 0xe7) = eax;
      return dirty(object, eax);
    }
    return eax;
  }
  case 0x5c:
    eax = low_byte(eax, BYTE(node + 0xf2));
    if (!(uint8_t)eax) {
      int32_t value = random_value();
      eax = value / 10;
      if (value % 10 == 0)
        return bb_entity_subtype(object, 0x2c);
    } else if ((uint8_t)eax == 0x2c) {
      eax = random_value() % 2;
      if (!eax) {
        bb_entity_subtype(object, 0x2d);
        eax = wander(object, 5, 2, 0);
        if (!BYTE(node + 0xf0))
          eax = bb_entity_subtype(object, 0x2c);
      }
      return dirty(object, eax);
    }
    return eax;
  case 0x64: {
    int32_t value = random_value();
    if (value % 3 == 0) {
      command(object, 0, value / 3);
      for (other = near(node, 5); other; other = near_next(node, other, 5))
        if (BYTE(other + 0xd4) == 0x6c)
          command(object, other, low_byte(other, 0x6c));
    }
    eax = BB_U32(node + 0xc);
    if (!eax) {
      int32_t value = random_value();
      eax = value / 3;
      if (value % 3 == 0) {
        eax = random_value() % 256;
        BYTE(node + 0xe7) = eax;
      }
    }
    return eax;
  }
  case 0x67: {
    if (!BYTE(node + 0xf2)) {
      int32_t value = random_value();
      BYTE(node + 0xea) = value % 12;
      dirty(object, value / 12);
    }
    if (!BB_U32(node + 0xc) && BYTE(node + 0xf4) == 6 && !BYTE(node + 0xfb))
      bb_entity_doctor_find(object);
    if (BYTE(node + 0xf6))
      bb_entity_resume(object);
    if (!BB_U32(node + 0x18) && BYTE(node + 0xfb) == 13) {
      other = (uintptr_t)bb_entity_nearest(
          (void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0x9a, BYTE(node + 0xd8),
          BYTE(node + 0xd9));
      if (other) {
        BYTE(node + 0xf2) = 0;
        eax = approach(object, other, 1, other);
      } else {
        BYTE(node + 0xfb) = 0;
        eax = BB_U32(node + 0x14);
        if (eax) {
          BYTE(BB_U32(node + 0x14) + 0x116) = 0;
          BYTE(BB_U32(node + 0x14) + 0x115) = 0;
          BB_U32(BB_U32(node + 0x14) + 0x9c) &= 0xfffbffffu;
          bb_entity_reset_move(BB_POINTER(node + 0x14));
          eax = bb_entity_set_ability(BB_POINTER(node + 0x14), 0);
        }
      }
    } else
      eax = BB_U32(node + 0x18);
    eax = low_byte(eax, BYTE(node + 0xf2));
    if ((uint8_t)eax == 7)
      return eax;
    eax = low_byte(eax, BYTE(node + 0x114));
    if ((uint8_t)eax)
      return eax;
    if (random_value() % 10 == 0 &&
        (!BB_U32(node + 0x1c) || BYTE(BB_U32(node + 0x1c) + 0xd4) == 0x8f))
      bb_entity_assign_target((void *)(uintptr_t)0x00553f90, object);
    uint32_t tile = (BYTE(node + 0xd9) * 192u + BYTE(node + 0xd8)) * 12u;
    eax = low_byte(BYTE(node + 0xd8), BYTE(node + 0xd5));
    eax = low_byte(eax, BYTE(0x00559b61 + tile) ? (uint8_t)eax & 0xfb
                                                : (uint8_t)eax | 4);
    BYTE(node + 0xd5) = eax;
    if (!BYTE(node + 0x108)) {
      eax = low_byte(eax, BYTE(node + 0x103));
      if ((uint8_t)eax) {
        eax = low_byte(eax, eax - 1u);
        BYTE(node + 0x103) = eax;
        dirty(object, eax);
      }
    } else {
      uint8_t remaining = BYTE(node + 0x108) - 1u;
      other = BB_U32(node + 0x28);
      BYTE(node + 0x108) = remaining;
      if (other) {
        if (remaining > 7)
          BYTE(other + 0xf9) = 0;
        else if (remaining > 3)
          BYTE(other + 0xf9) = 1;
        else {
          if (remaining == 3) {
            int32_t choice = random_value() % 2;
            bb_entity_command_sound(object, BB_U32(0x006033c0 + choice * 4u));
          }
          BYTE(BB_U32(node + 0x28) + 0xf9) = random_value() % 2 * 2 + 2;
        }
        dirty(BB_POINTER(node + 0x28), BB_U32(node + 0x28));
      }
      if (!BYTE(node + 0x108)) {
        other = BB_U32(node + 0x28);
        if (other) {
          BYTE(other + 0xf9) = 0;
          dirty((void *)(uintptr_t)other, other);
        }
        create(object, 0x67);
        BYTE(node + 0x103) = 0;
      }
    }
    if (!BYTE(node + 0x102) && BYTE(node + 0x100) < 20) {
      part = BYTE(node + 0xf4);
      if (part == 1 || part == 2) {
        int severe = BYTE(node + 0x100) < 8;
        feedback(object, part == 1 ? severe ? 0x006a9934 : 0x006634ec
                         : severe  ? 0x006a98b0
                                   : 0x00600160);
      }
      BYTE(node + 0x102) = 16;
    }
    if (BYTE(node + 0x101) < 16 && !BYTE(node + 0x102)) {
      feedback(object, BYTE(node + 0xf4) == 2 ? 0x005e4754 : 0x006afe0c);
      BYTE(node + 0x102) = 16;
    }
    /* The remaining human AI can return before replacing EAX. Keep the
     * full prefix through these final live gates and feedback callbacks.
     */
    eax = BB_U32(0x00484288);
    if (eax) {
      eax = low_byte(eax, BYTE(node + 0xf2));
      int shelter = (uint8_t)eax == 16;
      if (!shelter) {
        eax = BB_U32(node + 0x28);
        if (eax) {
          eax = BB_U32(node + 0x2c);
          shelter = BB_U32(node + 0x1c) == eax;
        }
      }
      if (shelter) {
        int32_t value = random_value();
        eax = value / 20;
        if (value % 20 == 0) {
          part = BYTE(node + 0xf4);
          eax = part - 1u;
          if (part == 1 || part == 4 || part == 11)
            eax = feedback(object, 0x005e42c0);
          else if (part == 2 || part == 12)
            eax = feedback(object, 0x006aff38);
        }
      }
    }
    eax = low_byte(eax, BYTE(node + 0x113));
    if ((uint8_t)eax == 100) {
      eax = low_byte(eax, BYTE(node + 0x102));
      if (!(uint8_t)eax) {
        eax = low_byte(eax, BYTE(node + 0xf4));
        if ((uint8_t)eax == 1) {
          eax = feedback(object, 0x006a9074);
          BYTE(node + 0x102) = 32;
        }
      }
    }
    return bb_entity_human_ai_c(object, eax);
  }
  case 0x68: {
    eax = BB_U32(node + 0xc);
    if (eax)
      return eax;
    part = BYTE(node + 0xf4);
    eax = part - 18u;
    if (part == 18) {
      bb_entity_target_last(object);
      if (random_value() % 12 == 0)
        command_sound(object, 0x005f6e14);
      eax = BB_U32(node + 0x9c);
      if (!(eax & 0x400000)) {
        eax = BB_U32(node + 0x18);
        if (eax)
          return eax;
        if (!BB_U32(node + 0xc) && !BYTE(node + 0x11d))
          wander(object, 11, 5, 1);
      }
      eax = BB_U32(node + 0x18);
      if (!eax && !(eax = BB_U32(node + 0xc))) {
        eax = low_byte(eax, BYTE(node + 0x11d));
        if (!(uint8_t)eax)
          return bb_entity_target_building(object);
      }
      return eax;
    }
    uintptr_t handler =
        part == 23 || part == 29   ? 0x00446ff0
        : part == 24 || part == 28 ? 0x00447120
        : part == 25 || part == 27 || part == 30 || part == 26 || part == 31
            ? 0x00447260
            : 0;
    if (!handler)
      return eax;
    eax = handler == 0x00446ff0   ? bb_entity_target_weak(object)
          : handler == 0x00447120 ? bb_entity_target_strong(object)
                                  : bb_entity_target_strong_alt(object);
    if (part == 26 || part == 31)
      return eax;
    if (part == 23 || part == 29)
      if (random_value() % 3 == 0) {
        int32_t choice = random_value() % 2;
        if (choice == 0 || choice == 1)
          command_sound(object, choice ? 0x006ad24c : 0x006634d4);
      }
    eax = BB_U32(node + 0x9c);
    if (eax & 0x400000)
      return eax;
    eax = BB_U32(node + 0x18);
    if (eax)
      return eax;
    eax = BB_U32(node + 0xc);
    if (eax)
      return eax;
    eax = low_byte(eax, BYTE(node + 0x11d));
    return (uint8_t)eax ? eax : wander(object, 11, 5, 1);
  }
  case 0x6e: {
    if (!BYTE(node + 0xf2) && random_value() % 4 == 0)
      wander(object, 5, 2, 0);
    unsigned subtype = BYTE(node + 0xf2);
    if (subtype == 0x5a) {
      other = BB_U32(node + 0x14);
      if (!other)
        bb_entity_subtype(object, 0);
      else {
        approach(object, other, 1, other);
        other = BB_U32(node + 0x14);
        if (!BYTE(other + 0x102)) {
          part = BYTE(other + 0xf4);
          if (part == 1 || part == 2)
            feedback((void *)(uintptr_t)other, part == 1 ? 0x005c77a4 : 0x006a9068);
          BYTE(BB_U32(node + 0x14) + 0x102) = 1;
        }
      }
    } else if (subtype == 0x60) {
      uint8_t count = BYTE(node + 0xe8) + 1u;
      BYTE(node + 0xe8) = count;
      if (count > 8)
        bb_entity_subtype(object, 0);
    } else if (!subtype) {
      BYTE(node + 0xe8) = 0;
      if (!BB_U32(0x00484288)) {
        if (random_value() % 60 == 0)
          bb_entity_event_feedback(object, BB_U32(0x00484334), 1);
        if (random_value() % 5 == 0 && !BYTE(node + 0xf9) &&
            !BYTE(node + 0xe8)) {
          eax = random_value() % 2;
          eax = low_byte(eax, BYTE(node + 0xe5) + (eax ? -64 : 64));
          BYTE(node + 0xe5) = eax;
          dirty(object, eax);
        }
      }
    }
    eax = BB_U32(0x00484288);
    if (!eax)
      return eax;
    eax = BB_U32(0x00484768);
    if (eax != 0 && eax != 2)
      return eax;
    eax = low_byte(eax, BYTE(node + 0xf7));
    if ((uint8_t)eax == 4)
      return eax;
    eax = low_byte(eax, BYTE(node + 0x113));
    if (!(uint8_t)eax)
      return eax;
    uintptr_t mate = 0;
    for (other = near(node, 3); other; other = near_next(node, other, 3))
      if (BYTE(other + 0xd4) == 0x6e &&
          BYTE(other + 0xf7) == BYTE(node + 0xf7) && other != node)
        mate = other;
    eax = 0;
    if (!mate)
      return eax;
    eax = low_byte(eax, BYTE(mate + 0x113));
    if (!(uint8_t)eax)
      return eax;
    uint8_t x, y;
    eax = EA_site_fn(0x004299c0,(void *)(uintptr_t)0x005e470c,BYTE(node + 0xd8),BYTE(node + 0xd9),&x,&y,1);
    if (!eax)
      return eax;
    eax = (uintptr_t)bb_entity_create(
        (void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0x60, (x << 16) + 0x8000u,
        (y << 16) + 0x8000u, BYTE(0x00559b65 + (y * 192u + x) * 12u) << 16);
    --BYTE(node + 0x113);
    --BYTE(mate + 0x113);
    return eax;
  }
  case 0x6f:
    eax = BB_U32(0x00484288);
    if (eax)
      return eax;
    if (BYTE(node + 0xea) == 1) {
      BYTE(node + 0xea) = 0;
      dirty(object, 1);
    }
    if (random_value() % 3 == 0) {
      eax = random_value() % 4;
      eax = low_byte(eax, eax << 6);
      BYTE(node + 0xe5) = eax;
      dirty(object, eax);
    }
    {
      int32_t value = random_value();
      eax = value / 3;
      if (value % 3 == 0) {
        BYTE(node + 0xea) = 1;
        return dirty(object, eax);
      }
    }
    return eax;
  case 0x71:
  case 0x75:
  case 0x76:
  case 0x77:
  case 0x7b:
  case 0x7c:
  case 0x7e:
  case 0x7f:
  case 0x89:
  case 0x92:
  case 0x97:
  case 0x99:
  case 0x9b:
  case 0x9e:
  case 0xaf:
  case 0xd0:
  case 0xe1: {
    eax = BYTE(node + 0xf2);
    if (eax != 0 && eax != 0x43)
      return eax;
    unsigned subtype = eax;
    eax = low_byte(eax, BYTE(node + 0xea));
    if ((uint8_t)eax)
      return eax;
    unsigned width = subtype ? 11 : 7, bias = subtype ? 5 : 3;
    uintptr_t table = subtype ? 0x00663688 : 0x006635c4;
    int occupied = 0;
    for (unsigned y = 0; y < width; y++)
      for (unsigned x = 0; x < width; x++) {
        uint32_t stride = BYTE(node + 0xd4) * 0x392u;
        if (BB_U32(table + stride + (y * width + x) * 4u))
          if (EA_tile_fn(0x00417060,0x67,(uint8_t)(BYTE(node + 0xd8) + x - bias),(uint8_t)(BYTE(node + 0xd9) + y - bias)))
            occupied = 1;
      }
    if (occupied)
      return width;
    remove_dependents(node);
    if (!subtype) {
      bb_entity_release_tiles(object);
      bb_entity_subtype(object, 0x43);
      bb_entity_register_tiles(object);
      uint32_t stride = BYTE(node + 0xd4) * 0x392u;
      BYTE(node + 0xea) = 0;
      int32_t end_y = BB_U32(0x006638aa + stride),
              start_y = BB_U32(0x006638a2 + stride);
      int32_t end_x = BB_U32(0x006638a6 + stride),
              start_x = BB_U32(0x0066389e + stride);
      uint8_t direction = end_y > start_y   ? 128
                          : end_y < start_y ? 0
                          : end_x > start_x ? 64
                          : end_x < start_x ? 192
                                            : 0;
      other = (uintptr_t)bb_entity_create(
          (void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0x74,
          ((BYTE(node + 0xd8) + (uint32_t)end_x + 3u) << 16) - 0x28000u,
          ((BYTE(node + 0xd9) + (uint32_t)end_y + 3u) << 16) - 0x28000u,
          BB_U32(node + 0x6a));
      BYTE(other + 0xe5) = direction;
      BB_U32(other + 0x1c) = node;
      WORD(other + 0x9a) = 0;
      ++BYTE(node + 0xea);
      dirty(object, other);
      return bb_entity_sound(object, BB_U32(0x0048434c));
    }
    bb_entity_subtype(object, 0x44);
    remove(object);
    create(object, BYTE(0x006635bc + BYTE(node + 0xd4) * 0x392u));
    return bb_entity_sound(object, BB_U32(0x00484350));
  }
  case 0x7a:
  case 0x80:
  case 0x82:
  case 0xa6:
  case 0xaa: {
    eax = low_byte(eax, BYTE(node + 0xea));
    if ((uint8_t)eax)
      return eax;
    eax = EA_tile_fn(0x00417060,0x67,BYTE(node + 0xd8),BYTE(node + 0xd9));
    if (eax)
      return eax;
    remove_dependents(node);
    unsigned replacement = kind == 0x7a   ? 0x1d
                           : kind == 0x80 ? 0x46
                           : kind == 0x82 ? 0x81
                           : kind == 0xa6 ? 0x68
                                          : 0xa9;
    other = create(object, replacement);
    if (kind == 0xa6)
      bb_entity_profession((void *)(uintptr_t)other, 15);
    remove(object);
    return bb_entity_sound(object, BB_U32(0x0048434c));
  }
  case 0xd6: {
    int32_t value = random_value();
    eax = value / 10;
    if (value % 10 == 0)
      return command_sound(object,
                           random_value() % 2 == 0 ? 0x005e4770 : 0x00604640);
    return eax;
  }
  case 0xdd:
    eax = random_value() % 2;
    if (!eax) {
      eax = random_value() % 4;
      eax = low_byte(eax, eax << 6);
      BYTE(node + 0xe6) = eax;
    }
    eax = low_byte(eax, BYTE(node + 0xf5));
    if ((uint8_t)eax)
      return dirty(object, eax);
    for (other = near(node, 8); other; other = near_next(node, other, 8)) {
      if (BYTE(other + 0xd4) == 0x67 &&
          BYTE(other + 0xf7) != BYTE(node + 0xf7)) {
        if (!BYTE(node + 0xf5) && BYTE(0x004843f0) == BYTE(node + 0xf7)) {
          ++BB_U32(0x00484410);
          eax = bb_entity_distance(BYTE(other + 0xd8), BYTE(other + 0xd9),
                                   BB_U32(0x004846e4), BB_U32(0x004846e8));
          if ((int32_t)eax < 0x1e0000) {
            uint32_t index = (BYTE(other + 0xd9) - BB_U32(0x004846e8)) * 64u +
                             BYTE(other + 0xd8) - BB_U32(0x004846e4);
            unsigned direction = BYTE(0x005fda98 + index);
            BYTE(node + 0xea) = direction;
            command_sound(object, direction > 64 && direction < 128 ? 0x00602270
                                  : direction < 64                  ? 0x005db16c
                                  : direction >= 128 && direction < 192
                                      ? 0x00513448
                                      : 0x005c77ac);
          }
        }
        BYTE(node + 0xf5) = 128;
      }
      if (BYTE(other + 0xd4) == 0x68 &&
          BYTE(other + 0xf7) != BYTE(node + 0xf7)) {
        if (!BYTE(node + 0xf5) && BYTE(0x004843f0) == BYTE(node + 0xf7)) {
          ++BB_U32(0x00484410);
          command_sound(object, 0x005e42a4);
        }
        BYTE(node + 0xf5) = 128;
      }
    }
    return 0;
  default:
    return eax;
  }
}
