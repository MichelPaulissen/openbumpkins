/* SPDX-License-Identifier: GPL-3.0-only */
#include "road_topology.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

static uintptr_t tile(uint32_t x, uint32_t y) {
  return 0x559b60u + (y * 192u + x) * 12u;
}
static uintptr_t queued(uint16_t i) { return 0x6046a0u + i * 4u; }

/* 00417060: queries always consume byte coordinates, including wrapped edges.
 */
uint32_t bb_neighbour_kind_entry(uint32_t kind, uint32_t x, uint32_t y) {
  x = (uint8_t)x;
  y = (uint8_t)y;
  for (void *p = bb_entity_at((void *)(uintptr_t)0x553f90, x, y); p;
       p = bb_entity_at_next((void *)(uintptr_t)0x553f90, p, x, y))
    if (BYTE((uintptr_t)p + 0xd4) == (uint8_t)kind)
      return 1;
  return 0;
}

/* 00416960: both paired queries run even when the first already matches. */
void bb_junction_refresh_entry(uint32_t kind, uint32_t x, uint32_t y) {
  uint8_t subtype = 0, k = kind;
  uint32_t partner = 0;
  uintptr_t bank = 0;
  x = (uint8_t)x;
  y = (uint8_t)y;
  switch (k) {
  case 0x1d:
    partner = 0x52;
    bank = 0x484530;
    break;
  case 0x52:
    partner = 0x1d;
    bank = 0x484540;
    break;
  case 0x81:
    partner = 0xa9;
    bank = 0x484560;
    break;
  case 0xa9:
    partner = 0x81;
    break;
  case 0xdb:
    partner = 0xdc;
    bank = 0x484560;
    break;
  case 0x4a: {
    unsigned mask = 8 * bb_neighbour_kind_entry(k, x - 1, y);
    mask |= 4 * bb_neighbour_kind_entry(k, x, y - 1);
    mask |= 2 * bb_neighbour_kind_entry(k, x, y + 1);
    mask |= bb_neighbour_kind_entry(k, x + 1, y);
    subtype = BYTE(0x484550 + mask);
    break;
  }
  default:
    break;
  }
  if (partner) {
    static const int8_t dx[] = {0, -1, 1, 0};
    static const int8_t dy[] = {-1, 0, 0, 1};
    unsigned mask = 0;
    for (unsigned i = 0; i < 4; ++i) {
      uint32_t nx = x + (uint32_t)dx[i], ny = y + (uint32_t)dy[i];
      uint32_t first = bb_neighbour_kind_entry(k, nx, ny);
      uint32_t second = bb_neighbour_kind_entry(partner, nx, ny);
      mask |= (first | second) << (3 - i);
    }
    subtype = bank ? BYTE(bank + mask) : (mask & 9) != 9;
  }
  for (void *p = bb_entity_at((void *)(uintptr_t)0x553f90, x, y); p;
       p = bb_entity_at_next((void *)(uintptr_t)0x553f90, p, x, y)) {
    uint32_t e = (uintptr_t)p;
    if (BYTE(e + 0xd4) == k && BYTE(e + 0xf9) != subtype) {
      BYTE(e + 0xf9) = subtype;
      bb_entity_dirty(p);
    }
  }
}

/* 00418360: center, left, right, up, down; the child consumes byte coordinates.
 */
void bb_road_refresh_cardinal_entry(uint32_t x, uint32_t y) {
  x = (uint8_t)x;
  y = (uint8_t)y;
  bb_road_shape_entry(x, y);
  bb_road_shape_entry((uint8_t)(x - 1), y);
  bb_road_shape_entry((uint8_t)(x + 1), y);
  bb_road_shape_entry(x, (uint8_t)(y - 1));
  bb_road_shape_entry(x, (uint8_t)(y + 1));
}

/* 004176a0 / 00417660: publish the road before dirtying and neighbour updates.
 */
void bb_road_add_entry(uint32_t x, uint32_t y) {
  x = (uint8_t)x;
  y = (uint8_t)y;
  uint32_t t = tile(x, y);
  if (!BYTE(t + 1)) {
    BYTE(t + 1) = 1;
    bb_terrain_dirty(x, y);
    bb_road_refresh_cardinal_entry(x, y);
  }
}
void bb_road_ensure_entry(uint32_t ignored, uint32_t x, uint32_t y) {
  (void)ignored;
  if (!BYTE(tile((uint8_t)x, (uint8_t)y) + 1))
    bb_road_add_entry(x, y);
}

/* 004170c0: breadth-first discovery and reverse predecessor-chain updates. */
void bb_road_route_entry(uint32_t owner, uint32_t start_x, uint32_t start_y,
                         uint32_t end_x, uint32_t end_y) {
  uint8_t sx = start_x, sy = start_y, ex = end_x, ey = end_y, x, y;
  uint16_t head = 0, tail = 1;
  BYTE(queued(0)) = sx;
  BYTE(queued(0) + 1) = sy;
  WORD(queued(0) + 2) = 0;
  BB_U32(tile(sx, sy) + 8) |= 0x10;
  do {
    uintptr_t q = queued(head);
    x = BYTE(q);
    y = BYTE(q + 1);
    if (x == ex && y == ey)
      break;
    if (x > 1 && y > 1 && x < 190 && y < 190) {
      static const int8_t dx[] = {0, -1, 0, 1};
      static const int8_t dy[] = {-1, 0, 1, 0};
      for (unsigned i = 0; i < 4; ++i) {
        uint8_t nx = x + dx[i], ny = y + dy[i];
        uint32_t t = tile(nx, ny);
        uint32_t flags = BB_U32(t + 8);
        if (!(flags & 0x10) &&
            (!BYTE(t + 6) || (nx == ex && ny == ey) || BYTE(t + 1)) &&
            ((BB_U32(0x6a9092 + BYTE(t) * 8u) & 4) || BYTE(t + 1)) &&
            (!(flags & 0x200000) || (flags & 0x100400)) &&
            (!(BYTE(t + 3) & (1u << (owner & 31))) || (flags & 0x20)) &&
            tail < 60000) {
          BB_U32(t + 8) = flags | 0x10;
          uint32_t next = queued(tail++);
          BYTE(next) = nx;
          BYTE(next + 1) = ny;
          WORD(next + 2) = head;
        }
      }
    }
    ++head;
  } while (head < tail);
  if (x == ex && y == ey) {
    for (uint16_t i = head; i; i = WORD(queued(i) + 2)) {
      uintptr_t q = queued(i);
      uint32_t t = tile(BYTE(q), BYTE(q + 1));
      if (!BYTE(t + 1))
        BYTE(t + 1) = 1;
    }
    WORD(0x4847a8) = tail;
    BYTE(tile(sx, sy) + 1) = 1;
    while (head) {
      uintptr_t q = queued(head);
      bb_road_refresh_cardinal_entry(BYTE(q), BYTE(q + 1));
      head = WORD(q + 2);
      tail = WORD(0x4847a8);
    }
  }
  WORD(0x4847a8) = tail;
  for (uint16_t i = 0; i < tail; ++i) {
    WORD(0x4847a8) = tail;
    uintptr_t q = queued(i);
    BB_U32(tile(BYTE(q), BYTE(q + 1)) + 8) &= ~0x10u;
  }
}

static uint32_t abs_wrapped(uint32_t value) {
  uint32_t sign = (int32_t)value < 0 ? UINT32_MAX : 0;
  return (value ^ sign) - sign;
}

/* 004529b0: the legacy no-candidate branch consumes an unwritten stack word.
 * Keep that input explicit in the C implementation until the binary bridge
 * is removed. The second seed matters only for empty search rows. */
uint64_t bb_building_road_connect_c(void *object, uint32_t seed_y,
                                    uint32_t seed_row) {
  uint32_t e = bb_address_encode(object);
  uint32_t offset = BYTE(e + 0xd4) * 914u;
  if (!BB_U32(0x6638ba + offset))
    return ((uint64_t)seed_row << 32) | seed_y;
  uint32_t ax = BYTE(e + 0xd8), ay = BYTE(e + 0xd9);
  int32_t min_x = (int32_t)ax - 10, min_y = (int32_t)ay - 10;
  int32_t max_x = (int32_t)ax + 10, max_y = (int32_t)ay + 10;
  if (min_x < 1)
    min_x = 1;
  if (min_y < 1)
    min_y = 1;
  if (max_x > 191)
    max_x = 191;
  if (max_y > 191)
    max_y = 191;
  int32_t best = 32;
  uint32_t closest_x = 0, closest_y = seed_y, row = seed_row;
  if (min_y <= max_y) {
    row = tile((uint32_t)min_x, (uint32_t)min_y) + 8;
    for (int32_t y = min_y; y <= max_y; ++y, row += 0x900) {
      for (int32_t x = min_x; x <= max_x; ++x) {
        uint32_t t = row + ((uint32_t)x - (uint32_t)min_x) * 12;
        uint32_t flags = BB_U32(t);
        int blocked = (flags & 0x200000) && !(flags & 0x400);
        uint32_t fx = (uint32_t)x - ax + 5, fy = (uint32_t)y - ay + 5;
        if (fx < 11 && fy < 11 &&
            BB_U32(0x663688 + offset + (fy * 11 + fx) * 4))
          blocked = 1;
        if (BYTE(t - 7) && !blocked) {
          int32_t distance =
              (int32_t)(abs_wrapped((uint32_t)x - BB_U32(0x66389e + offset) -
                                    ax) +
                        abs_wrapped((uint32_t)y - BB_U32(0x6638a2 + offset) -
                                    ay));
          if (distance < best) {
            best = distance;
            closest_x = x;
            closest_y = y;
          }
        }
      }
    }
  }
  uint32_t refresh_x = closest_y;
  if (best < 32) {
    refresh_x = ax + BB_U32(0x6638a6 + offset);
    row = ay + BB_U32(0x6638aa + offset);
    uint32_t entrance_x = ax + BB_U32(0x66389e + offset);
    uint32_t entrance_y = ay + BB_U32(0x6638a2 + offset);
    uintptr_t back = tile(refresh_x, row) + 8;
    uintptr_t front = tile(entrance_x, entrance_y) + 8;
    BB_U32(back) |= 0x20;
    BB_U32(front) |= 0x20;
    bb_road_route_entry(BYTE(e + 0xf7), entrance_x, entrance_y, closest_x,
                        closest_y);
    BB_U32(back) &= ~0x20u;
    BB_U32(front) &= ~0x20u;
  }
  bb_road_refresh_cardinal_entry(refresh_x, row);
  /* Later legacy calls can consume these same two residual stack words. */
  return ((uint64_t)row << 32) | closest_y;
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((thiscall, naked))
bb_building_road_connect_entry(void *object __attribute__((unused))) {
  __asm__ volatile(
      "movl -8(%esp),%eax;movl -44(%esp),%edx;"
      "pushl %edx;pushl %eax;pushl %ecx;call " BB_ASM(
          bb_building_road_connect_c) ";addl $12,%esp;movl %eax,-8(%esp);"
                                      "movl %edx,-44(%esp);ret");
}
#endif
