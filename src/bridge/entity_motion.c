/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);

static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00) | (uint8_t)byte;
}
static uint32_t tile(uint32_t x, uint32_t y) { return (y * 192 + x) * 12; }
static uint32_t node_tile(uint32_t node) {
  return tile(BYTE(node + 0xd8), BYTE(node + 0xd9));
}
static uint32_t multiply(uint32_t a, uint32_t b) {
  return bb_fixed_multiply((int32_t)a, (int32_t)b);
}

/* 0x0043fd10: approach a point target or the selected building entrance.
 * Arrival dispatch, movement, stop and reset stay in C.
 */
uint32_t bb_entity_approach_c(void *object, void *other, uint32_t entrance,
                              uint32_t eax) {
  uint32_t node = bb_address_encode(object), target = bb_address_encode(other);
  if (!target)
    return eax;
  uint32_t kind = BYTE(target + 0xd4), offset = kind * 914;
  if (!BB_U32(0x00663578 + offset) && BB_U32(node + 0x2c) == target &&
      (BYTE(node + 0xd8) != BYTE(target + 0xd8) ||
       BYTE(node + 0xd9) != BYTE(target + 0xd9)))
    BB_U32(node + 0x2c) = 0;
  BB_U32(node + 0x18) = target;
  if (BB_U32(node + 0x2c) == target) {
    bb_entity_move_stop_c(object);
    return bb_entity_arrive(object);
  }
  kind = BYTE(target + 0xd4);
  offset = kind * 914;
  uint32_t x, y;
  if (BB_U32(0x00663578 + offset)) {
    x = BB_U32((entrance ? 0x0066389e : 0x006638ae) + offset);
    y = BB_U32((entrance ? 0x006638a2 : 0x006638b2) + offset);
    x = low_byte(x, x + BYTE(target + 0xd8));
    y = low_byte(y, y + BYTE(target + 0xd9));
  } else {
    x = low_byte(kind * 7, BYTE(target + 0xd8));
    y = low_byte(offset, BYTE(target + 0xd9));
  }
  eax = bb_entity_move_escape(object, x, y);
  eax = low_byte(eax, BYTE(node + 0xf0));
  return (uint8_t)eax ? eax : bb_entity_reset_move(object);
}

/* 0x0043f8c0: resume the live activity, then handle the special target slot.
 * The first approach may change activity/state before the second gate.
 */
uint32_t BB_THISCALL bb_entity_resume(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t state = BYTE(node + 0xf6);
  switch (state) {
  case 2:
  case 3:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
  case 10:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
    if (!BYTE(node + 0x11d))
      bb_entity_approach_c(object, BB_POINTER(node + 0x34), state != 16,
                           BB_U32(node + 0x34));
  }
  uint32_t eax = BYTE(node + 0xf6);
  if (eax == 11) {
    eax = BB_U32(node + 0x30);
    if (eax)
      eax = low_byte(eax, BYTE(node + 0x11d));
    if (!(uint8_t)eax || !BB_U32(node + 0x30)) {
      BYTE(node + 0xfb) = 0;
      BYTE(node + 0xf6) = 0;
      eax = bb_entity_approach_c(object, BB_POINTER(node + 0x1c), 1,
                                 BB_U32(node + 0x1c));
    }
  }
  return eax;
}

/* 0x0043f970/0x0043fa30: replace tile or exact Q16 position, unregister and
 * register footprints, then sort and dirty. Only the exact-position form
 * gates reservation clearing/setting and stopping on live kind 0x67.
 */
uint32_t BB_THISCALL
bb_entity_place_tile(void *object, uint32_t x, uint32_t y) {
  uint32_t node = bb_address_encode(object);
  bb_entity_move_stop_c(object);
  BB_U32(0x00559b68 + node_tile(node)) &= 0xfffffffe;
  bb_entity_release_tiles(object);
  BYTE(node + 0xd8) = x;
  BYTE(node + 0xd9) = y;
  BB_U32(node + 0x62) = ((uint32_t)(uint8_t)x << 16) + 0x8000;
  BB_U32(node + 0x66) = ((uint32_t)(uint8_t)y << 16) + 0x8000;
  bb_entity_register_tiles(object);
  BB_U32(0x00559b68 + node_tile(node)) |= 1;
  uint32_t eax = bb_entity_sort((void *)(uintptr_t)0x00553f90, object);
  return bb_entity_dirty_c(object, eax, node);
}
uint32_t BB_THISCALL bb_entity_place(void *object, uint32_t x,
                                                   uint32_t y) {
  uint32_t node = bb_address_encode(object);
  if (BYTE(node + 0xd4) == 0x67) {
    bb_entity_move_stop_c(object);
    BB_U32(0x00559b68 + node_tile(node)) &= 0xfffffffe;
  }
  bb_entity_release_tiles(object);
  BB_U32(node + 0x62) = x;
  BB_U32(node + 0x66) = y;
  BYTE(node + 0xd8) = x >> 16;
  BYTE(node + 0xd9) = y >> 16;
  bb_entity_register_tiles(object);
  if (BYTE(node + 0xd4) == 0x67)
    BB_U32(0x00559b68 + node_tile(node)) |= 1;
  uint32_t eax = bb_entity_sort((void *)(uintptr_t)0x00553f90, object);
  return bb_entity_dirty_c(object, eax, node);
}

/* 0x004414e0: clear stale arrival state using signed Q16 distance, or the
 * live building attachment. A null target retains the incoming EAX.
 */
uint32_t bb_entity_check_arrival_c(void *object, uint32_t eax) {
  uint32_t node = bb_address_encode(object), target = BB_U32(node + 0x2c);
  if (!target)
    return eax;
  if (BB_U32(0x00663578 + BYTE(target + 0xd4) * 914)) {
    eax = BB_U32(node + 0x28);
    if (eax != target)
      BB_U32(node + 0x2c) = 0;
  } else {
    eax = bb_entity_distance(BYTE(target + 0xd8), BYTE(target + 0xd9),
                             BYTE(node + 0xd8), BYTE(node + 0xd9));
    if ((int32_t)eax > 0x18000)
      BB_U32(node + 0x2c) = 0;
  }
  return eax;
}

/* 0x00441560/0x004415d0: action subtype, faction mapping, live counters and
 * the two action bytes. The mapping faction is saved before subtype setup.
 */
uint32_t BB_THISCALL bb_entity_action_begin(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t faction = BYTE(node + 0xf7);
  bb_entity_subtype(object, 0x73);
  BYTE(node + 0x115) = BYTE(0x00555034 + faction * 0xd00 + BYTE(node + 0x115));
  BYTE(node + 0x116) = 0;
  BYTE(node + 0x107) = 0;
  BYTE(node + 0xe9) = 0;
  uint32_t eax = bb_entity_reset_move(object);
  ++BB_U32(0x00555c34 + BYTE(node + 0xf7) * 0xd00);
  return eax;
}
uint64_t bb_entity_action_c(void *object, uint32_t status, uint32_t duration,
                            uint32_t eax, uint32_t edx) {
  uint32_t node = bb_address_encode(object);
  eax = low_byte(eax, BYTE(node + 0xf2));
  if ((uint8_t)eax != 0x73) {
    eax = low_byte(eax, status);
    edx = low_byte(edx, duration);
    BYTE(node + 0x115) = eax;
    BYTE(node + 0x116) = edx;
  }
  return ((uint64_t)edx << 32) | eax;
}

/* 0x00441600/0x004416d0: cargo and secondary-target state. Cargo callbacks
 * may replace the old object or live cargo kind before its special flag gate.
 */
uint32_t BB_THISCALL bb_entity_cargo(void *object, void *other) {
  uint32_t node = bb_address_encode(object), target = bb_address_encode(other);
  uint32_t eax;
  if (BYTE(node + 0x11a)) {
    uintptr_t old = BB_U32(node + 0x20);
    if (old) {
      BB_U32(old + 0x9c) &= 0xfeffffff;
      eax = bb_entity_sound(object, BB_U32(0x0048433c));
      bb_entity_dirty_c(BB_POINTER(node + 0x20), eax, 0);
      if (BYTE(node + 0x11a) == 0xd3)
        BB_U32(node + 0x9c) &= 0xfdffffff;
    }
    BYTE(node + 0x11a) = 0;
    bb_entity_dirty_c(object, 0, 0);
  }
  eax = target;
  if (target && !(BB_U32(target + 0x9c) & 0x1000000)) {
    BB_U32(node + 0x20) = target;
    BYTE(node + 0x11a) = BYTE(target + 0xd4);
    BB_U32(target + 0x9c) |= 0x1000000;
    eax = bb_entity_dirty_c(BB_POINTER(node + 0x20), target, 0);
    eax = bb_entity_sound(object, BB_U32(0x00484340));
    eax = bb_entity_dirty_c(object, eax, 0);
    if (BYTE(node + 0x11a) == 0xd3)
      BB_U32(node + 0x9c) |= 0x2000000;
  }
  return eax;
}
uint64_t bb_entity_secondary_c(void *object, void *other) {
  uint32_t node = bb_address_encode(object);
  uint32_t edx = BB_U32(node + 0x9c);
  edx = other ? edx | 0x4000000 : edx & 0xfbffffff;
  BB_SET_POINTER(node + 0x24, other);
  BB_U32(node + 0x9c) = edx;
  return ((uint64_t)edx << 32) | (uint32_t)bb_address_encode(other);
}

/* 0x004503c0: set rounded Q16 velocity and advance byte path indices.
 * Direction/distance tables use wrapped bytes and signed threshold tests.
 */
uint32_t BB_THISCALL bb_entity_path_step(void *object) {
  uint32_t node = bb_address_encode(object);
  if (BYTE(node + 0xf1) >= BYTE(node + 0xf0) ||
      (BB_U32(node + 0x9c) & 0x44000)) {
    BB_U32(node + 0x7a) = 0;
    BB_U32(node + 0x7e) = 0;
    return 0;
  }
  uint32_t delta =
      ((uint32_t)BYTE(node + 0xd6) << 16) - BB_U32(node + 0x62) + 0x8000;
  uint8_t x = ((int32_t)multiply(delta, 0x80000) >> 16) + 32;
  delta = ((uint32_t)BYTE(node + 0xd7) << 16) - BB_U32(node + 0x66) + 0x8000;
  uint8_t y = ((int32_t)multiply(delta, 0x80000) >> 16) + 32;
  uint32_t index = (uint32_t)y * 64 + x;
  int32_t distance = BB_I32(0x005f9270 + index * 4);
  if (distance > 0x10000) {
    BYTE(node + 0xe6) = BYTE(0x005fd278 + index);
    BYTE(node + 0xe5) = BYTE(node + 0xe6);
  }
  BB_U32(node + 0x7a) =
      multiply(BB_U32(0x005d1150 + BYTE(node + 0xe5) * 4), BB_U32(node + 0x8a));
  BB_U32(node + 0x7e) =
      multiply(BB_U32(0x005d1558 + BYTE(node + 0xe5) * 4), BB_U32(node + 0x8a));
  BB_U32(node + 0x6e) = BB_U32(node + 0x7a);
  BB_U32(node + 0x72) = BB_U32(node + 0x7e);
  uint8_t current = BYTE(node + 0xf1);
  if (distance <= 0x10000) {
    BYTE(node + 0xf1) = current + 1;
    if ((int32_t)current < (int32_t)BYTE(node + 0xf0) - 1) {
      uint8_t direction = BYTE(BB_U32(node + 0xec) + BYTE(node + 0xf1));
      BYTE(node + 0xe6) = direction;
      index = (direction >> 1) & 0xfc;
      BYTE(node + 0xd6) += BYTE(0x00484580 + index);
      BYTE(node + 0xd7) += BYTE(0x00484600 + index);
    } else
      BYTE(node + 0x11d) = 3;
  }
  uint32_t eax = (uint32_t)BYTE(node + 0xd7) * 192 + BYTE(node + 0xd6);
  if (BYTE(0x00559b66 + eax * 12)) {
    eax = BB_U32(0x005e5ef6 + BYTE(node + 0xf4) * 292);
    if (!eax)
      return bb_entity_move_escape(object, BYTE(node + 0xdd),
                                   BYTE(node + 0xde));
  }
  return eax;
}

/* 0x00450660: approach the saved fractional destination, select an available
 * replacement or dispatch arrival and optional signed random wandering.
 */
uint32_t BB_THISCALL bb_entity_destination_step(void *object) {
  uint32_t node = bb_address_encode(object);
  uint8_t x = BYTE(node + 0xdd), y = BYTE(node + 0xde);
  uint32_t flags = BB_U32(0x00559b68 + tile(x, y));
  if ((flags & 1) && !(flags & 2)) {
    uint32_t eax = bb_entity_move_near(object, &x, &y);
    return eax ? bb_entity_move_escape(object, x, y) : eax;
  }
  uint32_t delta =
      ((uint32_t)x << 16) + WORD(node + 0xd0) - BB_U32(node + 0x62);
  x = ((int32_t)multiply(delta, 0x100000) >> 16) + 32;
  delta = ((uint32_t)BYTE(node + 0xde) << 16) + WORD(node + 0xd2) -
          BB_U32(node + 0x66);
  y = ((int32_t)multiply(delta, 0x100000) >> 16) + 32;
  if (x && y && x < 63 && y < 63) {
    uint32_t index = (uint32_t)y * 64 + x;
    int32_t distance = BB_I32(0x005f9270 + index * 4);
    if (distance < 0x20000) {
      bb_entity_move_stop_c(object);
      uint32_t eax = bb_entity_arrive(object);
      eax = low_byte(eax, BYTE(node + 0x106));
      if (!(uint8_t)eax)
        return eax;
      int32_t value = bb_random();
      y = BYTE(node + 0xd9) + value % 10 - 5;
      value = bb_random();
      x = BYTE(node + 0xd8) + value % 10 - 5;
      return bb_entity_move_escape(object, x, y);
    }
    if (distance > 0x10000)
      BYTE(node + 0xe6) = BYTE(0x005fd278 + index);
  }
  uint8_t direction = BYTE(node + 0xe6);
  BYTE(node + 0xe5) = direction;
  uint32_t vx = BB_U32(0x005d1150 + direction * 4);
  uint32_t vy = BB_U32(0x005d1558 + direction * 4);
  BB_U32(node + 0x7a) = vx;
  BB_U32(node + 0x7e) = vy;
  BB_U32(node + 0x6e) = vx;
  BB_U32(node + 0x72) = vy;
  return vy;
}

/* 0x00450820/0x00451b90: signed short health and profession idle feedback. */
uint32_t BB_THISCALL bb_entity_heal(void *object,
                                                  uint32_t amount) {
  uint32_t node = bb_address_encode(object);
  int16_t health = WORD(node + 0xa0) + amount;
  WORD(node + 0xa0) = health;
  if (health > SHORT(node + 0xa2))
    WORD(node + 0xa0) = WORD(node + 0xa2);
  uint32_t kind = BYTE(node + 0xd4), edx = kind, eax = amount;
  if (kind == 1 || kind == 2) {
    eax = kind * 7;
    edx = kind * 457;
    if ((SHORT(0x006635ae + kind * 914) >> 1) < SHORT(node + 0xa0))
      BB_U32(node + 0x9c) &= 0xffefffff;
  }
  eax = (eax & 0xffff0000) | WORD(node + 0xa0);
  WORD(node + 0xa4) = eax;
  return bb_entity_dirty_c(object, eax, edx);
}
uint32_t BB_THISCALL bb_entity_idle_sound(void *object) {
  uint32_t node = bb_address_encode(object);
  BYTE(node + 0xf9) = 0;
  uint32_t part = BYTE(node + 0xf4);
  switch (part) {
  case 1:
  case 3:
  case 4:
  case 17:
    return bb_entity_command_sound(object, BB_U32(0x005e47a8));
  case 2:
    return bb_entity_command_sound(object, BB_U32(0x005f912c));
  }
  return part - 1;
}

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_approach_entry(void) {
  __asm__ volatile("pushl %eax\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\tpushl "
                   "%ecx\n\tcall " BB_ASM(
                       bb_entity_approach_c) "\n\taddl $16, %esp\n\tret $8");
}
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_check_arrival_entry(void) {
  __asm__ volatile("pushl %eax\n\tpushl %ecx\n\tcall " BB_ASM(
      bb_entity_check_arrival_c) "\n\taddl $8, %esp\n\tret");
}
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_action_entry(void) {
  __asm__ volatile(
      "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl 20(%esp)\n\tpushl "
      "20(%esp)\n\tpushl %ecx\n\tcall " BB_ASM(
          bb_entity_action_c) "\n\taddl $20, %esp\n\tpopl %ecx\n\tcmpb $0x73, "
                              "0xf2(%ecx)\n\tret $8");
}
#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_secondary_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl 8(%esp)\n\tpushl %ecx\n\tcall " BB_ASM(
      bb_entity_secondary_c) "\n\taddl $8, %esp\n\tpopl %ecx\n\ttestl %edx, "
                             "%edx\n\tret $4");
}
#endif
