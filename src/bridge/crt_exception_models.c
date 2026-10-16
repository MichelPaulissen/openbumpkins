/* SPDX-License-Identifier: GPL-3.0-only */
#include "allocation_unwind.h"
#include "crt_cpp_dispatch.h"
#include "crt_cpp_objects.h"
#include "crt_exception_runtime.h"
#include "crt_seh.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
#define P(n) ((void *)(uintptr_t)A(n))
extern void bb_crt_seh_notice_c(uint32_t *, uint32_t);
extern void bb_crt_cpp_filter_c(uint32_t *, uint32_t);
extern void bb_crt_cpp_frame_catch_c(uintptr_t);
extern void bb_crt_cpp_terminate_catch_c(uintptr_t);
extern void bb_crt_cpp_catch_cleanup_c(uintptr_t);
extern void bb_crt_cpp_catch_filter_c(uint32_t *);
extern void bb_crt_cpp_translate_resume_c(uintptr_t);
extern void bb_crt_cpp_handler_c(uint32_t *);
extern void bb_crt_errors_stack_probe_c(uint32_t *);
static void seh_global(uint32_t *r) { bb_crt_seh_global_unwind_entry(P(0)); }
static void seh_nested(uint32_t *r) {
  r[7] = bb_crt_seh_nested_entry(P(0), P(1), P(2), P(3));
}
static void seh_local(uint32_t *r) {
  bb_crt_model_local_unwind(A(0), A(1), r[2] ? r[2] : A(0) + 16u);
}
static void seh_abnormal(uint32_t *r) { r[7] = bb_crt_seh_abnormal_entry(); }
static void seh_notice(uint32_t *r) { bb_crt_seh_notice_c(r, 0); }
static void seh_frame_notice(uint32_t *r) { bb_crt_seh_notice_c(r, 1); }
static void seh_handler(uint32_t *r) {
  r[7] = bb_crt_seh_handler_entry(P(0), P(1), P(2), P(3));
}
static void seh_context(uint32_t *r) { bb_crt_seh_context_unwind_entry(P(0)); }
static void seh_rtl(uint32_t *r) {
  bb_crt_seh_rtl_unwind_entry(P(0), P(1), P(2), P(3));
}
static void cpp_internal(uint32_t *r) {
  r[7] =
      bb_crt_cpp_internal_entry(P(0), P(1), P(2), P(3), P(4), A(5), P(6), A(7));
}
static void cpp_find(uint32_t *r) {
  bb_crt_cpp_find_entry(P(0), P(1), P(2), P(3), P(4), A(5), A(6), P(7));
}
static void cpp_foreign(uint32_t *r) {
  bb_crt_cpp_foreign_entry(P(0), P(1), P(2), P(3), P(4), A(5), A(6), P(7));
}
static void cpp_catch(uint32_t *r) {
  bb_crt_cpp_catch_entry(P(0), P(1), P(2), P(3), P(4), P(5), P(6), P(7), A(8),
                         P(9));
}
static void cpp_call_catch(uint32_t *r) {
  r[7] = bb_crt_cpp_call_catch_entry(P(0), P(1), P(2), P(3), P(4), A(5), A(6));
}
static void cpp_call_catch2(uint32_t *r) {
  r[7] = bb_crt_cpp_call_catch2_entry(P(0), P(1), P(2), A(3), A(4));
}
static void cpp_catch_guard(uint32_t *r) {
  r[7] = bb_crt_cpp_catch_guard_entry(P(0), P(1), P(2), P(3));
}
static void cpp_catch_filter(uint32_t *r) { bb_crt_cpp_catch_filter_c(r); }
static void cpp_catch_cleanup(uint32_t *r) { bb_crt_cpp_catch_cleanup_c(r[2]); }
static void cpp_catch_landing(uint32_t *r) {
  uint32_t frame = r[2];
  BB_U32(frame - 40) = BB_U32(frame - 44) = 0;
  if (BB_I32(frame - 4) >= 0)
    bb_crt_cpp_catch_cleanup_c(frame);
  BB_I32(frame - 4) = -1;
  if (bb_model_exceptions()->catch_frame != frame)
    bb_model_exceptions()->registration = BB_U32(frame - 16);
  r[7] = 0;
}
static void cpp_continue(uint32_t *r) { bb_crt_cpp_continue_entry(P(0), P(1)); }
static void cpp_nested(uint32_t *r) {
  bb_crt_cpp_nested_unwind_entry(P(0), P(1));
}
static void cpp_translate(uint32_t *r) {
  r[7] = bb_crt_cpp_translate_entry(P(0), P(1), P(2), P(3), P(4), A(5), P(6));
}
static void cpp_translate_guard(uint32_t *r) {
  r[7] = bb_crt_cpp_translate_guard_entry(P(0), P(1), P(2), P(3));
}
static void cpp_translate_resume(uint32_t *r) {
  bb_crt_cpp_translate_resume_c(r[2]);
  r[7] = BB_U32(r[2] - 52);
}
static void cpp_range(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_cpp_try_range_entry(P(0), A(1), A(2), P(3), P(4)));
}
static void cpp_match(uint32_t *r) {
  r[7] = bb_crt_cpp_type_match_entry(P(0), P(1), P(2));
}
static void cpp_adjust(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_cpp_adjust_pointer_entry(P(0), P(1)));
}
static void cpp_unwind_filter(uint32_t *r) {
  r[7] = bb_crt_cpp_unwind_filter_entry(P(0));
}
static void cpp_rethrow_filter(uint32_t *r) {
  r[7] = bb_crt_cpp_rethrow_filter_entry(P(0));
}
static void cpp_build(uint32_t *r) {
  bb_crt_cpp_build_entry(P(0), P(1), P(2), P(3));
}
static void cpp_destroy(uint32_t *r) { bb_crt_cpp_destroy_entry(P(0), A(1)); }
static void cpp_unwind(uint32_t *r) {
  bb_crt_cpp_frame_unwind_entry(P(0), P(1), P(2), A(3));
}
static void cpp_frame_filter(uint32_t *r) { bb_crt_cpp_filter_c(r, 2); }
static void cpp_build_filter(uint32_t *r) { bb_crt_cpp_filter_c(r, 0); }
static void cpp_destroy_filter(uint32_t *r) { bb_crt_cpp_filter_c(r, 1); }
static void cpp_frame_catch(uint32_t *r) { bb_crt_cpp_frame_catch_c(r[2]); }
static void cpp_terminate_catch(uint32_t *r) {
  bb_crt_cpp_terminate_catch_c(r[2]);
}
static void cpp_member0(uint32_t *r) { bb_crt_cpp_member0_entry(P(0), P(1)); }
static void cpp_member1(uint32_t *r) {
  bb_crt_cpp_member1_entry(P(0), P(1), P(2));
}
static void cpp_member2(uint32_t *r) {
  bb_crt_cpp_member2_entry(P(0), P(1), P(2), A(3));
}
static void cpp_call_frame(uint32_t *r) {
  r[7] = bb_crt_cpp_call_frame_entry(P(0), P(1), A(2));
}
#define CLEANUP(id, name, offset) extern void name##_c(uint32_t *);
BB_ALLOCATION_CLEANUPS(CLEANUP)
#undef CLEANUP
#define HANDLER(id, name, cleanup)                                             \
  static void model_##name(uint32_t *r) {                                      \
    r[7] = name##_entry(P(0), P(1), P(2), P(3));                               \
  }
BB_ALLOCATION_HANDLERS(HANDLER)
#undef HANDLER
struct target {
  struct bb_model_target target;
  void (*worker)(uint32_t *);
};
static void dispatch(uint32_t *);
#define T(id, worker, argc, reserve) {{id, dispatch, argc, reserve, 0}, worker}
static const struct target targets[] = {
    T(0x46f71c, seh_global, 1, 0),
    T(0x46f73c, seh_nested, 4, 0),
    T(0x46f75e, seh_local, 2, 0),
    T(0x46f7c6, seh_abnormal, 0, 0),
    T(0x46f7e9, seh_notice, 1, 0),
    T(0x46f7f2, seh_frame_notice, 1, 0),
    T(0x476478, seh_handler, 4, 0),
    T(0x476535, seh_context, 1, 0),
    T(0x481996, seh_rtl, 4, 0),
    T(0x46d250, bb_crt_errors_stack_probe_c, 0, 128),
    T(0x478e50, cpp_internal, 8, 0),
    T(0x478f70, cpp_find, 8, 0),
    T(0x479220, cpp_foreign, 8, 0),
    T(0x479660, cpp_catch, 10, 0),
    T(0x479740, cpp_call_catch, 7, 0),
    T(0x46f920, cpp_call_catch2, 5, 0),
    T(0x46f990, cpp_catch_guard, 4, 0),
    T(0x4797d1, cpp_catch_filter, 0, 0),
    T(0x4797e3, cpp_catch_landing, 0, 0),
    T(0x479822, cpp_catch_cleanup, 0, 0),
    T(0x46f810, cpp_continue, 2, 0),
    T(0x46f880, cpp_nested, 2, 0),
    T(0x46f9d0, cpp_translate, 7, 0),
    T(0x46fab0, cpp_translate_guard, 4, 0),
    T(0x46fa67, cpp_translate_resume, 0, 0),
    T(0x46f8e0, bb_crt_cpp_handler_c, 4, 128),
    T(0x479340, cpp_range, 5, 0),
    T(0x479430, cpp_match, 3, 0),
    T(0x479c90, cpp_adjust, 2, 0),
    T(0x479610, cpp_unwind_filter, 1, 0),
    T(0x4798a0, cpp_rethrow_filter, 1, 0),
    T(0x479910, cpp_build, 4, 0),
    T(0x479bf0, cpp_destroy, 2, 0),
    T(0x479500, cpp_unwind, 4, 0),
    T(0x4795a4, cpp_frame_filter, 0, 0),
    T(0x479bb8, cpp_build_filter, 0, 0),
    T(0x479c59, cpp_destroy_filter, 0, 0),
    T(0x4795b6, cpp_frame_catch, 0, 0),
    T(0x479bc3, cpp_terminate_catch, 0, 0),
    T(0x479c6a, cpp_terminate_catch, 0, 0),
    T(0x46f850, cpp_member0, 2, 0),
    T(0x46f860, cpp_member1, 3, 0),
    T(0x46f870, cpp_member2, 4, 0),
    T(0x479ce0, cpp_call_frame, 3, 0),
#define CLEANUP(id, name, offset) T(id, name##_c, 0, 128),
    BB_ALLOCATION_CLEANUPS(CLEANUP)
#undef CLEANUP
#define HANDLER(id, name, cleanup) T(id, model_##name, 4, 0),
        BB_ALLOCATION_HANDLERS(HANDLER)
#undef HANDLER
};
#undef T
static const struct target *find(uint32_t id) {
  for (unsigned i = 0; i < sizeof targets / sizeof *targets; ++i)
    if (targets[i].target.identity == id)
      return &targets[i];
  return NULL;
}
static void dispatch(uint32_t *r) {
  const struct target *target = find(bb_model_current_identity());
  ++bb_model_exceptions()->dispatch_depth;
  target->worker(r);
  bb_crt_model_finish(r);
}
const struct bb_model_target *bb_crt_exception_model_find(void *unused,
                                                          uint32_t id) {
  (void)unused;
  const struct target *target = find(id);
  return target ? &target->target : NULL;
}
