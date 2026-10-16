/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_LIFECYCLE_MODELS_H
#define BB_LIFECYCLE_MODELS_H
#include "model_runtime.h"
uint32_t bb_lifecycle_code_identity(void (*)(void));
const struct bb_model_target *bb_lifecycle_model_find(void *, uint32_t);
/* Original logical identities for the typed static callback boundaries. */
enum {
  BB_GLOBAL_ID_bb_action_panel_identity_entry = 0x0041b1a0u,
  BB_GLOBAL_ID_bb_ambience_identity_entry = 0x0041bd90u,
  BB_GLOBAL_ID_bb_archive_destroy_entry = 0x00465ffbu,
  BB_GLOBAL_ID_bb_audio_manager_construct_entry = 0x00401db0u,
  BB_GLOBAL_ID_bb_audio_manager_walk_entry = 0x00401dc0u,
  BB_GLOBAL_ID_bb_build_menu_construct_entry = 0x0041a400u,
  BB_GLOBAL_ID_bb_building_panel_initialize_entry = 0x00427190u,
  BB_GLOBAL_ID_bb_building_stock_construct_entry = 0x0042a5c0u,
  BB_GLOBAL_ID_bb_cd_bank_construct_entry = 0x0046924eu,
  BB_GLOBAL_ID_bb_cursor_construct_entry = 0x004193d0u,
  BB_GLOBAL_ID_bb_demo_construct_entry = 0x00420a70u,
  BB_GLOBAL_ID_bb_display_construct_entry = 0x0042ec00u,
  BB_GLOBAL_ID_bb_display_destroy_entry = 0x0042ec30u,
  BB_GLOBAL_ID_bb_entity_manager_construct_entry = 0x00433870u,
  BB_GLOBAL_ID_bb_entity_manager_destroy_entry = 0x00433890u,
  BB_GLOBAL_ID_bb_faction_manager_construct_entry = 0x0041aec0u,
  BB_GLOBAL_ID_bb_feedback_construct_entry = 0x0041de10u,
  BB_GLOBAL_ID_bb_font_construct_entry = 0x004302c0u,
  BB_GLOBAL_ID_bb_font_destroy_entry = 0x004302f0u,
  BB_GLOBAL_ID_bb_highlight_construct_entry = 0x004208c0u,
  BB_GLOBAL_ID_bb_letters_construct_entry = 0x00421ea0u,
  BB_GLOBAL_ID_bb_letters_destroy_entry = 0x00421eb0u,
  BB_GLOBAL_ID_bb_message_construct_entry = 0x0041e2a0u,
  BB_GLOBAL_ID_bb_message_destructor_entry = 0x0041e2d0u,
  BB_GLOBAL_ID_bb_message_self_entry = 0x0041e6b0u,
  BB_GLOBAL_ID_bb_minimap_panel_initialize_entry = 0x00429460u,
  BB_GLOBAL_ID_bb_mission_description_construct_entry = 0x0042b370u,
  BB_GLOBAL_ID_bb_notification_clear_entry = 0x0041e090u,
  BB_GLOBAL_ID_bb_number_draw_entry = 0x0041c5b0u,
  BB_GLOBAL_ID_bb_numeric_panel_construct_entry = 0x0041c2b0u,
  BB_GLOBAL_ID_bb_panel_construct_entry = 0x0042b8b0u,
  BB_GLOBAL_ID_bb_render_queue_construct_entry = 0x00418720u,
  BB_GLOBAL_ID_bb_script_construct_entry = 0x00423560u,
  BB_GLOBAL_ID_bb_shared_construct_entry = 0x00401000u,
  BB_GLOBAL_ID_bb_shared_destroy_entry = 0x00401030u,
  BB_GLOBAL_ID_bb_short_object_reset_entry = 0x00421580u,
  BB_GLOBAL_ID_bb_stock_panel_construct_entry = 0x00429d50u,
  BB_GLOBAL_ID_bb_toolbar_construct_entry = 0x00419a70u,
  BB_GLOBAL_ID_bb_tooltip_construct_entry = 0x0042aa80u,
};
#endif
