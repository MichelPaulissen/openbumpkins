/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_models.h"
#include "command_builders.h"
#include "controls.h"
#include "interface_controls.h"
#include "interface_platform.h"
#include "interop.h"
#include "statistics.h"
#include "worker_runtime.h"
#include "world_controls.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void model_bb_interface_mouse_drain_entry(uint32_t *r) {
  bb_interface_mouse_drain_entry(A(0));
}
static void model_bb_minimap_panel_draw_entry(uint32_t *r) {
  bb_minimap_panel_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_minimap_panel_visibility_entry(uint32_t *r) {
  bb_minimap_panel_visibility_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_minimap_panel_noop_entry(uint32_t *r) {
  (void)r;
  bb_minimap_panel_noop_entry();
}
static void model_bb_minimap_panel_click_entry(uint32_t *r) {
  r[7] = bb_minimap_panel_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_season_tick_entry(uint32_t *r) {
  (void)r;
  bb_season_tick_entry();
}
static void model_bb_nearest_free_tile_entry(uint32_t *r) {
  r[7] = bb_nearest_free_tile_entry(A(0), A(1), (uint8_t *)(uintptr_t)A(2),
                                    (uint8_t *)(uintptr_t)A(3), A(4));
}
static void model_bb_panel_mode_entry(uint32_t *r) {
  bb_panel_mode_entry(A(0));
}
static void model_bb_mission_description_draw_entry(uint32_t *r) {
  bb_mission_description_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_mission_description_click_entry(uint32_t *r) {
  bb_mission_description_click_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_mission_description_tick_entry(uint32_t *r) {
  bb_mission_description_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_mission_description_load_entry(uint32_t *r) {
  bb_mission_description_load_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_game_resource_guard_entry(uint32_t *r) {
  (void)r;
  bb_game_resource_guard_entry();
}
static void model_bb_selection_border_entry(uint32_t *r) {
  bb_selection_border_entry(A(0), A(1), A(2), A(3));
}
static void model_bb_toolbar_draw_entry(uint32_t *r) {
  bb_toolbar_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_toolbar_action_entry(uint32_t *r) {
  bb_toolbar_action_entry(A(0));
}
static void model_bb_toolbar_click_entry(uint32_t *r) {
  r[7] = bb_toolbar_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_toolbar_tick_entry(uint32_t *r) {
  (void)r;
  bb_toolbar_tick_entry();
}
static void model_bb_build_menu_draw_entry(uint32_t *r) {
  bb_build_menu_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_build_menu_click_entry(uint32_t *r) {
  r[7] = bb_build_menu_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_build_menu_tick_entry(uint32_t *r) {
  bb_build_menu_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_build_menu_open_entry(uint32_t *r) {
  bb_build_menu_open_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_build_menu_close_entry(uint32_t *r) {
  bb_build_menu_close_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_interface_resources_refresh_entry(uint32_t *r) {
  (void)r;
  bb_interface_resources_refresh_entry();
}
static void model_bb_observer_display_refresh_entry(uint32_t *r) {
  (void)r;
  bb_observer_display_refresh_entry();
}
static void model_bb_camera_focus_entity_entry(uint32_t *r) {
  bb_camera_focus_entity_entry((void *)(uintptr_t)A(0));
}
static void model_bb_camera_focus_tile_entry(uint32_t *r) {
  bb_camera_focus_tile_entry(A(0), A(1));
}
static void model_bb_ambience_noop_entry(uint32_t *r) {
  (void)r;
  bb_ambience_noop_entry();
}
static void model_bb_ambience_tick_entry(uint32_t *r) {
  (void)r;
  bb_ambience_tick_entry();
}
static void model_bb_digit_set_entry(uint32_t *r) {
  bb_digit_set_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_digit_direction_entry(uint32_t *r) {
  r[7] = bb_digit_direction_entry(A(0), A(1));
}
static void model_bb_number_set_entry(uint32_t *r) {
  bb_number_set_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_number_adjust_entry(uint32_t *r) {
  bb_number_adjust_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_number_click_entry(uint32_t *r) {
  r[7] = bb_number_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_numeric_panel_draw_entry(uint32_t *r) {
  (void)r;
  bb_numeric_panel_draw_entry();
}
static void model_bb_numeric_panel_click_entry(uint32_t *r) {
  r[7] = bb_numeric_panel_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_numeric_panel_tick_entry(uint32_t *r) {
  bb_numeric_panel_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_editor_remove_entry(uint32_t *r) {
  bb_editor_remove_entry(A(0), A(1), A(2));
}
static void model_bb_editor_heights_copy_entry(uint32_t *r) {
  (void)r;
  bb_editor_heights_copy_entry();
}
static void model_bb_editor_height_relax_entry(uint32_t *r) {
  r[7] = bb_editor_height_relax_entry(A(0), A(1), A(2));
}
static void model_bb_editor_height_change_entry(uint32_t *r) {
  r[7] = bb_editor_height_change_entry(A(0), A(1), A(2));
}
static void model_bb_editor_height_probe_entry(uint32_t *r) {
  r[7] = bb_editor_height_probe_entry(A(0), A(1), A(2));
}
static void model_bb_editor_height_apply_entry(uint32_t *r) {
  r[7] = bb_editor_height_apply_entry(A(0), A(1), A(2));
}
static void model_bb_minimap_draw_entry(uint32_t *r) {
  bb_minimap_draw_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4), A(5));
}
static void model_bb_building_stock_set_entry(uint32_t *r) {
  bb_building_stock_set_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_building_stock_tick_entry(uint32_t *r) {
  (void)r;
  bb_building_stock_tick_entry();
}
static void model_bb_building_stock_draw_entry(uint32_t *r) {
  bb_building_stock_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_building_stock_click_entry(uint32_t *r) {
  r[7] = bb_building_stock_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_mission_theme_entry(uint32_t *r) {
  bb_mission_theme_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_mission_popup_draw_entry(uint32_t *r) {
  bb_mission_popup_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_mission_popup_visibility_entry(uint32_t *r) {
  bb_mission_popup_visibility_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_faction_statistics_entry(uint32_t *r) {
  bb_faction_statistics_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_statistics_graph_draw_entry(uint32_t *r) {
  bb_statistics_graph_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_statistics_draw_entry(uint32_t *r) {
  bb_statistics_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_statistics_tick_entry(uint32_t *r) {
  bb_statistics_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_statistics_click_entry(uint32_t *r) {
  r[7] = bb_statistics_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_statistics_history_entry(uint32_t *r) {
  bb_statistics_history_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_economy_draw_entry(uint32_t *r) {
  bb_economy_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_economy_click_entry(uint32_t *r) {
  r[7] = bb_economy_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_economy_drag_entry(uint32_t *r) {
  bb_economy_drag_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_random_event_entry(uint32_t *r) {
  (void)r;
  bb_random_event_entry();
}
static void model_bb_initial_buildings_entry(uint32_t *r) {
  bb_initial_buildings_entry(A(0), A(1));
}
static void model_bb_faction_music_reset_entry(uint32_t *r) {
  (void)r;
  bb_faction_music_reset_entry();
}
static void model_bb_faction_end_state_entry(uint32_t *r) {
  bb_faction_end_state_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_clock_text_parse_entry(uint32_t *r) {
  r[7] = bb_clock_text_parse_entry((const char *)(uintptr_t)A(0));
}
static void model_bb_stock_panel_animation_entry(uint32_t *r) {
  bb_stock_panel_animation_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_stock_panel_needs_entry(uint32_t *r) {
  bb_stock_panel_needs_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_stock_panel_tick_entry(uint32_t *r) {
  bb_stock_panel_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_stock_panel_click_entry(uint32_t *r) {
  r[7] = bb_stock_panel_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_economy_set_entry(uint32_t *r) {
  union {
    uint32_t word[2];
    double value;
  } d = {{A(1), A(2)}};
  bb_economy_set_entry((void *)(uintptr_t)r[6], A(0), d.value);
}
static void model_bb_command_walk_entry(uint32_t *r) {
  r[7] = bb_command_walk_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_command_target_entry(uint32_t *r) {
  r[7] = bb_command_target_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_command_build_entry(uint32_t *r) {
  r[7] = bb_command_build_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2), A(3),
                                A(4));
}
static void model_bb_command_terrain_entry(uint32_t *r) {
  r[7] = bb_command_terrain_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void model_bb_command_chat_entry(uint32_t *r) {
  r[7] = bb_command_chat_entry((void *)(uintptr_t)r[6],
                               (const char *)(uintptr_t)A(0));
}
static void model_bb_command_relationship_entry(uint32_t *r) {
  r[7] = bb_command_relationship_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_command_pause_entry(uint32_t *r) {
  r[7] = bb_command_pause_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_command_resume_entry(uint32_t *r) {
  r[7] = bb_command_resume_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_command_sync_request_entry(uint32_t *r) {
  r[7] = bb_command_sync_request_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_command_city_name_entry(uint32_t *r) {
  r[7] = bb_command_city_name_entry((void *)(uintptr_t)r[6], A(0),
                                    (const char *)(uintptr_t)A(1));
}
static void model_bb_command_activity_entry(uint32_t *r) {
  r[7] = bb_command_activity_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_command_spell_entry(uint32_t *r) {
  r[7] = bb_command_spell_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void model_bb_command_stat_entry(uint32_t *r) {
  r[7] = bb_command_stat_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_command_enqueue(uint32_t *r) {
  r[7] = bb_command_enqueue((void *)(uintptr_t)r[6],
                            (const void *)(uintptr_t)A(0));
}
extern void bb_toolbar_help_c(int32_t, int32_t, char *, int32_t);
extern uint32_t bb_stock_panel_draw_c(void *, uint32_t);
static void toolbar_help(uint32_t *r) {
  bb_toolbar_help_c(BB_U32(r[3] + 12), BB_U32(r[3] + 16),
                    (char *)(uintptr_t)BB_U32(r[3] + 20), BB_I32(r[3] + 4));
}
static void stock_draw(uint32_t *r) {
  r[7] = bb_stock_panel_draw_c((void *)(uintptr_t)r[6], BB_U32(r[3] + 4));
  BB_U32(r[3] + 4) = r[7];
}
static void model_bb_line_right_down_x_entry(uint32_t *r) {
  bb_line_right_down_x_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4),
                             A(5), A(6));
}
static void model_bb_line_right_down_y_entry(uint32_t *r) {
  bb_line_right_down_y_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4),
                             A(5), A(6));
}
static void model_bb_line_left_down_y_entry(uint32_t *r) {
  bb_line_left_down_y_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4),
                            A(5), A(6));
}
static void model_bb_line_left_down_x_entry(uint32_t *r) {
  bb_line_left_down_x_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4),
                            A(5), A(6));
}
static void model_bb_line_right_up_x_entry(uint32_t *r) {
  bb_line_right_up_x_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4),
                           A(5), A(6));
}
static void model_bb_line_right_up_y_entry(uint32_t *r) {
  bb_line_right_up_y_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4),
                           A(5), A(6));
}
static void model_bb_line_left_up_y_entry(uint32_t *r) {
  bb_line_left_up_y_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4), A(5),
                          A(6));
}
static void model_bb_line_left_up_x_entry(uint32_t *r) {
  bb_line_left_up_x_entry((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4), A(5),
                          A(6));
}
static void display_line(uint32_t *r) {
  r[7] = bb_display_line_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0),
                               A(1), A(2), A(3), A(4), A(5));
}
const struct bb_model_target *bb_ui_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x433190, model_bb_line_right_down_x_entry, 7, 0, 0},
      {0x433220, model_bb_line_right_down_y_entry, 7, 0, 0},
      {0x4332b0, model_bb_line_left_down_y_entry, 7, 0, 0},
      {0x433340, model_bb_line_left_down_x_entry, 7, 0, 0},
      {0x4333d0, model_bb_line_right_up_x_entry, 7, 0, 0},
      {0x433460, model_bb_line_right_up_y_entry, 7, 0, 0},
      {0x4334f0, model_bb_line_left_up_y_entry, 7, 0, 0},
      {0x433580, model_bb_line_left_up_x_entry, 7, 0, 0},
      {0x433610, display_line, 6, 0, 0},
      {0x00403270u, model_bb_interface_mouse_drain_entry, 1, 0, 0},
      {0x00429480u, model_bb_minimap_panel_draw_entry, 0, 0, 0},
      {0x00429570u, model_bb_minimap_panel_visibility_entry, 1, 0, 0},
      {0x004295a0u, model_bb_minimap_panel_noop_entry, 0, 0, 0},
      {0x004295b0u, model_bb_minimap_panel_click_entry, 2, 0, 0},
      {0x004296b0u, model_bb_season_tick_entry, 0, 0, 0},
      {0x004299c0u, model_bb_nearest_free_tile_entry, 5, 0, 0},
      {0x0042b260u, model_bb_panel_mode_entry, 1, 0, 0},
      {0x0042b3c0u, model_bb_mission_description_draw_entry, 0, 0, 0},
      {0x0042b400u, model_bb_mission_description_click_entry, 0, 0, 0},
      {0x0042b430u, model_bb_mission_description_tick_entry, 0, 0, 0},
      {0x0042b480u, model_bb_mission_description_load_entry, 1, 0, 0},
      {0x0042ea50u, model_bb_game_resource_guard_entry, 0, 0, 0},
      {0x00415360u, model_bb_selection_border_entry, 4, 0, 0},
      {0x00419aa0u, model_bb_toolbar_draw_entry, 0, 0, 0},
      {0x00419cd0u, model_bb_toolbar_action_entry, 1, 0, 0},
      {0x00419d80u, model_bb_toolbar_click_entry, 2, 0, 0},
      {0x0041a0a0u, model_bb_toolbar_tick_entry, 0, 0, 0},
      {0x0041a430u, model_bb_build_menu_draw_entry, 0, 0, 0},
      {0x0041a7b0u, model_bb_build_menu_click_entry, 2, 0, 0},
      {0x0041a930u, model_bb_build_menu_tick_entry, 0, 0, 0},
      {0x0041a990u, model_bb_build_menu_open_entry, 0, 0, 0},
      {0x0041a9c0u, model_bb_build_menu_close_entry, 0, 0, 0},
      {0x0041aa30u, model_bb_interface_resources_refresh_entry, 0, 0, 0},
      {0x0041ab20u, model_bb_observer_display_refresh_entry, 0, 0, 0},
      {0x0041ad70u, model_bb_camera_focus_entity_entry, 1, 0, 0},
      {0x0041ae20u, model_bb_camera_focus_tile_entry, 2, 0, 0},
      {0x0041c2a0u, model_bb_ambience_noop_entry, 0, 0, 0},
      {0x0041bdc0u, model_bb_ambience_tick_entry, 0, 0, 0},
      {0x0041c540u, model_bb_digit_set_entry, 1, 0, 0},
      {0x0041c550u, model_bb_digit_direction_entry, 2, 0, 0},
      {0x0041c600u, model_bb_number_set_entry, 1, 0, 0},
      {0x0041c730u, model_bb_number_adjust_entry, 1, 0, 0},
      {0x0041c7a0u, model_bb_number_click_entry, 2, 0, 0},
      {0x0041c360u, model_bb_numeric_panel_draw_entry, 0, 0, 0},
      {0x0041c370u, model_bb_numeric_panel_click_entry, 2, 0, 0},
      {0x0041c400u, model_bb_numeric_panel_tick_entry, 0, 0, 0},
      {0x0041c8e0u, model_bb_editor_remove_entry, 3, 0, 0},
      {0x0041c9e0u, model_bb_editor_heights_copy_entry, 0, 0, 0},
      {0x0041ccf0u, model_bb_editor_height_relax_entry, 3, 0, 0},
      {0x0041cc30u, model_bb_editor_height_change_entry, 3, 0, 0},
      {0x0041ca10u, model_bb_editor_height_probe_entry, 3, 0, 0},
      {0x0041cab0u, model_bb_editor_height_apply_entry, 3, 0, 0},
      {0x0041d670u, model_bb_minimap_draw_entry, 6, 0, 0},
      {0x0042a5d0u, model_bb_building_stock_set_entry, 1, 0, 0},
      {0x0042a7d0u, model_bb_building_stock_tick_entry, 0, 0, 0},
      {0x0042a5f0u, model_bb_building_stock_draw_entry, 0, 0, 0},
      {0x0042a7e0u, model_bb_building_stock_click_entry, 2, 0, 0},
      {0x0042b5a0u, model_bb_mission_theme_entry, 1, 0, 0},
      {0x0042b710u, model_bb_mission_popup_draw_entry, 0, 0, 0},
      {0x0042b740u, model_bb_mission_popup_visibility_entry, 1, 0, 0},
      {0x0042b7b0u, model_bb_faction_statistics_entry, 0, 0, 0},
      {0x0042bff0u, model_bb_statistics_graph_draw_entry, 0, 0, 0},
      {0x0042c420u, model_bb_statistics_draw_entry, 0, 0, 0},
      {0x0042c450u, model_bb_statistics_tick_entry, 0, 0, 0},
      {0x0042c480u, model_bb_statistics_click_entry, 2, 0, 0},
      {0x0042c5b0u, model_bb_statistics_history_entry, 0, 0, 0},
      {0x0042cad0u, model_bb_economy_draw_entry, 0, 0, 0},
      {0x0042d070u, model_bb_economy_click_entry, 2, 0, 0},
      {0x0042d1f0u, model_bb_economy_drag_entry, 2, 0, 0},
      {0x004202c0u, model_bb_random_event_entry, 0, 0, 0},
      {0x004214f0u, model_bb_initial_buildings_entry, 2, 0, 0},
      {0x0042e0d0u, model_bb_faction_music_reset_entry, 0, 0, 0},
      {0x00428ce0u, model_bb_faction_end_state_entry, 1, 0, 0},
      {0x004292b0u, model_bb_clock_text_parse_entry, 1, 0, 0},
      {0x0042a450u, model_bb_stock_panel_animation_entry, 1, 0, 0},
      {0x0042a340u, model_bb_stock_panel_needs_entry, 0, 0, 0},
      {0x0042a1b0u, model_bb_stock_panel_tick_entry, 0, 0, 0},
      {0x0042a480u, model_bb_stock_panel_click_entry, 2, 0, 0},
      {0x0042d780u, model_bb_economy_set_entry, 3, 0, 0},
      {0x0042da00u, model_bb_command_walk_entry, 2, 0, 0},
      {0x0042da50u, model_bb_command_target_entry, 1, 0, 0},
      {0x0042da90u, model_bb_command_build_entry, 5, 0, 0},
      {0x0042dae0u, model_bb_command_terrain_entry, 3, 0, 0},
      {0x0042db30u, model_bb_command_chat_entry, 1, 0, 0},
      {0x0042db90u, model_bb_command_relationship_entry, 2, 0, 0},
      {0x0042dbd0u, model_bb_command_pause_entry, 0, 0, 0},
      {0x0042dc00u, model_bb_command_resume_entry, 0, 0, 0},
      {0x0042dc30u, model_bb_command_sync_request_entry, 0, 0, 0},
      {0x0042dca0u, model_bb_command_city_name_entry, 2, 0, 0},
      {0x0042dd00u, model_bb_command_activity_entry, 2, 0, 0},
      {0x0042dd40u, model_bb_command_spell_entry, 3, 0, 0},
      {0x0042dd90u, model_bb_command_stat_entry, 2, 0, 0},
      {0x00401200u, model_bb_command_enqueue, 1, 0, 0},
      {0x41a0b0, toolbar_help, 3, 4, 0},
      {0x429d60, stock_draw, 0, 0x114, 0},
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
