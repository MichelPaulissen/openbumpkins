/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_BOOTSTRAP_MODELS_H
#define BB_BOOTSTRAP_MODELS_H
#include "bootstrap_resources.h"
#include "model_runtime.h"
enum {
  BB_AUDIO_NAME_BYTES = 64,
  BB_AUDIO_FILL_BYTES = 16,
  BB_AUDIO_CREATE_BYTES = 28
};
void *bb_audio_resource_find_c(void *, const char *, uint32_t);
uint32_t bb_audio_asset_load_c(void *, const char *, uint32_t, uint32_t);
uint32_t bb_audio_buffer_fill_c(void *, const void *, uint32_t, uint32_t);
void *bb_audio_pcm_create_c(void *, void *, const char *, uint32_t);
const struct bb_model_target *bb_bootstrap_model_find(void *, uint32_t);
#endif
