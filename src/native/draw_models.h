/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_NATIVE_DRAW_MODELS_H
#define BB_NATIVE_DRAW_MODELS_H
#include "../bridge/model_runtime.h"

/* Native drawing services have logical identities separate from game code and
 * from the four-byte references to opaque host objects. */
enum {
  BB_DRAW_MODEL_API = 0x0d000000u,
  BB_DRAW_MODEL_DRAW = 0x0d100000u,
  BB_DRAW_MODEL_SURFACE = 0x0d200000u,
  BB_DRAW_MODEL_PALETTE = 0x0d300000u
};
uint32_t bb_draw_model_export(const char *name);
const struct bb_model_target *bb_draw_model_find(void *, uint32_t);
#endif
