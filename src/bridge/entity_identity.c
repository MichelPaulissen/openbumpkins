/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
typedef uint32_t (*sound_fn)(uint32_t, uint32_t, uint32_t);

/* These two name generators are empty in the pinned patched executable.
 * Their entries must also leave the incoming registers and flags untouched.
 */
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_first_name_entry(void) {
  __asm__ volatile("ret");
}
#else
void bb_entity_first_name_entry(void) {}

#endif
#ifndef BB_MODEL_RUNTIME
__attribute__((naked)) void bb_entity_last_name_entry(void) {
  __asm__ volatile("ret");
}
#else
void bb_entity_last_name_entry(void) {}

#endif

static uint32_t string_size(uintptr_t source) {
  uint32_t size = 0;
  do {
    ++size;
  } while (BYTE(source + size - 1));
  return size;
}

/* Scan before writing, then copy forwards in words followed by bytes, as
 * the original does. Overlapping names therefore retain its copy behavior.
 */
static void copy_string(uintptr_t destination, uintptr_t source,
                        uint32_t size) {
  while (size >= 4) {
    BB_U32(destination) = BB_U32(source);
    destination += 4;
    source += 4;
    size -= 4;
  }
  while (size--) {
    BYTE(destination++) = BYTE(source++);
  }
}
static void append_string(uintptr_t destination, uintptr_t source) {
  uint32_t size = string_size(source);
  destination += string_size(destination) - 1;
  copy_string(destination, source, size);
}

/* 0x00452c60..0x00452e98: select the age/profession/marriage title. */
uint32_t BB_THISCALL bb_entity_name_title(void *object,
                                                        char *destination) {
  uint32_t node = bb_address_encode(object), source;
  uint8_t age = BYTE(node + 0xce), part = BYTE(node + 0xf4);
  int female = part == 14 || part == 12 || part == 2;
  if (age < 12)
    source = female ? 0x00488a0c : 0x00488a04;
  else if (age >= 88)
    source = female ? 0x004889f0 : 0x00488a00;
  else if (female)
    source = BB_U32(node + 0x30) ? 0x00488a00 : 0x00488a0c;
  else if (part == 3)
    source = 0x004889f8;
  else if (part == 8 || part == 16)
    source = 0x004889f4;
  else {
    /* The original reads the spouse slot on both identical male paths. */
    (void)BB_U32(node + 0x30);
    source = 0x004889f0;
  }
  uint32_t size = string_size(source);
  copy_string((uintptr_t)destination, source, size);
  return size;
}

/* 0x00452ea0..0x00452f72: title, space, first name, space, surname. */
uint32_t BB_THISCALL bb_entity_name(void *object,
                                                  char *destination) {
  uint32_t node = bb_address_encode(object);
  uintptr_t output = (uintptr_t)destination;
  bb_entity_name_title(object, destination);
  append_string(output, 0x00487008);
  append_string(output, node + 0x40);
  append_string(output, 0x00487008);
  append_string(output, node + 0x4c);
  return 0;
}

/* 0x00452f80..0x00453131: birth balances the two profession populations,
 * inherits the second parent's surname in preference to the first, and
 * initializes health and traits. Keep the original conditional RNG calls.
 */
uint32_t BB_THISCALL bb_entity_birth(void *object, void *parent_a,
                                                   void *parent_b) {
  uint32_t node = bb_address_encode(object);
  uint32_t faction = BYTE(node + 0xf7) * 0xd00;
  uint32_t males = WORD(0x005559d6 + faction) + WORD(0x005559be + faction);
  uint32_t females = WORD(0x005559d8 + faction) + WORD(0x005559c0 + faction);
  uint32_t part = 13;
  if (males) {
    if (males < females && !(bb_random() % 2))
      part = 13;
    else if (males > females && !(bb_random() % 2))
      part = 14;
    else
      part = bb_random() % 2 ? 14 : 13;
  }
  bb_entity_profession(object, part);
  ++BB_U32(0x00555c10 + BYTE(node + 0xf7) * 0xd00);
  bb_entity_first_name_entry();
  uint32_t parent = bb_address_encode(parent_b ? parent_b : parent_a);
  if (parent)
    copy_string(node + 0x4c, parent + 0x4c, string_size(parent + 0x4c));
  else
    bb_entity_last_name_entry();
  WORD(node + 0xa2) = 40;
  BYTE(node + 0x10b) = BYTE(node + 0x10c);
  WORD(node + 0xa0) = 40;
  BYTE(node + 0x10d) = 128;
  BYTE(node + 0x10a) = 255;
  BYTE(node + 0x109) = 255;
  BYTE(node + 0x10e) = 255;
  BYTE(node + 0x110) = 128;
  BYTE(node + 0x112) = 128;
  BYTE(node + 0x111) = 128;
  BYTE(node + 0x113) = 14;
  BYTE(node + 0x10f) = 64;
  BYTE(node + 0x100) = bb_random() % 32 + 64;
  uint32_t result = bb_random() % 32;
  result = (result & 0xffffff00) | (uint8_t)(result + 64);
  BYTE(node + 0x101) = result;
  return result;
}

/* 0x00454de0..0x00454ea5: stop motion or release a related entity's state.
 * The old subtype itself is deliberately retained until subtype assignment.
 */
uint32_t BB_THISCALL bb_entity_clear_subtype(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t subtype = BYTE(node + 0xf2), result = subtype - 6;
  if (subtype == 7) {
    uint8_t y = BYTE(node + 0xde);
    BB_U32(node + 0x6e) = 0;
    BB_U32(node + 0x72) = 0;
    BYTE(node + 0xd9) = y;
    uint8_t x = BYTE(node + 0xdd);
    BYTE(node + 0xd8) = x;
    uint32_t world_x = ((uint32_t)x << 16) + WORD(node + 0xd0);
    result = WORD(node + 0xd2);
    BB_U32(node + 0x62) = world_x;
    BB_U32(node + 0x66) = ((uint32_t)y << 16) + result;
  } else if (subtype == 6 || subtype == 9 || subtype == 10 || subtype == 11) {
    void *related = BB_POINTER(node + 0x14);
    if (related)
      result = bb_entity_subtype(related, 0);
    BB_U32(node + 0x14) = 0;
  }
  return result;
}

/* 0x00454ed0..0x00454f3b: clear the old state before assigning the new one;
 * kind 0xd2 also updates its live animation slot's activity flag.
 */
uint32_t BB_THISCALL bb_entity_subtype(void *object,
                                                     uint32_t requested) {
  uint32_t node = bb_address_encode(object);
  if (BYTE(node + 0xf2))
    bb_entity_clear_subtype(object);
  BYTE(node + 0xf2) = requested;
  if (BYTE(node + 0xd4) == 0xd2) {
    if (!(uint8_t)requested)
      BB_U32(0x005e47b2 + BYTE(node + 0xea) * 4) = 0;
    else if ((uint8_t)requested == 0x6e)
      BB_U32(0x005e47b2 + BYTE(node + 0xea) * 4) = 1;
  }
  return 1;
}

/* 0x00453ab0..0x00453ad1: forward a sound at the entity's live tile. */
uint32_t BB_THISCALL bb_entity_sound(void *object,
                                                   uint32_t sound) {
  uint32_t node = bb_address_encode(object);
  uint32_t y = BYTE(node + 0xd9), x = BYTE(node + 0xd8);
  return bb_sample_position_entry(sound, x, y);
}
