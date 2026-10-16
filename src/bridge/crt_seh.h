/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_CRT_SEH_H
#define BUMPKINS_CRT_SEH_H
#include <stdint.h>
void bb_crt_seh_global_unwind_entry(void *registration);
/* Local unwind callbacks use the incoming EBP as their owner frame. */
void bb_crt_seh_local_unwind_entry(void *registration, int32_t level);
int32_t bb_crt_seh_abnormal_entry(void);
void BB_STDCALL bb_crt_seh_notice_entry(uint32_t unused);
void BB_STDCALL bb_crt_seh_frame_notice_entry(uint32_t unused);
int32_t bb_crt_seh_nested_entry(void *record, void *registration, void *context,
                                void *dispatcher);
int32_t bb_crt_seh_handler_entry(void *record, void *registration,
                                 void *context, void *dispatcher);
void BB_STDCALL bb_crt_seh_context_unwind_entry(void *context);
void BB_STDCALL bb_crt_seh_rtl_unwind_entry(void *registration,
                                                          void *continuation,
                                                          void *record,
                                                          void *value);
#endif
