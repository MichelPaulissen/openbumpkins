/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_AUDIO_MODELS_H
#define BB_AUDIO_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_AUDIO_MODEL_API = 0x0c006000u };
uint32_t bb_audio_model_export(const char *);
const struct bb_model_target *bb_audio_model_find(void *, uint32_t);
#endif
