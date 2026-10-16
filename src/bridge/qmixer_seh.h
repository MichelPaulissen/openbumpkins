/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_QMIXER_SEH_H
#define BUMPKINS_QMIXER_SEH_H
#include <stdint.h>
void bb_qmix_seh_global_unwind_entry(void *);
void bb_qmix_seh_local_unwind_entry(void *, int32_t);
int32_t bb_qmix_seh_abnormal_entry(void);
void BB_STDCALL bb_qmix_seh_notice_entry(uint32_t);
void BB_STDCALL bb_qmix_seh_frame_notice_entry(uint32_t);
int32_t bb_qmix_seh_nested_entry(void *, void *, void *, void *);
int32_t bb_qmix_seh_handler_entry(void *, void *, void *, void *);
void BB_STDCALL bb_qmix_seh_context_unwind_entry(void *);
void BB_STDCALL bb_qmix_seh_rtl_unwind_entry(void *, void *, void *,
                                                          void *);
#endif
