/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_tile_fn(id,a0,a1,a2) BB_GAME_CALL(id,0,EA_WORD(a0),EA_WORD(a1),EA_WORD(a2))
#define EA_one_fn(id,a0,a1) BB_GAME_CALL(id,EA_WORD(a0),EA_WORD(a1))
#else
#define EA_tile_fn(id,...) CALL(id,tile_fn)(__VA_ARGS__)
#define EA_one_fn(id,...) CALL(id,one_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef uint32_t(BB_THISCALL * one_fn)(void *, uint32_t);
typedef uint32_t(BB_THISCALL *
                 four_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*tile_fn)(uint32_t, uint32_t, uint32_t);

static uint32_t kind_offset(uint32_t node) { return BYTE(node + 0xd4) * 914; }
static uint32_t tile_index(uint32_t node) {
  return BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8);
}
static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00) | (uint8_t)byte;
}

/* 0x0041c290..0x0041c297: initialize only the owned two-byte state. */

/* 0x00438420..0x00438499: Q16 speed from the current speed byte and
 * terrain. The terrain-free path returns the tile index, not the speed.
 */
uint64_t bb_entity_speed_c(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t speed = bb_fixed_divide(BYTE(node + 0x10b) << 16, 0x300000);
  BB_U32(node + 0x8a) = speed;
  speed = bb_fixed_multiply(speed, 0x18000);
  BB_U32(node + 0x8a) = speed;
  uint32_t tile = tile_index(node);
  if (!BYTE(0x00559b61 + tile * 12))
    return ((uint64_t)(tile * 3) << 32) | tile;
  uint64_t result = bb_fixed_multiply_registers(speed, 80000);
  BB_U32(node + 0x8a) = result;
  return result;
}

/* 0x00453b20..0x00453b93: release a kind-two target's reservation
 * before assigning the new target. The primary slot wins when both match.
 */
uint64_t bb_entity_target_c(void *object, uint32_t requested, uint32_t edx) {
  uint32_t node = bb_address_encode(object), old = BB_U32(node + 0x1c);
  if (old) {
    edx = low_byte(edx, BYTE(old + 0xd4));
    if ((uint8_t)edx == 2) {
      edx = BB_U32(old + 0x20);
      if (edx == node)
        BB_U32(old + 0x20) = 0;
      else {
        edx = BB_U32(old + 0x24);
        if (edx == node)
          BB_U32(old + 0x24) = 0;
      }
    }
  }
  BB_U32(node + 0x1c) = requested;
  if (requested) {
    edx = low_byte(edx, BYTE(requested + 0xd4));
    if ((uint8_t)edx == 2) {
      edx = BYTE(0x005e5eff + BYTE(node + 0xf4) * 292);
      BB_U32(requested + (edx == 2 ? 0x20 : 0x24)) = node;
    }
  }
  return ((uint64_t)edx << 32) | requested;
}

/* The original stores the old signed health ratio to a double before
 * callbacks, then multiplies in x87 precision and truncates to an integer.
 * Keep those two rounding points and restore the caller's control word.
 */
#ifdef BB_MODEL_RUNTIME
static double health_ratio(int32_t health,int32_t maximum) {
 return maximum?(double)health/(double)maximum:0;
}
#else
static double health_ratio(int32_t health, int32_t maximum) {
  double ratio = 0;
  if (maximum)
    __asm__ volatile("fildl %1\n\tfildl %2\n\tfdivrp\n\tfstpl %0"
                     : "=m"(ratio)
                     : "m"(health), "m"(maximum)
                     : "st");
  return ratio;
}
#endif
#ifdef BB_MODEL_RUNTIME
static uint32_t scaled_health(int32_t maximum,double ratio) {
 volatile double value=(double)maximum*ratio;
 return (uint32_t)bb_entity_integer(value);
}
#else
static uint32_t scaled_health(int32_t maximum, double ratio) {
  uint16_t original, truncate;
  int64_t result;
  __asm__ volatile("fnstcw %0" : "=m"(original));
  truncate = original | 0x0c00;
  __asm__ volatile("fildl %2\n\tfmull %3\n\tfldcw %4\n\tfistpll %0\n\t"
                   "fldcw %1"
                   : "=m"(result)
                   : "m"(original), "m"(maximum), "m"(ratio), "m"(truncate)
                   : "st");
  return result;
}
#endif

/* 0x00437f30..0x004381c9: profession counters, child/target cleanup,
 * health scaling, profession defaults, abilities and movement state.
 * Callbacks may change kind, faction, profession and the child pointer.
 */
uint32_t BB_THISCALL bb_entity_profession(void *object,
                                                        uint32_t requested) {
  uint32_t node = bb_address_encode(object);
  uint8_t old_part = BYTE(node + 0xf4);
  int32_t maximum = SHORT(node + 0xa2);
  int32_t health = maximum ? SHORT(node + 0xa0) : 0;
  double ratio = health_ratio(health, maximum);
  uint8_t kind = BYTE(node + 0xd4);
  if (kind == 0xab || kind == 0xbf) {
    BYTE(node + 0xf4) = requested;
    return low_byte(health, requested);
  }
  bb_faction_count_remove((void *)(uintptr_t)0x00554fb8, BYTE(node + 0xf7), kind,
                          old_part);
  if (BYTE(node + 0xf4) == 0x1a && BB_U32(node + 0x10)) {
    bb_entity_remove((void *)(uintptr_t)0x00553f90, BB_POINTER(node + 0x10));
    BB_U32(node + 0x10) = 0;
  }
  uint8_t part = BYTE(node + 0xf4);
  if (part == 4 || part == 5 || part == 6)
    bb_entity_target_c(object, 0, 0);
  BYTE(node + 0xf4) = requested;
  bb_faction_count_add((void *)(uintptr_t)0x00554fb8, BYTE(node + 0xf7), BYTE(node + 0xd4),
                       (uint8_t)requested);
  uint32_t offset = BYTE(node + 0xf4) * 292;
  BYTE(node + 0xf3) = BYTE(0x005e5efc + offset);
  BYTE(node + 0x12a) = BYTE(0x005e5efd + offset);
  BYTE(node + 0x12c) = BYTE(0x005e5efe + offset);
  if (BYTE(node + 0x12c) < 7)
    BYTE(node + 0x12c) = 7;
  maximum = SHORT(0x005e5ee4 + offset);
  WORD(node + 0xa2) = maximum;
  WORD(node + 0xa0) = scaled_health(maximum, ratio);
  if (SHORT(node + 0xa0) > maximum)
    WORD(node + 0xa0) = maximum;
  BYTE(node + 0x10c) = BYTE(0x005e5f00 + offset);
  if (BYTE(node + 0xf4) != old_part)
    BYTE(node + 0x10b) = BYTE(node + 0x10c);
  bb_entity_speed_c(object);
  uint32_t eax;
  part = BYTE(node + 0xf4);
  switch (part) {
  case 1:
  case 2:
  case 4:
  case 5:
  case 6:
    BB_U32(node + 0x9c) &= 0xffdfffff;
    eax = bb_entity_set_ability(object, 0);
    eax = low_byte(eax, BYTE(node + 0xce));
    break;
  case 3:
  case 0x20:
  case 0x21:
    eax = BB_U32(node + 0x9c) | 0x200000;
    BB_U32(node + 0x9c) = eax;
    break;
  case 7:
  case 8:
  case 9:
  case 0x10:
  case 0x13:
  case 0x14:
    bb_entity_set_ability(object, 0x16);
    eax = BB_U32(node + 0x9c) | 0x200000;
    BB_U32(node + 0x9c) = eax;
    break;
  case 0x1a:
    if (!BB_U32(node + 0x10))
      BB_U32(node + 0x10) = (uintptr_t)bb_entity_create(
          (void *)(uintptr_t)0x00553f90, low_byte(BB_U32(node + 0x6a), BYTE(node + 0xf7)),
          0xc5, BB_U32(node + 0x62), BB_U32(node + 0x66), BB_U32(node + 0x6a));
    eax = BB_U32(node + 0x9c) | 0x800000;
    BB_U32(node + 0x9c) = eax;
    break;
  default:
    eax = bb_entity_set_ability(object, 0);
    goto finish;
  }
  if (BYTE(node + 0xce) < 12)
    BYTE(node + 0xce) = 12;
finish:
  eax = low_byte(eax, BYTE(node + 0xf7));
  if ((uint8_t)eax == 4) {
    eax = low_byte(eax, BYTE(node + 0xd4));
    if ((uint8_t)eax == 0x67) {
      eax = low_byte(eax, BYTE(node + 0x12c) + 12);
      BYTE(node + 0x12c) = eax;
    }
  }
  return eax;
}

/* 0x004382e0..0x0043841d: construct alternating 0x72/0x73 parts from
 * the mapped kind's 7x7 footprint. Coordinates deliberately extend beyond
 * the map and each creation may change the parent's definition/state.
 */
uint32_t BB_THISCALL bb_entity_upgrade_parts(void *object) {
  uint32_t node = bb_address_encode(object);
  BYTE(node + 0xea) = 0;
  uint32_t offset = BYTE(0x006635bc + kind_offset(node)) * 914;
  /* Retain the original preliminary scan's memory accesses. Its count is
   * discarded before the actual creation pass. */
  for (uint32_t i = 0; i < 49; ++i)
    (void)BB_U32(0x006635c4 + offset + i * 4);
  uint32_t eax = 0;
  for (uint32_t y = 0; y < 7; ++y)
    for (uint32_t x = 0; x < 7; ++x) {
      uint32_t mapped = BYTE(0x006635bc + kind_offset(node));
      eax = (y * 7 + x) * 4;
      if (!BB_U32(0x006635c4 + mapped * 914 + eax))
        continue;
      uint32_t child = (uintptr_t)bb_entity_create(
          (void *)(uintptr_t)0x00553f90, low_byte(mapped * 457, BYTE(node + 0xf7)),
          (x + y) & 1 ? 0x73 : 0x72, (BYTE(node + 0xd8) + x - 3) << 16,
          (BYTE(node + 0xd9) + y - 3) << 16, BB_U32(node + 0x6a));
      BB_U32(child + 0x1c) = node;
      eax = low_byte(child, BYTE(node + 0xea) + 1);
      BYTE(node + 0xea) = eax;
    }
  return eax;
}

/* 0x00447f20..0x0044848b: refresh neighboring tile-type state, including
 * the two paired kind groups. Re-read coordinates/kind after each callback.
 */
uint32_t BB_THISCALL bb_entity_refresh_tiles(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t kind = BYTE(node + 0xd4);
  if (!BB_U32(0x0066358e + kind * 914))
    return kind * 457;
  uint32_t first = 0, second = 0;
  if (kind == 0x1d || kind == 0x52) {
    first = 0x1d;
    second = 0x52;
  } else if (kind == 0x81 || kind == 0xa9) {
    first = 0x81;
    second = 0xa9;
  }
  uint32_t eax = 0;
  for (uint32_t pass = 0; pass < (first ? 2u : 1u); ++pass)
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x)
        eax = EA_tile_fn(0x00416960,first ? (pass ? second : first) : BYTE(node + 0xd4),(uint8_t)(BYTE(node + 0xd8) + x),(uint8_t)(BYTE(node + 0xd9) + y));
  return eax;
}

/* 0x00448490..0x00448836: register outer terrain flags, enclosed floor
 * changes and inner occupancy, then bind a point entity to its building.
 * The subtype-zero mapped-kind early return precedes all later effects.
 */
uint32_t BB_THISCALL bb_entity_register_tiles(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t offset = kind_offset(node);
  if (BYTE(0x006635bc + offset) || BB_U32(0x00663578 + offset)) {
    for (uint32_t y = 0; y < 11; ++y)
      for (uint32_t x = 0; x < 11; ++x) {
        uint32_t i = (y * 11 + x) * 4;
        offset = kind_offset(node);
        if (!BB_U32(0x00663688 + offset + i))
          continue;
        if (BYTE(0x006635bc + offset))
          BB_U32(0x00556e2c +
                 ((BYTE(node + 0xd9) + y) * 192 + BYTE(node + 0xd8) + x) *
                     12) |= 0x200000;
        offset = kind_offset(node) + i;
        if (BB_U32(0x0066365c + offset) && BB_U32(0x006636b4 + offset) &&
            BB_U32(0x00663684 + offset) && BB_U32(0x0066368c + offset))
          bb_terrain_set_entry((void *)(uintptr_t)0x005e470c,
                               (uint8_t)(BYTE(node + 0xd8) + x - 5),
                               (uint8_t)(BYTE(node + 0xd9) + y - 5), 0x4f, 1);
      }
    if (!BYTE(node + 0xf2) && BYTE(0x006635bc + kind_offset(node)))
      return BYTE(node + 0xd4) * 457;
  }
  offset = kind_offset(node);
  if (BB_U32(0x00663586 + offset) ||
      (BYTE(0x006635bc + offset) && BYTE(node + 0xf2)))
    for (uint32_t y = 0; y < 7; ++y)
      for (uint32_t x = 0; x < 7; ++x)
        if (BB_U32(0x006635c4 + kind_offset(node) + (y * 7 + x) * 4))
          ++BYTE(0x00558042 +
                 ((BYTE(node + 0xd9) + y) * 192 + BYTE(node + 0xd8) + x) * 12);
  if (BB_U32(0x0066358a + kind_offset(node)))
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 4;
  switch (BYTE(node + 0xd4)) {
  case 0x31:
  case 0x39:
  case 0x4e:
  case 0x6f:
    BB_U32(0x00559b68 + tile_index(node) * 12) |= 0x200000;
  }
  uint32_t eax = BYTE(node + 0xd4) * 457;
  if (BB_U32(0x00663578 + kind_offset(node))) {
    for (uint32_t y = 0; y < 11; ++y)
      for (uint32_t x = 0; x < 11; ++x)
        if (BB_U32(0x00663688 + kind_offset(node) + (y * 11 + x) * 4)) {
          uintptr_t tile =
              0x00556e2c +
              ((BYTE(node + 0xd9) + y) * 192 + BYTE(node + 0xd8) + x) * 12;
          BB_U32(tile) |= 2;
          BB_U32(tile) |= 0x200000;
        }
    return 11;
  }
  eax = low_byte(eax, BYTE(node + 0xd9));
  if (!(BB_U32(0x00559b68 + tile_index(node) * 12) & 0x100000))
    BB_U32(node + 0x28) = 0;
  else if (!BB_U32(node + 0x28))
    eax = BB_U32(node + 0x28) = (uintptr_t)bb_entity_outer_at(
        (void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8), BYTE(node + 0xd9));
  return eax;
}

/* 0x00449a70..0x00449c0d: update cached tile bytes, notify intersecting
 * entities and occupied buildings, recalculate speed and update visibility.
 * The shared traversal pointer is deliberately live across callbacks.
 */
uint32_t BB_THISCALL bb_entity_update_tile(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t eax = BB_I32(node + 0x66) >> 16;
  BYTE(node + 0xd9) = eax;
  BYTE(node + 0xda) = BB_U32(node + 0x6a) >> 16;
  BYTE(node + 0xd8) = BB_U32(node + 0x62) >> 16;
  if (BYTE(node + 0xdb) == BYTE(node + 0xd8) &&
      BYTE(node + 0xdc) == BYTE(node + 0xd9))
    return eax;
  BB_U32(0x006ba520) = (uintptr_t)bb_entity_at(
      (void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8), BYTE(node + 0xd9));
  while (BB_U32(0x006ba520)) {
    if (node != BB_U32(0x006ba520))
      bb_entity_contact(object, BB_POINTER(0x006ba520));
    BB_U32(0x006ba520) =
        (uintptr_t)bb_entity_at_next((void *)(uintptr_t)0x00553f90, BB_POINTER(0x006ba520),
                                     BYTE(node + 0xd8), BYTE(node + 0xd9));
  }
  if (BYTE(node + 0xd4) == 0x67) {
    uint32_t flags = BB_U32(0x00559b68 + tile_index(node) * 12);
    if ((flags & 0x100000) || ((flags & 0x400) && BB_U32(node + 0x28))) {
      BB_U32(0x006ba520) = (uintptr_t)bb_entity_outer_at(
          (void *)(uintptr_t)0x00553f90, BYTE(node + 0xd8), BYTE(node + 0xd9));
      if (BB_U32(0x006ba520)) {
        if (!(BB_U32(BB_U32(0x006ba520) + 0x9c) & 0x20000)) {
          EA_one_fn(0x00453ab0,object,BB_U32(0x00484320));
          BB_U32(BB_U32(0x006ba520) + 0x9c) |= 0x20000;
          bb_entity_dirty(BB_POINTER(0x006ba520));
        }
        BYTE(BB_U32(0x006ba520) + 0xe8) = 0x12;
      }
    }
  }
  BYTE(node + 0xdb) = BYTE(node + 0xd8);
  BYTE(node + 0xdc) = BYTE(node + 0xd9);
  eax = bb_entity_speed_c(object);
  uint32_t kind = BYTE(node + 0xd4);
  if (kind != 0x67 && kind != 0x68) {
    eax = kind * 457;
    if (!BB_U32(0x00663578 + kind * 914) && kind != 0xdf)
      return eax;
  }
  uint32_t radius = BYTE(node + 0x12c);
  return bb_entity_reveal_c(object, radius, radius);
}

#ifdef _WIN32
#define BB_DATA_CTOR_ASM "_bb_entity_data_construct"
#define BB_SPEED_ASM "_bb_entity_speed_c"
#define BB_TARGET_ASM "_bb_entity_target_c"
#else
#define BB_DATA_CTOR_ASM "bb_entity_data_construct"
#define BB_SPEED_ASM "bb_entity_speed_c"
#define BB_TARGET_ASM "bb_entity_target_c"
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_data_construct_entry(void) {
  __asm__ volatile(
      "pushfl\n\tpushl %edx\n\tpushl %ecx\n\tcall " BB_DATA_CTOR_ASM
      "\n\tpopl %ecx\n\tpopl %edx\n\tpopfl\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_speed_entry(void) {
  __asm__ volatile("pushl %ecx\n\tcall " BB_SPEED_ASM
                   "\n\tpopl %ecx\n\tmovzbl 0xd8(%ecx), %ecx\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_target_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl %edx\n\tpushl 12(%esp)\n\t"
                   "pushl %ecx\n\tcall " BB_TARGET_ASM
                   "\n\taddl $12, %esp\n\tpopl %ecx\n\tret $4");
}
#endif
