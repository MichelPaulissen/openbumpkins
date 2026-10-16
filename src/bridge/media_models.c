/* SPDX-License-Identifier: GPL-3.0-only */
#include "media_models.h"
#include "media_devices.h"
#include "address.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
#define P(n) ((void *)(uintptr_t)A(n))
extern void bb_media_sound_release_c(uint32_t *);
static void draw_create(uint32_t *r) {
  r[7] = bb_media_draw_create_entry(P(0), P(1), P(2));
}
static void sound_create(uint32_t *r) {
  r[7] = bb_media_sound_create_entry(P(0), P(1), P(2));
}
static void sound_device(uint32_t *r) {
  r[7] = bb_media_sound_device_entry(P(0), P(1));
}
static void sound_initialize(uint32_t *r) { bb_media_sound_initialize_entry(P(0)); }
static void sound_shutdown(uint32_t *r) { (void)r; bb_media_sound_shutdown_entry(); }
static const struct bb_model_target targets[] = {
    {0x00465f98u, draw_create, 3, 0, 0},
    {0x00465f9eu, sound_create, 3, 0, 0},
    {0x00463707u, sound_device, 2, 0, 0},
    {0x00463560u, sound_initialize, 1, 0, 0},
    {0x0046376fu, sound_shutdown, 0, 0, 0},
    {0x004637b4u, bb_media_sound_release_c, 1, 0, 0},
};
const struct bb_model_target *bb_media_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
