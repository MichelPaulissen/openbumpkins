/* SPDX-License-Identifier: GPL-3.0-only */
#include "selection_models.h"
#include "interop.h"
#include "script.h"
#include "selection.h"
#include "selection_panels.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void model_bb_selection_action_entry(uint32_t *r) {
  (void)r;
  bb_selection_action_entry();
}
static void model_bb_selection_help_entry(uint32_t *r) {
  r[7] = bb_selection_help_entry((void *)(uintptr_t)r[6], A(0), A(1),
                                 (char *)(uintptr_t)A(2));
}
static void model_bb_tooltip_tick_entry(uint32_t *r) {
  bb_tooltip_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_action_panel_draw_entry(uint32_t *r) {
  bb_action_panel_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_action_panel_visibility_entry(uint32_t *r) {
  bb_action_panel_visibility_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_action_panel_tick_entry(uint32_t *r) {
  bb_action_panel_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_action_panel_expand_entry(uint32_t *r) {
  bb_action_panel_expand_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_action_panel_button_entry(uint32_t *r) {
  bb_action_panel_button_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_action_panel_spell_begin_entry(uint32_t *r) {
  bb_action_panel_spell_begin_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_action_panel_spell_cast_entry(uint32_t *r) {
  bb_action_panel_spell_cast_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void model_bb_action_panel_click_entry(uint32_t *r) {
  r[7] = bb_action_panel_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_building_panel_set_entry(uint32_t *r) {
  bb_building_panel_set_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_building_panel_draw_entry(uint32_t *r) {
  bb_building_panel_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_building_panel_noop_entry(uint32_t *r) {
  bb_building_panel_noop_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_building_panel_left_entry(uint32_t *r) {
  bb_building_panel_left_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_building_panel_right_entry(uint32_t *r) {
  bb_building_panel_right_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_building_panel_click_entry(uint32_t *r) {
  r[7] = bb_building_panel_click_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_tooltip_draw_entry(uint32_t *r) {
  bb_tooltip_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_panel_hover_text_entry(uint32_t *r) {
  bb_panel_hover_text_entry((void *)(uintptr_t)r[6], A(0), A(1),
                            (char *)(uintptr_t)A(2));
}
static void model_bb_panel_hover_reset_entry(uint32_t *r) {
  bb_panel_hover_reset_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_help_entry(uint32_t *r) {
  bb_script_help_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_script_help_next_entry(uint32_t *r) {
  bb_script_help_next_entry((void *)(uintptr_t)r[6]);
}
static const struct bb_model_target targets[] = {
    {0x420cc0, model_bb_selection_action_entry, 0, 0, 0},
    {0x41b950, model_bb_selection_help_entry, 3, 0, 0},
    {0x42ab40, model_bb_tooltip_tick_entry, 0, 0, 0},
    {0x41b1e0, model_bb_action_panel_draw_entry, 0, 0, 0},
    {0x41b500, model_bb_action_panel_visibility_entry, 1, 0, 0},
    {0x41b450, model_bb_action_panel_tick_entry, 0, 0, 0},
    {0x41b550, model_bb_action_panel_expand_entry, 1, 0, 0},
    {0x41b570, model_bb_action_panel_button_entry, 1, 0, 0},
    {0x41b590, model_bb_action_panel_spell_begin_entry, 1, 0, 0},
    {0x41b5d0, model_bb_action_panel_spell_cast_entry, 3, 0, 0},
    {0x41b710, model_bb_action_panel_click_entry, 2, 0, 0},
    {0x4271a0, model_bb_building_panel_set_entry, 1, 0, 0},
    {0x4271c0, model_bb_building_panel_draw_entry, 0, 0, 0},
    {0x427280, model_bb_building_panel_noop_entry, 0, 0, 0},
    {0x4274b0, model_bb_building_panel_left_entry, 2, 0, 0},
    {0x4274d0, model_bb_building_panel_right_entry, 2, 0, 0},
    {0x427290, model_bb_building_panel_click_entry, 2, 0, 0},
    {0x42aac0, model_bb_tooltip_draw_entry, 0, 0, 0},
    {0x42d2f0, model_bb_panel_hover_text_entry, 3, 0, 0},
    {0x42d770, model_bb_panel_hover_reset_entry, 0, 0, 0},
    {0x4238a0, model_bb_script_help_entry, 2, 0, 0},
    {0x424c70, model_bb_script_help_next_entry, 0, 0, 0},
};
const struct bb_model_target *bb_selection_model_find(void *unused,
                                                      uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
