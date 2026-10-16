/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_cpp_dispatch.h"
#include "crt_signals.h"
#include "qmixer_cpp_objects.h"
#include "qmixer_internal.h"
#include "qmixer_seh.h"
#include "qmixer_terminal.h"
#include <stddef.h>
/* The DLL has separate exception globals and a different private catch frame.
 * Cleanup retains its cached node field even if a callback changes the
 * argument. */
#define BB_CPP_RECORD 0x1001fcdc
#define BB_CPP_CONTEXT 0x1001fce0
#define BB_CPP_TRANSLATOR 0x1001fcf0
#define BB_CPP_CATCH_GUARD 0x10011990
#define BB_CPP_TRANSLATE_GUARD 0x10011aa0
#define BB_CPP_TRANSLATE_RESUME 0x10011a57
#define BB_CPP_CATCH_TABLE 0x1001a788
#define BB_CPP_SEH_HANDLER 0x10015b74
#define BB_CC_RESULT -28
#define BB_CC_SAVED_STACK -44
#define BB_CC_PREVIOUS_RECORD -40
#define BB_CC_PREVIOUS_CONTEXT -36
#define BB_CC_CACHED_NODE_SLOT -32
#define BB_CC_RETURN_TEMP -48
#define BB_CC_FRAME_EXTRA "subl $24,%esp;"
#define bb_crt_signal_terminate_entry bb_qmix_terminal_entry
#define bb_crt_signal_inconsistent_entry bb_qmix_inconsistent_entry
#define bb_crt_seh_abnormal_entry bb_qmix_seh_abnormal_entry
#define bb_crt_seh_local_unwind_entry bb_qmix_seh_local_unwind_entry
#define bb_crt_seh_rtl_unwind_entry bb_qmix_seh_rtl_unwind_entry
#define bb_crt_cpp_handler_entry bb_qmix_cpp_handler_entry
#define bb_crt_cpp_internal_entry bb_qmix_cpp_internal_entry
#define bb_crt_cpp_find_entry bb_qmix_cpp_find_entry
#define bb_crt_cpp_foreign_entry bb_qmix_cpp_foreign_entry
#define bb_crt_cpp_catch_entry bb_qmix_cpp_catch_entry
#define bb_crt_cpp_call_catch_entry bb_qmix_cpp_call_catch_entry
#define bb_crt_cpp_call_catch2_entry bb_qmix_cpp_call_catch2_entry
#define bb_crt_cpp_catch_guard_entry bb_qmix_cpp_catch_guard_entry
#define bb_crt_cpp_translate_entry bb_qmix_cpp_translate_entry
#define bb_crt_cpp_translate_guard_entry bb_qmix_cpp_translate_guard_entry
#define bb_crt_cpp_nested_unwind_entry bb_qmix_cpp_nested_unwind_entry
#define bb_crt_cpp_continue_entry bb_qmix_cpp_continue_entry
#define bb_crt_cpp_catch_filter_entry bb_qmix_cpp_catch_filter_entry
#define bb_crt_cpp_catch_landing_entry bb_qmix_cpp_catch_landing_entry
#define bb_crt_cpp_catch_cleanup_entry bb_qmix_cpp_catch_cleanup_entry
#define bb_crt_cpp_translate_resume_entry bb_qmix_cpp_translate_resume_entry
#define bb_crt_cpp_type_match_entry bb_qmix_cpp_type_match_entry
#define bb_crt_cpp_adjust_pointer_entry bb_qmix_cpp_adjust_pointer_entry
#define bb_crt_cpp_try_range_entry bb_qmix_cpp_try_range_entry
#define bb_crt_cpp_unwind_filter_entry bb_qmix_cpp_unwind_filter_entry
#define bb_crt_cpp_rethrow_filter_entry bb_qmix_cpp_rethrow_filter_entry
#define bb_crt_cpp_build_entry bb_qmix_cpp_build_entry
#define bb_crt_cpp_destroy_entry bb_qmix_cpp_destroy_entry
#define bb_crt_cpp_frame_unwind_entry bb_qmix_cpp_frame_unwind_entry
#define bb_crt_cpp_build_filter_entry bb_qmix_cpp_build_filter_entry
#define bb_crt_cpp_build_catch_entry bb_qmix_cpp_build_catch_entry
#define bb_crt_cpp_destroy_filter_entry bb_qmix_cpp_destroy_filter_entry
#define bb_crt_cpp_destroy_catch_entry bb_qmix_cpp_destroy_catch_entry
#define bb_crt_cpp_frame_filter_entry bb_qmix_cpp_frame_filter_entry
#define bb_crt_cpp_frame_catch_entry bb_qmix_cpp_frame_catch_entry
#define bb_crt_cpp_member0_entry bb_qmix_cpp_member0_entry
#define bb_crt_cpp_member1_entry bb_qmix_cpp_member1_entry
#define bb_crt_cpp_member2_entry bb_qmix_cpp_member2_entry
#define bb_crt_cpp_call_frame_entry bb_qmix_cpp_call_frame_entry
#define bb_crt_cpp_call_catch2_c bb_qmix_cpp_call_catch2_worker
#define bb_crt_cpp_catch_cleanup_c bb_qmix_cpp_catch_cleanup_worker
#define bb_crt_cpp_call_catch_c bb_qmix_cpp_call_catch_worker
#define bb_crt_cpp_catch_filter_c bb_qmix_cpp_catch_filter_worker
#define bb_crt_cpp_nested_finish_c bb_qmix_cpp_nested_finish_worker
#define bb_crt_cpp_translate_c bb_qmix_cpp_translate_worker
#define bb_crt_cpp_translate_resume_c bb_qmix_cpp_translate_resume_worker
#define bb_crt_cpp_handler_c bb_qmix_cpp_handler_worker
#include "crt_cpp_dispatch.c"

/* C dispatcher adapters retain logical arguments and private register inputs.
 */
static uintptr_t arg(const uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 2056u + n * 4u);
}
void bb_qmix_cpp_internal_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_internal_entry(
      (void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2),
      (void *)arg(r, 3), (void *)arg(r, 4), (int32_t)arg(r, 5),
      (void *)arg(r, 6), (uint32_t)arg(r, 7));
}
void bb_qmix_cpp_find_c(uint32_t *r) {
  bb_qmix_cpp_find_entry((void *)arg(r, 0), (void *)arg(r, 1),
                         (void *)arg(r, 2), (void *)arg(r, 3),
                         (void *)arg(r, 4), (uint32_t)arg(r, 5),
                         (int32_t)arg(r, 6), (void *)arg(r, 7));
}
void bb_qmix_cpp_foreign_c(uint32_t *r) {
  bb_qmix_cpp_foreign_entry((void *)arg(r, 0), (void *)arg(r, 1),
                            (void *)arg(r, 2), (void *)arg(r, 3),
                            (void *)arg(r, 4), (int32_t)arg(r, 5),
                            (int32_t)arg(r, 6), (void *)arg(r, 7));
}
void bb_qmix_cpp_catch_c(uint32_t *r) {
  bb_qmix_cpp_catch_entry((void *)arg(r, 0), (void *)arg(r, 1),
                          (void *)arg(r, 2), (void *)arg(r, 3),
                          (void *)arg(r, 4), (void *)arg(r, 5),
                          (void *)arg(r, 6), (void *)arg(r, 7),
                          (int32_t)arg(r, 8), (void *)arg(r, 9));
}
void bb_qmix_cpp_call_catch_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_call_catch_entry(
      (void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2),
      (void *)arg(r, 3), (void *)arg(r, 4), (int32_t)arg(r, 5),
      (uint32_t)arg(r, 6));
}
void bb_qmix_cpp_call_catch2_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_call_catch2_entry(
      (void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2),
      (int32_t)arg(r, 3), (uint32_t)arg(r, 4));
}
void bb_qmix_cpp_catch_guard_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_catch_guard_entry(
      (void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2),
      (void *)arg(r, 3));
}
void bb_qmix_cpp_translate_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_translate_entry(
      (void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2),
      (void *)arg(r, 3), (void *)arg(r, 4), (int32_t)arg(r, 5),
      (void *)arg(r, 6));
}
void bb_qmix_cpp_translate_guard_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_translate_guard_entry(
      (void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2),
      (void *)arg(r, 3));
}
void bb_qmix_cpp_continue_c(uint32_t *r) {
  bb_qmix_cpp_continue_entry((void *)arg(r, 0), (void *)arg(r, 1));
}
void bb_qmix_cpp_nested_unwind_c(uint32_t *r) {
  bb_qmix_cpp_nested_unwind_entry((void *)arg(r, 0), (void *)arg(r, 1));
}
void bb_qmix_cpp_handler_c(uint32_t *r) {
  r[7] = bb_qmix_cpp_internal_entry((void *)arg(r, 0), (void *)arg(r, 1),
                                    (void *)arg(r, 2), (void *)arg(r, 3),
                                    (void *)r[7], 0, NULL, 0);
  r[8] &= ~0x400u;
}
void bb_qmix_cpp_catch_filter_c(uint32_t *r) {
  bb_qmix_cpp_catch_filter_worker(r);
}
void bb_qmix_cpp_catch_cleanup_c(uint32_t *r) {
  bb_qmix_cpp_catch_cleanup_worker(r[2]);
}
void bb_qmix_cpp_catch_landing_c(uint32_t *r) {
  uintptr_t frame = r[2];
  BB_U32(frame + BB_CC_RESULT) = 0;
  uint32_t context[8] = {(uint32_t)frame,      0,         0, 0, 0, 0,
                         (uint32_t)frame - 16, UINT32_MAX};
  bb_qmix_seh_context_unwind_entry(context);
  set_head(BB_U32(frame - 16));
  r[7] = 0;
}
void bb_qmix_cpp_translate_resume_c(uint32_t *r) {
  bb_qmix_cpp_translate_resume_worker(r[2]);
  r[7] = BB_U32(r[2] - 52);
}
