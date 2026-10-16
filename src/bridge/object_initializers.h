/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_OBJECT_INITIALIZERS_H
#define BB_OBJECT_INITIALIZERS_H
#include "abi.h"
#include <stdint.h>
void BB_THISCALL bb_cursor_initialize_entry(void *self);
void BB_THISCALL bb_build_menu_initialize_entry(void *self);
void BB_THISCALL bb_worker_menu_initialize_entry(void *self);
void *BB_THISCALL bb_cursor_construct_entry(void *p);
void BB_THISCALL bb_toolbar_initialize_entry(void *object);
void *BB_THISCALL bb_toolbar_construct_entry(void *p);
void *BB_THISCALL bb_build_menu_construct_entry(void *p);
void *BB_THISCALL bb_ambience_identity_entry(void *p);
void *BB_THISCALL bb_digit_construct_entry(void *object);
void *BB_THISCALL bb_number_construct_entry(void *object);
void bb_number_draw_entry(void);
void *BB_THISCALL bb_numeric_panel_construct_entry(void *object);
void *BB_THISCALL
bb_building_stock_construct_entry(void *object);
void *BB_THISCALL
bb_minimap_panel_initialize_entry(void *object);
void *BB_THISCALL bb_stock_panel_construct_entry(void *object);
void *BB_THISCALL
bb_mission_description_construct_entry(void *object);
void BB_THISCALL
bb_mission_description_clear_entry(void *object);
void *BB_THISCALL bb_action_panel_identity_entry(void *object);
void *BB_THISCALL
bb_building_panel_initialize_entry(void *object);
void *BB_THISCALL bb_tooltip_construct_entry(void *object);
void BB_THISCALL bb_tooltip_initialize_entry(void *object);
void *BB_THISCALL bb_feedback_construct_entry(void *object);
void *BB_THISCALL bb_highlight_construct_entry(void *object);
void *BB_THISCALL bb_demo_construct_entry(void *object);
void BB_THISCALL bb_demo_clear_entry(void *object);
void *BB_THISCALL bb_letters_construct_entry(void *object);
void BB_THISCALL bb_letters_clear_entry(void *object);
void BB_THISCALL bb_letters_destroy_entry(void *object);
void *BB_THISCALL bb_short_object_reset_entry(void *object);
void *BB_THISCALL bb_script_block_construct_entry(void *object);
uint32_t BB_THISCALL
bb_script_block_select_entry(void *object, uint32_t player_value,
                             uint32_t block_value);
void BB_THISCALL bb_script_player_select_entry(void *object,
                                                             uint32_t value);
void *BB_THISCALL bb_script_construct_entry(void *object);
void BB_THISCALL bb_panel_localize_entry(void *object);
void BB_THISCALL bb_panel_initialize_entry(void *object);
void *BB_THISCALL bb_panel_construct_entry(void *object);
void *BB_THISCALL bb_entity_manager_construct(void *object);
void *BB_THISCALL bb_faction_manager_construct(void *self);
void bb_message_construct_c(uint32_t input[9]);
void bb_message_self_c(uint32_t input[9]);
void bb_notification_clear_c(uint32_t input[9]);
void bb_message_destructor_c(uint32_t[9]);
void bb_message_destroy_c(uint32_t[9]);
#endif
