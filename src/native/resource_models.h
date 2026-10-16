/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RESOURCE_MODELS_H
#define BB_RESOURCE_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_RESOURCE_MODEL_API = 0x0c009000u };
uint32_t bb_resource_model_export(const char *);
const struct bb_model_target *bb_resource_model_find(void *, uint32_t);
#endif
