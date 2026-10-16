/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SYNC_MODELS_H
#define BB_SYNC_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_SYNC_MODEL_API = 0x0c004000u };
uint32_t bb_sync_model_export(const char *);
const struct bb_model_target *bb_sync_model_find(void *, uint32_t);
#endif
