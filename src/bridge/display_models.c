/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "render_models.h"
#include "sprite_models.h"

void *BB_THISCALL bb_display_construct(void *);
uint32_t bb_display_mode_record(void *, uint32_t *);
uint32_t bb_display_restore(void *, uint32_t *);

static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static uint32_t arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_display_construct(self(r)));
  r[5] = 255;
}
static void destroy(uint32_t *r) { (void)r; }
static void mode(uint32_t *r) {
  uint32_t depth;
  r[5] = bb_display_mode_record((void *)(uintptr_t)arg(r, 0), &depth);
  r[6] = depth;
  r[7] = 1;
}
static void release(uint32_t *r) { r[7] = bb_display_release(self(r)); }
static void lock(uint32_t *r) { r[7] = bb_display_lock(self(r)); }
static void unlock(uint32_t *r) { r[7] = bb_display_unlock(self(r)); }
static void clear(uint32_t *r) { r[7] = bb_display_clear(self(r)); }
static void flip(uint32_t *r) { r[7] = bb_display_flip(self(r)); }
static void reset(uint32_t *r) { r[7] = bb_display_reset(self(r)); }
static void wait_frame(uint32_t *r) {
  if ((int32_t)arg(r, 0) > 0)
    r[7] = bb_display_wait(self(r), arg(r, 0));
}
static void restore(uint32_t *r) {
  if (BB_U32(r[6] + 0x20ec2u)) {
    uint32_t called;
    r[7] = bb_display_restore(self(r), &called);
  }
}
static void shade(uint32_t *r) {
  r[7] = bb_display_shade(self(r), arg(r, 0), arg(r, 1), arg(r, 2), arg(r, 3));
}
static void shade_rect(uint32_t *r) {
  r[7] =
      bb_display_shade_rect(self(r), arg(r, 0), arg(r, 1), arg(r, 2), arg(r, 3),
                            (const unsigned char *)(uintptr_t)arg(r, 4));
}
static void nearest(uint32_t *r) {
  r[7] = bb_palette_nearest(self(r), arg(r, 0), arg(r, 1), arg(r, 2));
}
static void remap(uint32_t *r) { r[7] = bb_palette_remap(self(r)); }
static void palette_shade(uint32_t *r) { r[7] = bb_palette_shade(self(r)); }
static void ready(uint32_t *r) {
  r[5] = (r[5] & 0xffffff00u) | BB_U8(r[6] + 0x20ebdu);
  r[7] = bb_palette_ready(self(r));
}
static void fade(uint32_t *r) { r[7] = bb_palette_fade(self(r), arg(r, 0)); }
static void cycle(uint32_t *r) { r[7] = bb_palette_cycle(self(r)); }

const struct bb_model_target *bb_display_model_find(void *unused,
                                                    uint32_t identity) {
  (void)unused;
  switch (identity) {
#define MODEL(id, body, words, reserve)                                        \
  case id: {                                                                   \
    static const struct bb_model_target target = {id, body, words, reserve,    \
                                                  0};                          \
    return &target;                                                            \
  }
    MODEL(0x0042ec00u, construct, 0, 0)
    MODEL(0x0042ec30u, destroy, 0, 0)
    MODEL(0x0042ec40u, bb_display_initialize_model, 2, BB_DISPLAY_SETUP_BYTES)
    MODEL(0x0042ede0u, mode, 2, 0)
    MODEL(0x0042ee30u, release, 0, 0)
    MODEL(0x0042ee50u, bb_display_primary_model, 0, BB_DISPLAY_SETUP_BYTES)
    MODEL(0x0042ef20u, bb_palette_install_model, 1, BB_PALETTE_COLORS_BYTES)
    MODEL(0x0042f030u, bb_palette_black_model, 0, BB_PALETTE_COLORS_BYTES)
    MODEL(0x0042f080u, cycle, 0, 0)
    MODEL(0x0042f150u, lock, 0, 0)
    MODEL(0x0042f180u, unlock, 0, 0)
    MODEL(0x0042f1a0u, clear, 0, 0)
    MODEL(0x0042f200u, reset, 0, 0)
    MODEL(0x0042f240u, flip, 0, 0)
    MODEL(0x00431120u, bb_palette_load_model, 2, BB_PALETTE_LOAD_BYTES)
    MODEL(0x00431e80u, nearest, 3, 0)
    MODEL(0x00431f20u, remap, 0, 0)
    MODEL(0x00431fd0u, palette_shade, 0, 0)
    MODEL(0x00432030u, bb_palette_mix_model, 0, BB_PALETTE_FILENAME_BYTES)
    MODEL(0x00432240u, bb_palette_reflect_model, 0, BB_PALETTE_FILENAME_BYTES)
    MODEL(0x004324c0u, restore, 0, 0)
    MODEL(0x00432500u, shade, 4, 0)
    MODEL(0x00432530u, shade_rect, 5, 0)
    MODEL(0x00432c40u, ready, 0, 0)
    MODEL(0x00432c60u, fade, 1, 0)
    MODEL(0x00432dc0u, wait_frame, 1, 0)
#undef MODEL
  default:
    return bb_sprite_model_find(unused, identity);
  }
}
