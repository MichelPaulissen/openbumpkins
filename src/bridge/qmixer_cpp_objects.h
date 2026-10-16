/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_QMIXER_CPP_OBJECTS_H
#define BUMPKINS_QMIXER_CPP_OBJECTS_H
#include <stdint.h>
int32_t bb_qmix_cpp_type_match_entry(const void *, const void *, const void *);
void *bb_qmix_cpp_adjust_pointer_entry(void *, const void *);
void *bb_qmix_cpp_try_range_entry(const void *, int32_t, int32_t, uint32_t *,
                                 uint32_t *);
int32_t bb_qmix_cpp_unwind_filter_entry(const void *);
int32_t bb_qmix_cpp_rethrow_filter_entry(const void *);
void bb_qmix_cpp_build_entry(void *, void *, const void *, const void *);
void bb_qmix_cpp_destroy_entry(void *, uint32_t);
void bb_qmix_cpp_frame_unwind_entry(void *, void *, const void *, int32_t);
int32_t bb_qmix_cpp_build_filter_entry(void);
void bb_qmix_cpp_build_catch_entry(void);
int32_t bb_qmix_cpp_destroy_filter_entry(void);
void bb_qmix_cpp_destroy_catch_entry(void);
int32_t bb_qmix_cpp_frame_filter_entry(void);
void bb_qmix_cpp_frame_catch_entry(void);
void BB_STDCALL bb_qmix_cpp_member0_entry(void *, void *);
void BB_STDCALL bb_qmix_cpp_member1_entry(void *, void *, void *);
void BB_STDCALL bb_qmix_cpp_member2_entry(void *, void *, void *,
                                                       uint32_t);
uintptr_t BB_STDCALL bb_qmix_cpp_call_frame_entry(void *, void *,
                                                               uint32_t);
#endif
