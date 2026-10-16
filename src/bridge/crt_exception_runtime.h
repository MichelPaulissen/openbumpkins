/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CRT_EXCEPTION_RUNTIME_H
#define BB_CRT_EXCEPTION_RUNTIME_H
#include "address.h"
#include "model_runtime.h"
uint32_t bb_crt_model_frame(struct bb_model_scratch *);
uint32_t bb_crt_model_call(uint32_t target, uint32_t owner, uint32_t self,
                           const uint32_t *, unsigned);
void bb_crt_model_notice(uint32_t kind, uint32_t function, uint32_t owner);
void bb_crt_model_local_unwind(uint32_t registration, int32_t level,
                               uint32_t owner);
void bb_crt_model_unwind(uint32_t registration, uint32_t record);
void bb_crt_model_continue(uint32_t function, uint32_t owner);
void bb_crt_model_finish(uint32_t *);
const struct bb_model_target *bb_crt_exception_model_find(void *, uint32_t);
#endif
