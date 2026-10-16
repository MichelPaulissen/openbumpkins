/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include "qmixer_cpp_objects.h"
#include "qmixer_seh.h"
#include "qmixer_terminal.h"
#include "crt_signals.h"

/* Shared probes resolve the same native platform APIs. Terminal outcomes now
 * call the module's reconstructed C policy directly. */
#define bb_crt_signal_terminate_entry bb_qmix_terminal_entry
#define bb_crt_signal_inconsistent_entry bb_qmix_inconsistent_entry
#define BB_CPP_BUILD_TABLE 0x1001a7a0
#define BB_CPP_DESTROY_TABLE 0x1001a7b0
#define BB_CPP_FRAME_TABLE 0x1001a778
#define BB_CPP_SEH_HANDLER 0x10015b74
#define BB_CPP_FRAME_EXTRA "subl $12,%esp;"
#define BB_CPP_CACHED_STATE_SLOT 1
#define bb_crt_seh_notice_entry bb_qmix_seh_notice_entry
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
#define bb_crt_cpp_build_c bb_qmix_cpp_build_worker
#define bb_crt_cpp_destroy_c bb_qmix_cpp_destroy_worker
#define bb_crt_cpp_frame_unwind_c bb_qmix_cpp_frame_unwind_worker
#define bb_crt_cpp_frame_catch_c bb_qmix_cpp_frame_catch_worker
#define bb_crt_cpp_terminate_catch_c bb_qmix_cpp_terminate_catch_worker
#define bb_crt_cpp_filter_c bb_qmix_cpp_filter_worker
#include "crt_cpp_objects.c"

static uintptr_t arg(const uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 2056u + n * 4u);
}
void bb_qmix_cpp_try_range_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_try_range_entry(
      (void *)arg(r, 0), (int32_t)arg(r, 1), (int32_t)arg(r, 2),
      (uint32_t *)arg(r, 3), (uint32_t *)arg(r, 4));
}
void bb_qmix_cpp_adjust_pointer_c(uint32_t *r) {
  r[7] = (uintptr_t)bb_qmix_cpp_adjust_pointer_entry((void *)arg(r, 0), (void *)arg(r, 1));
}
void bb_qmix_cpp_unwind_filter_c(uint32_t *r) {
  r[7] = bb_qmix_cpp_unwind_filter_entry((void *)arg(r, 0));
}
void bb_qmix_cpp_rethrow_filter_c(uint32_t *r) {
  r[7] = bb_qmix_cpp_rethrow_filter_entry((void *)arg(r, 0));
}
void bb_qmix_cpp_build_c(uint32_t *r) {
  bb_qmix_cpp_build_entry((void *)arg(r, 0), (void *)arg(r, 1),
                         (void *)arg(r, 2), (void *)arg(r, 3));
}
void bb_qmix_cpp_destroy_c(uint32_t *r) {
  bb_qmix_cpp_destroy_entry((void *)arg(r, 0), arg(r, 1));
}
void bb_qmix_cpp_frame_unwind_c(uint32_t *r) {
  bb_qmix_cpp_frame_unwind_entry((void *)arg(r, 0), (void *)arg(r, 1),
                                (void *)arg(r, 2), (int32_t)arg(r, 3));
}
void bb_qmix_cpp_build_filter_c(uint32_t *r) { bb_qmix_cpp_filter_worker(r, 0); }
void bb_qmix_cpp_destroy_filter_c(uint32_t *r) { bb_qmix_cpp_filter_worker(r, 1); }
void bb_qmix_cpp_frame_filter_c(uint32_t *r) { bb_qmix_cpp_filter_worker(r, 2); }
void bb_qmix_cpp_build_catch_c(uint32_t *r) { bb_qmix_cpp_terminate_catch_worker(r[2]); }
void bb_qmix_cpp_destroy_catch_c(uint32_t *r) { bb_qmix_cpp_terminate_catch_worker(r[2]); }
void bb_qmix_cpp_frame_catch_c(uint32_t *r) { bb_qmix_cpp_frame_catch_worker(r[2]); }
void bb_qmix_cpp_member0_c(uint32_t *r) {
  bb_qmix_cpp_member0_entry((void *)arg(r, 0), (void *)arg(r, 1));
}
void bb_qmix_cpp_member1_c(uint32_t *r) {
  bb_qmix_cpp_member1_entry((void *)arg(r, 0), (void *)arg(r, 1), (void *)arg(r, 2));
}
void bb_qmix_cpp_member2_c(uint32_t *r) {
  bb_qmix_cpp_member2_entry((void *)arg(r, 0), (void *)arg(r, 1),
                           (void *)arg(r, 2), arg(r, 3));
}
void bb_qmix_cpp_call_frame_c(uint32_t *r) {
  r[7] = bb_qmix_cpp_call_frame_entry((void *)arg(r, 0), (void *)arg(r, 1), arg(r, 2));
}
