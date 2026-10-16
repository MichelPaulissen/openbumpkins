/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CD_MODELS_H
#define BB_CD_MODELS_H
#include "../bridge/model_runtime.h"
enum { BB_CD_MODEL_API = 0x0c002000u };
uint32_t bb_cd_model_export(const char *);
const struct bb_model_target *bb_cd_model_find(void *, uint32_t);
#endif
