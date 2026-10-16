/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)

static uint32_t tile(uint32_t x, uint32_t y) { return (y * 192u + x) * 12u; }
static uint32_t queue(uint16_t index) { return 0x006046a0 + index * 4u; }
static int interior(int32_t x, int32_t y) {
  return x > 0 && y > 0 && x < 191 && y < 191;
}
static uint32_t magnitude(uint32_t value) {
  uint32_t mask = 0u - (value >> 31);
  return (value ^ mask) - mask;
}

/* 0x00417700/0x00417c90: complete construction route searches. Four-way
 * preview search visits N,W,S,E; reachability visits N,E,S,W,NW,NE,SE,SW.
 * Both admit an occupied destination and clear visited bits in queue order.
 */
static uint32_t route(uint8_t owner, uint8_t start_x, uint8_t start_y,
                      uint8_t goal_x, uint8_t goal_y, uint32_t preview,
                      int eight_way) {
  static const int8_t steps[2][8][2] = {
      {{0, -1}, {-1, 0}, {0, 1}, {1, 0}},
      {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
  BYTE(queue(0)) = start_x;
  BYTE(queue(0) + 1) = start_y;
  WORD(queue(0) + 2) = 0;
  BB_U32(0x00559b68 + tile(start_x, start_y)) |= 0x10;
  uint16_t count = 1, current = 0;
  uint8_t x, y;
  do {
    x = BYTE(queue(current));
    y = BYTE(queue(current) + 1);
    if (x == goal_x && y == goal_y)
      break;
    if (x && y && x < 191 && y < 191) {
      uint32_t centre = 0x00559b60 + tile(x, y);
      for (unsigned direction = 0; direction < (eight_way ? 8u : 4u);
           ++direction) {
        int32_t dx = steps[eight_way][direction][0];
        int32_t dy = steps[eight_way][direction][1];
        uint32_t next = centre + (uint32_t)(dy * 192 + dx) * 12u;
        uint32_t flags = BB_U32(next + 8);
        if (flags & 0x10)
          continue;
        if (BYTE(next + 6) && (x + dx != goal_x || y + dy != goal_y) &&
            !BYTE(next + 1))
          continue;
        if (!(BB_U32(0x006a9092 + BYTE(next) * 8u) & 4) && !BYTE(next + 1))
          continue;
        if (!eight_way) {
          if ((flags & 0x200000) && !(flags & 0x100400))
            continue;
          if ((BYTE(next + 3) & (1u << (owner & 31))) && !(flags & 0x20))
            continue;
        }
        if (count >= 60000)
          continue;
        BB_U32(next + 8) = flags | 0x10;
        uint32_t added = queue(count++);
        BYTE(added) = x + dx;
        BYTE(added + 1) = y + dy;
        WORD(added + 2) = current;
      }
    }
    ++current;
  } while (current < count);

  WORD(0x004847a8) = count;
  for (uint16_t i = 0; i < count; ++i) {
    WORD(0x004847a8) = count;
    BB_U32(0x00559b68 + tile(BYTE(queue(i)), BYTE(queue(i) + 1))) &= 0xffffffef;
  }
  if (eight_way)
    return x == goal_x && y == goal_y;
  uint8_t player = BYTE(0x004843f0);
  if (owner == player)
    BB_U32(0x00484404) = 0;
  if (x != goal_x || y != goal_y)
    return 0;
  if (owner == player && preview)
    while (current) {
      uint32_t entry = queue(current);
      uint8_t column = BYTE(entry), row = BYTE(entry + 1);
      BB_U32(0x00559b68 + tile(column, row)) |= 0x8000;
      bb_terrain_dirty(column, row);
      current = WORD(entry + 2);
      uint32_t index = BB_U32(0x00484404);
      BB_U32(0x00484404) = index + 1;
      BYTE(0x006024d0 + index) = column;
      BYTE(0x006032c0 + index) = row;
    }
  return 1;
}
uint32_t bb_placement_route(uint32_t owner, uint32_t x, uint32_t y,
                            uint32_t goal_x, uint32_t goal_y,
                            uint32_t preview) {
  return route(owner, x, y, goal_x, goal_y, preview, 0);
}
uint32_t bb_placement_reachable(uint32_t owner, uint32_t x, uint32_t y,
                                uint32_t goal_x, uint32_t goal_y) {
  (void)owner;
  return route(0, x, y, goal_x, goal_y, 0, 1);
}

/* 0x00418300: clear the live preview list, dirty each tile after clearing.
 * A nonpositive initial count is returned; otherwise return the final dirty
 * result. Callback changes to the count affect the next iteration.
 */
uint32_t bb_placement_preview_clear(void) {
  uint32_t eax = BB_U32(0x00484404);
  for (int32_t i = 0; i < BB_I32(0x00484404); ++i) {
    uint8_t y = BYTE(0x006032c0 + (uint32_t)i);
    uint8_t x = BYTE(0x006024d0 + (uint32_t)i);
    BB_U32(0x00559b68 + tile(x, y)) &= 0xffff7fff;
    eax = bb_terrain_dirty(x, y);
  }
  BB_U32(0x00484404) = 0;
  return eax;
}

/* 0x00418470: nearest usable road by signed Manhattan distance from the
 * primary entrance. Exclude this footprint and blocked roads, temporarily
 * open both entrances and preserve the original unconditional flag cleanup.
 */
uint32_t bb_placement_connect(uint32_t owner, uint32_t requested_kind,
                              uint32_t requested_x, uint32_t requested_y) {
  uint32_t definition = (uint8_t)requested_kind * 914u;
  if (!BB_U32(0x006638ba + definition))
    return 0;
  uint32_t x = (uint8_t)requested_x, y = (uint8_t)requested_y;
  int32_t left = x < 11 ? 1 : x - 10, top = y < 11 ? 1 : y - 10;
  int32_t right = x + 10 > 191 ? 191 : x + 10;
  int32_t bottom = y + 10 > 191 ? 191 : y + 10;
  int32_t best = 32;
  uint32_t best_x = 0, best_y = 0;
  for (int32_t row = top; row <= bottom; ++row)
    for (int32_t col = left; col <= right; ++col) {
      uint32_t cell = 0x00559b60 + tile(col, row);
      uint32_t flags = BB_U32(cell + 8);
      int blocked = (flags & 0x200000) && !(flags & 0x400);
      int32_t ix = (uint32_t)col - x + 5, iy = (uint32_t)row - y + 5;
      if (ix >= 0 && ix < 11 && iy >= 0 && iy < 11 &&
          BB_U32(0x00663688 + definition + (iy * 11 + ix) * 4u))
        blocked = 1;
      if (BYTE(cell + 1) && !blocked) {
        uint32_t dx = (uint32_t)col - BB_U32(0x0066389e + definition) - x;
        uint32_t dy = (uint32_t)row - BB_U32(0x006638a2 + definition) - y;
        int32_t distance = magnitude(dx) + magnitude(dy);
        if (distance < best) {
          best = distance;
          best_x = col;
          best_y = row;
        }
      }
    }
  if (best >= 32)
    return 0;
  uint32_t second_x = BB_U32(0x006638a6 + definition) + x;
  uint32_t second_y = BB_U32(0x006638aa + definition) + y;
  uint32_t first_x = BB_U32(0x0066389e + definition) + x;
  uint32_t first_y = BB_U32(0x006638a2 + definition) + y;
  uint32_t second = 0x00559b68 + tile(second_x, second_y);
  uint32_t first = 0x00559b68 + tile(first_x, first_y);
  BB_U32(second) |= 0x20;
  uint32_t flags = BB_U32(first) | 0x20;
  BB_U32(first) = flags;
  uint32_t owner_argument = (flags & 0xffffff00u) | (uint8_t)owner;
  uint32_t result =
      bb_placement_route(owner_argument, first_x, first_y, best_x, best_y, 1);
  BB_U32(second) &= 0xffffffdf;
  BB_U32(first) &= 0xffffffdf;
  return result;
}

static void highlight(uint32_t definition, uint8_t x, uint8_t y) {
  for (int32_t row = 0; row < 11; ++row)
    for (int32_t col = 0; col < 11; ++col)
      if (BB_U32(0x00663688 + definition + (row * 11 + col) * 4u) &&
          interior(x - 5 + col, y - 5 + row))
        BB_U32(0x00559b68 + tile(x - 5 + col, y - 5 + row)) |= 0x100;
}

/* 0x004033f0: complete placement probe. Out-of-bounds footprint cells are
 * skipped, decoration 0xcc is permitted, hostile buildings veto within the
 * strict 14-tile square, and each road mode retains its own bounds/tie rules.
 */
uint32_t bb_placement_probe(uint32_t requested_owner, uint32_t requested_kind,
                            uint32_t requested_x, uint32_t requested_y) {
  uint8_t owner = requested_owner, x = requested_x, y = requested_y;
  uint32_t definition = (uint8_t)requested_kind * 914u;
  uint32_t valid = 1;
  BB_U32(0x0048472c) = 0;
  for (int32_t row = 0; row < 11; ++row)
    for (int32_t col = 0; col < 11; ++col) {
      if (!BB_U32(0x00663688 + definition + (row * 11 + col) * 4u))
        continue;
      int32_t tx = x - 5 + col, ty = y - 5 + row;
      if (!interior(tx, ty))
        continue;
      uint32_t cell = 0x00559b60 + tile(tx, ty);
      BYTE(cell + 3) |= (uint8_t)(1u << (owner & 31));
      if (BYTE(0x004843f0) == owner) {
        BB_U32(cell + 8) &= 0xfffffeff;
        bb_terrain_dirty(tx, ty);
      }
      uint32_t property = BB_U32(0x006a9092 + BYTE(cell) * 8u);
      uint32_t flags = BB_U32(cell + 8);
      int eligible = !(property & 8) && (property & 0x20800) &&
                     !(flags & 0x200000) && !BYTE(cell + 1) &&
                     ((flags & 0x1800) || BYTE(0x004843f0) != owner);
      if (eligible) {
        for (void *entity = bb_entity_at((void *)0x00553f90, tx, ty); entity;
             entity = bb_entity_at_next((void *)0x00553f90, entity, tx, ty))
          if (BYTE((uintptr_t)entity + 0xd4) != 0xcc) {
            valid = 0;
            if (BYTE(0x004843f0) == owner)
              BB_U32(cell + 8) |= 0x100;
            break;
          }
        if (BB_U32(0x006a9092 + BYTE(cell) * 8u) & 4)
          continue;
      }
      valid = 0;
      if (BYTE(0x004843f0) == owner)
        BB_U32(cell + 8) |= 0x100;
    }
  if (!valid)
    BB_U32(0x0048472c) = 1;
  else
    for (uint32_t at = BB_U32(0x00554f98); at && valid; at = BB_U32(at + 4))
      if (BB_U32(0x00663578 + BYTE(at + 0xd4) * 914u) &&
          BYTE(at + 0xf7) != owner) {
        int32_t dx = (int32_t)BYTE(at + 0xd8) - x;
        int32_t dy = (int32_t)BYTE(at + 0xd9) - y;
        if (dx > -14 && dx < 14 && dy > -14 && dy < 14) {
          valid = 0;
          BB_U32(0x0048472c) = 3;
        }
      }
  if (BB_U32(0x006638ba + definition)) {
    if (valid && !bb_placement_connect(owner, requested_kind, requested_x,
                                       requested_y)) {
      valid = 0;
      BB_U32(0x0048472c) = 4;
    }
  } else if (valid) {
    int32_t best = 32;
    uint32_t best_x = 0, best_y = 0;
    for (int32_t row = -10; row <= 10; ++row)
      for (int32_t col = -10; col <= 10; ++col) {
        int32_t tx = x + col, ty = y + row;
        if (tx <= 1 || ty <= 1 || tx >= 190 || ty >= 190)
          continue;
        uint32_t cell = 0x00559b60 + tile(tx, ty);
        uint32_t flags = BB_U32(cell + 8);
        if (!BYTE(cell + 1) || ((flags & 0x200000) && !(flags & 0x400)))
          continue;
        int32_t distance = magnitude(col) + magnitude(row);
        if (distance < best) {
          best = distance;
          best_x = tx;
          best_y = ty;
        }
      }
    valid = best_x && best_y
                ? bb_placement_reachable(owner, requested_x, requested_y,
                                         best_x, best_y)
                : 0;
    if (!valid) {
      if (BYTE(0x004843f0) == owner)
        highlight(definition, x, y);
      BB_U32(0x0048472c) = 1;
    }
  }
  if (!valid && (BB_U32(0x0048472c) == 4 || BB_U32(0x0048472c) == 3) &&
      BYTE(0x004843f0) == owner)
    highlight(definition, x, y);
  return valid;
}

/* 0x00403950: remove this owner's footprint preview bits, dirtying only
 * the current player's cells, then clear that player's live road preview.
 */
uint32_t bb_placement_clear(uint32_t requested_owner, uint32_t requested_kind,
                            uint32_t requested_x, uint32_t requested_y) {
  uint8_t owner = requested_owner, x = requested_x, y = requested_y;
  uint32_t definition = (uint8_t)requested_kind * 914u;
  for (int32_t row = 0; row < 11; ++row)
    for (int32_t col = 0; col < 11; ++col)
      if (BB_U32(0x00663688 + definition + (row * 11 + col) * 4u) &&
          interior(x - 5 + col, y - 5 + row)) {
        int32_t tx = x - 5 + col, ty = y - 5 + row;
        BYTE(0x00559b63 + tile(tx, ty)) &= ~(uint8_t)(1u << (owner & 31));
        if (BYTE(0x004843f0) == owner)
          bb_terrain_dirty(tx, ty);
      }
  return BYTE(0x004843f0) == owner ? bb_placement_preview_clear() : owner;
}
