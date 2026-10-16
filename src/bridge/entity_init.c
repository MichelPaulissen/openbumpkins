/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"
#include "world_controls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_plain_one_fn(id,a0) BB_GAME_CALL(id,0,EA_WORD(a0))
#define EA_object_fn(id,a0) BB_GAME_CALL(id,EA_WORD(a0))
#define EA_three_fn(id,a0,a1,a2) BB_GAME_CALL(id,0,EA_WORD(a0),EA_WORD(a1),EA_WORD(a2))
#else
#define EA_plain_one_fn(id,...) CALL(id,plain_one_fn)(__VA_ARGS__)
#define EA_object_fn(id,...) CALL(id,object_fn)(__VA_ARGS__)
#define EA_three_fn(id,...) CALL(id,three_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef uint32_t (*plain_one_fn)(uint32_t);
typedef uint32_t (*plain_fn)(void);
typedef uint32_t (*three_fn)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * object_fn)(void *);
typedef uint32_t(BB_THISCALL * one_fn)(void *, uint32_t);
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef uint32_t(BB_THISCALL * three_this_fn)(void *, uint32_t,
                                                            uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *
                 four_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);

enum { MOTION = 1, RESOURCE = 2, BUILDING = 4, PARTS = 8 };
struct type_defaults {
  uintptr_t resource;
  uint32_t motion;
  unsigned flags;
};
/* The original kind argument selects this table, even when callbacks change
 * the entity's live kind. Resource slots remain live game data. Flags specify
 * which fields are actually written: absent defaults preserve callback writes
 * and preexisting sprite fields until the final sprite-selection step.
 */
static const struct type_defaults types[256] = {
    [0x00] = {0, 0x20000, 1},
    [0x01] = {0x00602318, 0x20000, 7},
    [0x02] = {0x005f51c0, 0x10000, 7},
    [0x05] = {0x005f90d0, 0x20000, 3},
    [0x06] = {0x006ad264, 0x20000, 3},
    [0x07] = {0x006ad248, 0x10000, 3},
    [0x09] = {0x006ad1ec, 0x20000, 3},
    [0x0a] = {0x0060232c, 0x10000, 3},
    [0x10] = {0x006ad1ec, 0x10000, 3},
    [0x18] = {0x005f9194, 0x10000, 3},
    [0x19] = {0x006ad1ec, 0x10000, 3},
    [0x1a] = {0x006022f8, 0x10000, 3},
    [0x1b] = {0, 0x10000, 1},
    [0x1c] = {0x006ad1ec, 0x10000, 3},
    [0x1d] = {0x0055911c, 0x10000, 3},
    [0x21] = {0x00553e84, 0x10000, 3},
    [0x22] = {0x005d1024, 0, 3},
    [0x24] = {0x006034e0, 0x10000, 3},
    [0x25] = {0, 0x10000, 1},
    [0x26] = {0x006a904c, 0, 3},
    [0x27] = {0x005e4798, 0x20000, 3},
    [0x28] = {0x005f6e18, 0, 3},
    [0x29] = {0x006634d8, 0, 3},
    [0x2a] = {0x006634d0, 0, 3},
    [0x2b] = {0x006634d0, 0, 3},
    [0x2c] = {0x006afb30, 0, 3},
    [0x2e] = {0, 0x20000, 1},
    [0x2f] = {0, 0x10000, 5},
    [0x30] = {0x005f74b0, 0x10000, 3},
    [0x31] = {0x00604610, 0x10000, 3},
    [0x32] = {0, 0x10000, 1},
    [0x33] = {0x006a9044, 0x10000, 7},
    [0x35] = {0x006afcf8, 0x10000, 7},
    [0x36] = {0x005c77b4, 0x10000, 7},
    [0x37] = {0x00663524, 0, 3},
    [0x38] = {0x006ad248, 0, 3},
    [0x39] = {0x005c9510, 0, 3},
    [0x3a] = {0, 0x10000, 1},
    [0x3c] = {0x006afb2c, 0x10000, 3},
    [0x3d] = {0x005eac9c, 0, 3},
    [0x3e] = {0x00513434, 0, 3},
    [0x3f] = {0x006ad1ec, 0, 3},
    [0x40] = {0x006ad1ec, 0, 3},
    [0x42] = {0x005d19d8, 0x10000, 7},
    [0x43] = {0x00554fb0, 0x10000, 3},
    [0x44] = {0x00600164, 0, 3},
    [0x45] = {0x00640ce4, 0x10000, 7},
    [0x46] = {0x00604648, 0x10000, 3},
    [0x47] = {0x00663504, 0x10000, 3},
    [0x48] = {0x006a98d0, 0x10000, 3},
    [0x49] = {0x005db04c, 0x10000, 7},
    [0x4a] = {0x005d1554, 0x10000, 3},
    [0x4b] = {0x005f7358, 0x40000, 3},
    [0x4c] = {0x005fe3f8, 0x10000, 3},
    [0x4d] = {0x00602310, 0, 3},
    [0x4e] = {0x005c78dc, 0x10000, 3},
    [0x4f] = {0x006ad1fc, 0x20000, 3},
    [0x50] = {0x0063f05c, 0x10000, 3},
    [0x51] = {0x005e4744, 0x80000, 3},
    [0x52] = {0x005cf3e8, 0x10000, 3},
    [0x53] = {0x0063f034, 0x10000, 3},
    [0x54] = {0x005c9508, 0, 3},
    [0x55] = {0x005f918c, 0, 3},
    [0x56] = {0x0063f098, 0x40000, 7},
    [0x57] = {0, 0x20000, 1},
    [0x58] = {0, 0x20000, 1},
    [0x59] = {0, 0x20000, 1},
    [0x5a] = {0, 0x20000, 1},
    [0x5b] = {0x00600150, 0x10000, 3},
    [0x5c] = {0x0063f0c0, 0x10000, 3},
    [0x5d] = {0x005f9164, 0x10000, 3},
    [0x5e] = {0x00600170, 0x10000, 3},
    [0x5f] = {0x005f9250, 0x10000, 3},
    [0x60] = {0x00662c94, 0x10000, 3},
    [0x61] = {0x005fe3e8, 0x10000, 3},
    [0x62] = {0x005ecf2c, 0x10000, 3},
    [0x63] = {0, 0x20000, 1},
    [0x64] = {0x005e42c8, 0x10000, 3},
    [0x66] = {0x005c77a0, 0x10000, 3},
    [0x67] = {0, 0x10000, 1},
    [0x68] = {0, 0x10000, 1},
    [0x6a] = {0x006a9088, 0x10000, 3},
    [0x6e] = {0x006a98ac, 0x10000, 3},
    [0x6f] = {0x0066351c, 0x10000, 3},
    [0x71] = {0x005f51c0, 0x10000, 15},
    [0x72] = {0x00602188, 0x10000, 3},
    [0x73] = {0x005c77bc, 0x10000, 3},
    [0x74] = {0x005c77bc, 0x10000, 3},
    [0x75] = {0x006afcf8, 0x10000, 15},
    [0x76] = {0, 0x10000, 13},
    [0x77] = {0x006a9044, 0x10000, 15},
    [0x7a] = {0x0055911c, 0x10000, 3},
    [0x7b] = {0x00640ce4, 0x10000, 15},
    [0x7c] = {0x005c77b4, 0x10000, 15},
    [0x7e] = {0x005d19d8, 0x10000, 15},
    [0x7f] = {0x00602318, 0x20000, 15},
    [0x80] = {0x00604648, 0x10000, 3},
    [0x81] = {0, 0x10000, 1},
    [0x82] = {0x006b0844, 0x10000, 3},
    [0x83] = {0x006ad1fc, 0x20000, 3},
    [0x84] = {0x006aff20, 0, 3},
    [0x86] = {0x006022f4, 0x10000, 3},
    [0x87] = {0x00604674, 0x10000, 7},
    [0x88] = {0x006a9944, 0x10000, 7},
    [0x89] = {0x006a9944, 0x10000, 15},
    [0x8a] = {0x005c78d4, 0x40000, 3},
    [0x8c] = {0x006ad1ec, 0, 2},
    [0x8d] = {0x006ad1ec, 0, 2},
    [0x8e] = {0x00663524, 0, 3},
    [0x8f] = {0x00663510, 0x10000, 3},
    [0x90] = {0x006ad1ec, 0x10000, 3},
    [0x91] = {0x005f51bc, 0x10000, 7},
    [0x92] = {0x005f51bc, 0x10000, 15},
    [0x93] = {0x0063f03c, 0, 3},
    [0x94] = {0x005d19c0, 0, 3},
    [0x95] = {0x005f9118, 0, 3},
    [0x96] = {0x00604694, 0x10000, 7},
    [0x97] = {0x00604694, 0x10000, 15},
    [0x98] = {0x006a994c, 0x10000, 7},
    [0x99] = {0x006a994c, 0x10000, 15},
    [0x9a] = {0x006034e4, 0x10000, 7},
    [0x9b] = {0x006034e4, 0x10000, 15},
    [0x9c] = {0x006afb34, 0, 3},
    [0x9d] = {0x005f51cc, 0x10000, 7},
    [0x9e] = {0x005f51cc, 0x10000, 15},
    [0x9f] = {0, 0x10000, 1},
    [0xa0] = {0, 0, 0},
    [0xa1] = {0, 0x10000, 1},
    [0xa2] = {0x006ad1f8, 0x10000, 3},
    [0xa3] = {0x006afb28, 0x10000, 3},
    [0xa4] = {0x00600140, 0, 3},
    [0xa6] = {0x005c77b0, 0x10000, 3},
    [0xa9] = {0, 0x10000, 1},
    [0xaa] = {0x006b0844, 0x10000, 3},
    [0xac] = {0, 0x10000, 1},
    [0xad] = {0, 0x10000, 1},
    [0xae] = {0, 0x10000, 1},
    [0xaf] = {0x0063f098, 0x10000, 15},
    [0xb0] = {0, 0x10000, 1},
    [0xb3] = {0, 0x20000, 1},
    [0xb4] = {0, 0, 0},
    [0xb5] = {0, 0, 0},
    [0xb6] = {0, 0, 0},
    [0xb7] = {0, 0, 0},
    [0xb8] = {0, 0, 0},
    [0xb9] = {0, 0, 0},
    [0xba] = {0, 0, 0},
    [0xbc] = {0, 0, 0},
    [0xbd] = {0, 0, 0},
    [0xbe] = {0, 0x20000, 1},
    [0xc2] = {0x006ad1d4, 0, 3},
    [0xc3] = {0x0063f0b0, 0, 3},
    [0xc4] = {0, 0, 1},
    [0xc5] = {0x005d195c, 0x10000, 3},
    [0xc9] = {0x00663524, 0, 3},
    [0xca] = {0x006ad1dc, 0, 3},
    [0xcb] = {0x005e473c, 0, 3},
    [0xcc] = {0x005133f0, 0, 3},
    [0xcd] = {0x00663524, 0, 3},
    [0xce] = {0x006ad1ec, 0, 2},
    [0xcf] = {0x005e46d0, 0x10000, 7},
    [0xd0] = {0x005e46d0, 0x10000, 15},
    [0xd1] = {0x006022a4, 0, 3},
    [0xd5] = {0, 0, 1},
    [0xd6] = {0, 0x40000, 1},
    [0xd7] = {0, 0x20000, 1},
    [0xd8] = {0, 0x20000, 1},
    [0xd9] = {0x005e4190, 0, 3},
    [0xda] = {0, 0x20000, 1},
    [0xdb] = {0, 0x10000, 1},
    [0xdc] = {0, 0x10000, 1},
    [0xdd] = {0, 0x40000, 1},
    [0xde] = {0x006ad248, 0, 3},
    [0xdf] = {0, 0, 1},
    [0xe0] = {0, 0, 1},
    [0xe1] = {0, 0x10000, 9},
    [0xe2] = {0, 0x10000, 1},
    [0xe3] = {0x005e4728, 0, 3},
    [0xe4] = {0, 0x20000, 1},
    [0xe5] = {0, 0x20000, 1},
    [0xe6] = {0x006ad1cc, 0, 3},
    [0xe7] = {0x005ecf30, 0, 3},
};

static uint32_t kind_offset(uint32_t node) { return BYTE(node + 0xd4) * 914; }
static uint32_t tile_index(uint32_t node) {
  return BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8);
}
static void bounds(uint32_t node, int x1, int y1, int x2, int y2) {
  WORD(node + 0xc6) = x1;
  WORD(node + 0xc8) = y1;
  WORD(node + 0xca) = x2;
  WORD(node + 0xcc) = y2;
}
static uintptr_t create(uint32_t node, uint32_t kind, uint32_t x, uint32_t y,
                        uint32_t z) {
  return (uintptr_t)bb_entity_create((void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7),
                                     kind, x, y, z);
}
static void remove_self(void *object) {
  bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
}
static void terrain_change(uint32_t node, uint32_t kind) {
  bb_terrain_set_entry((void *)(uintptr_t)0x005e470c, BYTE(node + 0xd8), BYTE(node + 0xd9),
                       kind, 1);
}

/* 0x00435340..0x00437b33: full type-specific initialization. This is a
 * partial record reset, not a memset: list links, identity/faction/color and
 * numerous record bytes remain untouched. Callback ordering and the original
 * argument/live-coordinate distinction are part of the behavior.
 */
uint32_t BB_THISCALL bb_entity_initialize(void *object,
                                                        uint32_t requested,
                                                        uint32_t x, uint32_t y,
                                                        uint32_t z) {
  uint32_t node = bb_address_encode(object);
  uint8_t kind = requested, original_x = x >> 16, original_y = y >> 16;
  BYTE(node + 0xd4) = kind;
  BYTE(node + 0x129) = BYTE(node + 0x128) = BYTE(node + 0xe3) =
      BYTE(node + 0xe4) = 0;
  for (unsigned i = 0xc; i <= 0x3c; i += 4)
    BB_U32(node + i) = 0;
  BYTE(node + 0x40) = BYTE(node + 0x4c) = 0;
  BB_U32(node + 0x62) = x;
  BB_U32(node + 0x66) = y;
  BB_U32(node + 0x6a) = z;
  for (unsigned i = 0x6e; i <= 0x82; i += 4)
    BB_U32(node + i) = 0;
  BB_U32(node + 0x9c) = 0;
  uint16_t health = WORD(0x006635ae + kind * 914);
  WORD(node + 0xa2) = health;
  WORD(node + 0xa0) = health;
  WORD(node + 0xa4) = health;
  for (unsigned i = 0; i < 3; ++i) {
    WORD(node + 0xa6 + i * 2) = 0;
    WORD(node + 0xac + i * 2) = 0;
    WORD(node + 0xb2 + i * 2) = 0;
    WORD(node + 0xb8 + i * 2) = 0;
  }
  for (unsigned i = 0xbe; i <= 0xcc; i += 2)
    WORD(node + i) = 0;
  BYTE(node + 0xd6) = BYTE(node + 0xdd) = BYTE(node + 0xdf) = original_x;
  BYTE(node + 0xd7) = BYTE(node + 0xde) = BYTE(node + 0xe0) = original_y;
  BYTE(node + 0xe2) = BYTE(node + 0xf2) = BYTE(node + 0xf5) = 0;
  BB_U32(node + 0xec) = 0;
  BYTE(node + 0xf0) = BYTE(node + 0xf1) = BYTE(node + 0xf6) =
      BYTE(node + 0xf9) = 0;
  BYTE(node + 0xd8) = original_x;
  BYTE(node + 0xd9) = original_y;
  BYTE(node + 0xe1) = 0x48;
  BYTE(node + 0xd5) = BYTE(0x00663584 + kind_offset(node));
  BYTE(node + 0x92) = BYTE(node + 0x93) = BYTE(node + 0x94) = 0;
  BYTE(node + 0xe5) = BYTE(node + 0xe6) = BYTE(node + 0xe8) =
      BYTE(node + 0xe7) = BYTE(node + 0xf4) = 0;
  bb_entity_set_ability(object, 0);
  if (BYTE(node + 0xd4) == 0x67)
    bb_entity_profession(object, 1);
  BYTE(node + 0xce) = 0;
  uint32_t offset = kind_offset(node);
  BYTE(node + 0xf3) = BYTE(0x006638c2 + offset);
  BYTE(node + 0x12a) = BYTE(0x006638c3 + offset);
  BYTE(node + 0x12c) = BYTE(0x005e5efe + BYTE(node + 0xf4) * 292);
  if (BYTE(node + 0x12c) < 7)
    BYTE(node + 0x12c) = 7;
  BYTE(node + 0xea) = BYTE(node + 0x138) = BYTE(node + 0x139) = 0;
  if (BB_U32(0x006635c0 + offset))
    BB_U32(node + 0x9c) |= 0x800000;
  BYTE(node + 0xfe) = BYTE(node + 0xff) = 0;
  BYTE(node + 0x100) = BYTE(node + 0x101) = BYTE(node + 0x10d) = 0x40;
  BYTE(node + 0x113) = 2;
  BYTE(node + 0x114) = 0;
  BB_U32(node + 0x96) = 1000;
  BYTE(node + 0x102) = BYTE(node + 0x103) = BYTE(node + 0xfd) =
      BYTE(node + 0x105) = BYTE(node + 0x106) = BYTE(node + 0x11c) =
          BYTE(node + 0x104) = 0;
  BB_U32(node + 0x5c) = 0;
  BYTE(node + 0x60) = 0;
  BYTE(node + 0xfa) = 1;
  BYTE(node + 0xfb) = 0;
  BYTE(node + 0x10a) = BYTE(node + 0x109) = BYTE(node + 0x10e) = 0xff;
  BYTE(node + 0x10c) = BYTE(node + 0x10b) = BYTE(node + 0x10d) =
      BYTE(node + 0x111) = BYTE(node + 0x112) = 0x80;
  BYTE(node + 0x107) = BYTE(node + 0x11d) = BYTE(node + 0x108) =
      BYTE(node + 0x110) = BYTE(node + 0x10f) = BYTE(node + 0x116) =
          BYTE(node + 0x115) = BYTE(node + 0x117) = BYTE(node + 0x11e) =
              BYTE(node + 0x118) = BYTE(node + 0x119) = BYTE(node + 0x11a) = 0;
  BYTE(node + 0x11b) = 0x80;
  BYTE(node + 0x12d) = BYTE(node + 0x12e) = 0;
  WORD(node + 0x9a) = 0;
  bb_entity_update_tile(object);
  bb_entity_register_tiles(object);
  uint32_t px = (int32_t)bb_fixed_multiply(
                    BB_U32(node + 0x62) - BB_U32(node + 0x66), 0x270000) >>
                16;
  BB_U32(node + 0x120) = px - BB_U32(0x006024b8) + BB_U32(0x005db014);
  uint32_t py = (int32_t)bb_fixed_multiply(
                    BB_U32(node + 0x62) + BB_U32(node + 0x66), 0x140000) >>
                16;
  uint32_t pz = (int32_t)bb_fixed_multiply(BB_U32(node + 0x6a), 0xb0000) >> 16;
  BB_U32(node + 0x124) = py - pz - BB_U32(0x00602484) + BB_U32(0x006a9078) - 1;
  if (BB_I32(node + 0x120) > 0 && BB_I32(node + 0x120) < BB_I32(0x00484444) &&
      BB_I32(node + 0x124) > 0 && BB_I32(node + 0x124) < BB_I32(0x00484448))
    BYTE(node + 0x128) = BB_U32(0x00484454);
  offset = kind_offset(node);
  if (BYTE(0x006638c6 + offset) || BYTE(0x006638c7 + offset)) {
    uintptr_t data = EA_plain_one_fn(0x0046c060,2);
    BB_U32(node + 0x38) = data ? bb_entity_data_construct((void *)(uintptr_t)data) : 0;
  }
  const struct type_defaults *type = types + kind;
  if (type->flags & MOTION)
    BB_U32(node + 0x82) = type->motion;
  if (type->flags & RESOURCE)
    BB_U32(node + 0x58) = BB_U32(type->resource);
  if (type->flags & BUILDING) {
    bb_building_footprint_entry(BYTE(node + 0xd4), original_x, (int32_t)y >> 16);
    if (kind == 1)
      BYTE(node + 0x10d) = 0;
    EA_object_fn(0x004529b0,object);
  }
  if (type->flags & PARTS)
    bb_entity_upgrade_parts(object);

  switch (kind) {
  case 2:
    bounds(node, 0, -1, 0, 0);
    break;
  case 0x19:
    bounds(node, -5, -40, 5, 0);
    break;
  case 0x21:
    BYTE(node + 0xf5) = 100;
    BYTE(node + 0xf9) = BYTE(node + 0xe5) = BYTE(node + 0x101) =
        BYTE(node + 0x10d) = BYTE(node + 0x113) = BYTE(node + 0x102) = 0;
    break;
  case 0x28:
    create(node, 0x29, x, y, z + 0x140000);
    for (unsigned i = 0; i < 6; ++i) {
      uint32_t cy = y + ((uint32_t)((int32_t)bb_random() % 7 - 3) << 16);
      uint32_t cx = x + ((uint32_t)((int32_t)bb_random() % 7 - 3) << 16);
      create(node, 0x29, cx, cy, z + 0x160000 + i * 0x20000);
    }
    break;
  case 0x2a:
  case 0x2b:
    bounds(node, 0, -80, 0, 0);
    break;
  case 0x2c:
    create(node, 7, x + 20000, y - 20000, z);
    create(node, 7, x + 20000, y + 20000, z);
    create(node, 7, x - 20000, y + 20000, z);
    create(node, 7, x - 20000, y - 20000, z);
    break;
  case 0x2f:
    WORD(BB_U32(node + 0x38)) = 4;
    break;
  case 0x35:
    BYTE(node + 0x100) = 0;
    bounds(node, 0, -5, 0, 0);
    break;
  case 0x37: {
    BYTE(node + 0xf9) = 0;
    bounds(node, -5, -5, 5, 5);
    uint32_t child = create(node, 0xc5, BB_U32(node + 0x62),
                             BB_U32(node + 0x66), BB_U32(node + 0x6a));
    BB_U32(node + 0x10) = child;
    BYTE(child + 0xea) = 1;
    break;
  }
  case 0x38:
  case 0x94:
  case 0x95:
  case 0x9c:
  case 0xa4:
  case 0xd1:
    bounds(node, -10, -10, 10, 10);
    break;
  case 0x39:
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 2;
    break;
  case 0x3a:
    BYTE(node + 0xf5) = 0;
    bounds(node, 0, -26, 0, 0);
    break;
  case 0x3d:
    BYTE(node + 0xf5) = 4;
    BYTE(node + 0xf9) = BB_U32(0x004842c4) & 3;
    break;
  case 0x3f:
  case 0x40:
    remove_self(object);
    break;
  case 0x42: {
    BYTE(node + 0x100) = 0;
    uint32_t tx = (uint32_t)original_x << 16, ty = (uint32_t)original_y << 16;
    static const int32_t offsets[][2] = {
        {-0x28000, -0x8000}, {0x18000, -0x8000}, {-0x28000, 0x38000},
        {0x18000, 0x38000},  {-0x8000, -0x8000}, {-0x28000, 0x18000},
        {0x18000, 0x18000}};
    for (unsigned i = 0; i < 7; ++i)
      create(node, 5, tx + offsets[i][0], ty + offsets[i][1], z);
    break;
  }
  case 0x43:
    BB_U32(node + 0x10) =
        create(node, 0x38, BB_U32(node + 0x62), BB_U32(node + 0x66),
               BB_U32(node + 0x6a) + 0x7fff);
    break;
  case 0x44:
    WORD(node + 0xa0) = 1000;
    break;
  case 0x45:
    BYTE(node + 0xf9) = 1;
    break;
  case 0x46:
    BB_U32(node + 0x10) = create(node, 0x47, (BYTE(node + 0xd8) << 16) + 0x8000,
                                 (BYTE(node + 0xd9) << 16) + 0x8000,
                                 BB_U32(node + 0x6a) + 0x18000);
    break;
  case 0x47:
  case 0x93:
    bounds(node, -20, -20, 20, 20);
    break;
  case 0x4d: {
    uint32_t target = (uintptr_t)bb_entity_nearest(
        (void *)(uintptr_t)0x00553f90, BYTE(node + 0xf7), 0x42, BYTE(node + 0xd8),
        BYTE(node + 0xd9));
    if (target) {
      uint32_t definition = BYTE(target + 0xd4) * 914;
      uint32_t child = create(
          node, 0x67,
          BB_U32(target + 0x62) + (BB_U32(0x0066389e + definition) << 16),
          BB_U32(target + 0x66) + (BB_U32(0x006638a2 + definition) << 16),
          BB_U32(target + 0x6a));
      BYTE(child + 0xf3) = 4;
      BYTE(child + 0xd5) |= 4;
      bb_entity_move_escape((void *)(uintptr_t)child, BYTE(node + 0xd8),
                            BYTE(node + 0xd9));
      BB_U32(child + 0x10) = node;
      BB_U32(node + 0x10) = child;
    }
    break;
  }
  case 0x4e:
  case 0x86:
    if (kind == 0x86)
      BYTE(node + 0xf5) = 6;
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 0x200000;
    break;
  case 0x51:
    bounds(node, 0, 0u - BB_U32(BB_U32(0x00602298) + 0x48), 0, 0);
    break;
  case 0x53:
    BYTE(node + 0xea) = (int32_t)bb_random() % 7;
    break;
  case 0x54:
    BYTE(node + 0xf5) = 4;
    BYTE(node + 0xe9) = 0x40;
    break;
  case 0x55:
    BYTE(node + 0xf5) = 0x40;
    break;
  case 0x5b:
  case 0xbc: {
    uint32_t child = create(node, 0x67, BB_U32(node + 0x62),
                             BB_U32(node + 0x66), BB_U32(node + 0x6a));
    bb_entity_profession((void *)(uintptr_t)child, kind == 0x5b ? 0x11 : 0x12);
    if (kind == 0xbc)
      bb_entity_subtype((void *)(uintptr_t)child, 0xf);
    remove_self(object);
    break;
  }
  case 0x5e:
  case 0x60:
    BYTE(node + 0xf5) = 6;
    break;
  case 0x5f:
  case 0x61:
  case 0x90:
    terrain_change(node, kind == 0x5f ? 0x27 : kind == 0x61 ? 0x28 : 0x2d);
    if (kind == 0x5f)
      BYTE(node + 0xf5) = 6;
    if (kind == 0x90)
      BYTE(node + 0x101) = 0;
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 0x200000;
    break;
  case 0x62: {
    uintptr_t data = EA_plain_one_fn(0x0046c060,32);
    BB_U32(node + 0x3c) = data;
    BYTE(data) = 0;
    BYTE(node + 0xe5) = 0x80;
    bounds(node, -40, -60, 40, 0);
    uint32_t length = 0;
    while (BYTE(0x004889e0 + length++))
      ;
    for (uint32_t i = 0; i < length / 4; ++i)
      BB_U32(data + i * 4) = BB_U32(0x004889e0 + i * 4);
    for (uint32_t i = length & ~3u; i < length; ++i)
      BYTE(data + i) = BYTE(0x004889e0 + i);
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 0x200000;
    break;
  }
  case 0x67: {
    WORD(node + 0xa2) = 255;
    bounds(node, -4, -24, 4, 10);
    uint32_t part = BYTE(node + 0xf4) * 292;
    BYTE(node + 0xf3) = BYTE(0x005e5efc + part);
    BYTE(node + 0x12a) = BYTE(0x005e5efd + part);
    BYTE(node + 0x12c) = BYTE(0x005e5efe + part);
    if (BYTE(node + 0x12c) < 7)
      BYTE(node + 0x12c) = 7;
    bb_entity_birth(object, 0, 0);
    break;
  }
  case 0x68:
    WORD(node + 0xa2) = 255;
    bounds(node, -4, -24, 4, 10);
    break;
  case 0x6e:
    bounds(node, -5, -20, 5, 0);
    break;
  case 0x6f:
    BYTE(node + 0xf5) = 20;
    bounds(node, 0, 0, 20, 0);
    break;
  case 0x7a:
  case 0x80:
  case 0x82:
  case 0xa6:
  case 0xaa: {
    uint32_t center = kind == 0x80 || kind == 0xa6 ? 0x8000 : 0;
    uint32_t child = create(
        node, kind == 0x7a ? 0x72 : 0x73, (BYTE(node + 0xd8) << 16) + center,
        (BYTE(node + 0xd9) << 16) + center, BB_U32(node + 0x6a));
    BB_U32(child + 0x1c) = node;
    if (kind == 0x7a || kind == 0x80)
      BYTE(0x00559b60 + tile_index(node) * 12) = 0x2b;
    BYTE(node + 0xea) = 1;
    if (kind == 0x7a)
      WORD(node + 0x9a) = 40;
    if (kind == 0x82 || kind == 0xaa)
      WORD(node + 0x9a) = 10;
    break;
  }
  case 0x87:
    BYTE(node + 0x100) = 0;
    break;
  case 0x8f: {
    static const int32_t offsets[][2] = {
        {-0x10000, 0}, {0x10000, 0}, {0, -0x10000}, {0, 0x10000}};
    for (unsigned i = 0; i < 4; ++i) {
      uint32_t child =
          create(node, 0x67, BB_U32(node + 0x62) + offsets[i][0],
                 BB_U32(node + 0x66) + offsets[i][1], BB_U32(node + 0x6a));
      bb_entity_profession((void *)(uintptr_t)child, i < 2 ? 1 : 2);
      bb_entity_first_name_entry();
      BYTE(child + 0xce) = 12;
    }
    create(node, 2, BB_U32(node + 0x62), BB_U32(node + 0x66) - 0x50000,
           BB_U32(node + 0x6a));
    break;
  }
  case 0x96:
    BB_U32(node + 0x10) =
        create(node, 0x32, BB_U32(node + 0x62) - 0x15120,
               BB_U32(node + 0x66) + 0x11388, BB_U32(node + 0x6a) + 0x70000);
    break;
  case 0xa0: {
    int32_t remainder = (int32_t)bb_random() % 3;
    uint32_t subtype =
        ((uint32_t)remainder & 0xffffff00) | (uint8_t)(remainder + 0x54);
    bb_entity_subtype(object, subtype);
    BB_U32(node + 0x82) = 0x10000;
    break;
  }
  case 0xa2:
    create(node, 0xa3, BB_U32(node + 0x62) + 0x10000,
           BB_U32(node + 0x66) + 0x20000, BB_U32(node + 0x6a));
    break;
  case 0xa9:
    bounds(node, 0, -16, 0, 0);
    break;
  case 0xb0:
    bounds(node, 0, -8, 0, 16);
    break;
  case 0xb4:
  case 0xb5:
  case 0xb6:
  case 0xb7:
  case 0xb8:
  case 0xb9:
  case 0xba:
  case 0xbd: {
    static const uint32_t terrain[] = {0, 0x35, 6, 0x83, 0xe3, 0x4f, 0xe1};
    bb_terrain_fill_entry((void *)(uintptr_t)0x005e470c, BYTE(node + 0xd8),
                          BYTE(node + 0xd9),
                          kind == 0xbd ? 0xc8 : terrain[kind - 0xb4]);
    remove_self(object);
    break;
  }
  case 0xc2:
    BYTE(node + 0xf5) = 255;
    break;
  case 0xca:
  case 0xe6:
  case 0xe7:
    bounds(node, -4, -4, 4, 4);
    break;
  case 0xcb:
    bb_entity_sound(object, BB_U32(0x00484330));
    break;
  case 0xcc:
    WORD(node + 0xa0) = (int32_t)bb_random() % 100 + 100;
    break;
  case 0xce:
    EA_three_fn(0x00417660,BYTE(node + 0xf7),BYTE(node + 0xd8),BYTE(node + 0xd9));
    remove_self(object);
    break;
  case 0xe3:
    BYTE(node + 0xf5) = 100;
    break;
  }
  if (BB_U32(0x006635aa + kind_offset(node)))
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 1;
  bb_entity_sprite(object);
  uint32_t eax = BB_U32(node + 0x9c);
  if (!(eax & 0x1000))
    eax = bb_entity_force_dirty_c(object, bb_entity_refresh_tiles(object));
  return eax;
}
