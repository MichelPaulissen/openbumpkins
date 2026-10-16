/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_STARTUP_MODELS_H
#define BB_STARTUP_MODELS_H
#include "model_runtime.h"
#include "startup.h"
enum { BB_WINDOW_CLASS_BYTES = 40, BB_STARTUP_BYTES = 256 };
uint32_t bb_game_window_create_c(void *, int32_t, uint32_t);
int32_t bb_game_main_c(void *, void *, const char *, int32_t, uint32_t);
const struct bb_model_target *bb_startup_model_find(void *, uint32_t);
#endif
