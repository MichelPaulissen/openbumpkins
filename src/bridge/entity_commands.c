/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef uint32_t (*sound_fn)(uint32_t, uint32_t, uint32_t);
typedef int32_t (*random_fn)(void);

static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00) | (uint8_t)byte;
}
static uint32_t tile(uint32_t x, uint32_t y) { return (y * 192 + x) * 12; }
static uint32_t node_tile(uint32_t node) {
  return tile(BYTE(node + 0xd8), BYTE(node + 0xd9));
}

/* 0x00435270/0x004352d0: temporarily count active kind-0xa9 obstacles.
 * Multiple entities on the same tile and byte overflow remain significant.
 */
uint32_t bb_entity_blockers_c(void *manager, uint32_t increment, uint32_t eax) {
  uint32_t node = BB_U32(bb_address_encode(manager) + 0x1008);
  if (node)
    eax = 0xa9;
  for (; node; node = BB_U32(node + 4))
    if (BYTE(node + 0xd4) == 0xa9 && BYTE(node + 0xf2))
      BYTE(0x00559b66 + node_tile(node)) += increment;
  return eax;
}

/* 0x004505a0/0x004505e0: start or stop movement and adjust tile occupancy.
 * Stop snaps the saved fractions to the live tile, not the old world position.
 */
uint64_t bb_entity_move_start_c(void *object, uint32_t eax, uint32_t edx) {
  uint32_t node = bb_address_encode(object);
  eax = low_byte(eax, BYTE(node + 0x11d));
  if (!(uint8_t)eax) {
    edx = BYTE(node + 0xd8);
    eax = BYTE(node + 0xd9) * 192 + edx;
    BB_U32(0x00559b68 + eax * 12) &= 0xfffffffe;
  }
  BYTE(node + 0x11d) = 1;
  return ((uint64_t)edx << 32) | eax;
}
uint64_t bb_entity_move_stop_c(void *object) {
  uint32_t node = bb_address_encode(object);
  if (BYTE(node + 0x11d))
    BB_U32(0x00559b68 + node_tile(node)) |= 1;
  BYTE(node + 0x11d) = 0;
  BB_U32(node + 0x7a) = 0;
  uint32_t x = (uint32_t)BYTE(node + 0xd8) << 16;
  BB_U32(node + 0x7e) = 0;
  BB_U32(node + 0x6e) = 0;
  BB_U32(node + 0x72) = 0;
  x += WORD(node + 0xd0);
  uint32_t eax = WORD(node + 0xd2);
  BB_U32(node + 0x62) = x;
  uint32_t y = ((uint32_t)BYTE(node + 0xd9) << 16) + eax;
  BB_U32(node + 0x66) = y;
  return ((uint64_t)y << 32) | eax;
}

/* 0x004559c0: locate a command target, leading its motion for ranged units.
 * Buildings use their live definition offsets; the second read stays live.
 */
uint32_t BB_THISCALL bb_entity_command_position(void *object) {
  uint32_t node = bb_address_encode(object), target = BB_U32(node + 0xc);
  if (!target) {
    BYTE(node + 0xe3) = 0;
    BYTE(node + 0xe4) = 0;
    return 0;
  }
  uint32_t kind = BYTE(target + 0xd4) * 914;
  if (BB_U32(0x00663578 + kind)) {
    uint32_t eax = BB_U32(0x0066389e + kind);
    BYTE(node + 0xe3) = eax + BYTE(target + 0xd8);
    eax = BB_U32(0x006638a2 + BYTE(target + 0xd4) * 914);
    eax = low_byte(eax, eax + BYTE(target + 0xd9));
    BYTE(node + 0xe4) = eax;
    return eax;
  }
  uint32_t vx = BB_U32(target + 0x6e);
  if ((vx || BB_U32(target + 0x72)) && BYTE(node + 0x12a) > 2 &&
      BYTE(target + 0xf2) != 7) {
    uint32_t delta = bb_fixed_multiply(vx, 0x240000);
    uint32_t x = BB_U32(target + 0x62) + delta;
    target = BB_U32(node + 0xc);
    BYTE(node + 0xe3) = x >> 16;
    uint32_t eax = bb_fixed_multiply(BB_U32(target + 0x72), 0x240000);
    BYTE(node + 0xe4) = (BB_U32(target + 0x66) + eax) >> 16;
    return eax;
  }
  uint32_t eax = low_byte(vx, BYTE(target + 0xd8));
  BYTE(node + 0xe3) = eax;
  BYTE(node + 0xe4) = BYTE(target + 0xd9);
  return eax;
}

/* 0x00441740: check and reserve a destination. Reservation masks use a
 * DWORD shift for checks and a byte result for clearing/setting tile bits.
 */
uint32_t BB_THISCALL
bb_entity_move_goal(void *object, uint32_t requested_x, uint32_t requested_y) {
  uint32_t node = bb_address_encode(object);
  uint8_t x = requested_x, y = requested_y;
  uint32_t state = BYTE(node + 0x11d);
  uint32_t result = x == BYTE(node + 0xd8) && y == BYTE(node + 0xd9) &&
                    (state == 0 || state == 3);
  uint32_t offset = tile(x, y), faction = BYTE(node + 0xf7);
  uint32_t mask = 1u << (faction & 31);
  if (!(BYTE(0x00559b64 + offset) & mask) &&
      !(BB_U32(0x00559b68 + offset) & 1) && !BYTE(0x00559b66 + offset) &&
      (BB_U32(0x006a9092 + BYTE(0x00559b60 + offset) * 8) &
       (BYTE(node + 0xd5) | 4)))
    result = 1;
  else if ((BB_U32(0x00559b68 + offset) & 0x100000) || (!x && !y))
    result = 1;
  if (BYTE(node + 0xd4) == 0x68 && (BB_U32(0x00559b68 + offset) & 2))
    result = 0;
  if (result) {
    uint32_t old = tile(BYTE(node + 0xdd), BYTE(node + 0xde));
    BYTE(0x00559b64 + old) &= (uint8_t)~mask;
    BYTE(node + 0xdd) = x;
    BYTE(node + 0xde) = y;
    mask = 1u << (BYTE(node + 0xf7) & 31);
    BYTE(0x00559b64 + offset) |= (uint8_t)mask;
  }
  return result;
}

static int candidate(uint32_t node, int32_t x, int32_t row, uintptr_t flags,
                     uint32_t movement) {
  return row > 0 && x > 0 && row < 0x9000 && x < 192 && !BYTE(flags - 2) &&
         !(BB_U32(flags) & 3) &&
         !(BYTE(flags - 4) & (1u << (BYTE(node + 0xf7) & 31))) &&
         (BB_U32(0x006a9092 + BYTE(flags - 8) * 8) & movement);
}

/* 0x00441ad0: choose the closest usable tile in a live 13-by-13 region.
 * The first pass respects the target's footprint/range, the fallback does
 * not. Strict distance improvement keeps the original row-first tie order.
 */
uint32_t BB_THISCALL
bb_entity_move_near(void *object, uint8_t *output_x, uint8_t *output_y) {
  uint32_t node = bb_address_encode(object);
  uint32_t movement = BYTE(node + 0xd5) | 4;
  BYTE(node + 0xdf) = BYTE(output_x);
  BYTE(node + 0xe0) = BYTE(output_y);
  BYTE(output_x) = 0;
  BYTE(output_y) = 0;
  bb_entity_command_position(object);
  int32_t best = 0x800000;
  uint32_t found = 0;
  for (unsigned pass = 0; pass < 2; ++pass) {
    if (pass && (found || !BB_U32(node + 0xc)))
      break;
    int32_t y = (int32_t)BYTE(node + 0xe0) - 6, row = y * 192;
    for (; y <= (int32_t)BYTE(node + 0xe0) + 6; ++y, row += 192) {
      int32_t x = (int32_t)BYTE(node + 0xdf) - 6;
      uintptr_t flags = 0x00559b68 + (uint32_t)(row + x) * 12;
      for (; x <= (int32_t)BYTE(node + 0xdf) + 6; ++x, flags += 12) {
        if (!candidate(node, x, row, flags, movement))
          continue;
        int32_t index = (y - BYTE(node + 0xe0)) * 64 - BYTE(node + 0xdf) + x;
        int32_t distance = BB_I32(0x005fb2f0 + (uint32_t)index * 4);
        if (distance >= best)
          continue;
        if (!pass && BB_U32(node + 0xc)) {
          uint32_t target = BB_U32(node + 0xc);
          uint32_t property = BB_U32(0x00663578 + BYTE(target + 0xd4) * 914);
          if (property) {
            uint32_t adjacent = (uintptr_t)bb_entity_outer_at(
                (void *)(uintptr_t)0x00553f90, (uint8_t)x, low_byte(property, y - 1));
            if (adjacent != BB_U32(node + 0xc)) {
              adjacent = (uintptr_t)bb_entity_outer_at(
                  (void *)(uintptr_t)0x00553f90, (uint8_t)x, low_byte(adjacent, y + 1));
              if (adjacent != BB_U32(node + 0xc)) {
                adjacent = (uintptr_t)bb_entity_outer_at((void *)(uintptr_t)0x00553f90,
                                                         low_byte(y, x - 1), y);
                if (adjacent != BB_U32(node + 0xc)) {
                  adjacent = (uintptr_t)bb_entity_outer_at(
                      (void *)(uintptr_t)0x00553f90, low_byte(y, x + 1), y);
                  if (adjacent != BB_U32(node + 0xc))
                    continue;
                }
              }
            }
          } else {
            int32_t range_distance =
                bb_entity_distance(x, y, BYTE(node + 0xe3), BYTE(node + 0xe4));
            if ((int32_t)((BYTE(node + 0x12a) << 16) + 0x8000) < range_distance)
              continue;
          }
        }
        BYTE(output_x) = x;
        BYTE(output_y) = y;
        best = distance;
        found = 1;
      }
    }
  }
  return found;
}

/* 0x004418a0: issue a Q16 movement request, find a usable destination,
 * retry route search with bit four temporarily enabled, then start or stop.
 * Route search and terrain unregistration also run in C.
 */
uint32_t BB_THISCALL bb_entity_move(void *object, uint32_t x,
                                                  uint32_t y) {
  uint32_t node = bb_address_encode(object);
  if (BYTE(node + 0xd4) == 0x67 && BYTE(node + 0xf2))
    return 1;
  BYTE(node + 0xd6) = BYTE(node + 0xd8);
  BYTE(node + 0xd7) = BYTE(node + 0xd9);
  BB_U32(node + 0x2c) = 0;
  WORD(node + 0xd0) = x;
  WORD(node + 0xd2) = y;
  uint8_t tile_x = x >> 16, tile_y = y >> 16;
  if (!bb_entity_move_goal(object, tile_x, tile_y)) {
    if (!bb_entity_move_near(object, &tile_x, &tile_y)) {
      if (tile_x == BYTE(node + 0xd8) && tile_y == BYTE(node + 0xd9)) {
        BYTE(node + 0xdd) = tile_x;
        BYTE(node + 0xde) = tile_y;
        bb_entity_move_goal(object, tile_x, tile_y);
        BYTE(node + 0x11d) = 3;
        return 1;
      }
      bb_entity_move_stop_c(object);
      return 0;
    }
    bb_entity_move_goal(object, tile_x, tile_y);
  }
  if (tile_x == BYTE(node + 0xd8) && tile_y == BYTE(node + 0xd9)) {
    BYTE(node + 0xdd) = tile_x;
    BYTE(node + 0xde) = tile_y;
    BYTE(node + 0x11d) = 3;
    bb_entity_move_stop_c(object);
    return 1;
  }
  bb_entity_release_tiles(object);
  if (BYTE(node + 0xd4) == 0x6e || BYTE(node + 0xd4) == 0x68)
    bb_entity_blockers_c((void *)(uintptr_t)0x00553f90, 1, 0);
  if (!bb_entity_route(object, tile_x, tile_y) && !(BYTE(node + 0xd5) & 4)) {
    BYTE(node + 0xd5) |= 4;
    bb_entity_route(object, tile_x, tile_y);
    BYTE(node + 0xd5) &= 0xfb;
  }
  if (BYTE(node + 0xd4) == 0x6e || BYTE(node + 0xd4) == 0x68)
    bb_entity_blockers_c((void *)(uintptr_t)0x00553f90, 0xffffffff, 0);
  if (BYTE(node + 0xf0)) {
    bb_entity_move_start_c(object, 0, 0);
    bb_entity_register_tiles(object);
    return 1;
  }
  bb_entity_move_stop_c(object);
  bb_entity_register_tiles(object);
  return 0;
}

/* 0x0043fb50: step back through occupied tiles toward the unit until a
 * move succeeds. Preserve retries, byte counters and the original loop when
 * every request fails; reaching the unit does not force a successful result.
 */
uint32_t BB_THISCALL bb_entity_move_escape(void *object,
                                                         uint32_t requested_x,
                                                         uint32_t requested_y) {
  uint32_t node = bb_address_encode(object);
  uint8_t original_x = requested_x, original_y = requested_y;
  uint8_t x = original_x, y = original_y;
  uint32_t eax = bb_entity_move(object, ((uint32_t)x << 16) + 0x8000,
                                ((uint32_t)y << 16) + 0x8000);
  for (;;) {
    if (eax) {
      eax = low_byte(eax, BYTE(node + 0xd8));
      if ((uint8_t)eax == x) {
        eax = low_byte(eax, BYTE(node + 0xd9));
        if ((uint8_t)eax == y) {
          eax = low_byte(eax, original_x);
          if (x == original_x) {
            eax = low_byte(eax, original_y);
            if (y == original_y) {
              BYTE(node + 0x139) = 0;
              return eax;
            }
          }
          eax = bb_entity_command_c(object, 0, 1, 1, eax);
          BYTE(node + 0xfa) = 200;
          return eax;
        }
      }
      BYTE(node + 0x139) = 0;
      return eax;
    }
    unsigned state = 2;
    uint8_t own_x = BYTE(node + 0xd8);
    for (;;) {
      if (state == 2 && BYTE(0x00559b66 + tile(x, y)))
        state = 1;
      else if (state == 1 && !BYTE(0x00559b66 + tile(x, y)))
        state = 0;
      if (own_x == x && BYTE(node + 0xd9) == y)
        state = 0;
      if (!state)
        break;
      if (x < BYTE(node + 0xd8))
        ++x;
      else if (x > BYTE(node + 0xd8))
        --x;
      if (y < BYTE(node + 0xd9))
        ++y;
      else if (y > BYTE(node + 0xd9))
        --y;
    }
    ++BYTE(node + 0x139);
    eax = bb_entity_move(object, ((uint32_t)x << 16) + 0x8000,
                         ((uint32_t)y << 16) + 0x8000);
  }
}

/* 0x00441710 and 0x0043fae0: reset at the current tile or replace a walk. */
uint32_t BB_THISCALL bb_entity_reset_move(void *object) {
  uint32_t node = bb_address_encode(object);
  bb_entity_move_escape(object, BYTE(node + 0xd8), BYTE(node + 0xd9));
  BB_U32(node + 0x18) = 0;
  BYTE(node + 0xf6) = 0;
  return 0;
}
uint32_t bb_entity_walk_c(void *object, uint32_t x, uint32_t y,
                          uint32_t change_ability, uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  eax = low_byte(eax, BYTE(node + 0xf2));
  if ((uint8_t)eax == 0x73)
    return eax;
  bb_entity_command_c(object, 0, 1, 1, eax);
  BYTE(node + 0xe3) = 0;
  BYTE(node + 0xe4) = 0;
  bb_entity_subtype(object, 0);
  BB_U32(node + 0x18) = 0;
  if (change_ability)
    bb_entity_set_ability(object, 0x16);
  BYTE(node + 0xf6) = 0;
  return bb_entity_move_escape(object, x, y);
}

/* 0x00455240: move five tiles away from a command target, with byte wrap. */
uint32_t bb_entity_evade_c(void *object, void *other, uint32_t eax) {
  uint32_t node = bb_address_encode(object), target = bb_address_encode(other);
  eax = BB_U32(node + 0x9c);
  if (eax & 0x800000)
    return eax;
  uint8_t x = BYTE(node + 0xd8), tx = BYTE(target + 0xd8);
  uint8_t y = BYTE(node + 0xd9), ty = BYTE(target + 0xd9);
  int same_x = x == tx;
  eax = low_byte(eax, ty);
  if (x < tx)
    x -= 5;
  else if (x > tx)
    x += 5;
  if (y < ty)
    y -= 5;
  else if (y > ty || same_x)
    y += 5;
  return bb_entity_walk_c(object, x, y, 0, eax);
}

/* 0x00453a00/0x00453a80: optional command feedback and positional sound.
 * The feedback counter is read after sound playback and redraw stays C.
 */
uint32_t bb_entity_feedback_c(void *object, void *sound, uint32_t force,
                              uint32_t other_faction, uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  if (!sound)
    return eax;
  eax = low_byte(eax, BYTE(node + 0x104));
  if ((uint8_t)eax) {
    eax = force;
    if (!force)
      return eax;
  }
  eax = low_byte(eax, BYTE(0x004843f0));
  if (BYTE(node + 0xf7) != (uint8_t)eax) {
    eax = low_byte(eax, BYTE(node + 0xd4));
    if ((uint8_t)eax == 0x67) {
      eax = other_faction;
      if (!other_faction)
        return eax;
    }
  }
  uint32_t y = BYTE(node + 0xd9), x = BYTE(node + 0xd8);
  eax = bb_sound_position_entry((uintptr_t)sound, x, y);
  BYTE(node + 0x104) = BYTE((uintptr_t)sound + 0x82);
  return bb_entity_dirty_c(object, eax, 0);
}
uint32_t BB_THISCALL bb_entity_command_sound(void *object,
                                                           uint32_t sound) {
  uint32_t node = bb_address_encode(object);
  uint32_t y = BYTE(node + 0xd9), x = BYTE(node + 0xd8);
  return bb_sound_position_entry(sound, x, y);
}

/* 0x00445ff0: recruit idle same-faction units within ten tiles, up to the
 * target's live follower limit. Selection/faction checks remain live after
 * each command and the next query uses the source's current coordinates.
 */
uint32_t bb_entity_recruit_c(void *object, void *other, uint32_t eax) {
  uint32_t node = bb_address_encode(object), target = bb_address_encode(other);
  if (!target)
    return eax;
  eax = BB_U32(target + 0x9c);
  if (eax & 0x2800000)
    return eax;
  eax = bb_entity_distance(BYTE(target + 0xd8), BYTE(target + 0xd9),
                           BYTE(node + 0xd8), BYTE(node + 0xd9));
  if ((int32_t)eax > 0xe0000)
    return eax;
  uint32_t next = (uintptr_t)bb_entity_near(
      (void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8), BYTE(node + 0xd9), 10);
  while (next) {
    uint32_t faction = BYTE(node + 0xf7);
    if (BYTE(next + 0xf7) == faction &&
        (BYTE(next + 0xd4) == 0x67 || BYTE(next + 0xd4) == 0x68) &&
        !BB_U32(next + 0xc) && BYTE(target + 0x118) < 4 &&
        !BYTE(next + 0x11d) &&
        (!(BYTE(next + 0x12d) & (1u << (faction & 31))) ||
         BYTE(0x004843f0) != faction) &&
        next != node)
      bb_entity_command_c((void *)(uintptr_t)next, other, 1, 0, BYTE(0x004843f0));
    next = (uintptr_t)bb_entity_near_next((void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)next,
                                          BYTE(node + 0xd8), BYTE(node + 0xd9),
                                          10);
  }
  return 0;
}

/* 0x0043f440: replace a command target, update follower counts, recruit,
 * leave an attachment when necessary and issue profession-specific feedback.
 */
uint32_t bb_entity_command_c(void *object, void *other, uint32_t commanded,
                             uint32_t recruit, uint32_t eax) {
  uint32_t node = bb_address_encode(object), target = bb_address_encode(other);
  eax = low_byte(eax, BYTE(node + 0xf2));
  if ((uint8_t)eax == 0x73)
    return eax;
  if (target) {
    eax = BB_U32(target + 0x9c);
    if (eax & 0x1000)
      target = 0;
  }
  if (target == node)
    return eax;
  eax = low_byte(eax, BYTE(node + 0xfb));
  if ((uint8_t)eax && target)
    return eax;
  uintptr_t old = BB_U32(node + 0xc);
  if (old)
    --BYTE(old + 0x118);
  if (!target) {
    BB_U32(node + 0xc) = 0;
    BYTE(node + 0xe3) = 0;
    BYTE(node + 0xe4) = 0;
    eax = BB_U32(node + 0x9c) & 0xfffffffe;
    BB_U32(node + 0x9c) = eax;
    return eax;
  }
  eax = BB_U32(node + 0x9c);
  eax = commanded ? eax | 1 : eax & 0xfffffffe;
  BB_U32(node + 0x9c) = eax;
  eax = recruit;
  if (recruit)
    eax = bb_entity_recruit_c(object, (void *)(uintptr_t)target, eax);
  eax = low_byte(eax, BYTE(node + 0xf3));
  if (!(uint8_t)eax) {
    BB_U32(node + 0xc) = 0;
    eax = low_byte(eax, BYTE(node + 0xd4));
    if ((uint8_t)eax == 0x6e)
      return eax;
    uint8_t faction = BYTE(node + 0xf7);
    eax = low_byte(eax, BYTE(0x004843f0));
    if ((uint8_t)eax == faction) {
      eax = BYTE(node + 0x12d);
      if (eax & (1u << (faction & 31)))
        return eax;
    }
    return bb_entity_evade_c(object, (void *)(uintptr_t)target, eax);
  }
  uintptr_t attachment = BB_U32(node + 0x28);
  if (attachment && attachment != target) {
    uint32_t kind = BYTE(attachment + 0xd4) * 914;
    uint32_t y = BB_U32(0x006638b2 + kind), x = BB_U32(0x006638ae + kind);
    y = low_byte(y, y + BYTE(attachment + 0xd9));
    x = low_byte(x, x + BYTE(attachment + 0xd8));
    return bb_entity_move_escape(object, x, y);
  }
  BB_U32(node + 0xc) = target;
  bb_entity_command_position(object);
  ++BYTE(target + 0x118);
  uint32_t part = BYTE(node + 0xf4);
  eax = part - 23;
  if (part == 23 || part == 29) {
    int32_t choice = bb_random() % 2;
    if (choice < 0)
      return choice;
    return bb_entity_command_sound(object,
                                   BB_U32(choice ? 0x006ad24c : 0x006634d4));
  }
  if (part == 27) {
    eax = low_byte(eax, BYTE(node + 0x102));
    if (!(uint8_t)eax) {
      int32_t value = bb_random();
      int32_t choice = value % 3;
      eax = value / 3;
      if (choice >= 0) {
        uintptr_t slots[] = {0x006a8f18, 0x00642c38, 0x006afe10};
        uint32_t sound = BB_U32(slots[choice]);
        eax = bb_entity_feedback_c(object, (void *)(uintptr_t)sound, 0, 0, sound);
      }
      BYTE(node + 0x102) = 2;
    }
  } else if (part == 31) {
    eax = low_byte(eax, BYTE(target + 0x11a));
    if ((uint8_t)eax == 0xc1) {
      eax = low_byte(eax, BYTE(node + 0x11c));
      if (!(uint8_t)eax) {
        uint32_t sound = BB_U32(0x005e5dd0);
        return bb_entity_feedback_c(object, (void *)(uintptr_t)sound, 0, 0, sound);
      }
    } else {
      eax = low_byte(eax, BYTE(node + 0x102));
      if (!(uint8_t)eax) {
        uint32_t sound = BB_U32(0x005e4774);
        eax = bb_entity_feedback_c(object, (void *)(uintptr_t)sound, 0, 0, sound);
        BYTE(node + 0x102) = 2;
      }
    }
  }
  return eax;
}

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_blockers_add_entry(void) {
  __asm__ volatile("pushl %eax\n\tpushl $1\n\tpushl %ecx\n\tcall " BB_ASM(
      bb_entity_blockers_c) "\n\taddl $12, %esp\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_blockers_remove_entry(void) {
  __asm__ volatile("pushl %eax\n\tpushl $-1\n\tpushl %ecx\n\tcall " BB_ASM(
      bb_entity_blockers_c) "\n\taddl $12, %esp\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_move_start_entry(void) {
  __asm__ volatile(
      "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl %ecx\n\tcall " BB_ASM(
          bb_entity_move_start_c) "\n\taddl $12, %esp\n\tpopl %ecx\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_move_stop_entry(void) {
  __asm__ volatile("pushl %ecx\n\tcall " BB_ASM(
      bb_entity_move_stop_c) "\n\tpopl %ecx\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
#define ENTRY_ONE(name, function)                                              \
  __attribute__((naked)) void name(void) {                                     \
    __asm__ volatile(                                                          \
        "pushl %eax\n\tpushl 8(%esp)\n\tpushl %ecx\n\tcall " BB_ASM(           \
            function) "\n\taddl $12, %esp\n\tret $4");                         \
  }
#else
#define ENTRY_ONE(...)
#endif
#ifndef BB_MODEL_RUNTIME
#define ENTRY_THREE(name, function)                                            \
  __attribute__((naked)) void name(void) {                                     \
    __asm__ volatile("pushl %eax\n\tpushl 16(%esp)\n\tpushl 16(%esp)\n\t"      \
                     "pushl 16(%esp)\n\tpushl %ecx\n\tcall " BB_ASM(           \
                         function) "\n\taddl $20, %esp\n\tret $12");           \
  }
#else
#define ENTRY_THREE(...)
#endif
ENTRY_ONE(bb_entity_recruit_entry, bb_entity_recruit_c)
ENTRY_ONE(bb_entity_evade_entry, bb_entity_evade_c)
ENTRY_THREE(bb_entity_command_entry, bb_entity_command_c)
ENTRY_THREE(bb_entity_walk_entry, bb_entity_walk_c)
ENTRY_THREE(bb_entity_feedback_entry, bb_entity_feedback_c)
