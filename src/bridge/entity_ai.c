/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
typedef void *(BB_THISCALL * search_fn)(void *);
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
static uint32_t low_byte(uint32_t eax, uint32_t value) {
  return (eax & 0xffffff00u) | (uint8_t)value;
}
static int32_t random_value(void) { return bb_random(); }
static uintptr_t near(uint32_t node, unsigned radius) {
  return (uintptr_t)bb_entity_near((void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8),
                                   BYTE(node + 0xd9), radius);
}
static uintptr_t near_next(uint32_t node, uint32_t other, unsigned radius) {
  return (uintptr_t)bb_entity_near_next((void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)other,
                                        BYTE(node + 0xd8), BYTE(node + 0xd9),
                                        radius);
}
static uint32_t distance(uint32_t node, uint32_t other) {
  return bb_entity_distance(BYTE(node + 0xd8), BYTE(node + 0xd9),
                            BYTE(other + 0xd8), BYTE(other + 0xd9));
}
static uint32_t feedback(void *object, uintptr_t slot) {
  uint32_t sample = BB_U32(slot);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sample, 0, 0, sample);
}

/* 0x00442800: try the preferred work search, then abilities 1,2,4,8,5,23.
 * Successful searches retain their live preferred activity and reservation.
 */
uint32_t BB_THISCALL bb_entity_work_auto(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t eax = BB_U32(node + 0x9c);
  if (eax & 0x400000)
    return eax;
  uint32_t preferred = BYTE(node + 0xfc), activity = 0;
  eax = preferred - 1u;
  const unsigned types[] = {1, 2, 4, 8, 5, 23};
  const search_fn searches[] = {bb_entity_work_wood,  bb_entity_work_stone,
                                bb_entity_work_hunt,  bb_entity_work_field,
                                bb_entity_work_clear, bb_entity_work_repair};
  uint32_t target = 0;
  for (unsigned i = 0; i < 6; i++)
    if (preferred == types[i]) {
      eax = (uintptr_t)searches[i](object);
      target = eax;
      activity = BYTE(node + 0xfc);
      break;
    }
  for (unsigned i = 0; !target && i < 6; i++) {
    eax = bb_entity_has_ability(object, types[i]);
    if (eax) {
      activity = types[i];
      eax = (uintptr_t)searches[i](object);
      target = eax;
    }
  }
  if (!target)
    return eax;
  uint32_t previous = BB_U32(target + 0x14);
  if (previous && previous != node)
    eax = bb_entity_reset_move((void *)(uintptr_t)previous);
  BB_U32(target + 0x14) = node;
  if (activity == 1 || activity == 2 || activity == 4)
    BYTE(node + 0xf6) = 1;
  else if (activity == 5)
    BYTE(node + 0xf6) = 7;
  else if (activity == 23)
    BYTE(node + 0xf6) = 14;
  eax = bb_entity_approach_c(object, (void *)(uintptr_t)target, 1, eax);
  eax = low_byte(eax, BYTE(target + 0xd4));
  return (uint8_t)eax == 0x6e ? bb_entity_subtype((void *)(uintptr_t)target, 0x60) : eax;
}
uint32_t BB_THISCALL bb_entity_work_retry(void *object) {
  uint32_t target = BB_U32(bb_address_encode(object) + 0x18);
  return target ? target : bb_entity_work_auto(object);
}

/* 0x004429e0: all profession feedback and ordered need/partner/work priorities.
 * Incoming EAX remains observable on the early subtype/activity gates.
 */
uint32_t bb_entity_human_ai_c(void *object, uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  eax = low_byte(eax, BYTE(node + 0xf6));
  if ((uint8_t)eax == 1) {
    eax = low_byte(eax, BYTE(node + 0xd5) | 4u);
    BYTE(node + 0xd5) = eax;
    eax = bb_entity_work_retry(object);
  }
  eax = low_byte(eax, BYTE(node + 0xf6));
  if ((uint8_t)eax == 2) {
    eax = BB_U32(node + 0x18);
    if (!eax) {
      eax = BB_U32(node + 0x34);
      eax = bb_entity_approach_c(object, (void *)(uintptr_t)eax, 1, eax);
    }
  }
  eax = low_byte(eax, BYTE(node + 0xf2));
  if ((uint8_t)eax)
    return eax;
  unsigned profession = BYTE(node + 0xf4);
  eax = profession - 1u;
  if (profession == 1 || profession == 2) {
    int32_t value = random_value();
    eax = value / (profession == 1 ? 60 : 200);
    int weather_feedback = 0;
    if (value % (profession == 1 ? 60 : 200) == 0) {
      eax = BB_U32(0x00484768);
      weather_feedback = eax == 3;
    }
    if (weather_feedback) {
      if (profession == 1)
        eax = feedback(object, 0x005e471c);
      else {
        eax = BB_U32(0x00484288);
        eax = feedback(object, eax ? 0x005fe3d4 : 0x0063f080);
      }
    } else {
      value = random_value();
      eax = value / 60;
      if (value % 60 == 0) {
        eax = BB_U32(0x00484288);
        if (eax)
          eax = feedback(object, profession == 1 ? 0x005c954c : 0x005fe3d4);
      }
    }
  } else if (profession == 11 || profession == 13 || profession == 14 ||
             profession == 20) {
    int32_t value = random_value();
    unsigned divisor = profession == 20 ? 20 : 60;
    eax = value / (int32_t)divisor;
    if (value % (int32_t)divisor == 0) {
      eax = BB_U32(0x00484288);
      if (!eax) {
        if (profession == 11)
          eax = feedback(object, 0x006afbcc);
        else if (profession == 14)
          eax = feedback(object, 0x00662c50);
        else if (profession == 20)
          eax = bb_entity_clown_action(object);
        else {
          eax = random_value() % 2;
          if (eax == 0 || eax == 1)
            eax = feedback(object, eax ? 0x005eac94 : 0x005eac98);
        }
      }
    }
  }
  eax = low_byte(eax, BYTE(node + 0xfc));
  if ((uint8_t)eax == 22) {
    eax = low_byte(eax, BYTE(node + 0xf7));
    if ((uint8_t)eax != 4)
      return eax;
  }
  eax = BB_U32(node + 0x18);
  if (!eax && BYTE(node + 0xfb) == 14)
    return (uintptr_t)bb_entity_child_site(object);
  if (eax)
    return eax;
  uintptr_t food = 0, water = 0, courage = 0, partner = 0, override = 0;
  eax = BB_U32(node + 0x9c);
  if (!(eax & 0x200000)) {
    eax = BB_U32(0x00484288);
    if (!eax) {
      eax = low_byte(eax, BYTE(node + 0x100));
      if ((uint8_t)eax < 96)
        food = eax = (uintptr_t)bb_entity_need_food(object);
      eax = low_byte(eax, BYTE(node + 0x101));
      if ((uint8_t)eax < 96)
        water = eax = (uintptr_t)bb_entity_need_water(object);
      eax = low_byte(eax, BYTE(node + 0x10d));
      if ((uint8_t)eax < 32)
        courage = eax = (uintptr_t)bb_entity_home_nearest(object);
    }
    if (BYTE(node + 0x113) >= 20 && BB_U32(node + 0x1c) && !BYTE(node + 0xf6)) {
      uint32_t owner_offset = BYTE(node + 0xf7) * 0xd00u;
      uint32_t limit = (!BB_U32(0x00555000 + owner_offset) ? 5u : 0u) +
                       WORD(0x0055503e + owner_offset) + 1u;
      if ((WORD(0x00555108 + owner_offset) >> 1) < limit &&
          WORD(0x00555108 + owner_offset) < 40 && BB_U32(node + 0x30) &&
          bb_entity_pair_allowed(object, BB_POINTER(node + 0x30), 0)) {
        uint32_t owner = BYTE(node + 0xf7), bit = 1u << (owner & 31);
        if (owner != BYTE(0x004843f0) ||
            (!(bit & BYTE(node + 0x12d)) &&
             !(bit & BYTE(BB_U32(node + 0x30) + 0x12d))))
          partner = BB_U32(node + 0x30);
      }
    }
    if (!BB_U32(node + 0x30))
      BB_U32(node + 0x9c) &= 0xdfffffffu;
    unsigned part = BYTE(node + 0xf4);
    eax = low_byte(part * 9u, BYTE(0x005e5eff + part * 0x124u));
    if ((uint8_t)eax == 2 && !BB_U32(node + 0x30)) {
      eax = BB_U32(node + 0x1c);
      if (eax && BYTE(eax + 0xd4) == 2)
        eax = (uintptr_t)bb_entity_partner_find(object);
    }
    eax = low_byte(eax, BYTE(node + 0xfb));
    if ((uint8_t)eax)
      override = (uintptr_t)bb_entity_order_site(object);
  }
  uintptr_t targets[4];
  int32_t priorities[4];
  unsigned count = 0;
  if (food) {
    targets[count] = food;
    priorities[count++] = 96 - (int32_t)BYTE(node + 0x100);
  }
  if (water) {
    targets[count] = water;
    priorities[count++] = 96 - (int32_t)BYTE(node + 0x101);
  }
  if (courage) {
    targets[count] = courage;
    priorities[count++] = 96 - (int32_t)BYTE(node + 0x10d);
  }
  if (partner) {
    targets[count] = partner;
    priorities[count++] = BYTE(node + 0x113) + 64;
  }
  uint32_t target = override;
  if (!target && count) {
    unsigned best = 0;
    for (unsigned i = 0; i < count; i++)
      if (priorities[i] > priorities[best])
        best = i;
    target = targets[best];
  }
  eax = count;
  if (target) {
    if (target == partner)
      bb_entity_partner_begin(object, (void *)(uintptr_t)target);
    else {
      eax = low_byte(eax, BYTE(target + 0xd4));
      if ((uint8_t)eax == 0x90)
        BYTE(target + 0xea) = 1;
      bb_entity_approach_c(object, (void *)(uintptr_t)target, 1, eax);
    }
  } else if (!BYTE(node + 0xf6) && !BYTE(node + 0xfb) && !BB_U32(0x00484288))
    bb_entity_work_auto(object);
  eax = BB_U32(node + 0x18);
  return eax ? eax : bb_entity_home_idle(object);
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_human_ai_entry(void) {
  __asm__ volatile("pushl %eax;pushl %ecx;call " BB_ASM(
      bb_entity_human_ai_c) ";addl $8,%esp;ret");
}
#endif

/* 0x00446dc0: nearest eligible enemy building, within captured radius. */
uint32_t BB_THISCALL bb_entity_target_building(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  unsigned radius = BYTE(node + 0x12c), part = BYTE(node + 0xf4);
  int32_t best = radius << 16;
  if (part == 23 || part == 24)
    return 0;
  for (uint32_t other = near(node, radius); other;
       other = near_next(node, other, BYTE(node + 0x12c))) {
    if (BYTE(other + 0xf7) != BYTE(node + 0xf7) &&
        (BYTE(other + 0xd4) == 0x81 ||
         BB_U32(0x00663578 + BYTE(other + 0xd4) * 0x392u)) &&
        !(BB_U32(other + 0x9c) & 0x800000)) {
      int32_t value = distance(node, other);
      if (value < best) {
        chosen = other;
        best = value;
      }
    }
  }
  if (chosen)
    bb_entity_command_c(object, (void *)(uintptr_t)chosen, 1, 1, 0);
  return chosen;
}
static int target_matches(uint32_t node, uint32_t other) {
  unsigned kind = BYTE(other + 0xd4), filter = BYTE(node + 0x93);
  if (filter ? kind != filter : kind != 0x67 && kind != 0x6e)
    return 0;
  filter = BYTE(node + 0x92);
  if (filter ? (int32_t)BYTE(other + 0xf7) - (int32_t)filter != -1
             : BYTE(node + 0xf7) == BYTE(other + 0xf7))
    if (BYTE(other + 0xd4) != 0x6e)
      return 0;
  filter = BYTE(node + 0x94);
  return (!filter || BYTE(other + 0xf4) == filter) &&
         !(BB_U32(other + 0x9c) & 0x2800000);
}
/* Four complete selectors share eligibility, retaining their distinct ranking
 * and ties: last; lowest signed health with animal override; highest strength.
 */
static uint32_t target_unit(void *object, unsigned mode) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  int32_t best = mode == 1 ? 255 : 0;
  for (uint32_t other = near(node, BYTE(node + 0x12c)); other;
       other = near_next(node, other, BYTE(node + 0x12c)))
    if (target_matches(node, other)) {
      if (!mode) {
        chosen = other;
        continue;
      }
      int32_t value =
          mode == 1 ? SHORT(other + 0xa0)
                    : BYTE(other + 0x118) * 100 - SHORT(other + 0xa0) + 256;
      if (mode == 1 ? value < best || BYTE(other + 0xd4) == 0x6e
                    : value > best) {
        chosen = other;
        best = value;
      }
    }
  if (chosen)
    bb_entity_command_c(object, (void *)(uintptr_t)chosen, 1, 1, 0);
  return chosen;
}
uint32_t BB_THISCALL bb_entity_target_last(void *object) {
  return target_unit(object, 0);
}
uint32_t BB_THISCALL bb_entity_target_weak(void *object) {
  return target_unit(object, 1);
}
uint32_t BB_THISCALL bb_entity_target_strong(void *object) {
  return target_unit(object, 2);
}
uint32_t BB_THISCALL bb_entity_target_strong_alt(void *object) {
  return target_unit(object, 2);
}

/* 0x004473a0: nearest weighted enemy, prioritizing its current group target. */
uint32_t BB_THISCALL bb_entity_enemy_command(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  uint32_t eax = (BYTE(node + 0x12c) + 20u) << 16;
  int32_t best = eax;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4)) {
    eax = low_byte(eax, BYTE(node + 0xf7));
    if (BYTE(other + 0xf7) == (uint8_t)eax)
      continue;
    eax = low_byte(eax, BYTE(other + 0xd4));
    if ((uint8_t)eax != 0x67 && (uint8_t)eax != 0x68)
      continue;
    eax = BB_U32(other + 0x9c);
    if (eax & 0x2801000)
      continue;
    eax = low_byte(eax, BYTE(other + 0xf4));
    if (!(uint8_t)eax)
      continue;
    eax = distance(node, other);
    if ((int32_t)eax >= 0x1e0000)
      continue;
    eax += 0x140000;
    if (BB_U32(node + 0x9c) & 0x4000000) {
      if (BB_U32(node + 0x24) == BB_U32(other + 0xc))
        eax -= 0x140000;
      if (BYTE(other + 0x118))
        eax += 0xa0000;
    }
    if ((int32_t)eax < best) {
      chosen = other;
      best = eax;
    }
  }
  if (chosen) {
    uint32_t previous = BB_U32(chosen + 0x14);
    if (previous)
      eax = bb_entity_reset_move((void *)(uintptr_t)previous);
    BB_U32(chosen + 0x14) = node;
    bb_entity_command_c(object, (void *)(uintptr_t)chosen, 0, 1, eax);
  }
  return chosen;
}
/* 0x004474b0: retreat from the largest unsigned profession/distance score. */
uint32_t BB_THISCALL bb_entity_enemy_evade(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  uint32_t best = 0;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4))
    if (BYTE(other + 0xf7) != BYTE(node + 0xf7) &&
        (BYTE(other + 0xd4) == 0x67 || BYTE(other + 0xd4) == 0x68) &&
        !(BB_U32(other + 0x9c) & 0x2001000)) {
      int32_t value = distance(node, other);
      if (value < (int32_t)(BYTE(node + 0x12c) << 16)) {
        uint32_t score = BYTE(0x005e5f03 + BYTE(other + 0xf4) * 0x124u) -
                         (uint32_t)(value >> 16) + 30u;
        if (score > best) {
          chosen = other;
          best = score;
        }
      }
    }
  if (chosen) {
    uint32_t eax = BYTE(node + 0xf4);
    if (eax == 3) {
      eax = random_value() % 2;
      if (!eax)
        eax = feedback(object, 0x005fe398);
    } else if (eax == 9) {
      eax = BYTE(0x004843f0);
      if (BYTE(node + 0xf7) != (uint8_t)eax)
        eax = bb_entity_mage_offense(object);
    }
    bb_entity_evade_c(object, (void *)(uintptr_t)chosen, eax);
  }
  return chosen;
}
/* 0x004475d0: last nearby ally needing treatment, ordered actor/target
 * feedback. */
uint32_t BB_THISCALL bb_entity_doctor_find(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4))
    if (BYTE(other + 0x116) && BYTE(node + 0xf7) == BYTE(other + 0xf7) &&
        other != node && !(BB_U32(other + 0x9c) & 0x2000000) &&
        (int32_t)distance(node, other) < 0xa0000)
      chosen = other;
  if (chosen) {
    uint32_t eax = bb_entity_reset_move(object);
    BYTE(node + 0xf6) = 0;
    bb_entity_activity_c(object, 17, (void *)(uintptr_t)chosen, eax);
    if (!BYTE(node + 0x102)) {
      BYTE(node + 0x102) = 3;
      feedback(object, 0x005e42ac);
      bb_entity_feedback_queue((void *)(uintptr_t)chosen, BB_U32(0x006a992c),
                               BB_POINTER(0x005e42ac));
    }
  }
  return chosen;
}

/* 0x00455340: area spells use the captured enemy centroid, then a strongest
 * profession fallback. Successful area requests return the literal flag 1.
 */
uint32_t BB_THISCALL bb_entity_mage_offense(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  uint32_t count = 0, sum_x = 0, sum_y = 0, strongest = 0;
  for (uint32_t other = near(node, 12); other;
       other = near_next(node, other, 12))
    if (BYTE(other + 0xf7) != BYTE(node + 0xf7) &&
        (BYTE(other + 0xd4) == 0x67 || BYTE(other + 0xd4) == 0x68) &&
        !(BB_U32(other + 0x9c) & 0x2800000)) {
      sum_x += BYTE(other + 0xd8);
      sum_y += BYTE(other + 0xd9);
      unsigned power = BYTE(0x005e5f03 + BYTE(other + 0xf4) * 0x124u);
      if (power > strongest) {
        chosen = other;
        strongest = power;
      }
      ++count;
    }
  if (!count)
    return 0;
  if (count > 2) {
    uint32_t x = sum_x / count, y = sum_y / count;
    if ((int32_t)bb_entity_distance((uint8_t)x, (uint8_t)y, BYTE(node + 0xd8),
                                    BYTE(node + 0xd9)) > 0x60000) {
      unsigned energy = BYTE(node + 0x109);
      if (energy >= BYTE(0x00640f6b)) {
        bb_entity_cast(object, 0x4b, x, y);
        return 1;
      }
      if (energy >= BYTE(0x00640f70)) {
        bb_entity_cast(object, 0x50, x, y);
        return 1;
      }
    }
  }
  if (!chosen)
    return 0;
  return bb_entity_cast(object,
                        BYTE(node + 0x109) >= BYTE(0x00640f6c) ? 0x4c : 0x48,
                        BYTE(chosen + 0xd8), BYTE(chosen + 0xd9));
}
/* 0x004554f0: three independent allied populations; strict burn dominance,
 * then health on a disease tie, with the original low-energy return byte.
 */
uint32_t BB_THISCALL bb_entity_mage_support(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t counts[3] = {0}, xs[3] = {0}, ys[3] = {0};
  for (uint32_t other = near(node, 12); other;
       other = near_next(node, other, 12))
    if (BYTE(other + 0xf7) == BYTE(node + 0xf7)) {
      if ((BYTE(other + 0xd4) == 0x67 || BYTE(other + 0xd4) == 0x68) &&
          SHORT(other + 0xa0) < SHORT(other + 0xa2)) {
        ++counts[1];
        xs[1] += BYTE(other + 0xd8);
        ys[1] += BYTE(other + 0xd9);
      }
      if (BB_U32(other + 0x9c) & 0x80000) {
        ++counts[0];
        xs[0] += BYTE(other + 0xd8);
        ys[0] += BYTE(other + 0xd9);
      }
      if (BYTE(other + 0x11e)) {
        ++counts[2];
        xs[2] += BYTE(other + 0xd8);
        ys[2] += BYTE(other + 0xd9);
      }
    }
  if (!counts[0] && !counts[1] && !counts[2])
    return 0;
  unsigned group = counts[0] > counts[1] && counts[0] > counts[2] ? 0
                   : counts[2] > counts[1]                        ? 2
                                                                  : 1;
  const uintptr_t prices[] = {0x00640f6d, 0x00640f86, 0x00640f6e};
  const unsigned spells[] = {0x4d, 0x66, 0x4e};
  uint32_t price = BYTE(prices[group]);
  if (BYTE(node + 0x109) < price)
    return price;
  return bb_entity_cast(object, spells[group], xs[group] / counts[group],
                        ys[group] / counts[group]);
}

/* 0x00455ae0: idle feedback; selection 3 provokes nearby humans and animals,
 * queuing each recipient's feedback using the actor's live profession.
 */
uint32_t BB_THISCALL bb_entity_idle_action(void *object) {
  uint32_t node = bb_address_encode(object);
  int32_t selection = random_value() % 4;
  uint32_t eax = low_byte(selection, BYTE(node + 0x104));
  if ((uint8_t)eax)
    return eax;
  uint32_t sample = BB_U32(0x006a9058 + selection * 4u);
  eax = bb_entity_feedback_c(object, (void *)(uintptr_t)sample, 0, 0, sample);
  if (selection != 3)
    return eax;
  for (uint32_t other = near(node, 5); other;
       other = near_next(node, other, 5))
    if (other != node) {
      unsigned kind = BYTE(other + 0xd4);
      if (kind == 0x67 || kind == 0x68 || kind == 0x6e) {
        bb_entity_evade_c((void *)(uintptr_t)other, object, kind);
        if (random_value() % 4 == 0) {
          unsigned part = BYTE(node + 0xf4);
          bb_entity_feedback_delay((void *)(uintptr_t)other,
                                   BB_U32(part == 2 || part == 14 ? 0x00602494
                                          : part == 12            ? 0x0066350c
                                                                  : 0x005117d4),
                                   16);
        }
      }
    }
  return 0;
}
