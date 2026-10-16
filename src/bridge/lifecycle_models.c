/* SPDX-License-Identifier: GPL-3.0-only */
#include "lifecycle_models.h"
#include "static_initializers.h"
#include "global_lifecycle.h"
#include "address.h"
#define GLOBAL(id, name) \
  static void model_##name(uint32_t *r) { (void)r; name(); }
BB_GLOBAL_LIFECYCLE_ENTRIES(GLOBAL)
#undef GLOBAL
#define INITIALIZER(id, name, child, callback) \
  static void model_##name(uint32_t *r) { r[7] = name(); }
BB_STATIC_INITIALIZERS(INITIALIZER)
#undef INITIALIZER
#define REGISTRATION(id, name, callback) \
  static void model_##name(uint32_t *r) { r[7] = name(); }
BB_STATIC_REGISTRATIONS(REGISTRATION)
#undef REGISTRATION
static void shared_destroy(uint32_t *r) { (void)r; bb_shared_destroy_entry(); }
static void entity_destroy(uint32_t *r) { (void)r; bb_entity_manager_destroy_entry(); }
#define OBJECT(name, fn) \
  static void name(uint32_t *r) { \
    r[7] = bb_address_encode(fn((void *)(uintptr_t)r[6])); \
  }
OBJECT(cd_handle, bb_cd_handle_construct_entry)
OBJECT(cd_track, bb_cd_track_construct_entry)
OBJECT(cd_tracks, bb_cd_tracks_construct_entry)
OBJECT(cd_bank, bb_cd_bank_construct_entry)
#undef OBJECT

#include "object_initializers.h"
static void model_bb_cursor_initialize_entry(uint32_t *r) { (void)r; bb_cursor_initialize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_build_menu_initialize_entry(uint32_t *r) { (void)r; bb_build_menu_initialize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_worker_menu_initialize_entry(uint32_t *r) { (void)r; bb_worker_menu_initialize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_cursor_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_cursor_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_toolbar_initialize_entry(uint32_t *r) { (void)r; bb_toolbar_initialize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_toolbar_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_toolbar_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_build_menu_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_build_menu_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_ambience_identity_entry(uint32_t *r) { r[7] = bb_address_encode(bb_ambience_identity_entry((void *)(uintptr_t)r[6])); }
static void model_bb_digit_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_digit_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_number_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_number_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_number_draw_entry(uint32_t *r) { (void)r; bb_number_draw_entry(); }
static void model_bb_numeric_panel_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_numeric_panel_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_building_stock_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_building_stock_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_minimap_panel_initialize_entry(uint32_t *r) { r[7] = bb_address_encode(bb_minimap_panel_initialize_entry((void *)(uintptr_t)r[6])); }
static void model_bb_stock_panel_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_stock_panel_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_mission_description_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_mission_description_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_mission_description_clear_entry(uint32_t *r) { (void)r; bb_mission_description_clear_entry((void *)(uintptr_t)r[6]); }
static void model_bb_action_panel_identity_entry(uint32_t *r) { r[7] = bb_address_encode(bb_action_panel_identity_entry((void *)(uintptr_t)r[6])); }
static void model_bb_building_panel_initialize_entry(uint32_t *r) { r[7] = bb_address_encode(bb_building_panel_initialize_entry((void *)(uintptr_t)r[6])); }
static void model_bb_tooltip_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_tooltip_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_tooltip_initialize_entry(uint32_t *r) { (void)r; bb_tooltip_initialize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_feedback_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_feedback_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_highlight_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_highlight_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_demo_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_demo_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_demo_clear_entry(uint32_t *r) { (void)r; bb_demo_clear_entry((void *)(uintptr_t)r[6]); }
static void model_bb_letters_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_letters_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_letters_clear_entry(uint32_t *r) { (void)r; bb_letters_clear_entry((void *)(uintptr_t)r[6]); }
static void model_bb_letters_destroy_entry(uint32_t *r) { (void)r; bb_letters_destroy_entry((void *)(uintptr_t)r[6]); }
static void model_bb_short_object_reset_entry(uint32_t *r) { r[7] = bb_address_encode(bb_short_object_reset_entry((void *)(uintptr_t)r[6])); }
static void model_bb_script_block_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_script_block_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_script_block_select_entry(uint32_t *r) { (void)r; bb_script_block_select_entry((void *)(uintptr_t)r[6], BB_U32(r[3]+8), BB_U32(r[3]+12)); }
static void model_bb_script_player_select_entry(uint32_t *r) { (void)r; bb_script_player_select_entry((void *)(uintptr_t)r[6], BB_U32(r[3]+8)); }
static void model_bb_script_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_script_construct_entry((void *)(uintptr_t)r[6])); }
static void model_bb_panel_localize_entry(uint32_t *r) { (void)r; bb_panel_localize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_panel_initialize_entry(uint32_t *r) { (void)r; bb_panel_initialize_entry((void *)(uintptr_t)r[6]); }
static void model_bb_panel_construct_entry(uint32_t *r) { r[7] = bb_address_encode(bb_panel_construct_entry((void *)(uintptr_t)r[6])); }
static void entity_construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_entity_manager_construct((void *)(uintptr_t)r[6]));
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void faction_construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_faction_manager_construct((void *)(uintptr_t)r[6]));
  r[6] = r[7] + 0x384c;
  r[8] = (r[8] & ~0x8d5u) | 0x94u | ((uint64_t)r[7] + 0x3854 > UINT32_MAX);
}

static const struct bb_model_target targets[] = {
  {0x0041e2d0u, bb_message_destructor_c, 0, 0, 0},
  {0x0041e2e0u, bb_message_destroy_c, 0, 0, 0},
  {0x004193e0u, model_bb_cursor_initialize_entry, 0, 0, 0},
  {0x0041a410u, model_bb_build_menu_initialize_entry, 0, 0, 0},
  {0x0042a310u, model_bb_worker_menu_initialize_entry, 0, 0, 0},
  {0x004193d0u, model_bb_cursor_construct_entry, 0, 0, 0},
  {0x00419a80u, model_bb_toolbar_initialize_entry, 0, 0, 0},
  {0x00419a70u, model_bb_toolbar_construct_entry, 0, 0, 0},
  {0x0041a400u, model_bb_build_menu_construct_entry, 0, 0, 0},
  {0x0041bd90u, model_bb_ambience_identity_entry, 0, 0, 0},
  {0x0041c510u, model_bb_digit_construct_entry, 0, 0, 0},
  {0x0041c590u, model_bb_number_construct_entry, 0, 0, 0},
  {0x0041c5b0u, model_bb_number_draw_entry, 0, 0, 0},
  {0x0041c2b0u, model_bb_numeric_panel_construct_entry, 0, 0, 0},
  {0x0042a5c0u, model_bb_building_stock_construct_entry, 0, 0, 0},
  {0x00429460u, model_bb_minimap_panel_initialize_entry, 0, 0, 0},
  {0x00429d50u, model_bb_stock_panel_construct_entry, 0, 0, 0},
  {0x0042b370u, model_bb_mission_description_construct_entry, 0, 0, 0},
  {0x0042b390u, model_bb_mission_description_clear_entry, 0, 0, 0},
  {0x0041b1a0u, model_bb_action_panel_identity_entry, 0, 0, 0},
  {0x00427190u, model_bb_building_panel_initialize_entry, 0, 0, 0},
  {0x0042aa80u, model_bb_tooltip_construct_entry, 0, 0, 0},
  {0x0042aa90u, model_bb_tooltip_initialize_entry, 0, 0, 0},
  {0x0041de10u, model_bb_feedback_construct_entry, 0, 0, 0},
  {0x004208c0u, model_bb_highlight_construct_entry, 0, 0, 0},
  {0x00420a70u, model_bb_demo_construct_entry, 0, 0, 0},
  {0x00420c90u, model_bb_demo_clear_entry, 0, 0, 0},
  {0x00421ea0u, model_bb_letters_construct_entry, 0, 0, 0},
  {0x00421ec0u, model_bb_letters_clear_entry, 0, 0, 0},
  {0x00421eb0u, model_bb_letters_destroy_entry, 0, 0, 0},
  {0x00421580u, model_bb_short_object_reset_entry, 0, 0, 0},
  {0x004228e0u, model_bb_script_block_construct_entry, 0, 0, 0},
  {0x00426b20u, model_bb_script_block_select_entry, 2, 0, 0},
  {0x00426b70u, model_bb_script_player_select_entry, 1, 0, 0},
  {0x00423560u, model_bb_script_construct_entry, 0, 0, 0},
  {0x0042b9f0u, model_bb_panel_localize_entry, 0, 0, 0},
  {0x0042b8c0u, model_bb_panel_initialize_entry, 0, 0, 0},
  {0x0042b8b0u, model_bb_panel_construct_entry, 0, 0, 0},
  {0x00433870u, entity_construct, 0, 0, 0},
  {0x0041aec0u, faction_construct, 0, 0, 0},
  {0x0041e2a0u, bb_message_construct_c, 0, 0, 0},
  {0x0041e6b0u, bb_message_self_c, 0, 0, 0},
  {0x0041e090u, bb_notification_clear_c, 0, 0, 0},

#define GLOBAL(id, name) {id, model_##name, 0, 0, 0},
BB_GLOBAL_LIFECYCLE_ENTRIES(GLOBAL)
#undef GLOBAL
#define INITIALIZER(id, name, child, callback) {id, model_##name, 0, 0, 0},
BB_STATIC_INITIALIZERS(INITIALIZER)
#undef INITIALIZER
#define REGISTRATION(id, name, callback) {id, model_##name, 0, 0, 0},
BB_STATIC_REGISTRATIONS(REGISTRATION)
#undef REGISTRATION
  {0x00401030u, shared_destroy, 0, 0, 0},
  {0x00433890u, entity_destroy, 0, 0, 0},
  {0x00466fe0u, cd_handle, 0, 0, 0},
  {0x00469d79u, cd_track, 0, 0, 0},
  {0x00469d0au, cd_tracks, 0, 0, 0},
  {0x0046924eu, cd_bank, 0, 0, 0},
};
const struct bb_model_target *bb_lifecycle_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof targets / sizeof *targets; i++)
    if (targets[i].identity == id)
      return &targets[i];
  return NULL;
}
uint32_t bb_lifecycle_code_identity(void (*callback)(void)) {
  if (!callback)
    return 0;
#define GLOBAL(id, name) if (callback == name) return id;
  BB_GLOBAL_LIFECYCLE_ENTRIES(GLOBAL)
#undef GLOBAL
  abort(); /* A static destructor must have a registered logical identity. */
}
