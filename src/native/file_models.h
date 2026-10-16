/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_NATIVE_FILE_MODELS_H
#define BB_NATIVE_FILE_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_FILE_MODEL_API = 0x0e000000u };
uint32_t bb_file_model_export(const char *);
const struct bb_model_target *bb_file_model_find(void *, uint32_t);
#endif
