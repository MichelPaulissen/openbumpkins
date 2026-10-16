/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ENTITY_CALLS_H
#define BB_ENTITY_CALLS_H
/* Calls across the packed game ABI carry logical DWORD references. Private
 * command packets are borrowed into the logical stack for the duration of the
 * call; ordinary C helpers continue to accept mapped native object pointers. */
#ifdef BB_MODEL_RUNTIME
#include "crt_output.h"
#include "game_calls.h"
#include "model_buffer.h"
#include <limits.h>
#include <math.h>
#include <signal.h>
static inline int64_t bb_entity_integer(long double value) {
  return !isfinite(value) || value < -0x1p63L || value >= 0x1p63L
             ? INT64_MIN
             : (int64_t)truncl(value);
}
static inline uint32_t bb_entity_packet(void *manager, const void *packet) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, packet, BB_U16((uintptr_t)packet + 2));
  uint32_t result =
      BB_GAME_CALL(0x4013d0, bb_address_encode(manager), buffer.reference);
  bb_model_buffer_end(&buffer, 1);
  return result;
}
#define bb_command_dispatch bb_entity_packet
#define bb_sample_play_entry(n) BB_GAME_CALL(0x415860, 0, n)
#define bb_sound_play_entry(n) BB_GAME_CALL(0x415730, 0, n)
#define bb_sample_position_entry(n, x, y) BB_GAME_CALL(0x415880, 0, n, x, y)
#define bb_sound_position_entry(n, x, y) BB_GAME_CALL(0x415740, 0, n, x, y)

#define EA_WORD(p) bb_game_word((uintptr_t)(p))
#define EA_FORMAT(out, fmt, ...)                                               \
  bb_crt_output_words(out, 256, fmt, (uint32_t[]){__VA_ARGS__},                \
                      sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#define BB_ENTITY_TARGET(name) BB_ENTITY_##name
#define BB_ENTITY_CALL(state, target, args, esp, edi, esi, ebp, ebx)           \
  bb_model_call_at(state, target, args, 6, esp, edi, esi, ebp, ebx)
#define BB_ENTITY_bb_direction_step_entry 0x4031a0u
#define BB_ENTITY_bb_entity_adjust_100_entry 0x454280u
#define BB_ENTITY_bb_entity_adjust_101_entry 0x4542d0u
#define BB_ENTITY_bb_entity_adjust_10d_entry 0x454140u
#define BB_ENTITY_bb_entity_adjust_111_entry 0x4540a0u
#define BB_ENTITY_bb_entity_adjust_11b_entry 0x454320u
#define BB_ENTITY_bb_entity_adjust_health_entry 0x453ee0u
#define BB_ENTITY_bb_entity_approach_entry 0x43fd10u
#define BB_ENTITY_bb_entity_area_damage_entry 0x434e60u
#define BB_ENTITY_bb_entity_attack_begin_entry 0x44f880u
#define BB_ENTITY_bb_entity_attack_range_entry 0x455c10u
#define BB_ENTITY_bb_entity_attack_tick_entry 0x44f900u
#define BB_ENTITY_bb_entity_command_entry 0x43f440u
#define BB_ENTITY_bb_entity_command_position 0x4559c0u
#define BB_ENTITY_bb_entity_command_sound 0x453a80u
#define BB_ENTITY_bb_entity_create 0x433ba0u
#define BB_ENTITY_bb_entity_damage_entry 0x4508a0u
#define BB_ENTITY_bb_entity_decision_tick 0x444150u
#define BB_ENTITY_bb_entity_destination_step 0x450660u
#define BB_ENTITY_bb_entity_dirty_entry 0x437ee0u
#define BB_ENTITY_bb_entity_distance 0x434de0u
#define BB_ENTITY_bb_entity_enemy_command 0x4473a0u
#define BB_ENTITY_bb_entity_evade_entry 0x455240u
#define BB_ENTITY_bb_entity_feedback_delay 0x453ae0u
#define BB_ENTITY_bb_entity_feedback_entry 0x453a00u
#define BB_ENTITY_bb_entity_force_dirty_entry 0x437f10u
#define BB_ENTITY_bb_entity_heal 0x450820u
#define BB_ENTITY_bb_entity_idle_sound 0x451b90u
#define BB_ENTITY_bb_entity_kind_tick_entry 0x442f40u
#define BB_ENTITY_bb_entity_morale_shock 0x447750u
#define BB_ENTITY_bb_entity_move_escape 0x43fb50u
#define BB_ENTITY_bb_entity_near 0x4340b0u
#define BB_ENTITY_bb_entity_near_next 0x434120u
#define BB_ENTITY_bb_entity_outer_at 0x434020u
#define BB_ENTITY_bb_entity_path_step 0x4503c0u
#define BB_ENTITY_bb_entity_place 0x43fa30u
#define BB_ENTITY_bb_entity_place_tile 0x43f970u
#define BB_ENTITY_bb_entity_profession 0x437f30u
#define BB_ENTITY_bb_entity_recruit_entry 0x445ff0u
#define BB_ENTITY_bb_entity_register_tiles 0x448490u
#define BB_ENTITY_bb_entity_release_tiles 0x448890u
#define BB_ENTITY_bb_entity_remove 0x4342c0u
#define BB_ENTITY_bb_entity_reset_move 0x441710u
#define BB_ENTITY_bb_entity_secondary_entry 0x4416d0u
#define BB_ENTITY_bb_entity_sort_entry 0x433d50u
#define BB_ENTITY_bb_entity_sound 0x453ab0u
#define BB_ENTITY_bb_entity_subtype 0x454ed0u
#define BB_ENTITY_bb_entity_subtype_clear_entry 0x454ec0u
#define BB_ENTITY_bb_entity_target_building 0x446dc0u
#define BB_ENTITY_bb_entity_target_moved_entry 0x455d50u
#define BB_ENTITY_bb_entity_transfer_entry 0x435070u
#define BB_ENTITY_bb_entity_treatment_fee 0x453450u
#define BB_ENTITY_bb_entity_update_tile 0x449a70u
#define BB_ENTITY_bb_entity_walk_entry 0x43fae0u
#define BB_ENTITY_bb_faction_attack_alert_entry 0x429190u
#define BB_ENTITY_bb_fixed_divide_entry 0x401d90u
#define BB_ENTITY_bb_fixed_multiply_entry 0x401d70u
#define BB_ENTITY_bb_notification_entity_entry 0x41e1f0u
#define BB_ENTITY_bb_random 0x46be20u
#define BB_ENTITY_bb_road_prune_near_entry 0x41ab80u
#define BB_ENTITY_bb_sample_play_entry 0x415860u
#define BB_ENTITY_bb_sound_play_entry 0x415730u
#define BB_ENTITY_bb_terrain_set_entry 0x421a80u
#else
#define EA_WORD(p) (p)
#define BB_ENTITY_TARGET(name) ((uintptr_t)(name))
#define BB_ENTITY_CALL(state, target, args, esp, edi, esi, ebp, ebx)           \
  bb_call_words6(state, BB_CODE(target), args)
#endif
#endif
