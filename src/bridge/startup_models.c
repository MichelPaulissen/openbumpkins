/* SPDX-License-Identifier: GPL-3.0-only */
#include "startup_models.h"
#include "commands_models.h"
#include "dialog_models.h"
#include "entity_lifecycle_models.h"
#include "entity_query_models.h"
#include "indexed_text.h"
#include "input.h"
#include "input_models.h"
#include "interop.h"
#include "lifecycle_models.h"
#include "media_models.h"
#include "message_models.h"
#include "mission_models.h"
#include "movie_models.h"
#include "selection_models.h"
#include "entity_models.h"
#include "legacy_models.h"
#include "terrain_models.h"
#include "ui_models.h"
#include "world_models.h"
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
static void window_create(uint32_t *r) {
  r[7] = bb_game_window_create_c(
      (void *)(uintptr_t)arg(r, 0, BB_WINDOW_CLASS_BYTES),
      arg(r, 1, BB_WINDOW_CLASS_BYTES), r[3] + 4u);
}
static void main_entry(uint32_t *r) {
  r[7] = bb_game_main_c((void *)(uintptr_t)arg(r, 0, BB_STARTUP_BYTES),
                        (void *)(uintptr_t)arg(r, 1, BB_STARTUP_BYTES),
                        (const char *)(uintptr_t)arg(r, 2, BB_STARTUP_BYTES),
                        arg(r, 3, BB_STARTUP_BYTES), r[3] + 4u);
}
static void window_message(uint32_t *r) {
  r[7] = bb_game_window_entry((void *)(uintptr_t)arg(r, 0, 0), arg(r, 1, 0),
                              arg(r, 2, 0), arg(r, 3, 0));
}
static void progress(uint32_t *r) {
  if (!BB_I32(0x4843f4u))
    r[7] = 0;
  else
    bb_loading_progress();
}
static const struct bb_model_target *support(uint32_t id) {
  const struct bb_model_target *target = bb_lifecycle_model_find(NULL, id);
  if (!target)
    target = bb_input_model_find(NULL, id);
  if (!target)
    target = bb_entity_lifecycle_model_find(NULL, id);
  if (!target)
    target = bb_world_model_find(NULL, id);
  if (!target)
    target = bb_dialog_model_find(NULL, id);
  if (!target)
    target = bb_mission_model_find(NULL, id);
  if (!target)
    target = bb_ui_model_find(NULL, id);
  if (!target)
    target = bb_entity_query_model_find(NULL, id);
  if (!target)
    target = bb_terrain_model_find(NULL, id);
  if (!target)
    target = bb_movie_model_find(NULL, id);
  if (!target)
    target = bb_commands_model_find(NULL, id);
  if (!target)
    target = bb_message_model_find(NULL, id);
  if (!target)
    target = bb_selection_model_find(NULL, id);
  if (!target)target=bb_entity_model_find(NULL,id);
  if(!target)target=bb_legacy_model_find(NULL,id);
  return target ? target : bb_media_model_find(NULL, id);
}
const struct bb_model_target *bb_startup_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target procedure = {0x403b40u, window_message, 4,
                                                   0, 0};
  static const struct bb_model_target window = {0x40b8b0u, window_create, 2,
                                                BB_WINDOW_CLASS_BYTES, 0};
  static const struct bb_model_target main = {0x406b30u, main_entry, 4,
                                              BB_STARTUP_BYTES, 0};
  static const struct bb_model_target loading = {0x406a60u, progress, 0, 0, 0};
  static const struct bb_model_target text = {
      0x42a870u, bb_indexed_text_load_model, 1, BB_INDEXED_TEXT_BYTES, 0};
  return id == procedure.identity ? &procedure
         : id == window.identity  ? &window
         : id == main.identity    ? &main
         : id == loading.identity ? &loading
         : id == text.identity    ? &text
                                  : support(id);
}
