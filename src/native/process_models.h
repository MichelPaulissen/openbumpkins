/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROCESS_MODELS_H
#define BB_PROCESS_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_PROCESS_MODEL_API = 0x0c00d000u };
uint32_t bb_process_model_export(const char *);
const struct bb_model_target *bb_process_model_find(void *, uint32_t);
#endif
