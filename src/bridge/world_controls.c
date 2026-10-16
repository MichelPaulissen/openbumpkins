/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "world_controls.h"
#include "dialog.h"
#include "interface_controls.h"
#include "interop.h"
#include "entity_calls.h"
#include "road_topology.h"
#include <stddef.h>

#define BYTE(a) BB_U8(a)
#define SHORT(a) BB_I16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
typedef int (*format_fn)(char *, const char *, ...);
#ifdef BB_MODEL_RUNTIME
#define WORLD_FORMAT(out,fmt,...) EA_FORMAT(out,fmt,__VA_ARGS__)
#else
#define WORLD_FORMAT(out,fmt,...) ((format_fn)BB_CODE(0x46bae0))(out,fmt,##__VA_ARGS__)
#endif

/* 0x4031a0: every return has zero upper EAX bytes. Equal directions take the
 * original decrement path; redundant wrap branches reduce to byte arithmetic.
 * Explicit widening also preserves the result for register-capturing parents. */
uint32_t bb_direction_step_entry(uint32_t current, uint32_t target) {
  uint8_t from = current, to = target;
  uint32_t step =
      (uint8_t)bb_direction_difference_entry(from, target) > 8 ? 8 : 1;
  int increase = to > from ? to - from < 128 : from - to >= 128;
  return (uint8_t)(increase ? from + step : from - step);
}

/* 0x403320: road_add consumes only each packed argument's low byte. The
 * executable's adjacent local/return-address bytes are not coordinates. */
void bb_building_footprint_entry(uint32_t kind, uint32_t x, uint32_t y) {
  uint32_t k = (uint8_t)kind * 0x392u;
  uint8_t right = BB_U32(0x6638a6u + k) + (uint8_t)x;
  uint8_t bottom = BB_U32(0x6638aau + k) + (uint8_t)y;
  uint8_t top = BB_U32(0x6638a2u + k) + (uint8_t)y;
  uint8_t left = BB_U32(0x66389eu + k) + (uint8_t)x;
  uintptr_t first = 0x559b68u + ((uint32_t)top * 192u + left) * 12u;
  BB_U32(first) |= 0x100000;
  bb_road_add_entry(right, bottom);
  bb_road_add_entry(left, top);
  uintptr_t last = 0x559b68u + ((uint32_t)bottom * 192u + right) * 12u;
  BB_U32(last) |= 0x400;
}

void BB_THISCALL bb_entity_remove_kind_entry(void *object,
                                                           uint32_t kind) {
  uint32_t count = 0;
  for (uint32_t node = BB_U32(bb_address_encode(object) + 0x1008); node;
       node = BB_U32(node + 4)) {
    if (BYTE(node + 0xd4) == (uint8_t)kind) {
      ++count;
      bb_entity_remove(object, PTR(node));
    }
  }
  WORLD_FORMAT(PTR(0x6033d8), PTR(0x488968), count,
                                   (uint32_t)(uint8_t)kind);
  bb_feedback_append_entry(PTR(0x662c98), PTR(0x6033d8));
}
static int inside(uint32_t node, int32_t left, int32_t top, int32_t right,
                  int32_t bottom) {
  int32_t x = (SHORT(node + 0xbe) >> 1) + (SHORT(node + 0xc2) >> 1);
  int32_t y = (SHORT(node + 0xc0) >> 1) + (SHORT(node + 0xc4) >> 1);
  return left <= x && top <= y && x <= right && y <= bottom;
}
static int owned_human(uint32_t node) {
  return BYTE(node + 0xd4) == 0x67 && BYTE(node + 0xf7) == BYTE(0x4843f0);
}
/* 0x434af0: owned humans take priority, then active humans/animals, then
 * selectable kinds. The counting and selection traversals are distinct. */
void BB_THISCALL
bb_entity_select_rect_entry(void *object, int32_t left, int32_t top,
                            int32_t right, int32_t bottom) {
  if (right < left) {
    int32_t old = left;
    left = right;
    right = old;
  }
  if (bottom < top) {
    int32_t old = top;
    top = bottom;
    bottom = old;
  }
  if (!BB_U32(0x48473c))
    bb_faction_selection_clear_c(BB_POINTER(0x4844a8), BB_U32(0x48473c));
  uint32_t count = 0, selected = 0;
  uintptr_t manager = bb_address_encode(object);
  for (uint32_t node = BB_U32(manager + 0x1008); node; node = BB_U32(node + 4))
    if (owned_human(node) && inside(node, left, top, right, bottom))
      ++count;
  if (count) {
    int32_t choice = bb_random() % (int32_t)count;
    for (uint32_t node = BB_U32(manager + 0x1008); node;
         node = BB_U32(node + 4)) {
      if (owned_human(node) && inside(node, left, top, right, bottom)) {
        uint32_t notify = selected++ == (uint32_t)choice;
        bb_faction_selection_add(BB_POINTER(0x4844a8), PTR(node), notify);
      }
    }
  }
  if (!selected) {
    for (uint32_t node = BB_U32(manager + 0x1008); node;
         node = BB_U32(node + 4)) {
      uint8_t kind = BYTE(node + 0xd4);
      if ((kind == 0x67 || kind == 0x68) && BYTE(node + 0xf4) &&
          inside(node, left, top, right, bottom)) {
        ++selected;
        bb_faction_selection_add(BB_POINTER(0x4844a8), PTR(node), 0);
      }
    }
    if (!selected) {
      for (uint32_t node = BB_U32(manager + 0x1008); node;
           node = BB_U32(node + 4)) {
        if (BB_U32(0x66359au + BYTE(node + 0xd4) * 0x392u) &&
            inside(node, left, top, right, bottom)) {
          ++selected;
          bb_faction_selection_add(BB_POINTER(0x4844a8), PTR(node), 0);
        }
      }
    }
  }
  if (selected)
    bb_sample_play_entry(BB_U32(0x484310));
}
static void depth_message(uint32_t node, uintptr_t format) {
  uint32_t kind = BYTE(node + 0xd4);
  WORLD_FORMAT(
      PTR(0x6033d8), PTR(format), BB_U32(node + 0x8e), kind,
      EA_WORD(PTR(0x663538u + kind * 0x392u)), BB_U32(node + 0x86));
}
void BB_THISCALL bb_entity_depth_check_entry(void *object) {
  int32_t depth = 0;
  uint32_t node = BB_U32(bb_address_encode(object) + 0x1008);
  while (node && BB_I32(node + 0x86) >= depth) {
    depth = BB_I32(node + 0x86);
    node = BB_U32(node + 4);
  }
  if (node) {
    depth_message(node, 0x4889bc);
    bb_feedback_append_entry(PTR(0x662c98), PTR(0x6033d8));
    uint32_t previous = BB_U32(node + 8);
    if (previous)
      depth_message(previous, 0x488998);
  } else {
    WORLD_FORMAT(PTR(0x6033d8), PTR(0x48898c));
    bb_feedback_append_entry(PTR(0x662c98), PTR(0x6033d8));
  }
}

/* 0x4556e0: first speaker's threshold is <=64, the partner's is <64.
 * Speech handles are loaded at each original branch, before its callbacks.
 * Retain EAX results because existing contact callers forward that value. */
uint32_t BB_THISCALL bb_entity_pair_entry(void *object,
                                                        void *other) {
  uint32_t self = bb_address_encode(object), partner = bb_address_encode(other);
  uint32_t first = 0, response = 0;
  int unhappy = 0;
  uint8_t profession = BYTE(self + 0xf4);
  if (profession == 1 || profession == 2) {
    if (BYTE(self + 0x111) <= 64) {
      unhappy = 1;
      first = BB_U32(profession == 1 ? 0x5c77a8 : 0x5f6e00);
    } else {
      int32_t choice = bb_random() % 3;
      static const uintptr_t voices[2][3] = {{0x60247c, 0x60016c, 0x63f054},
                                             {0x5c5b60, 0x6a8f38, 0x5f51d4}};
      if (choice >= 0 && choice < 3)
        first = BB_U32(voices[profession - 1][choice]);
    }
  } else if (profession == 5) {
    first = BB_U32(0x5e4760);
  }
  if (first)
    bb_entity_feedback_c(object, PTR(first), 0, 0, first);
  uint32_t eax = BYTE(partner + 0xf4);
  profession = eax;
  if (profession == 1 || profession == 2) {
    eax = BYTE(partner + 0x111);
    if (eax < 64) {
      if (unhappy) {
        response = BB_U32(profession == 1 ? 0x6022fc : 0x5c7798);
        eax = (uint32_t)bb_entity_action_c(object, 1, 2, eax, 0);
        eax = bb_entity_adjust(object, 0x111, -8u, 0);
        eax = bb_entity_adjust(other, 0x111, -8u, 0);
        eax = bb_entity_morale_shock(object);
      } else {
        response = BB_U32(profession == 1 ? 0x5c779c : 0x5f51e8);
        eax = bb_entity_adjust(object, 0x111, -4u, 0);
      }
    } else if (unhappy) {
      response = BB_U32(profession == 1 ? 0x6afbd4 : 0x63f024);
      eax = bb_entity_adjust(other, 0x111, -4u, 0);
    } else {
      int same_voice = BB_U32(0x60016c) == first;
      int32_t choice = bb_random() % 2;
      eax = choice;
      if (!choice)
        response = BB_U32(profession == 1 ? 0x63f048 : 0x5f51c4);
      else if (choice == 1)
        response = BB_U32(profession == 1 ? (same_voice ? 0x60247c : 0x60016c)
                                          : (same_voice ? 0x5c5b60 : 0x6a8f38));
      eax = bb_entity_adjust(object, 0x111, 4, 0);
    }
  } else if (profession == 5) {
    response = BB_U32(0x5e4760);
  }
  if (response)
    eax = bb_entity_feedback_queue(other, response, PTR(first));
  return eax;
}
