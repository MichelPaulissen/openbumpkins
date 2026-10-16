/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "command_builders.h"
#include "interop.h"
#include <string.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)

static uint32_t truncate_value(long double value) {
  /* QWORD integer-indefinite has a zero low DWORD. */
  if (!(value >= -0x1p63L && value < 0x1p63L))
    return 0;
  return (uint32_t)(int64_t)value;
}
/* The setter uses the current faction, independently of its incoming ECX.
 * Cost rows multiply an unsigned reference WORD in extended precision;
 * goods rows truncate the supplied double directly before narrowing. */
void BB_THISCALL bb_economy_set_entry(void *object, uint32_t row,
                                                    double value) {
  (void)object;
  uintptr_t costs = BB_U32(0x004844a8) + 0x4e;
  if (row < 7) {
    static const uintptr_t reference[] = {0x005e44ac, 0x005e441c, 0x005e44f4,
                                          0x005e4680, 0x005e453c, 0x005e45a8,
                                          0x005e4584};
    static const uint32_t offset[] = {10, 2, 14, 36, 18, 24, 22};
    long double product = (long double)WORD(reference[row]) * value;
    WORD(costs + offset[row]) = truncate_value(product);
  } else if (row < 11) {
    static const uint32_t offset[] = {0x2b, 0x2d, 0x2e, 0x2f};
    BYTE(costs + offset[row - 7]) = truncate_value(value);
  }
}

static void header(unsigned char *packet, uint16_t type, uint16_t length,
                   uint8_t owner) {
  WORD(packet) = type;
  WORD(packet + 2) = length;
  packet[4] = owner;
}
static uint32_t submit(const unsigned char *packet) {
  return bb_command_enqueue((void *)0x00513458, packet);
}
/* Original constructors leave alignment bytes and unused string tails
 * uninitialized. Zero those unused inputs; retain each packet's declared
 * length, every consumed field and the existing queue's partial-copy behavior.
 */
uint32_t BB_THISCALL
bb_command_walk_entry(void *object, uint32_t x, uint32_t y) {
  unsigned char packet[12] = {0};
  header(packet, 1, 12, BYTE((uintptr_t)object + 0x48));
  packet[5] = x;
  packet[6] = y;
  BB_U32(packet + 8) = 0;
  return submit(packet);
}
uint32_t BB_THISCALL bb_command_target_entry(void *object,
                                                           uint32_t target) {
  unsigned char packet[12] = {0};
  header(packet, 3, 10, BYTE((uintptr_t)object + 0x48));
  BB_U32(packet + 6) = target;
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_build_entry(void *object, uint32_t owner, uint32_t kind, uint32_t x,
                       uint32_t y, uint32_t extra) {
  (void)object;
  unsigned char packet[12] = {0};
  header(packet, 4, 10, owner);
  packet[5] = kind;
  packet[6] = x;
  packet[7] = y;
  packet[8] = extra;
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_terrain_entry(void *object, uint32_t kind, uint32_t x, uint32_t y) {
  unsigned char packet[8] = {0};
  header(packet, 5, 8, BYTE((uintptr_t)object + 0x48));
  packet[5] = kind;
  packet[6] = x;
  packet[7] = y;
  return submit(packet);
}
/* Copies retain the original forward DWORD/byte order and terminator. */
static void copy_text(unsigned char *out, const char *text) {
  text = BB_DATA(text);
  size_t length = strlen(text) + 1;
  while (length >= 4) {
    BB_U32(out) = BB_U32(text);
    out += 4;
    text += 4;
    length -= 4;
  }
  while (length--)
    *out++ = (unsigned char)*text++;
}
uint32_t BB_THISCALL bb_command_chat_entry(void *object,
                                                         const char *text) {
  unsigned char packet[248] = {0};
  header(packet, 6, 246, BYTE((uintptr_t)object + 0x48));
  copy_text(packet + 5, text);
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_relationship_entry(void *object, uint32_t other, uint32_t value) {
  unsigned char packet[8] = {0};
  header(packet, 12, 8, BYTE((uintptr_t)object + 0x48));
  packet[5] = other;
  packet[6] = value;
  return submit(packet);
}
uint32_t BB_THISCALL bb_command_pause_entry(void *object) {
  unsigned char packet[8] = {0};
  header(packet, 10, 6, BYTE((uintptr_t)object + 0x48));
  return submit(packet);
}
uint32_t BB_THISCALL bb_command_resume_entry(void *object) {
  unsigned char packet[8] = {0};
  header(packet, 11, 6, BYTE((uintptr_t)object + 0x48));
  return submit(packet);
}
uint32_t BB_THISCALL bb_command_sync_request_entry(void *object) {
  unsigned char packet[8] = {0};
  header(packet, 14, 8, BYTE((uintptr_t)object + 0x48));
  packet[5] = 0;
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_city_name_entry(void *object, uint32_t city, const char *name) {
  unsigned char packet[44] = {0};
  header(packet, 15, 42, BYTE((uintptr_t)object + 0x48));
  BB_U32(packet + 6) = city;
  copy_text(packet + 10, name);
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_activity_entry(void *object, uint32_t target, uint32_t activity) {
  unsigned char packet[12] = {0};
  header(packet, 17, 12, BYTE((uintptr_t)object + 0x48));
  BB_U32(packet + 6) = target;
  packet[10] = activity;
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_spell_entry(void *object, uint32_t spell, uint32_t x, uint32_t y) {
  unsigned char packet[8] = {0};
  header(packet, 18, 8, BYTE((uintptr_t)object + 0x48));
  packet[5] = spell;
  packet[6] = x;
  packet[7] = y;
  return submit(packet);
}
uint32_t BB_THISCALL
bb_command_stat_entry(void *object, uint32_t stat, uint32_t value) {
  unsigned char packet[16] = {0};
  header(packet, 19, 16, BYTE((uintptr_t)object + 0x48));
  packet[10] = stat;
  BB_U32(packet + 6) = 0;
  BB_U32(packet + 12) = value;
  return submit(packet);
}

/* Shared command queue submission, formerly in commands.c. */
uint32_t BB_THISCALL bb_command_enqueue(void *manager,
                                                      const void *packet) {
  (void)manager;
  uintptr_t source = (uintptr_t)packet;
  uint32_t length = WORD(source + 2);
  uint32_t destination = 0x004de758 + (BB_U32(0x0048e754) << 8);
  for (uint32_t i=0;i<(length>>2);++i,source+=4,destination+=4)
    BB_U32(destination)=BB_U32(source);
  for (uint32_t i=0;i<(length&3);++i)
    BYTE(destination+i)=BYTE(source+i);
  uint32_t count = BB_U32(0x0048e754) + 1u;
  BB_U32(0x0048e754) = count;
  return count;
}
