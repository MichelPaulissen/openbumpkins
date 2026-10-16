/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CLOCK_MODELS_H
#define BB_CLOCK_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_CLOCK_MODEL_API = 0x0c001000u };
uint32_t bb_clock_model_export(const char *);
const struct bb_model_target *bb_clock_model_find(void *, uint32_t);
#endif
