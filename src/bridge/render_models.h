/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RENDER_MODELS_H
#define BB_RENDER_MODELS_H
#include "abi.h"
#include <stdint.h>

/* Explicit volatile results shared by the original adapters and C frames. */
struct queue_result {
  uint32_t eax, edx;
};
uint32_t BB_THISCALL bb_render_queue_reset(void *);
void bb_render_queue_pending_clear_c(void *, uint32_t, uint32_t,
                                     struct queue_result *);
void bb_render_queue_insert_c(void *, uint32_t, uint32_t, int32_t, int32_t,
                              int32_t, int32_t, uint32_t,
                              struct queue_result *);
void bb_render_queue_both_c(void *, uint32_t, uint32_t, int32_t, int32_t,
                            int32_t, int32_t, struct queue_result *);
void bb_render_queue_pending_add_c(void *, uint32_t, uint32_t, int32_t, int32_t,
                                   int32_t, int32_t, uint32_t,
                                   struct queue_result *);
void bb_render_queue_translate_c(void *, uint32_t, uint32_t, uint32_t, uint32_t,
                                 struct queue_result *);
void bb_render_queue_merge_c(void *, uint32_t, uint32_t, struct queue_result *);

#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
/* World drawing, sprites and surfaces compose through ordinary C lookups.
 * Debug output/CRT and remaining platform services retain logical calls;
 * no native function address is packed into a game word. */
const struct bb_model_target *bb_render_model_find(void *, uint32_t);
uint32_t bb_render_model_service(uint32_t, uint32_t, const uint32_t *, size_t);
uint32_t bb_render_model_method(uint32_t, uint32_t, const uint32_t *, size_t);
void bb_display_scroll_model(uint32_t *);
const struct bb_model_target *bb_display_model_find(void *, uint32_t);
/* Callback-visible locals belong to each active logical frame. */
enum {
  BB_DISPLAY_SETUP_BYTES = 112,
  BB_PALETTE_LOAD_BYTES = 1084,
  BB_PALETTE_COLORS_BYTES = 1024,
  BB_PALETTE_FILENAME_BYTES = 256
};
void bb_display_primary_model(uint32_t *);
void bb_display_initialize_model(uint32_t *);
void bb_palette_load_model(uint32_t *);
void bb_palette_install_model(uint32_t *);
void bb_palette_black_model(uint32_t *);
void bb_palette_mix_model(uint32_t *);
void bb_palette_reflect_model(uint32_t *);
#endif
#endif
