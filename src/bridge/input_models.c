/* SPDX-License-Identifier: GPL-3.0-only */
#include "input_models.h"
#include "game_calls.h"
#include "interface_controls.h"
#include "controls.h"
#include "selection.h"
#include "dialog.h"
static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void cursor(uint32_t *r) { bb_cursor_select_entry(A(0), A(1), A(2)); }
static void toolbar_reset(uint32_t *r) { bb_toolbar_reset_entry(self(r)); }
static void can_work(uint32_t *r) { r[7] = bb_entity_can_work(self(r), A(0)); }
static void can_train(uint32_t *r) { r[7] = bb_entity_can_train(self(r), A(0)); }
static void tooltip_visibility(uint32_t *r) { bb_tooltip_visibility_entry(self(r), A(0)); }
static void tooltip_reset(uint32_t *r) { bb_tooltip_reset_entry(self(r)); }
static void numeric_set(uint32_t *r) { bb_numeric_panel_set_entry(self(r), A(0)); }
static void feedback_append(uint32_t *r) { bb_feedback_append_entry(self(r), (const char *)(uintptr_t)A(0)); }
static void feedback_draw(uint32_t *r) { bb_feedback_draw_entry(self(r)); }
static void feedback_tick(uint32_t *r) { bb_feedback_tick_entry(self(r)); }
static void direction(uint32_t *r) { r[7] = bb_direction_entry(A(0), A(1)); }
static void difference(uint32_t *r) { r[7] = bb_direction_difference_entry(A(0), A(1)); }
static const struct bb_model_target targets[] = {
    {0x00415280u, direction, 2, 0, 0},
    {0x00415330u, difference, 2, 0, 0},
    {0x0041de40u, feedback_append, 1, 0, 0},
    {0x0041ded0u, feedback_draw, 0, 0, 0},
    {0x0041e040u, feedback_tick, 0, 0, 0},
    {0x004193f0u, cursor, 3, 0, 0},
    {0x0041a080u, toolbar_reset, 0, 0, 0},
    {0x00454f40u, can_work, 1, 0, 0},
    {0x00454f80u, can_train, 1, 0, 0},
    {0x0042b220u, tooltip_visibility, 1, 0, 0},
    {0x0042b250u, tooltip_reset, 0, 0, 0},
    {0x0041c490u, numeric_set, 1, 0, 0},
};
const struct bb_model_target *bb_input_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
