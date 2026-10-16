/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_NATIVE_MEMORY_MODELS_H
#define BB_NATIVE_MEMORY_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_MEMORY_MODEL_API = 0x0f000000u };
uint32_t bb_memory_model_export(const char *);
const struct bb_model_target *bb_memory_model_find(void *, uint32_t);
#endif
