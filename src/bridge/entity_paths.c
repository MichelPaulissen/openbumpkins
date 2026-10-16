/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_release_fn(id,a0) (void)BB_GAME_CALL(id,0,EA_WORD(a0))
#define EA_allocate_fn(id,a0) (void *)(uintptr_t)BB_GAME_CALL(id,0,EA_WORD(a0))
#else
#define EA_release_fn(id,...) CALL(id,release_fn)(__VA_ARGS__)
#define EA_allocate_fn(id,...) CALL(id,allocate_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef void *(*allocate_fn)(uint32_t);
typedef void (*release_fn)(void *);
typedef uint32_t(BB_THISCALL *
                 terrain_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);

static uint32_t kind_offset(uint32_t node) { return BYTE(node + 0xd4) * 914; }
static uint32_t tile(uint32_t x, uint32_t y) { return (y * 192 + x) * 12; }
static uintptr_t queue_entry(uint16_t index) {
  return 0x006046a0 + (uint32_t)index * 4;
}
static uint32_t special_route(uint32_t node) {
  return BB_U32(0x005e5ef6 + BYTE(node + 0xf4) * 292);
}

/* 0x00451c00..0x004529ac: breadth-first route search. The shared queue stores
 * byte coordinates and word parent indices. Queue limits use the low word of
 * squared Q16 distance, not its full value. Ordinary and special traversal
 * mark the occupancy byte by one and 64 respectively, then undo those marks.
 */
uint32_t BB_THISCALL
bb_entity_route(void *object, uint32_t requested_x, uint32_t requested_y) {
  uint32_t node = bb_address_encode(object);
  uint8_t origin_x = BYTE(node + 0xd8), origin_y = BYTE(node + 0xd9);
  uint8_t goal_x = requested_x, goal_y = requested_y;
  uint32_t distance = bb_entity_distance(origin_x, origin_y, goal_x, goal_y);
  uint32_t whole = (int32_t)distance >> 16;
  uint16_t limit = whole * whole * 9;
  if (limit < 0x400)
    limit = 0x400;
  if (limit > 0x8000)
    limit = 0x8000;
  if (BYTE(node + 0xf4) == 0x1a)
    limit = 60000;
  BYTE(node + 0xf0) = 0;
  BYTE(node + 0xf1) = 0;
  WORD(0x006046a2) = 0;
  BYTE(0x006046a0) = origin_x;
  BYTE(0x006046a1) = origin_y;
  WORD(0x004847a8) = 1;
  uint32_t special = special_route(node);
  uint8_t mark = special ? 64 : 1;
  uintptr_t origin_mark = 0x00559b66 + tile(origin_x, origin_y);
  BYTE(origin_mark) += mark;
  uint16_t current = 0;
  uint8_t x, y;
  static const int8_t steps[8][2] = {{0, -1},  {1, 0},  {0, 1}, {-1, 0},
                                     {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  do {
    uintptr_t entry = queue_entry(current);
    x = BYTE(entry);
    y = BYTE(entry + 1);
    if (x == goal_x && y == goal_y)
      break;
    if (x && y && x < 191 && y < 191) {
      uintptr_t center = 0x00559b60 + tile(x, y);
      for (unsigned direction = 0; direction < 8; ++direction) {
        if (direction == 4 && !special && !(BYTE(node + 0xd5) & 4))
          break;
        int32_t dx = steps[direction][0], dy = steps[direction][1];
        uint32_t next = center + (uint32_t)(dy * 192 + dx) * 12;
        if (special) {
          if (BYTE(next + 6) >= 64)
            continue;
        } else {
          if (BYTE(next + 6))
            continue;
          if (!(BB_U32(0x006a9092 + BYTE(next) * 8) & BYTE(node + 0xd5)) &&
              !BYTE(next + 1))
            continue;
        }
        if (WORD(0x004847a8) >= limit)
          continue;
        uint16_t count = WORD(0x004847a8);
        ++WORD(0x004847a8);
        uintptr_t added = queue_entry(count);
        BYTE(added) = x + dx;
        BYTE(added + 1) = y + dy;
        WORD(added + 2) = current;
        BYTE(next + 6) += mark;
      }
    }
    ++current;
  } while (current < WORD(0x004847a8));

  uint32_t result = x == goal_x && y == goal_y;
  if (result) {
    BYTE(node + 0xf0) = 0;
    BYTE(node + 0xf1) = 0;
    while (current) {
      uintptr_t entry = queue_entry(current);
      uint32_t parent = queue_entry(WORD(entry + 2));
      int32_t dx = (int32_t)BYTE(entry) - BYTE(parent);
      int32_t dy = (int32_t)BYTE(entry + 1) - BYTE(parent + 1);
      uint8_t direction;
      if (dx > 0)
        direction = dy > 0 ? 0xe0 : dy < 0 ? 0xa0 : 0xc0;
      else if (dx < 0)
        direction = dy > 0 ? 0x20 : dy < 0 ? 0x60 : 0x40;
      else
        direction = dy > 0 ? 0 : 0x80;
      /* The original compares a zero-extended BYTE with 1024. It never
       * rejects a step: path length wraps at 256 and overwrites scratch.
       */
      BYTE(0x006ba538 + BYTE(node + 0xf0)) = direction;
      ++BYTE(node + 0xf0);
      current = WORD(entry + 2);
    }
    for (uint16_t i = 0; i < (BYTE(node + 0xf0) >> 1); ++i) {
      uint8_t value = BYTE(0x006ba538 + i);
      BYTE(0x006ba538 + i) = BYTE(0x006ba537 + BYTE(node + 0xf0) - i);
      BYTE(0x006ba537 + BYTE(node + 0xf0) - i) = value;
    }
    void *old = BB_POINTER(node + 0xec);
    if (old) {
      EA_release_fn(0x0046c110,old);
      BB_U32(node + 0xec) = 0;
    }
    if (BYTE(node + 0xf0)) {
      void *path = EA_allocate_fn(0x0046c060,BYTE(node + 0xf0));
      BB_SET_POINTER(node + 0xec, path);
      for (uint16_t i = 0; i < BYTE(node + 0xf0); ++i)
        BYTE(BB_U32(node + 0xec) + i) = BYTE(0x006ba538 + i);
    }
    BYTE(node + 0xd6) = origin_x;
    BYTE(node + 0xd7) = origin_y;
    uintptr_t path = BB_U32(node + 0xec);
    if (path) {
      uint32_t offset = (BYTE(path) >> 1) & 0xfc;
      BYTE(node + 0xd6) = BB_U32(0x00484580 + offset) + origin_x;
      offset = (BYTE(path) >> 1) & 0xfc;
      BYTE(node + 0xd7) = BB_U32(0x00484600 + offset) + origin_y;
    }
  }
  /* Allocation/delete callbacks may change the live profession. The original
   * chooses its cleanup decrement again here, even if the traversal differed.
   */
  mark = special_route(node) ? 64 : 1;
  BYTE(origin_mark) -= mark;
  for (uint16_t i = 1; i < WORD(0x004847a8); ++i) {
    uintptr_t entry = queue_entry(i);
    BYTE(0x00559b66 + tile(BYTE(entry), BYTE(entry + 1))) -= mark;
  }
  if (!result || !BB_U32(node + 0xec))
    BYTE(node + 0xf0) = 0;
  return result;
}

static uint32_t footprint_tile(uint32_t node, uint32_t x, uint32_t y) {
  return tile(BYTE(node + 0xd8) + x, BYTE(node + 0xd9) + y);
}
static void clear_outer(uint32_t node) {
  for (uint32_t y = 0; y < 11; ++y)
    for (uint32_t x = 0; x < 11; ++x)
      if (BB_U32(0x00663688 + kind_offset(node) + (y * 11 + x) * 4)) {
        /* Keep four separate live writes before the terrain callback. */
        BB_U32(0x00556e2c + footprint_tile(node, x, y)) &= 0xfffffffd;
        BB_U32(0x00556e2c + footprint_tile(node, x, y)) &= 0xffefffff;
        BB_U32(0x00556e2c + footprint_tile(node, x, y)) &= 0xffdfffff;
        BB_U32(0x00556e2c + footprint_tile(node, x, y)) &= 0xfffffbff;
        bb_terrain_set_entry((void *)(uintptr_t)0x005e470c,
                             (uint8_t)(BYTE(node + 0xd8) + x - 5),
                             (uint8_t)(BYTE(node + 0xd9) + y - 5), 0, 1);
      }
}

/* 0x00448890..0x00448d0c: unregister terrain occupancy and building flags.
 * This does not free the path buffer; successful route search owns that step.
 * Definitions, kind and coordinates remain live across terrain callbacks.
 */
uint32_t BB_THISCALL bb_entity_release_tiles(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t associated = BYTE(0x006635bc + kind_offset(node));
  if (associated) {
    uint32_t eax;
    if (((BB_U32(node + 0x9c) & 0x1000) && SHORT(node + 0xa0) < 0) ||
        !BB_U32(0x00663578 + associated * 914)) {
      clear_outer(node);
      eax = 11;
    } else
      eax = associated * 457;
    eax = (eax & 0xffffff00) | BYTE(node + 0xf2);
    if (!(uint8_t)eax)
      return eax;
  }
  uint32_t offset = kind_offset(node);
  if (BB_U32(0x00663586 + offset) ||
      (BYTE(0x006635bc + offset) && BYTE(node + 0xf2)))
    for (uint32_t y = 0; y < 7; ++y)
      for (uint32_t x = 0; x < 7; ++x)
        if (BB_U32(0x006635c4 + kind_offset(node) + (y * 7 + x) * 4)) {
          uintptr_t count = 0x00558042 + footprint_tile(node, x, y);
          if (BYTE(count))
            --BYTE(count);
        }
  if (BB_U32(0x0066358a + kind_offset(node)))
    BB_U32(0x00559b68 + tile(BYTE(node + 0xd8), BYTE(node + 0xd9))) &=
        0xfffffffb;
  switch (BYTE(node + 0xd4)) {
  case 0x31:
  case 0x39:
  case 0x4e:
  case 0x6f:
    BB_U32(0x00559b68 + tile(BYTE(node + 0xd8), BYTE(node + 0xd9))) &=
        0xffdfffff;
  }
  uint32_t eax = BYTE(node + 0xd4) * 457;
  if (BB_U32(0x00663578 + kind_offset(node))) {
    clear_outer(node);
    return 11;
  }
  return eax;
}
