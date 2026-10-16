/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_QMIXER_CPP_DISPATCH_H
#define BUMPKINS_QMIXER_CPP_DISPATCH_H
#include <stdint.h>
uint32_t bb_qmix_cpp_handler_entry(void *, void *, void *, void *);
uint32_t bb_qmix_cpp_internal_entry(void *, void *, void *, void *,
                                    const void *, int32_t, void *, uint32_t);
void bb_qmix_cpp_find_entry(void *, void *, void *, void *, const void *,
                            uint32_t, int32_t, void *);
void bb_qmix_cpp_foreign_entry(void *, void *, void *, void *, const void *,
                               int32_t, int32_t, void *);
void bb_qmix_cpp_catch_entry(void *, void *, void *, void *, const void *,
                             const void *, const void *, const void *, int32_t,
                             void *);
uintptr_t bb_qmix_cpp_call_catch_entry(void *, void *, void *, const void *,
                                       void *, int32_t, uint32_t);
uintptr_t bb_qmix_cpp_call_catch2_entry(void *, const void *, void *, int32_t,
                                        uint32_t);
uint32_t bb_qmix_cpp_catch_guard_entry(void *, void *, void *, void *);
int32_t bb_qmix_cpp_translate_entry(void *, void *, void *, void *,
                                    const void *, int32_t, void *);
uint32_t bb_qmix_cpp_translate_guard_entry(void *, void *, void *, void *);
void BB_STDCALL bb_qmix_cpp_nested_unwind_entry(void *, void *);
void BB_STDCALL bb_qmix_cpp_continue_entry(void *, void *);
int32_t bb_qmix_cpp_catch_filter_entry(void);
uintptr_t bb_qmix_cpp_catch_landing_entry(void);
void bb_qmix_cpp_catch_cleanup_entry(void);
int32_t bb_qmix_cpp_translate_resume_entry(void);
#endif
