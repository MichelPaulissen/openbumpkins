/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_file.h"
#include "crt_output.h"
#endif
#include "script.h"
#include <string.h>

/* Keep the game's allocator while these packed records cross old/C callers. */
static void *allocate(uint32_t size) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_allocate(size);
#else
  return ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x0046c060))(size);
#endif
}
static void release(uintptr_t record) {
#ifdef BB_MODEL_RUNTIME
  bb_game_release(record);
#else
  ((void (*)(void *))BB_CODE((uintptr_t)0x0046c110))((void *)record);
#endif
}

/* 00422660: event construction deliberately leaves the allocation untouched. */
void *BB_THISCALL bb_script_event_construct_entry(void *object) {
  return object;
}

/* 004228e0: leave the clock and final block word for the caller to initialize.
 */

/* 00422900: payload release precedes the live head reread/node release. Do not
 * repair other cursor fields; the original leaves those to block selection. */
void BB_THISCALL bb_script_block_clear_entry(void *object) {
  uintptr_t self = (uintptr_t)object;
  while (BB_U32(self)) {
    uint32_t next = BB_U32(BB_U32(self) + 10);
    release(BB_U32(BB_U32(self) + 6));
    release(BB_U32(self));
    BB_U32(self) = next;
  }
  while (BB_U32(self + 12)) {
    uint32_t next = BB_U32(BB_U32(self + 12) + 20);
    release(BB_U32(self + 12));
    BB_U32(self + 12) = next;
  }
}

/* 00422c10: stable ascending unsigned timestamps, swapping one adjacent pair
 * per pass. Reposition the execution cursor at the first not-yet-past event. */
void BB_THISCALL bb_script_event_sort_entry(void *object) {
  uintptr_t self = (uintptr_t)object;
  for (;;) {
    uintptr_t previous = 0, node = BB_U32(self);
    int swapped = 0;
    while (node) {
      uintptr_t next = BB_U32(node + 10);
      if (next && BB_U32(node + 2) > BB_U32(next + 2)) {
        BB_U32(previous ? previous + 10 : self) = next;
        BB_U32(node + 10) = BB_U32(next + 10);
        BB_U32(next + 10) = node;
        swapped = 1;
        break;
      }
      previous = node;
      node = next;
    }
    if (!swapped)
      break;
  }
  BB_U32(self + 4) = BB_U32(self);
  while (BB_U32(self + 4)) {
    uintptr_t node = BB_U32(self + 4);
    if (BB_U32(0x004842c4) - BB_U32(self + 20) <= BB_U32(node + 2))
      break;
    BB_U32(self + 4) = BB_U32(node + 10);
  }
}

/* 00422b70: insert with a clock-relative timestamp before sorting in C. */
void BB_THISCALL bb_script_event_add_entry(void *object,
                                                         void *record) {
  uintptr_t self = (uintptr_t)object, node = (uintptr_t)record;
  BB_U32(node + 2) = BB_U32(0x004842c4) - BB_U32(self + 20);
  BB_U32(node + 10) = BB_U32(self);
  BB_U32(self) = BB_U32(self + 8) = BB_U32(self + 4) = bb_address_encode((void *)node);
  bb_script_event_sort_entry(object);
}

/* 00422c90: conditions prepend and become the current editable condition. */
void BB_THISCALL bb_script_condition_add_entry(void *object,
                                                             void *record) {
  uintptr_t self = (uintptr_t)object, node = (uintptr_t)record;
  BB_U32(node + 20) = BB_U32(self + 12);
  BB_U32(self + 12) = BB_U32(self + 16) = bb_address_encode((void *)node);
}

static void remove_node(uintptr_t head, uintptr_t node, unsigned next_offset) {
  uintptr_t previous = BB_U32(head);
  if (previous == node)
    BB_U32(head) = BB_U32(node + next_offset);
  else {
    while (BB_U32(previous + next_offset) != node)
      previous = BB_U32(previous + next_offset);
    BB_U32(previous + next_offset) = BB_U32(node + next_offset);
  }
  /* Individual event removal releases the node, retaining its payload. */
  release(node);
}

void BB_THISCALL bb_script_event_remove_entry(void *object,
                                                            void *record) {
  remove_node((uintptr_t)object, (uintptr_t)record, 10); /* 00422ba0 */
}
void BB_THISCALL bb_script_condition_remove_entry(void *object,
                                                                void *record) {
  remove_node((uintptr_t)object + 12, (uintptr_t)record, 20); /* 00422cb0 */
}

static uintptr_t find_index(uintptr_t head, unsigned next_offset,
                            uint32_t index) {
  uintptr_t found = 0;
  uint32_t i = 0;
  for (uintptr_t node = BB_U32(head); node;
       node = BB_U32(node + next_offset), ++i)
    if (i == index)
      found = node;
  return found;
}
void BB_THISCALL
bb_script_event_remove_index_entry(void *object, uint32_t index) {
  uintptr_t node = find_index((uintptr_t)object, 10, index); /* 00422be0 */
  if (node)
    bb_script_event_remove_entry(object, (void *)node);
}
void BB_THISCALL
bb_script_condition_remove_index_entry(void *object, uint32_t index) {
  uintptr_t node = find_index((uintptr_t)object + 12, 20, index); /* 00422cf0 */
  if (node)
    bb_script_condition_remove_entry(object, (void *)node);
}

static uint32_t count(uintptr_t head, unsigned next_offset) {
  uint32_t result = 0;
  for (uintptr_t node = BB_U32(head); node; node = BB_U32(node + next_offset))
    ++result;
  return result;
}
uint32_t BB_THISCALL bb_script_event_count_entry(void *object) {
  return count((uintptr_t)object, 10); /* 00422d20 */
}
uint32_t BB_THISCALL
bb_script_condition_count_entry(void *object) {
  return count((uintptr_t)object + 12, 20); /* 00422d40 */
}

void *BB_THISCALL
bb_script_condition_construct_entry(void *object) {
  unsigned char *self = BB_DATA(object); /* 00422d60: preserve padding at 1 and 15. */
  self[0] = self[14] = 0;
  BB_U32(self + 2) = BB_U32(self + 6) = BB_U32(self + 10) = 0;
  BB_U32(self + 20) = BB_U32(self + 16) = 0;
  return object;
}

void BB_THISCALL bb_script_event_new_entry(void *object,
                                                         void *payload) {
  unsigned char *node = allocate(14); /* 004237f0 */
  if (node)
    bb_script_event_construct_entry(node);
  BB_U8(node) = 1;
  BB_U32(node + 6) = bb_address_encode(payload);
  BB_U32(node + 10) = 0;
  bb_script_event_add_entry((void *)bb_script_selected_block(object), node);
}
void BB_THISCALL bb_script_condition_new_entry(void *object,
                                                             uint32_t kind) {
  unsigned char *node = allocate(24); /* 00423850 */
  if (node)
    bb_script_condition_construct_entry(node);
  BB_U8(node) = kind;
  bb_script_condition_add_entry((void *)bb_script_selected_block(object), node);
}

void BB_THISCALL bb_script_clear_entry(void *object) {
  unsigned char *self = BB_DATA(object); /* 00426bd0 */
  for (unsigned player = 0; player < 5; ++player) {
    for (unsigned block = 0; block < 32; ++block)
      bb_script_block_clear_entry(self + 0x88 + (player * 32 + block) * 28);
    bb_script_block_select_entry(object, player, 0);
  }
  self[0x1511] = self[0x1512] = 0;
  self[0x1510] = 5;
}

void BB_THISCALL bb_script_rewind_entry(void *object) {
  const unsigned char *self = BB_DATA(object); /* 00426b90 */
  bb_script_block_select_entry(object, self[0x82], 0);
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x415ed0, 0, 0x5116d0, 0);
  strcpy(BB_DATA(0x6033d8), BB_DATA(0x488550));
  BB_GAME_CALL(0x41de40, 0x662c98, 0x6033d8);
#else
  typedef void (*load_fn)(void *, uint32_t);
  ((load_fn)BB_CODE((uintptr_t)0x00415ed0))((void *)0x005116d0, 0);
  /* sprintf has no substitutions here; retain the original text data. */
  strcpy((char *)(uintptr_t)0x006033d8, (const char *)(uintptr_t)0x00488550);
  typedef void(BB_THISCALL * print_fn)(void *, const char *);
  ((print_fn)BB_CODE((uintptr_t)0x0041de40))((void *)0x00662c98,
                                    (const char *)(uintptr_t)0x006033d8);
#endif
}
