/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PLATFORM_MODELS_H
#define BB_PLATFORM_MODELS_H
#include "../bridge/model_runtime.h"
/* Host services return logical C identities. Known unconverted services get a
 * precise fatal C boundary; genuinely absent exports still return zero. */
uint32_t bb_platform_model_export(const char *dll, const char *name);
uint32_t bb_platform_model_require(const char *dll, const char *name);
const struct bb_model_target *bb_platform_model_find(void *, uint32_t);
void bb_platform_model_reset(void);
#endif
