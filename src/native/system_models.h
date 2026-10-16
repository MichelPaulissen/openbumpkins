/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SYSTEM_MODELS_H
#define BB_SYSTEM_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_SYSTEM_MODEL_API = 0x0c00a000u };
uint32_t bb_system_model_export(const char *);
const struct bb_model_target *bb_system_model_find(void *, uint32_t);
#endif
