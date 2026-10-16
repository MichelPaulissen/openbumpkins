/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SOUND_MODELS_H
#define BB_SOUND_MODELS_H
#include "../bridge/model_runtime.h"
enum {
  BB_SOUND_MODEL_API = 0x0c003000u,
  BB_SOUND_MODEL_SOUND = 0x0c010000u,
  BB_SOUND_MODEL_BUFFER = 0x0c020000u
};
uint32_t bb_sound_model_export(const char *);
const struct bb_model_target *bb_sound_model_find(void *, uint32_t);
#endif
