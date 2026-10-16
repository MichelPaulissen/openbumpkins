/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MODULE_MODELS_H
#define BB_MODULE_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_MODULE_MODEL_API = 0x0c008000u };
uint32_t bb_module_model_export(const char *);
const struct bb_model_target *bb_module_model_find(void *, uint32_t);
#endif
