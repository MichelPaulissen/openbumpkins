/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_TIMER_MODELS_H
#define BB_TIMER_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_TIMER_MODEL_API = 0x0c005000u };
uint32_t bb_timer_model_export(const char *);
const struct bb_model_target *bb_timer_model_find(void *, uint32_t);
#endif
