/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_file.h"
#include "crt_output.h"
#endif
#include "mission_io.h"
#include "script.h"

#define WORD(a) BB_U16(a)

/* The shared C mission reader routes input from archives or opaque original
 * CRT FILE handles. The packed script format itself is read/written in C. */
static void read_record(void *file, void *data, uint32_t size) {
  bb_mission_read_entry(data, size, 1, file);
}
static void write_record(void *file, const void *data, uint32_t size) {
#ifndef BB_MODEL_RUNTIME
  typedef uint32_t (*write_fn)(const void *, uint32_t, uint32_t, void *);
#endif
#ifdef BB_MODEL_RUNTIME
  bb_game_write(data, size, 1, file);
#else
  ((write_fn)BB_CODE((uintptr_t)0x0046cd20))(data, size, 1, file);
#endif
}
static void *allocate(uint32_t size) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_allocate(size);
#else
  return ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x0046c060))(size);
#endif
}
static int32_t version(void) { return (int32_t)BB_U32(0x004843d4); }

/* 00426c20: load all 160 blocks, fixing serialized links and payload pointers.
 * Allocation failure retains the partially loaded state, as in the original.
 */
void BB_THISCALL bb_script_read_entry(void *object, void *file) {
  unsigned char *self = BB_DATA(object);
  bb_script_clear_entry(object);
  for (unsigned player = 0; player < 5; ++player) {
    for (unsigned block = 0; block < 32; ++block) {
      uintptr_t record = (uintptr_t)self + 0x88 + (player * 32 + block) * 28;
      uint32_t count;
      read_record(file, &count, 4);
      uintptr_t previous = 0;
      for (uint32_t i = 0; i < count; ++i) {
        void *node = allocate(14);
        if (node)
          node = bb_script_event_construct_entry(node);
        if (!node)
          return;
        if (!i)
          BB_U32(record) = BB_U32(record + 4) = BB_U32(record + 8) =
              bb_address_encode(node);
        read_record(file, node, 14);
        if (previous)
          BB_U32(previous + 10) = bb_address_encode((void *)(uintptr_t)node);
        BB_U32((uintptr_t)node + 10) = 0;
        uint16_t size;
        read_record(file, &size, 2);
        void *payload = allocate(size);
        if (!payload)
          return;
        read_record(file, payload, size);
        BB_U32((uintptr_t)node + 6) = bb_address_encode((void *)(uintptr_t)payload);
        previous = bb_address_encode((void *)(uintptr_t)node);
      }
      if (version() >= 121) {
        uint32_t index;
        read_record(file, &index, 4);
        while (index--)
          BB_U32(record + 4) = BB_U32(BB_U32(record + 4) + 10);
      }
      read_record(file, &count, 4);
      previous = 0;
      for (uint32_t i = 0; i < count; ++i) {
        void *node = allocate(24);
        if (node)
          node = bb_script_condition_construct_entry(node);
        if (!node)
          return;
        if (!i)
          BB_U32(record + 12) = BB_U32(record + 16) = bb_address_encode((void *)(uintptr_t)node);
        read_record(file, node, 24);
        if (previous)
          BB_U32(previous + 20) = bb_address_encode((void *)(uintptr_t)node);
        BB_U32((uintptr_t)node + 20) = 0;
        previous = bb_address_encode((void *)(uintptr_t)node);
      }
      if (version() >= 122)
        read_record(file, (void *)(record + 20), 4);
      bb_script_event_sort_entry((void *)record);
    }
    read_record(file, self + 0x83 + player, 1);
  }
  read_record(file, self + 0x1510, 1);
  read_record(file, self + 0x1511, 1);
  read_record(file, self + 0x1512, 1);
  if (version() >= 118)
    read_record(file, self + 0x1513, 1);
  if (version() >= 120)
    read_record(file, self + 0x151c, 1);
  else
#ifdef BB_MODEL_RUNTIME
    self[0x151c] = (int32_t)BB_GAME_CALL(0x46be20, 0) % 9;
#else
    self[0x151c] = ((int32_t (*)(void))BB_CODE((uintptr_t)0x0046be20))() % 9;
#endif
  if (version() >= 124) {
    read_record(file, self + 0x1514, 4);
    read_record(file, self + 0x1518, 4);
  } else {
    BB_U32(self + 0x1514) = BB_U32(self + 0x1518) = 0;
  }
}

/* 00426f50: exact packed record stream, including original pointer/padding
 * bytes. Reader repairs those pointers; preserve compatibility with old saves.
 */
void BB_THISCALL bb_script_write_entry(void *object, void *file) {
  unsigned char *self = BB_DATA(object);
  for (unsigned player = 0; player < 5; ++player) {
    for (unsigned block = 0; block < 32; ++block) {
      uintptr_t record = (uintptr_t)self + 0x88 + (player * 32 + block) * 28;
      uint32_t count = bb_script_event_count_entry((void *)record);
      uintptr_t node = BB_U32(record);
      write_record(file, &count, 4);
      for (uint32_t i = 0; i < count; ++i) {
        write_record(file, (void *)node, 14);
        uintptr_t payload = BB_U32(node + 6);
        uint16_t size = WORD(payload + 2);
        write_record(file, &size, 2);
        write_record(file, (void *)payload, size);
        node = BB_U32(node + 10);
      }
      uint32_t index = 0;
      uintptr_t cursor = BB_U32(record + 4);
      for (node = BB_U32(record); node && node != cursor;
           node = BB_U32(node + 10))
        ++index;
      write_record(file, &index, 4);
      count = bb_script_condition_count_entry((void *)record);
      node = BB_U32(record + 12);
      write_record(file, &count, 4);
      for (uint32_t i = 0; i < count; ++i) {
        write_record(file, (void *)node, 24);
        node = BB_U32(node + 20);
      }
      write_record(file, (void *)(record + 20), 4);
    }
    write_record(file, self + 0x83 + player, 1);
  }
  static const unsigned fields[] = {0x1510, 0x1511, 0x1512, 0x1513,
                                    0x151c, 0x1514, 0x1518};
  for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    write_record(file, self + fields[i], i < 5 ? 1 : 4);
}
