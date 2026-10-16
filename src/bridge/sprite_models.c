/* SPDX-License-Identifier: GPL-3.0-only */
#include "sprite_models.h"
#include "render_calls.h"

void *bb_sprite_construct(void *, void *);
uint32_t bb_sprite_remove(void *, void *);
uint32_t bb_sprite_anchor(void *, uint32_t, uint32_t);
uint32_t bb_sprite_center(void *);
void *bb_font_construct(void *);
uint32_t bb_font_destroy(void *);
static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}

static void sprite_blank(uint32_t *r) {
  r[7] = bb_sprite_blank_c(self(r), arg(r, 0, BB_SPRITE_DESCRIPTOR_BYTES),
                           arg(r, 1, BB_SPRITE_DESCRIPTOR_BYTES),
                           arg(r, 2, BB_SPRITE_DESCRIPTOR_BYTES), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_blank(void *object, int32_t width,
                                     int32_t height, uint32_t transparent) {
  return bb_render_call(
      0x004313e0u, bb_address_encode(object),
      (uint32_t[]){(uint32_t)width, (uint32_t)height, (uint32_t)transparent},
      3);
}

static void sprite_decode(uint32_t *r) {
  r[7] = bb_sprite_decode_c(self(r), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_decode(void *object) {
  return bb_render_call(0x0042fb50u, bb_address_encode(object), NULL, 0);
}

static void sprite_load(uint32_t *r) {
  r[7] = bb_sprite_load_c(
      self(r), (const char *)(uintptr_t)arg(r, 0, BB_SPRITE_LOAD_BYTES),
      r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_load(void *object, const char *filename) {
  return bb_render_call(0x00431500u, bb_address_encode(object),
                        (uint32_t[]){bb_address_encode(filename)}, 1);
}

static void surface_pixel(uint32_t *r) {
  r[7] = bb_surface_pixel_c((void *)(uintptr_t)arg(r, 0, BB_SPRITE_PIXEL_BYTES),
                            arg(r, 1, BB_SPRITE_PIXEL_BYTES), r[3] + 4u);
}
uint32_t bb_surface_pixel(void *surface, uint32_t color) {
  return bb_render_call(
      0x004312d0u, 0, (uint32_t[]){bb_address_encode(surface), (uint32_t)color},
      2);
}

static void surface_color_key(uint32_t *r) {
  r[7] = bb_surface_color_key_c((void *)(uintptr_t)arg(r, 0, 8), arg(r, 1, 8),
                                r[3] + 4u);
}
uint32_t bb_surface_color_key(void *surface, uint32_t color) {
  return bb_render_call(
      0x004313a0u, 0, (uint32_t[]){bb_address_encode(surface), (uint32_t)color},
      2);
}

static void bitmap_load(uint32_t *r) {
  r[7] = bb_address_encode(bb_bitmap_load_c(
      (void *)(uintptr_t)arg(r, 0, BB_SPRITE_BITMAP_BYTES),
      (const char *)(uintptr_t)arg(r, 1, BB_SPRITE_BITMAP_BYTES),
      arg(r, 2, BB_SPRITE_BITMAP_BYTES), arg(r, 3, BB_SPRITE_BITMAP_BYTES),
      (uint32_t *)(uintptr_t)arg(r, 4, BB_SPRITE_BITMAP_BYTES),
      (uint32_t *)(uintptr_t)arg(r, 5, BB_SPRITE_BITMAP_BYTES), r[3] + 4u));
}
void *bb_bitmap_load(void *draw, const char *name, int32_t width,
                     int32_t height, uint32_t *actual_width,
                     uint32_t *actual_height) {
  return (void *)(uintptr_t)bb_render_call(
      0x00430f00u, 0,
      (uint32_t[]){bb_address_encode(draw), bb_address_encode(name),
                   (uint32_t)width, (uint32_t)height,
                   bb_address_encode(actual_width),
                   bb_address_encode(actual_height)},
      6);
}

static void sprite_create_file(uint32_t *r) {
  r[7] = bb_address_encode(bb_sprite_create_file_c(
      self(r), (const char *)(uintptr_t)arg(r, 0, BB_SPRITE_FILENAME_BYTES),
      arg(r, 1, BB_SPRITE_FILENAME_BYTES), r[3] + 4u));
}
void *BB_THISCALL bb_sprite_create_file(void *object, const char *name,
                                        uint32_t transparent) {
  return (void *)(uintptr_t)bb_render_call(
      0x0042f320u, bb_address_encode(object),
      (uint32_t[]){bb_address_encode(name), (uint32_t)transparent}, 2);
}

static void sprite_remap(uint32_t *r) {
  r[7] = bb_sprite_remap_c(self(r), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_remap(void *object) {
  return bb_render_call(0x00430160u, bb_address_encode(object), NULL, 0);
}

static void sprite_restore_mfb(uint32_t *r) {
  r[7] = bb_sprite_restore_mfb_c(self(r), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_restore_mfb(void *object) {
  return bb_render_call(0x004318c0u, bb_address_encode(object), NULL, 0);
}

static void bitmap_copy(uint32_t *r) {
  r[7] = bb_bitmap_copy_c((void *)(uintptr_t)arg(r, 0, BB_SPRITE_BITMAP_BYTES),
                          (void *)(uintptr_t)arg(r, 1, BB_SPRITE_BITMAP_BYTES),
                          arg(r, 2, BB_SPRITE_BITMAP_BYTES),
                          arg(r, 3, BB_SPRITE_BITMAP_BYTES),
                          arg(r, 4, BB_SPRITE_BITMAP_BYTES),
                          arg(r, 5, BB_SPRITE_BITMAP_BYTES), r[3] + 4u);
}
uint32_t bb_bitmap_copy(void *surface, void *bitmap, int32_t x, int32_t y,
                        uint32_t width, uint32_t height) {
  return bb_render_call(
      0x00431020u, 0,
      (uint32_t[]){bb_address_encode(surface), bb_address_encode(bitmap),
                   (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height},
      6);
}

static void sprite_clear(uint32_t *r) {
  r[7] = bb_sprite_clear_c(self(r), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_clear(void *object) {
  return bb_render_call(0x0042f5f0u, bb_address_encode(object), NULL, 0);
}

static void sprite_draw(uint32_t *r) {
  r[7] = bb_sprite_draw_c(self(r), arg(r, 0, BB_SPRITE_RECT_BYTES),
                          arg(r, 1, BB_SPRITE_RECT_BYTES),
                          arg(r, 2, BB_SPRITE_RECT_BYTES),
                          arg(r, 3, BB_SPRITE_RECT_BYTES), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_draw(void *object, int32_t x, int32_t y,
                                    int32_t frame, uint32_t back) {
  return bb_render_call(
      0x0042fd90u, bb_address_encode(object),
      (uint32_t[]){(uint32_t)x, (uint32_t)y, (uint32_t)frame, (uint32_t)back},
      4);
}

static void sprite_cursor_erase(uint32_t *r) {
  r[7] = bb_sprite_cursor_erase_c(self(r), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_cursor_erase(void *object) {
  return bb_render_call(0x0042f7c0u, bb_address_encode(object), NULL, 0);
}

static void sprite_cursor_draw(uint32_t *r) {
  r[7] = bb_sprite_cursor_draw_c(self(r), arg(r, 0, BB_SPRITE_RECT_BYTES),
                                 arg(r, 1, BB_SPRITE_RECT_BYTES),
                                 arg(r, 2, BB_SPRITE_RECT_BYTES), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_cursor_draw(void *object, int32_t x, int32_t y,
                                           uint32_t back) {
  return bb_render_call(0x0042f910u, bb_address_encode(object),
                        (uint32_t[]){(uint32_t)x, (uint32_t)y, (uint32_t)back},
                        3);
}

static void sprite_blend(uint32_t *r) {
  r[7] = bb_sprite_blend_c(
      self(r), (void *)(uintptr_t)arg(r, 0, BB_SPRITE_BLEND_BYTES),
      arg(r, 1, BB_SPRITE_BLEND_BYTES), arg(r, 2, BB_SPRITE_BLEND_BYTES),
      (void *)(uintptr_t)arg(r, 3, BB_SPRITE_BLEND_BYTES),
      (const int32_t *)(uintptr_t)arg(r, 4, BB_SPRITE_BLEND_BYTES),
      arg(r, 5, BB_SPRITE_BLEND_BYTES), arg(r, 6, BB_SPRITE_BLEND_BYTES),
      r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_blend(void *object, void *source, uint32_t x,
                                     uint32_t y, void *sprite,
                                     const int32_t *rect, uint32_t ignored,
                                     unsigned char mode) {
  return bb_render_call(0x004327f0u, bb_address_encode(object),
                        (uint32_t[]){bb_address_encode(source), (uint32_t)x,
                                     (uint32_t)y, bb_address_encode(sprite),
                                     bb_address_encode(rect), (uint32_t)ignored,
                                     (uint32_t)mode},
                        7);
}

static void sprite_draw_region(uint32_t *r) {
  r[7] = bb_sprite_draw_region_c(
      self(r), arg(r, 0, BB_SPRITE_RECT_BYTES), arg(r, 1, BB_SPRITE_RECT_BYTES),
      arg(r, 2, BB_SPRITE_RECT_BYTES), arg(r, 3, BB_SPRITE_RECT_BYTES),
      arg(r, 4, BB_SPRITE_RECT_BYTES), arg(r, 5, BB_SPRITE_RECT_BYTES),
      (void *)(uintptr_t)arg(r, 6, BB_SPRITE_RECT_BYTES), r[3] + 4u);
}
uint32_t BB_THISCALL bb_sprite_draw_region(void *object, int32_t source_x,
                                           int32_t source_y, uint32_t width,
                                           uint32_t height, int32_t x,
                                           int32_t y, void *target) {
  return bb_render_call(0x004325f0u, bb_address_encode(object),
                        (uint32_t[]){(uint32_t)source_x, (uint32_t)source_y,
                                     (uint32_t)width, (uint32_t)height,
                                     (uint32_t)x, (uint32_t)y,
                                     bb_address_encode(target)},
                        7);
}

static void font_load(uint32_t *r) {
  r[7] = bb_font_load_c(
      self(r), (void *)(uintptr_t)arg(r, 0, BB_FONT_LOAD_BYTES),
      (const char *)(uintptr_t)arg(r, 1, BB_FONT_LOAD_BYTES), r[3] + 4u);
}
uint32_t BB_THISCALL bb_font_load(void *object, void *owner, const char *name) {
  return bb_render_call(
      0x00430310u, bb_address_encode(object),
      (uint32_t[]){bb_address_encode(owner), bb_address_encode(name)}, 2);
}

static void construct(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_sprite_construct(self(r), (void *)(uintptr_t)arg(r, 0, 0)));
}
static void destroy(uint32_t *r) { (void)r; }
static void bitmap(uint32_t *r) {
  r[7] = bb_sprite_bitmap(self(r), (const char *)(uintptr_t)arg(r, 0, 0),
                          arg(r, 1, 0));
}
static void blank(uint32_t *r) {
  r[7] = bb_address_encode(bb_sprite_create_blank(self(r), arg(r, 0, 0),
                                                  arg(r, 1, 0), arg(r, 2, 0)));
}
static void restore(uint32_t *r) { r[7] = bb_sprite_restore(self(r)); }
static void remove_sprite(uint32_t *r) {
  if (arg(r, 0, 0))
    r[7] = bb_sprite_remove(self(r), (void *)(uintptr_t)arg(r, 0, 0));
}
static void copy_back(uint32_t *r) { r[7] = bb_sprite_copy_back(self(r)); }
static void anchor(uint32_t *r) {
  r[7] = bb_sprite_anchor(self(r), arg(r, 0, 0), arg(r, 1, 0));
  r[5] = arg(r, 1, 0);
}
static void center(uint32_t *r) {
  r[7] = bb_sprite_center(self(r));
  r[5] = BB_I32(r[6] + 0x48u) < 0 ? UINT32_MAX : 0;
}
static void font_construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_font_construct(self(r)));
  r[5] = r[6] + 0x1c04u;
}
static void font_destroy(uint32_t *r) {
  if (BB_U32(r[6]))
    r[7] = bb_font_destroy(self(r));
}
static void font_spacing(uint32_t *r) {
  r[7] = arg(r, 0, 0);
  BB_U32(r[6] + 0x1c04u) = r[7];
}
static void font_draw(uint32_t *r) {
  r[7] = bb_font_draw(self(r), (const char *)(uintptr_t)arg(r, 0, 0),
                      arg(r, 1, 0), arg(r, 2, 0), arg(r, 3, 0));
}
static void font_wrapped(uint32_t *r) {
  r[7] = bb_font_draw_wrapped(self(r), (const char *)(uintptr_t)arg(r, 0, 0),
                              arg(r, 1, 0), arg(r, 2, 0), arg(r, 3, 0),
                              arg(r, 4, 0), (void *)(uintptr_t)arg(r, 5, 0));
}
static void font_bounds(uint32_t *r) {
  r[7] = bb_font_bounds(
      self(r), (const char *)(uintptr_t)arg(r, 0, 0), arg(r, 1, 0),
      arg(r, 2, 0), arg(r, 3, 0), (int32_t *)(uintptr_t)arg(r, 4, 0),
      (int32_t *)(uintptr_t)arg(r, 5, 0), (int32_t *)(uintptr_t)arg(r, 6, 0),
      (int32_t *)(uintptr_t)arg(r, 7, 0));
}
static void font_bounds_wrapped(uint32_t *r) {
  r[7] = bb_font_bounds_wrapped(
      self(r), (const char *)(uintptr_t)arg(r, 0, 0), arg(r, 1, 0),
      arg(r, 2, 0), arg(r, 3, 0), arg(r, 4, 0),
      (int32_t *)(uintptr_t)arg(r, 5, 0), (int32_t *)(uintptr_t)arg(r, 6, 0),
      (int32_t *)(uintptr_t)arg(r, 7, 0), (int32_t *)(uintptr_t)arg(r, 8, 0));
}

const struct bb_model_target *bb_sprite_model_find(void *unused,
                                                   uint32_t identity) {
  (void)unused;
  switch (identity) {
#define MODEL(id, body, words, reserve)                                        \
  case id: {                                                                   \
    static const struct bb_model_target target = {id, body, words, reserve,    \
                                                  0};                          \
    return &target;                                                            \
  }
    MODEL(0x0042f2c0u, blank, 3, 0)
    MODEL(0x0042f320u, sprite_create_file, 2, BB_SPRITE_FILENAME_BYTES)
    MODEL(0x0042f5a0u, construct, 1, 0)
    MODEL(0x0042f5e0u, destroy, 0, 0)
    MODEL(0x0042f5f0u, sprite_clear, 0, BB_SPRITE_DESCRIPTOR_BYTES)
    MODEL(0x0042f680u, restore, 0, 0)
    MODEL(0x0042f6e0u, bitmap, 2, 0)
    MODEL(0x0042f740u, copy_back, 0, 0)
    MODEL(0x0042f780u, anchor, 2, 0)
    MODEL(0x0042f7a0u, center, 0, 0)
    MODEL(0x0042f7c0u, sprite_cursor_erase, 0, BB_SPRITE_RECT_BYTES)
    MODEL(0x0042f910u, sprite_cursor_draw, 3, BB_SPRITE_RECT_BYTES)
    MODEL(0x0042fb50u, sprite_decode, 0, BB_SPRITE_DESCRIPTOR_BYTES)
    MODEL(0x0042fd90u, sprite_draw, 4, BB_SPRITE_RECT_BYTES)
    MODEL(0x00430160u, sprite_remap, 0, BB_SPRITE_DESCRIPTOR_BYTES)
    MODEL(0x004302c0u, font_construct, 0, 0)
    MODEL(0x004302f0u, font_destroy, 0, 0)
    MODEL(0x00430310u, font_load, 2, BB_FONT_LOAD_BYTES)
    MODEL(0x00430550u, font_spacing, 1, 0)
    MODEL(0x00430560u, font_draw, 4, 0)
    MODEL(0x004306e0u, font_wrapped, 6, 0)
    MODEL(0x004309b0u, font_bounds, 8, 0)
    MODEL(0x00430bb0u, font_bounds_wrapped, 9, 0)
    MODEL(0x00430f00u, bitmap_load, 6, BB_SPRITE_BITMAP_BYTES)
    MODEL(0x00431020u, bitmap_copy, 6, BB_SPRITE_BITMAP_BYTES)
    MODEL(0x004312d0u, surface_pixel, 2, BB_SPRITE_PIXEL_BYTES)
    MODEL(0x004313a0u, surface_color_key, 2, 8)
    MODEL(0x004313e0u, sprite_blank, 3, BB_SPRITE_DESCRIPTOR_BYTES)
    MODEL(0x00431500u, sprite_load, 1, BB_SPRITE_LOAD_BYTES)
    MODEL(0x004318c0u, sprite_restore_mfb, 0, BB_SPRITE_RESTORE_BYTES)
    MODEL(0x004325f0u, sprite_draw_region, 7, BB_SPRITE_RECT_BYTES)
    MODEL(0x004327f0u, sprite_blend, 7, BB_SPRITE_BLEND_BYTES)
    MODEL(0x00433120u, remove_sprite, 1, 0)
#undef MODEL
  default:
    return NULL;
  }
}
