/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ENVIRONMENT_MODELS_H
#define BB_ENVIRONMENT_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_ENVIRONMENT_MODEL_API = 0x0c00c000u };
uint32_t bb_environment_model_export(const char *);
const struct bb_model_target *bb_environment_model_find(void *, uint32_t);
#endif
