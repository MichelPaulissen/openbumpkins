/* SPDX-License-Identifier: GPL-3.0-only */
#include "mission_models.h"
#include "controls.h"
#include "coordinator.h"
#include "interop.h"
#include "letters.h"
#include "mission_io.h"
#include "script.h"
#include "selection_panels.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static uint32_t arithmetic_flags(uint32_t a, uint32_t b, unsigned subtract) {
  uint32_t v = subtract ? a - b : a + b;
  return (!v ? 0x40u : 0) | (v >> 31 ? 0x80u : 0) |
         (__builtin_parity(v & 255u) ? 0 : 4u) | ((a ^ b ^ v) & 16u) |
         (subtract ? a < b : (uint64_t)a + b > UINT32_MAX) |
         (((subtract ? a ^ b : ~(a ^ b)) & (a ^ v)) >> 31 ? 0x800u : 0);
}
static void faction_cash(uint32_t *r) {
  uint64_t value = bb_faction_cash_c((void *)(uintptr_t)r[6], A(0), A(1));
  r[7] = value;
  r[5] = value >> 32;
  r[6] = A(1);
  uint32_t before = (int32_t)r[6] < 0 ? r[5] + r[6] : r[5] - r[6];
  r[8] = (r[8] & ~0x8d5u) | arithmetic_flags(before, r[6], (int32_t)r[6] < 0);
}
static void faction_scroll(uint32_t *r) {
  uint64_t value = bb_faction_scroll_c((void *)(uintptr_t)r[6], A(0));
  r[7] = value;
  r[5] = value >> 32;
  r[8] = (r[8] & ~0x8d5u) | (!r[7] ? 0x40u : 0) | (r[7] >> 31 ? 0x80u : 0) |
         (__builtin_parity(r[7] & 255u) ? 0 : 4u);
}
static void faction_reset(uint32_t *r) {
  r[7] = bb_faction_manager_reset((void *)(uintptr_t)r[6]);
}
static void faction_buildable(uint32_t *r) {
  r[7] = bb_faction_buildable((void *)(uintptr_t)r[6]);
}
static void faction_count_add(uint32_t *r) {
  r[7] = bb_faction_count_add((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void faction_count_remove(uint32_t *r) {
  r[7] = bb_faction_count_remove((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void faction_tick(uint32_t *r) {
  r[7] = bb_faction_tick((void *)(uintptr_t)r[6]);
}
static void faction_enable(uint32_t *r) {
  r[7] = bb_faction_enable();
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void faction_initialize(uint32_t *r) {
  r[5] = r[6];
  bb_faction_initialize_c((void *)(uintptr_t)r[6], A(0));
  r[6] = 191;
  r[7] = 0;
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void save_encode(uint32_t *r) {
  r[7] = bb_save_encode((const unsigned char *)(uintptr_t)A(0), A(1), A(2),
                        (void *)(uintptr_t)A(3));
}
static void save_decode(uint32_t *r) {
  r[7] = bb_save_decode((unsigned char *)(uintptr_t)A(0), A(1), A(2),
                        (void *)(uintptr_t)A(3), !!BB_U32(0x4842a4));
}
static void relocate(uint32_t *r) {
  r[7] = BB_U32(A(0));
  if (r[7]) {
    r[7] = bb_entity_relocate((uint32_t *)(uintptr_t)A(0), A(1), &r[5]);
    r[6] = BB_U32(A(0));
  } else
    r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void screen_to_map(uint32_t *r) {
  bb_screen_to_map_entry(A(0), A(1), (int32_t *)(uintptr_t)A(2),
                         (int32_t *)(uintptr_t)A(3), A(4));
}
static void camera_tick(uint32_t *r) {
  (void)r;
  bb_camera_update_entry();
}
static void faction_subconstruct(uint32_t *r) { r[7] = r[6]; }
static void faction_construct(uint32_t *r) {
  r[7] = r[6];
  r[6] += 0x448;
  r[8] = (r[8] & ~0x8d5u) | 0x94u | ((uint64_t)r[7] + 0x450 > UINT32_MAX);
}
static void numeric_initialize(uint32_t *r) {
  bb_numeric_panel_initialize_entry((void *)(uintptr_t)r[6]);
}
static void ambience_initialize(uint32_t *r) {
  bb_ambience_initialize_entry((void *)(uintptr_t)r[6]);
}
static void action_initialize(uint32_t *r) {
  bb_action_panel_initialize_entry((void *)(uintptr_t)r[6]);
}
static void digit_position(uint32_t *r) {
  bb_digit_position_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void number_initialize(uint32_t *r) {
  bb_number_initialize_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void entity_data_construct(uint32_t *r) {
  r[7] = bb_entity_data_construct((void *)(uintptr_t)r[6]);
}
static void faction_recount(uint32_t *r) {
  uint64_t result = bb_faction_recount_c((void *)(uintptr_t)r[6]);
  r[7] = result;
  r[6] = result >> 32;
  r[5] = 0;
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void model_bb_clock_text_entry(uint32_t *r) {
  bb_clock_text_entry(A(0), (char *)(uintptr_t)A(1));
}
static void model_bb_interface_tick_entry(uint32_t *r) {
  (void)r;
  bb_interface_tick_entry();
}
static void model_bb_simulation_tick_entry(uint32_t *r) {
  (void)r;
  bb_simulation_tick_entry();
}
static void model_bb_frame_update_entry(uint32_t *r) {
  (void)r;
  bb_frame_update_entry();
}
static void model_bb_mission_intro_entry(uint32_t *r) {
  (void)r;
  bb_mission_intro_entry();
}
static void model_bb_mission_begin_entry(uint32_t *r) {
  bb_mission_begin_entry(A(0));
}
static void model_bb_mission_export_entry(uint32_t *r) {
  bb_mission_export_entry(A(0));
}
static void model_bb_world_refresh_entry(uint32_t *r) {
  (void)r;
  bb_world_refresh_entry();
}
static void model_bb_mission_refresh_entry(uint32_t *r) {
  (void)r;
  bb_mission_refresh_entry();
}
static void model_bb_mission_read_entry(uint32_t *r) {
  r[7] = bb_mission_read_entry((void *)(uintptr_t)A(0), A(1), A(2),
                               (void *)(uintptr_t)A(3));
}
static void model_bb_mission_decode_entry(uint32_t *r) {
  r[7] = bb_mission_decode_entry((void *)(uintptr_t)A(0), A(1), A(2),
                                 (void *)(uintptr_t)A(3));
}
static void model_bb_panel_write_entry(uint32_t *r) {
  bb_panel_write_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_panel_read_entry(uint32_t *r) {
  bb_panel_read_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_mission_save_entry(uint32_t *r) {
  r[7] = bb_mission_save_entry((const char *)(uintptr_t)A(0),
                               (const char *)(uintptr_t)A(1));
}
static void model_bb_mission_load_entry(uint32_t *r) {
  r[7] = bb_mission_load_entry((const char *)(uintptr_t)A(0), A(1));
}
static void model_bb_letter_archive_text_entry(uint32_t *r) {
  r[7] = bb_letter_archive_text_entry((void *)(uintptr_t)r[6],
                                      (const char *)(uintptr_t)A(0),
                                      (char *)(uintptr_t)A(1));
}
static void model_bb_letter_resolve_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_letter_resolve_entry(
      (void *)(uintptr_t)r[6], (const char *)(uintptr_t)A(0),
      (char *)(uintptr_t)A(1)));
}
static void model_bb_letter_post_entry(uint32_t *r) {
  bb_letter_post_entry((void *)(uintptr_t)r[6], A(0),
                       (const char *)(uintptr_t)A(1));
}
static void model_bb_letters_count_entry(uint32_t *r) {
  r[7] = bb_letters_count_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_letter_predecessor_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_letter_predecessor_entry(
      (void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0)));
}
static void model_bb_letter_select_entry(uint32_t *r) {
  bb_letter_select_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_letter_select_index_entry(uint32_t *r) {
  bb_letter_select_index_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_letter_delete_entry(uint32_t *r) {
  bb_letter_delete_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_letters_tick_entry(uint32_t *r) {
  bb_letters_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_letters_click_entry(uint32_t *r) {
  r[7] = bb_letters_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_letters_noop_entry(uint32_t *r) {
  r[7] = bb_letters_noop_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_letters_hover_entry(uint32_t *r) {
  r[7] = bb_letters_hover_entry((void *)(uintptr_t)r[6], A(0), A(1),
                                (char *)(uintptr_t)A(2));
}
static void model_bb_letters_draw_entry(uint32_t *r) {
  bb_letters_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_letters_read_entry(uint32_t *r) {
  bb_letters_read_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_letters_write_entry(uint32_t *r) {
  bb_letters_write_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_script_read_entry(uint32_t *r) {
  bb_script_read_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_script_write_entry(uint32_t *r) {
  bb_script_write_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_script_event_construct_entry(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_script_event_construct_entry((void *)(uintptr_t)r[6]));
}
static void model_bb_script_block_clear_entry(uint32_t *r) {
  bb_script_block_clear_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_event_sort_entry(uint32_t *r) {
  bb_script_event_sort_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_event_add_entry(uint32_t *r) {
  bb_script_event_add_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_script_condition_add_entry(uint32_t *r) {
  bb_script_condition_add_entry((void *)(uintptr_t)r[6],
                                (void *)(uintptr_t)A(0));
}
static void model_bb_script_event_remove_entry(uint32_t *r) {
  bb_script_event_remove_entry((void *)(uintptr_t)r[6],
                               (void *)(uintptr_t)A(0));
}
static void model_bb_script_condition_remove_entry(uint32_t *r) {
  bb_script_condition_remove_entry((void *)(uintptr_t)r[6],
                                   (void *)(uintptr_t)A(0));
}
static void model_bb_script_event_remove_index_entry(uint32_t *r) {
  bb_script_event_remove_index_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_script_condition_remove_index_entry(uint32_t *r) {
  bb_script_condition_remove_index_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_script_event_count_entry(uint32_t *r) {
  r[7] = bb_script_event_count_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_condition_count_entry(uint32_t *r) {
  r[7] = bb_script_condition_count_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_condition_construct_entry(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_script_condition_construct_entry((void *)(uintptr_t)r[6]));
}
static void model_bb_script_event_new_entry(uint32_t *r) {
  bb_script_event_new_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_script_condition_new_entry(uint32_t *r) {
  bb_script_condition_new_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_script_clear_entry(uint32_t *r) {
  bb_script_clear_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_rewind_entry(uint32_t *r) {
  bb_script_rewind_entry((void *)(uintptr_t)r[6]);
}
const struct bb_model_target *bb_mission_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x0041c2d0u, numeric_initialize, 0, 0, 0},
      {0x0041bda0u, ambience_initialize, 0, 0, 0},
      {0x0041b1b0u, action_initialize, 0, 0, 0},
      {0x0041c520u, digit_position, 2, 0, 0},
      {0x0041c5c0u, number_initialize, 2, 0, 0},
      {0x0041c290u, entity_data_construct, 0, 0, 0},
      {0x00429120u, faction_recount, 0, 0, 0},
      {0x00414be0u, screen_to_map, 5, 0, 0},
      {0x00414c90u, camera_tick, 0, 0, 0},
      {0x004274f0u, faction_subconstruct, 0, 0, 0},
      {0x00427500u, faction_construct, 0, 0, 0},
      {0x0041af40u, faction_reset, 0, 0, 0},
      {0x0041ced0u, faction_buildable, 0, 0, 0},
      {0x0041b0c0u, faction_count_add, 3, 0, 0},
      {0x0041b130u, faction_count_remove, 3, 0, 0},
      {0x0041afe0u, faction_tick, 0, 0, 0},
      {0x0041afc0u, faction_enable, 0, 0, 0},
      {0x0041aef0u, faction_cash, 2, 0, 0},
      {0x0041a9e0u, faction_scroll, 1, 0, 0},
      {0x00427a80u, faction_initialize, 1, 0, 0},
      {0x0042dde0u, save_encode, 4, 0, 0},
      {0x0042de80u, save_decode, 4, 0, 0},
      {0x00415d30u, relocate, 2, 0, 0},
      {0x00429250u, model_bb_clock_text_entry, 2, 0, 0},
      {0x0040ccc0u, model_bb_interface_tick_entry, 0, 0, 0},
      {0x0040cd60u, model_bb_simulation_tick_entry, 0, 0, 0},
      {0x0040bba0u, model_bb_frame_update_entry, 0, 0, 0},
      {0x00414990u, model_bb_mission_intro_entry, 0, 0, 0},
      {0x00414a20u, model_bb_mission_begin_entry, 1, 0, 0},
      {0x00414b10u, model_bb_mission_export_entry, 1, 0, 0},
      {0x00414b70u, model_bb_world_refresh_entry, 0, 0, 0},
      {0x00415d70u, model_bb_mission_refresh_entry, 0, 0, 0},
      {0x00415e30u, model_bb_mission_read_entry, 4, 0, 0},
      {0x00415e80u, model_bb_mission_decode_entry, 4, 0, 0},
      {0x0042d950u, model_bb_panel_write_entry, 1, 0, 0},
      {0x0042d9a0u, model_bb_panel_read_entry, 1, 0, 0},
      {0x004159c0u, model_bb_mission_save_entry, 2, 0, 0},
      {0x00415ed0u, model_bb_mission_load_entry, 2, 0, 0},
      {0x00421ef0u, model_bb_letter_archive_text_entry, 2, 0, 0},
      {0x00421f70u, model_bb_letter_resolve_entry, 2, 0, 0},
      {0x00422040u, model_bb_letter_post_entry, 2, 0, 0},
      {0x00422290u, model_bb_letters_count_entry, 0, 0, 0},
      {0x004222b0u, model_bb_letter_predecessor_entry, 1, 0, 0},
      {0x004222f0u, model_bb_letter_select_entry, 1, 0, 0},
      {0x00422320u, model_bb_letter_select_index_entry, 1, 0, 0},
      {0x004224e0u, model_bb_letter_delete_entry, 0, 0, 0},
      {0x00422350u, model_bb_letters_tick_entry, 0, 0, 0},
      {0x00422390u, model_bb_letters_click_entry, 2, 0, 0},
      {0x00422410u, model_bb_letters_noop_entry, 2, 0, 0},
      {0x00422420u, model_bb_letters_hover_entry, 3, 0, 0},
      {0x00422140u, model_bb_letters_draw_entry, 0, 0, 0},
      {0x00422530u, model_bb_letters_read_entry, 1, 0, 0},
      {0x004225b0u, model_bb_letters_write_entry, 1, 0, 0},
      {0x00426c20u, model_bb_script_read_entry, 1, 0, 0},
      {0x00426f50u, model_bb_script_write_entry, 1, 0, 0},
      {0x00422660u, model_bb_script_event_construct_entry, 0, 0, 0},
      {0x00422900u, model_bb_script_block_clear_entry, 0, 0, 0},
      {0x00422c10u, model_bb_script_event_sort_entry, 0, 0, 0},
      {0x00422b70u, model_bb_script_event_add_entry, 1, 0, 0},
      {0x00422c90u, model_bb_script_condition_add_entry, 1, 0, 0},
      {0x00422ba0u, model_bb_script_event_remove_entry, 1, 0, 0},
      {0x00422cb0u, model_bb_script_condition_remove_entry, 1, 0, 0},
      {0x00422be0u, model_bb_script_event_remove_index_entry, 1, 0, 0},
      {0x00422cf0u, model_bb_script_condition_remove_index_entry, 1, 0, 0},
      {0x00422d20u, model_bb_script_event_count_entry, 0, 0, 0},
      {0x00422d40u, model_bb_script_condition_count_entry, 0, 0, 0},
      {0x00422d60u, model_bb_script_condition_construct_entry, 0, 0, 0},
      {0x004237f0u, model_bb_script_event_new_entry, 1, 0, 0},
      {0x00423850u, model_bb_script_condition_new_entry, 1, 0, 0},
      {0x00426bd0u, model_bb_script_clear_entry, 0, 0, 0},
      {0x00426b90u, model_bb_script_rewind_entry, 0, 0, 0},
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; ++n)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
