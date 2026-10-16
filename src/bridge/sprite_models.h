/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SPRITE_MODELS_H
#define BB_SPRITE_MODELS_H
#include "abi.h"
#include <stdint.h>
/* Each callback-visible buffer is borrowed from its own active frame. */
enum {
  BB_SPRITE_DESCRIPTOR_BYTES = 108,
  BB_SPRITE_PIXEL_BYTES = 112,
  BB_SPRITE_BITMAP_BYTES = 136,
  BB_SPRITE_LOAD_BYTES = 132,
  BB_SPRITE_RESTORE_BYTES = 128,
  BB_SPRITE_RECT_BYTES = 16,
  BB_SPRITE_BLEND_BYTES = 216,
  BB_SPRITE_FILENAME_BYTES = 256,
  BB_FONT_LOAD_BYTES = 32
};
uint32_t bb_sprite_blank_c(void *, int32_t, int32_t, uint32_t, uint32_t);
uint32_t bb_sprite_decode_c(void *, uint32_t);
uint32_t bb_sprite_load_c(void *, const char *, uint32_t);
uint32_t bb_surface_pixel_c(void *, uint32_t, uint32_t);
uint32_t bb_surface_color_key_c(void *, uint32_t, uint32_t);
void *bb_bitmap_load_c(void *, const char *, int32_t, int32_t, uint32_t *,
                       uint32_t *, uint32_t);
void *bb_sprite_create_file_c(void *, const char *, uint32_t, uint32_t);
uint32_t bb_sprite_remap_c(void *, uint32_t);
uint32_t bb_sprite_restore_mfb_c(void *, uint32_t);
uint32_t bb_bitmap_copy_c(void *, void *, int32_t, int32_t, uint32_t, uint32_t,
                          uint32_t);
uint32_t bb_sprite_clear_c(void *, uint32_t);
uint32_t bb_sprite_draw_c(void *, int32_t, int32_t, int32_t, uint32_t,
                          uint32_t);
uint32_t bb_sprite_cursor_erase_c(void *, uint32_t);
uint32_t bb_sprite_cursor_draw_c(void *, int32_t, int32_t, uint32_t, uint32_t);
uint32_t bb_sprite_blend_c(void *, void *, uint32_t, uint32_t, void *,
                           const int32_t *, uint32_t, unsigned char, uint32_t);
uint32_t bb_sprite_draw_region_c(void *, int32_t, int32_t, uint32_t, uint32_t,
                                 int32_t, int32_t, void *, uint32_t);
uint32_t bb_font_load_c(void *, void *, const char *, uint32_t);
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
const struct bb_model_target *bb_sprite_model_find(void *, uint32_t);
#endif
#endif
