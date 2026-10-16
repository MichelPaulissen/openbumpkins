/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include "qmixer_seh.h"

/* Share the reconstructed CRT traversal; the mixer owns separate notification
 * fields, nested-guard identity and platform import. No DLL body is copied. */
#define BB_SEH_NESTED_ADDRESS 0x10011b2c
#define BB_SEH_NOTICE_KIND 0x1001f45c
#define BB_SEH_NOTICE_FUNCTION 0x1001f458
#define BB_SEH_NOTICE_OWNER 0x1001f460
#define BB_SEH_RTL_IMPORT 0x100212e0
#define bb_crt_seh_global_unwind_entry bb_qmix_seh_global_unwind_entry
#define bb_crt_seh_local_unwind_entry bb_qmix_seh_local_unwind_entry
#define bb_crt_seh_abnormal_entry bb_qmix_seh_abnormal_entry
#define bb_crt_seh_notice_entry bb_qmix_seh_notice_entry
#define bb_crt_seh_frame_notice_entry bb_qmix_seh_frame_notice_entry
#define bb_crt_seh_nested_entry bb_qmix_seh_nested_entry
#define bb_crt_seh_handler_entry bb_qmix_seh_handler_entry
#define bb_crt_seh_context_unwind_entry bb_qmix_seh_context_unwind_entry
#define bb_crt_seh_rtl_unwind_entry bb_qmix_seh_rtl_unwind_entry
#define bb_crt_seh_local_unwind_c bb_qmix_seh_local_unwind_worker
#define bb_crt_seh_notice_c bb_qmix_seh_notice_worker
#define bb_crt_seh_handler_c bb_qmix_seh_handler_worker
#include "crt_seh.c"

/* Existing reconstructed callers supply a logical stack and their original
 * owner EBP. Dispatch adapters translate those arguments to the shared C API. */
static uintptr_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(input[3] + 2056u + n * 4u);
}
void bb_qmix_seh_global_unwind_c(uint32_t *input) {
  bb_qmix_seh_global_unwind_entry((void *)arg(input, 0));
}
void bb_qmix_seh_local_unwind_c(uint32_t *input) {
  local_at((void *)arg(input, 0), (int32_t)arg(input, 1), input[2]);
}
void bb_qmix_seh_abnormal_c(uint32_t *input) {
  input[7] = bb_qmix_seh_abnormal_entry();
}
void bb_qmix_seh_notice_c(uint32_t *input) {
  bb_qmix_seh_notice_worker(input, 0);
}
void bb_qmix_seh_frame_notice_c(uint32_t *input) {
  bb_qmix_seh_notice_worker(input, 1);
}
void bb_qmix_seh_nested_c(uint32_t *input) {
  input[7] = bb_qmix_seh_nested_entry(
      (void *)arg(input, 0), (void *)arg(input, 1), (void *)arg(input, 2),
      (void *)arg(input, 3));
}
void bb_qmix_seh_handler_c(uint32_t *input) {
  input[7] = bb_qmix_seh_handler_entry(
      (void *)arg(input, 0), (void *)arg(input, 1), (void *)arg(input, 2),
      (void *)arg(input, 3));
}
void bb_qmix_seh_context_unwind_c(uint32_t *input) {
  bb_qmix_seh_context_unwind_entry((void *)arg(input, 0));
}
void bb_qmix_seh_rtl_unwind_c(uint32_t *input) {
  bb_qmix_seh_rtl_unwind_entry((void *)arg(input, 0), (void *)arg(input, 1),
                             (void *)arg(input, 2), (void *)arg(input, 3));
}
