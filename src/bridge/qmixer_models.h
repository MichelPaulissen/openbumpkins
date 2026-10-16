/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_MODELS_H
#define BB_QMIXER_MODELS_H
void bb_qmix_model_tables_initialize(unsigned char *);
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
const struct bb_model_target *bb_qmix_model_find(void *, uint32_t);
void bb_qmix_model_call(struct bb_call_state *, uintptr_t, const uint32_t *,
                        uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                        uint32_t, long double *);
#define BB_QMIX_DISPATCH(state, target, args, esp, edi, esi, ebp, ebx, ret)    \
  bb_qmix_model_call(state, target, args, esp, edi, esi, ebp, ebx, ret,        \
                     &call_float)
#define BB_QMIX_RETURN_FLOAT(input, value) bb_model_return_float(value)
#define BB_QMIX_CAPTURE_FLOAT(value) ((value) = call_float)
#else
#define BB_QMIX_DISPATCH bb_qmix_call
#define BB_QMIX_RETURN_FLOAT(input, value)                                     \
  (*(volatile long double *)(uintptr_t)(input)[9] = (value))
#define BB_QMIX_CAPTURE_FLOAT(value)                                           \
  __asm__ volatile("fstpt %0" : "=m"(value) : : "st")
#endif
#endif
