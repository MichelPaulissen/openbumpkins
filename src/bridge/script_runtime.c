/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "script.h"
#include "game_calls.h"
#ifdef BB_MODEL_RUNTIME
#include "entity_calls.h"
#define bb_script_input_entry(p,t) BB_GAME_CALL(0x424c80,bb_address_encode(p),bb_address_encode(t))
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)

static uintptr_t entity(uint32_t id) { return BB_U32(0x00553f90) + id * 314u; }

/* 00422670: a script message uses the normal immediate command dispatcher. */
void BB_THISCALL bb_script_event_execute_entry(void *object) {
  bb_command_dispatch((void *)(uintptr_t)0x00513458,
                      (const void *)(uintptr_t)BB_U32(bb_address_encode(object) + 6));
}

/* 00423060: first satisfied condition selects a block in the live editor.
 * The old return is an incidental working value, not a Boolean. Keep it for
 * bridged callers while the branch and search ordering are implemented in C.
 */
uint32_t BB_THISCALL
bb_script_condition_evaluate_entry(void *object, uint32_t player, void *block) {
  uintptr_t node = bb_address_encode(object);
  uint32_t result = 0;
  do {
    int matched = 0;
    result = 0;
    if (BB_U32(node + 16)) {
      result = BYTE(node) - 1u;
      switch (BYTE(node)) {
      case 1:
        result = bb_address_encode(block);
        matched = BB_U32(node + 2) <=
                  BB_U32(0x004842c4) - BB_U32(bb_address_encode(block) + 20);
        break;
      case 2:
      case 3:
        result = BB_U32(node + 6);
        if (BYTE(node) == 2)
          matched = (int32_t)BB_U32(0x00555006 + BB_U32(node + 2) * 0xd00u) >
                    (int32_t)result;
        else
          matched = (int32_t)BB_U32(0x00555006 + BB_U32(node + 2) * 0xd00u) <
                    (int32_t)result;
        break;
      case 4:
      case 5:
        result = WORD(0x0055503a + BB_U32(node + 2) * 0xd00u +
                      BB_U32(node + 6) * 2u);
        matched = BYTE(node) == 4 ? result > BB_U32(node + 10)
                                  : result < BB_U32(node + 10);
        break;
      case 6:
        result = BB_U32(0x00553f90);
        matched = !BB_U32(entity(BB_U32(node + 2)));
        break;
      case 7: {
        uintptr_t target = entity(BB_U32(node + 2));
        result = BYTE(target + 0xd8);
        if (result == BB_U32(node + 6)) {
          result = BYTE(target + 0xd9);
          matched = result == BB_U32(node + 10);
        }
        break;
      }
      case 8:
        result = BB_U32(0x00553f90);
        matched = !BYTE(entity(BB_U32(node + 2)) + 0x111);
        break;
      case 9:
        result = BYTE(entity(BB_U32(node + 6)) + 0xf7);
        matched = result == BB_U32(node + 2);
        break;
      case 10:
        if (BB_U32(node + 2)) {
          uintptr_t a = entity(BB_U32(node + 2));
          uintptr_t b = entity(BB_U32(node + 6));
          result = bb_entity_distance(BYTE(a + 0xd8), BYTE(a + 0xd9),
                                      BYTE(b + 0xd8), BYTE(b + 0xd9));
          matched = (int32_t)result < 0x30000;
        }
        break;
      case 11:
        result = BB_U32(0x005e47b2 + BB_U32(node + 2) * 4u);
        matched = !!result == !!BB_U32(node + 6);
        if (!result)
          result = BB_U32(node + 6);
        break;
      case 12:
      case 13:
      case 14: {
        unsigned kind = BYTE(node);
        uint32_t tick = BB_U32(0x004842c4);
        result = tick / 36u;
        if (tick % 36u != kind - 12u)
          break;
        if (kind == 14 && !(result = BB_U32(0x00484288)))
          break;
        uintptr_t target = entity(BB_U32(node + (kind == 14 ? 2 : 6)));
        result = kind == 14 ? (uintptr_t)bb_entity_at((void *)(uintptr_t)0x00553f90,
                                                      BYTE(target + 0xd8),
                                                      BYTE(target + 0xd9))
                            : (uintptr_t)bb_entity_near((void *)(uintptr_t)0x00553f90,
                                                        BYTE(target + 0xd8),
                                                        BYTE(target + 0xd9), 3);
        while (result) {
          uintptr_t candidate = result;
          if (kind == 12)
            matched = BYTE(candidate + 0xf7) == BYTE(0x004843f0) &&
                      (BYTE(candidate + 0xd4) == BB_U32(node + 2) ||
                       BYTE(candidate + 0x11a) == BB_U32(node + 2));
          else if (kind == 13)
            matched = BYTE(candidate + 0xf4) == BB_U32(node + 2);
          else
            matched = BYTE(candidate + 0xd4) == 0x3a &&
                      (BB_U32(candidate + 0x9c) & 0x80000u);
          /* Even a matching candidate advances before selecting the block.
           * Reread the target record after each external search callback. */
          target = entity(BB_U32(node + (kind == 14 ? 2 : 6)));
          result = kind == 14
                       ? (uintptr_t)bb_entity_at_next(
                             (void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)candidate,
                             BYTE(target + 0xd8), BYTE(target + 0xd9))
                       : (uintptr_t)bb_entity_near_next(
                             (void *)(uintptr_t)0x00553f90, (void *)(uintptr_t)candidate,
                             BYTE(target + 0xd8), BYTE(target + 0xd9), 3);
          if (matched)
            break;
        }
        break;
      }
      }
    }
    if (matched)
      return bb_script_block_select_entry((void *)(uintptr_t)0x005e47b0, player,
                                          BYTE(node + 14));
    node = BB_U32(node + 20);
  } while (node);
  return result;
}

/* 00422b20: execute at most one due message, then test the condition chain.
 * Dispatch may move the live cursor, so read that cursor again afterwards. */
void BB_THISCALL bb_script_block_tick_entry(void *object,
                                                          uint32_t player) {
  uintptr_t self = bb_address_encode(object), event = BB_U32(self + 4);
  if (event && BB_U32(event + 2) <= BB_U32(0x004842c4) - BB_U32(self + 20)) {
    bb_script_event_execute_entry((void *)(uintptr_t)event);
    BB_U32(self + 4) = BB_U32(BB_U32(self + 4) + 10);
  }
  uintptr_t conditions = BB_U32(self + 12);
  if (conditions)
    bb_script_condition_evaluate_entry((void *)(uintptr_t)conditions, player, object);
}

/* 00426a60: each active player ticks its selected block and always block 31.
 * The fixed block is independent of a branch taken in the selected block. */
void BB_THISCALL bb_script_tick_entry(void *object) {
  uint32_t self = bb_address_encode(object);
  for (unsigned player = 0; player < 5; ++player)
    if (!BYTE(0x0055540c + player * 0xd00u)) {
      bb_script_block_tick_entry(
          (void *)(uintptr_t)(self + 0x88 + (player * 32 + BYTE(self + 0x83 + player)) * 28), player);
      bb_script_block_tick_entry((void *)(uintptr_t)(self + 0x3ec + player * 0x380), player);
    }
  if (BB_U32(self + 0x1518) && !--BB_U32(self + 0x1518)) {
    unsigned mission = BYTE(self + 0x1513) + (BB_U32(self + 0x1514) ? 0 : 1);
    BB_GAME_CALL(0x414a20,0,mission);
  }
}

/* 004268d0: the original accepts only these keys and leaves its input
 * unbounded. Enter delegates to the C input dispatcher before clearing it. */
uint32_t BB_THISCALL bb_script_key_entry(void *object,
                                                       uint32_t key) {
  uint32_t self = bb_address_encode(object);
  unsigned char character;
  if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || key == ' ' ||
      key == 0xba || key == 0xbc || key == 0xbd || key == 0xbe) {
    character = key == 0xba   ? ':'
                : key == 0xbc ? ','
                : key == 0xbd ? '-'
                : key == 0xbe ? '.'
                              : (unsigned char)key;
    BYTE(self + 0x140c + BB_U32(self + 0x150c)) = character;
    ++BB_U32(self + 0x150c);
    BYTE(self + 0x140c + BB_U32(self + 0x150c)) = 0;
  } else if (key == 8) {
    if (BB_U32(self + 0x150c)) {
      --BB_U32(self + 0x150c);
      BYTE(self + 0x140c + BB_U32(self + 0x150c)) = 0;
    }
  } else if (key == 13) {
    bb_script_input_entry(object, (char *)(uintptr_t)(self + 0x140c));
    BB_U32(self + 0x150c) = 0;
    BYTE(self + 0x140c) = 0;
  } else if (key == 0x7a) {
    BYTE(0x004843e8) = (BYTE(0x004843e8) + 1u) % 3u;
    bb_sample_play_entry(BB_U32(0x004842d8));
    BB_GAME_CALL(0x41ced0,BB_U32(0x4844a8));
  }
  return 1;
}
