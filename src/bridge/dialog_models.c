/* SPDX-License-Identifier: GPL-3.0-only */
#include "dialog_models.h"
#include "dialog.h"
#include "interop.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void entry_0(uint32_t *r) {
  bb_mouse_map_position_entry(A(0), A(1), A(2), A(3), (void *)(uintptr_t)A(4),
                              (void *)(uintptr_t)A(5));
}
static void entry_1(uint32_t *r) {
  bb_dialog_build_entry((void *)(uintptr_t)r[6], A(0));
}
static void entry_2(uint32_t *r) {
  bb_dialog_select_entry((void *)(uintptr_t)r[6], A(0));
}
static void entry_3(uint32_t *r) {
  bb_dialog_draw_entry((void *)(uintptr_t)r[6]);
}
static void entry_4(uint32_t *r) {
  bb_dialog_tick_entry((void *)(uintptr_t)r[6]);
}
static void entry_5(uint32_t *r) {
  r[7] = bb_dialog_key_entry((void *)(uintptr_t)r[6], A(0));
}
static void entry_6(uint32_t *r) {
  r[7] = bb_dialog_activate_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void entry_7(uint32_t *r) {
  r[7] = bb_dialog_hover_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void entry_8(uint32_t *r) {
  (void)r;
  bb_save_slots_refresh_entry();
}
static void entry_9(uint32_t *r) {
  bb_dialog_visibility_entry((void *)(uintptr_t)r[6], A(0));
}
static void entry_10(uint32_t *r) {
  bb_dialog_command_entry((void *)(uintptr_t)r[6]);
}
static void entry_11(uint32_t *r) {
  bb_highlight_draw_entry((void *)(uintptr_t)r[6]);
}
static void entry_12(uint32_t *r) {
  bb_highlight_tick_entry((void *)(uintptr_t)r[6]);
}
static void entry_13(uint32_t *r) {
  bb_highlight_region_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2), A(3));
}
static void entry_14(uint32_t *r) {
  bb_highlight_enabled_entry((void *)(uintptr_t)r[6], A(0));
}
static void entry_15(uint32_t *r) {
  bb_demo_rewind_entry((void *)(uintptr_t)r[6]);
}
static void entry_16(uint32_t *r) {
  bb_demo_load_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void entry_17(uint32_t *r) {
  bb_demo_save_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void entry_18(uint32_t *r) {
  bb_demo_append_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void entry_19(uint32_t *r) {
  bb_demo_pump_entry((void *)(uintptr_t)r[6]);
}
const struct bb_model_target *bb_dialog_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x0041dd80u, entry_0, 6, 0, 0},  /* bb_mouse_map_position_entry */
      {0x0041ed10u, entry_1, 1, 0, 0},  /* bb_dialog_build_entry */
      {0x0041f7a0u, entry_2, 1, 0, 0},  /* bb_dialog_select_entry */
      {0x0041e700u, entry_3, 0, 0, 0},  /* bb_dialog_draw_entry */
      {0x0041e9c0u, entry_4, 0, 0, 0},  /* bb_dialog_tick_entry */
      {0x0041eab0u, entry_5, 1, 0, 0},  /* bb_dialog_key_entry */
      {0x0041ec60u, entry_6, 2, 0, 0},  /* bb_dialog_activate_entry */
      {0x0041ec90u, entry_7, 2, 0, 0},  /* bb_dialog_hover_entry */
      {0x00420180u, entry_8, 0, 0, 0},  /* bb_save_slots_refresh_entry */
      {0x0041ffc0u, entry_9, 1, 0, 0},  /* bb_dialog_visibility_entry */
      {0x0041f810u, entry_10, 0, 0, 0}, /* bb_dialog_command_entry */
      {0x00420900u, entry_11, 0, 0, 0}, /* bb_highlight_draw_entry */
      {0x00420950u, entry_12, 0, 0, 0}, /* bb_highlight_tick_entry */
      {0x00420a00u, entry_13, 4, 0, 0}, /* bb_highlight_region_entry */
      {0x00420a60u, entry_14, 1, 0, 0}, /* bb_highlight_enabled_entry */
      {0x00420ca0u, entry_15, 0, 0, 0}, /* bb_demo_rewind_entry */
      {0x00420a90u, entry_16, 1, 0, 0}, /* bb_demo_load_entry */
      {0x00420b30u, entry_17, 1, 0, 0}, /* bb_demo_save_entry */
      {0x00420bc0u, entry_18, 3, 0, 0}, /* bb_demo_append_entry */
      {0x00420c20u, entry_19, 0, 0, 0}, /* bb_demo_pump_entry */
  };
  for (unsigned i = 0; i < sizeof targets / sizeof *targets; i++)
    if (targets[i].identity == id)
      return targets + i;
  return NULL;
}
