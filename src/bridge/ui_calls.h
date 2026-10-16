/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_UI_CALLS_H
#define BB_UI_CALLS_H
#include "crt_output.h"
#include "game_file.h"
#define UI_WORD(v) bb_game_word((uintptr_t)(v))
#define C0(id) BB_GAME_CALL(id, 0)
#define C1(id, a0) BB_GAME_CALL(id, 0, UI_WORD(a0))
#define C2(id, a0, a1) BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1))
#define C3(id, a0, a1, a2)                                                     \
  BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1), UI_WORD(a2))
#define C4(id, a0, a1, a2, a3)                                                 \
  BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1), UI_WORD(a2), UI_WORD(a3))
#define C5(id, a0, a1, a2, a3, a4)                                             \
  BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1), UI_WORD(a2), UI_WORD(a3),      \
               UI_WORD(a4))
#define C6(id, a0, a1, a2, a3, a4, a5)                                         \
  BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1), UI_WORD(a2), UI_WORD(a3),      \
               UI_WORD(a4), UI_WORD(a5))
#define C7(id, a0, a1, a2, a3, a4, a5, a6)                                     \
  BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1), UI_WORD(a2), UI_WORD(a3),      \
               UI_WORD(a4), UI_WORD(a5), UI_WORD(a6))
#define C8(id, a0, a1, a2, a3, a4, a5, a6, a7)                                 \
  BB_GAME_CALL(id, 0, UI_WORD(a0), UI_WORD(a1), UI_WORD(a2), UI_WORD(a3),      \
               UI_WORD(a4), UI_WORD(a5), UI_WORD(a6), UI_WORD(a7))
#define M0(id, self) BB_GAME_CALL(id, UI_WORD(self))
#define M1(id, self, a0) BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0))
#define M2(id, self, a0, a1)                                                   \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1))
#define M3(id, self, a0, a1, a2)                                               \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1), UI_WORD(a2))
#define M4(id, self, a0, a1, a2, a3)                                           \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1), UI_WORD(a2),       \
               UI_WORD(a3))
#define M5(id, self, a0, a1, a2, a3, a4)                                       \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1), UI_WORD(a2),       \
               UI_WORD(a3), UI_WORD(a4))
#define M6(id, self, a0, a1, a2, a3, a4, a5)                                   \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1), UI_WORD(a2),       \
               UI_WORD(a3), UI_WORD(a4), UI_WORD(a5))
#define M7(id, self, a0, a1, a2, a3, a4, a5, a6)                               \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1), UI_WORD(a2),       \
               UI_WORD(a3), UI_WORD(a4), UI_WORD(a5), UI_WORD(a6))
#define M8(id, self, a0, a1, a2, a3, a4, a5, a6, a7)                           \
  BB_GAME_CALL(id, UI_WORD(self), UI_WORD(a0), UI_WORD(a1), UI_WORD(a2),       \
               UI_WORD(a3), UI_WORD(a4), UI_WORD(a5), UI_WORD(a6),             \
               UI_WORD(a7))
#define FORMAT_WORD(p) UI_WORD(p)
#define FORMAT(out, fmt, ...)                                                  \
  bb_crt_output_words(out, 256, fmt, (uint32_t[]){__VA_ARGS__},                \
                      sizeof((uint32_t[]){__VA_ARGS__}) / 4)
/* Unconverted gameplay children retain their real identities. These calls
 * fail explicitly until the corresponding C boundaries are available. */
#define bb_sample_play_entry(n) C1(0x415860, n)
#define bb_sound_event_entry(a, b) C2(0x459b80, a, b)
#define bb_sound_play_entry(n) C1(0x415730, n)
#define bb_message_append_entry(p, a, b) M2(0x41e310, p, a, b)
#define bb_entity_damage_entry(p, a, b) M2(0x4508a0, p, a, b)
#define bb_entity_idle_action(p) M0(0x455ae0, p)
#define bb_entity_cargo(p, a) M1(0x441600, p, a)
static inline uint32_t bb_ui_blockers(void *p, uint32_t delta, uint32_t eax) {
  return delta == 1 ? bb_entity_occupancy_add_c(p, eax, 0)
                    : bb_entity_occupancy_remove_c(p, eax, 0);
}
#define bb_entity_blockers_c bb_ui_blockers
static inline uint32_t bb_ui_feedback(void *object, void *sound, uint32_t force,
                                      uint32_t kind, uint32_t eax) {
  struct bb_call_state state = {eax, bb_address_encode(object), 0, 0x202};
  uint32_t args[] = {bb_address_encode(sound), force, kind};
  bb_model_invoke(&state, 0x453a00, args, 3);
  return state.eax;
}
#define bb_entity_feedback_c bb_ui_feedback
#endif
