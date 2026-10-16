/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_WINDOW_MODELS_H
#define BB_WINDOW_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_WINDOW_MODEL_API = 0x0c007000u };
/* Fixed window services and wsprintfA's logical variadic argument cursor. */
uint32_t bb_window_model_export(const char *);
const struct bb_model_target *bb_window_model_find(void *, uint32_t);
#endif
