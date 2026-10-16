/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SETTINGS_MODELS_H
#define BB_SETTINGS_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_SETTINGS_MODEL_API = 0x0c000000u };
uint32_t bb_settings_model_export(const char *);
const struct bb_model_target *bb_settings_model_find(void *, uint32_t);
#endif
