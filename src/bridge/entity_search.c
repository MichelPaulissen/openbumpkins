/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
static int32_t random_value(void) { return bb_random(); }
static int32_t distance(uint32_t node, uint32_t other) {
  return bb_entity_distance(BYTE(node + 0xd8), BYTE(node + 0xd9),
                            BYTE(other + 0xd8), BYTE(other + 0xd9));
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
static uintptr_t reserve(uintptr_t actor, uintptr_t chosen) {
  if (chosen) {
    uint32_t previous = BB_U32(chosen + 0x14);
    if (previous)
      bb_entity_reset_move((void *)(uintptr_t)previous);
    BB_U32(chosen + 0x14) = actor;
  }
  return chosen;
}
/* The stale link is cleared only after this candidate beats the current best;
 * it is skipped on that visit. A live worker must be idle and farther away.
 */
static int worker_available(uint32_t other, int32_t actor_distance) {
  uintptr_t worker = BB_U32(other + 0x14);
  if (!worker)
    return 1;
  if (BB_U32(worker + 0x18) != other) {
    BB_U32(other + 0x14) = 0;
    return 0;
  }
  return !BYTE(worker + 0xf2) &&
         (distance(worker, other) >> 16) > actor_distance;
}
/* 0x00442490/0x004425b0/0x004426d0/0x00446b80: ordered work searches with
 * signed whole-tile distances, strict first ties and live reservation reads.
 */
static uintptr_t work_search(void *object, unsigned mode) {
  uintptr_t actor = bb_address_encode(object), chosen = 0;
  int32_t best = mode == 5 ? 10000 : 32;
  uintptr_t count = mode == 1   ? 0x005550a4
                    : mode == 2 ? 0x005550a6
                                : 0x005550a0;
  if (mode != 5 && !WORD(count + BYTE(actor + 0xf7) * 0xd00u))
    return 0;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4)) {
    if (BYTE(actor + 0xf7) != BYTE(other + 0xf7))
      continue;
    unsigned kind = BYTE(other + 0xd4);
    int eligible =
        mode == 1   ? kind == 0x4e && BYTE(other + 0xf2) == 2
        : mode == 2 ? kind == 0x4f
        : mode == 4 ? kind == 0x6e && !BYTE(other + 0xf5) && !BYTE(other + 0xf2)
                    : ((kind == 0x72 || kind == 0x73 || kind == 0x74) &&
                       !BYTE(other + 0xf2)) ||
                          (kind == 0x46 && BYTE(other + 0xf2) == 0x16);
    if (!eligible)
      continue;
    int32_t value = distance(actor, other) >> 16;
    if (value < best && worker_available(other, value)) {
      best = value;
      chosen = other;
    }
  }
  return mode == 5 ? reserve(actor, chosen) : chosen;
}
void *BB_THISCALL bb_entity_work_wood(void *object) {
  return (void *)(uintptr_t)work_search(object, 1);
}
void *BB_THISCALL bb_entity_work_stone(void *object) {
  return (void *)(uintptr_t)work_search(object, 2);
}
void *BB_THISCALL bb_entity_work_hunt(void *object) {
  return (void *)(uintptr_t)work_search(object, 4);
}
void *BB_THISCALL bb_entity_work_clear(void *object) {
  return (void *)(uintptr_t)work_search(object, 5);
}
/* 0x00446cb0: the owner's count gates a search that accepts foreign crops.
 * Existing workers are compared by distance regardless of subtype/link state.
 */
void *BB_THISCALL bb_entity_work_field(void *object) {
  uintptr_t actor = bb_address_encode(object), chosen = 0;
  if (!WORD(0x0055503c + BYTE(actor + 0xf7) * 0xd00u))
    return 0;
  int32_t best = 20;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4)) {
    if (BYTE(other + 0xd4) != 0x3a)
      continue;
    unsigned subtype = BYTE(other + 0xf2);
    if (subtype != 0x1b && subtype != 0x22 && subtype != 0x21 &&
        subtype != 0x24 && subtype != 0x25)
      continue;
    int32_t value = distance(actor, other) >> 16;
    if (value >= best)
      continue;
    uintptr_t worker = BB_U32(other + 0x14);
    if (!worker || (distance(worker, other) >> 16) > value) {
      best = value;
      chosen = other;
    }
  }
  return (void *)(uintptr_t)reserve(actor, chosen);
}
/* 0x00446960: prefer repair score (distance*5 - missing health), comparing
 * existing workers by Manhattan distance, then kind-seven cultivation work.
 */
void *BB_THISCALL bb_entity_work_repair(void *object) {
  uintptr_t actor = bb_address_encode(object), chosen = 0;
  int32_t best = 10000;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4)) {
    if (BYTE(actor + 0xf7) != BYTE(other + 0xf7))
      continue;
    uintptr_t definition = BYTE(other + 0xd4) * 0x392u;
    if (!BB_U32(0x00663578 + definition) || !BB_U32(0x006638ba + definition) ||
        SHORT(other + 0xa0) >= SHORT(0x006635ae + definition))
      continue;
    int32_t value = distance(actor, other) >> 16;
    definition = BYTE(other + 0xd4) * 0x392u;
    value = value * 5 - SHORT(0x006635ae + definition) + SHORT(other + 0xa0);
    if (value >= best)
      continue;
    uintptr_t worker = BB_U32(other + 0x14);
    if (worker) {
      int32_t x = (int32_t)BYTE(worker + 0xd8) - BYTE(other + 0xd8);
      int32_t y =
          (int32_t)BYTE(BB_U32(other + 0x14) + 0xd9) - BYTE(other + 0xd9);
      if ((x < 0 ? -x : x) + (y < 0 ? -y : y) <= value)
        continue;
    }
    best = value;
    chosen = other;
  }
  if (!chosen) {
    best = 10;
    for (uint32_t other = BB_U32(0x00554f98); other;
         other = BB_U32(other + 4)) {
      if (BYTE(other + 0xd4) != 7 ||
          !BYTE(0x00559b61 +
                (BYTE(actor + 0xd9) * 192u + BYTE(actor + 0xd8)) * 12u))
        continue;
      int32_t value = distance(actor, other) >> 16;
      if (value >= best)
        continue;
      uintptr_t worker = BB_U32(other + 0x14);
      if (!worker || (distance(worker, other) >> 16) > value) {
        best = value;
        chosen = other;
      }
    }
  }
  return (void *)(uintptr_t)reserve(actor, chosen);
}
/* 0x00446110/0x004461f0: nearest stock or independent live fruit/well gate.
 * Distances truncate to signed whole tiles; nonzero stock includes high bits.
 */
static uintptr_t need_search(void *object, unsigned water) {
  uintptr_t actor = bb_address_encode(object), chosen = 0;
  int32_t best = 10000;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4)) {
    uintptr_t payload = BB_U32(other + 0x38);
    if (payload && BYTE(other + 0xf7) == BYTE(actor + 0xf7)) {
      unsigned type =
          water ? BYTE(0x006638c7 + BYTE(other + 0xd4) * 0x392u) : 1;
      if ((type == 1 || type == 9) && WORD(payload)) {
        int32_t value = distance(actor, other) >> 16;
        if (value < best) {
          best = value;
          chosen = other;
        }
      }
    }
    int eligible =
        water ? BYTE(other + 0xd4) == 0x90 && BYTE(other + 0x101) == 3 &&
                    BYTE(other + 0xf7) == BYTE(actor + 0xf7) &&
                    !BYTE(other + 0xea)
              : BYTE(actor + 0xf7) == BYTE(other + 0xf7) &&
                    BYTE(other + 0xd4) == 0x6f && BYTE(other + 0xf2) == 0x5c;
    if (eligible) {
      int32_t value = distance(actor, other) >> 16;
      if (value < best) {
        best = value;
        chosen = other;
      }
    }
  }
  return chosen;
}
void *BB_THISCALL bb_entity_need_food(void *object) {
  return (void *)(uintptr_t)need_search(object, 0);
}
void *BB_THISCALL bb_entity_need_water(void *object) {
  return (void *)(uintptr_t)need_search(object, 1);
}
/* 0x00446880: select the nearest matching order and update its captured price
 * on every strict improvement, even if a callback changes live order metadata.
 */
void *BB_THISCALL bb_entity_order_site(void *object) {
  uintptr_t actor = bb_address_encode(object), chosen = 0;
  int32_t best = 0x400000;
  for (uint32_t other = BB_U32(0x00554f98); other; other = BB_U32(other + 4)) {
    if (!BB_U32(other + 0x38) || BYTE(other + 0xf7) != BYTE(actor + 0xf7))
      continue;
    uintptr_t definition = BYTE(other + 0xd4) * 0x392u;
    if (BYTE(0x006635bc + definition))
      continue;
    unsigned order = BYTE(0x006638c6 + definition);
    if (BYTE(actor + 0xfb) != order)
      continue;
    int32_t value = distance(actor, other);
    if (value < best) {
      best = value;
      chosen = other;
      WORD(actor + 0x9a) = WORD(0x005e43f8 + order * 36u);
    }
  }
  return (void *)(uintptr_t)chosen;
}
/* 0x00446300: optional force ignores readiness, never sex/home/fire/subtypes.
 */
uint32_t BB_THISCALL
bb_entity_pair_allowed(void *object, void *other, uint32_t force) {
  uintptr_t actor = bb_address_encode(object), partner = bb_address_encode(other);
  if (BYTE(0x005e5eff + BYTE(actor + 0xf4) * 0x124u) != 2 ||
      BYTE(0x005e5eff + BYTE(partner + 0xf4) * 0x124u) != 1)
    return 0;
  int ready = !BYTE(actor + 0x108);
  if (BYTE(actor + 0x113) < 20 || BYTE(partner + 0x113) < 20 ||
      BYTE(actor + 0xf6) || BYTE(partner + 0xf6))
    ready = 0;
  uint32_t home = BB_U32(actor + 0x1c);
  if (!home || BYTE(home + 0xd4) != 2 || (BB_U32(home + 0x9c) & 0x80000))
    return 0;
  return !BYTE(actor + 0xf2) && !BYTE(partner + 0xf2) && (ready || force);
}
/* 0x004476b0: the captured signed selection adjusts all allied humans,
 * then selects feedback using the same possibly negative table index.
 */
uint32_t BB_THISCALL bb_entity_clown_action(void *object) {
  uintptr_t actor = bb_address_encode(object);
  int32_t choice = random_value() % 5;
  for (uint32_t other = near(actor, 4); other;
       other = near_next(actor, other, 4))
    if (BYTE(actor + 0xf7) == BYTE(other + 0xf7) && BYTE(other + 0xd4) == 0x67)
      bb_entity_adjust((void *)(uintptr_t)other, 0x111, choice + 5, 0);
  uint32_t sample = BB_U32(0x006a8ec0 + choice * 4u);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sample, 0, 0, sample);
}
/* 0x00447750: fresh RNG for each eligible ally, preserving signed penalties. */
uint32_t BB_THISCALL bb_entity_morale_shock(void *object) {
  uintptr_t actor = bb_address_encode(object);
  for (uint32_t other = near(actor, 6); other;
       other = near_next(actor, other, 6))
    if (BYTE(actor + 0xf7) == BYTE(other + 0xf7) &&
        BYTE(other + 0xd4) == 0x67) {
      unsigned part = BYTE(other + 0xf4);
      if (part == 1 || part == 2 || part == 13 || part == 14 || part == 11 ||
          part == 12)
        bb_entity_adjust((void *)(uintptr_t)other, 0x111, -8 - random_value() % 8, 0);
    }
  return 0;
}
/* 0x00447810: return home, change homeless professions 32/33, or seek the
 * first allied building near captured random byte coordinates.
 */
uint32_t BB_THISCALL bb_entity_home_idle(void *object) {
  uintptr_t actor = bb_address_encode(object);
  uint32_t eax = BB_U32(actor + 0x9c);
  if (eax & 0x600000)
    return eax;
  unsigned part = BYTE(actor + 0xf4);
  if (part == 32 || part == 33) {
    eax = BB_U32(actor + 0x1c);
    return eax ? bb_entity_approach_c(object, (void *)(uintptr_t)eax, 1, eax)
               : bb_entity_profession(object, part == 33 ? 2 : 1);
  }
  int32_t value = random_value();
  eax = BB_U32(0x00484288) ? 0 : (uint8_t)(value % 5);
  if (!eax) {
    eax = BB_U32(actor + 0x1c);
    if (eax && !(BB_U32(eax + 0x9c) & 0x80000))
      eax = bb_entity_approach_c(object, (void *)(uintptr_t)eax, 1, eax);
    return eax;
  }
  if (eax != 1)
    return eax;
  value = random_value();
  uint8_t x = BYTE(actor + 0xd8) + value % 33 - 16;
  value = random_value();
  uint8_t y = BYTE(actor + 0xd9) + value % 33 - 16;
  for (uint32_t other = (uintptr_t)bb_entity_near((void *)(uintptr_t)0x00553f90, x, y, 6);
       other; other = (uintptr_t)bb_entity_near_next((void *)(uintptr_t)0x00553f90,
                                                     (void *)(uintptr_t)other, x, y, 6))
    if (BYTE(actor + 0xf7) == BYTE(other + 0xf7) &&
        BB_U32(0x00663578 + BYTE(other + 0xd4) * 0x392u))
      return bb_entity_approach_c(object, (void *)(uintptr_t)other, 1, other);
  return 0;
}
