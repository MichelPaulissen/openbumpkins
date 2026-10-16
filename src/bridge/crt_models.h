/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CRT_MODELS_H
#define BB_CRT_MODELS_H
#include <stdint.h>
/* Character adapters borrow twelve mapped scratch bytes. Locale callbacks
 * receive logical references within that private active frame. */
uint32_t bb_crt_character_type_c(int32_t, uint32_t, uint32_t);
int32_t bb_crt_character_lower_c(int32_t, uint32_t);
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
const struct bb_model_target *bb_crt_model_find(void *, uint32_t);
#endif
#endif
