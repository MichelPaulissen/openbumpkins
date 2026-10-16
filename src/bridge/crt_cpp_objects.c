/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "crt_cpp_objects.h"
#include "crt_primitives.h"
#include "crt_seh.h"
#include "crt_signals.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "crt_exception_runtime.h"
#endif
#define BYTE(a) BB_U8(a)
#define WIN BB_STDCALL
#ifndef BB_CPP_BUILD_TABLE
#define BB_CPP_BUILD_TABLE 0x4833e8
#define BB_CPP_DESTROY_TABLE 0x4833f8
#define BB_CPP_FRAME_TABLE 0x4833c0
#define BB_CPP_SEH_HANDLER 0x476478
#define BB_CPP_FRAME_EXTRA "subl $4,%esp;"
#endif

int32_t bb_crt_cpp_type_match_entry(const void *handler, const void *type,
                                    const void *info) {
  uintptr_t h = (uintptr_t)handler, t = (uintptr_t)type, i = (uintptr_t)info;
  if (!BB_U32(h + 4) || !BYTE(BB_U32(h + 4) + 8))
    return 1;
  if (BB_U32(t + 4) != BB_U32(h + 4) &&
      bb_crt_string_compare_entry((const char *)(uintptr_t)(uint32_t)(BB_U32(h + 4) + 8),
                                  (const char *)(uintptr_t)(uint32_t)(BB_U32(t + 4) + 8)))
    return 0;
  return (!(BYTE(t) & 2) || (BYTE(h) & 8)) &&
         (!(BYTE(i) & 1) || (BYTE(h) & 1)) && (!(BYTE(i) & 2) || (BYTE(h) & 2));
}
void *bb_crt_cpp_adjust_pointer_entry(void *object, const void *displacement) {
  uintptr_t p = bb_address_encode(object), d = (uintptr_t)displacement;
  uint32_t result = p + BB_U32(d);
  if (BB_I32(d + 4) >= 0) {
    result += BB_U32(BB_U32(p + BB_U32(d + 4)) + BB_U32(d + 8));
    result += BB_U32(d + 4);
  }
  return (void *)(uintptr_t)result;
}
void *bb_crt_cpp_try_range_entry(const void *info, int32_t depth, int32_t state,
                                 uint32_t *first, uint32_t *last) {
  uintptr_t i = (uintptr_t)info, table = BB_U32(i + 16);
  uint32_t cursor = BB_U32(i + 12), previous = cursor, end = cursor;
  while (depth >= 0) {
    if (cursor == UINT32_MAX)
      bb_crt_signal_inconsistent_entry();
    --cursor;
    /* The sentinel row is read before testing cursor == -1 in the old code. */
    if ((BB_I32((uint32_t)(table + cursor * 20 + 4)) < state &&
         state <= BB_I32((uint32_t)(table + cursor * 20 + 8))) ||
        cursor == UINT32_MAX) {
      --depth;
      end = previous;
      previous = cursor;
    }
  }
  ++cursor;
  BB_U32(first) = cursor;
  BB_U32(last) = end;
  if (BB_U32(i + 12) < end || end < cursor)
    bb_crt_signal_inconsistent_entry();
  return (void *)(uintptr_t)(uint32_t)(table + cursor * 20);
}
int32_t bb_crt_cpp_unwind_filter_entry(const void *pointers) {
  if (BB_U32(BB_U32(pointers)) == 0xe06d7363)
    bb_crt_signal_terminate_entry();
  return 0;
}
int32_t bb_crt_cpp_rethrow_filter_entry(const void *pointers) {
  uintptr_t r = BB_U32(pointers);
  return BB_U32(r) == 0xe06d7363 && BB_U32(r + 16) == 3 &&
         BB_U32(r + 20) == 0x19930520 && !BB_U32(r + 28);
}
static uintptr_t handler(uintptr_t f) { return BB_U32(f + 16); }
static uintptr_t type(uintptr_t f) { return BB_U32(f + 20); }
static void *source(uintptr_t f) { return BB_POINTER(BB_U32(f + 8) + 24); }
static void *destination(uintptr_t f) { return BB_POINTER(f - 28); }
static int32_t valid_storage(uintptr_t f) {
  return bb_crt_signal_validate_read_entry(source(f), 1) &&
         bb_crt_signal_validate_write_entry(destination(f), 1);
}
static void *adjust_source(uintptr_t f) {
  return bb_crt_cpp_adjust_pointer_entry(source(f), (void *)(type(f) + 8));
}
void __attribute__((used, noinline)) bb_crt_cpp_build_c(uintptr_t frame) {
  if (!BB_U32(handler(frame) + 4) || !BYTE(BB_U32(handler(frame) + 4) + 8) ||
      !BB_U32(handler(frame) + 8))
    return;
  BB_U32(frame - 28) = BB_U32(handler(frame) + 8) + BB_U32(frame + 12) + 12;
  BB_I32(frame - 4) = 0;
  if (BYTE(handler(frame)) & 8) {
    if (valid_storage(frame)) {
      BB_SET_POINTER((uintptr_t)destination(frame), source(frame));
      BB_SET_POINTER((uintptr_t)destination(frame), bb_crt_cpp_adjust_pointer_entry(
              BB_POINTER((uintptr_t)destination(frame)),
              (void *)(type(frame) + 8)));
    } else
      bb_crt_signal_inconsistent_entry();
  } else if (BYTE(type(frame)) & 1) {
    if (valid_storage(frame)) {
      bb_crt_memory_move_entry(destination(frame), source(frame),
                               BB_U32(type(frame) + 20));
      if (BB_U32(type(frame) + 20) == 4 &&
          BB_U32((uintptr_t)destination(frame)))
        BB_SET_POINTER((uintptr_t)destination(frame), bb_crt_cpp_adjust_pointer_entry(
                BB_POINTER((uintptr_t)destination(frame)),
                (void *)(type(frame) + 8)));
    } else
      bb_crt_signal_inconsistent_entry();
  } else if (!BB_U32(type(frame) + 24)) {
    if (valid_storage(frame)) {
      uint32_t size = BB_U32(type(frame) + 20);
      void *p = adjust_source(frame);
      bb_crt_memory_move_entry(destination(frame), p, size);
    } else
      bb_crt_signal_inconsistent_entry();
  } else if (!valid_storage(frame) ||
             !bb_crt_signal_validate_code_entry(BB_POINTER(type(frame) + 24)))
    bb_crt_signal_inconsistent_entry();
  else if (BYTE(type(frame)) & 4) {
    void *p = adjust_source(frame);
    bb_crt_cpp_member2_entry(destination(frame), BB_POINTER(type(frame) + 24),
                             p, 1);
  } else {
    void *p = adjust_source(frame);
    bb_crt_cpp_member1_entry(destination(frame), BB_POINTER(type(frame) + 24),
                             p);
  }
  BB_I32(frame - 4) = -1;
}
void __attribute__((used, noinline)) bb_crt_cpp_destroy_c(uintptr_t frame) {
  uintptr_t record = BB_U32(frame + 8);
  if (record && BB_U32(BB_U32(record + 28) + 4)) {
    BB_I32(frame - 4) = 0;
    bb_crt_cpp_member0_entry(BB_POINTER(BB_U32(frame + 8) + 24),
                             BB_POINTER(BB_U32(BB_U32(frame + 8) + 28) + 4));
    BB_I32(frame - 4) = -1;
  }
}
static void advance_state(uintptr_t frame) {
  uintptr_t info = BB_U32(frame + 16);
  BB_U32(frame - 28) = BB_U32(BB_U32(info + 8) + BB_U32(frame - 28) * 8);
}
void __attribute__((used, noinline))
bb_crt_cpp_frame_unwind_c(uintptr_t frame, uint32_t resume) {
  if (!resume) {
#ifdef BB_CPP_CACHED_STATE_SLOT
    BB_U32(frame - 32) = BB_U32(frame + 8) + 8;
#endif
    BB_U32(frame - 28) = BB_U32(BB_U32(frame + 8) + 8);
  }
  while (BB_I32(frame - 28) != BB_I32(frame + 20)) {
    int32_t state = BB_I32(frame - 28);
    if (state < 0 || BB_I32(BB_U32(frame + 16) + 4) <= state)
      bb_crt_signal_inconsistent_entry();
    BB_I32(frame - 4) = 0;
    uintptr_t info = BB_U32(frame + 16);
    uintptr_t function = BB_U32(BB_U32(info + 8) + BB_U32(frame - 28) * 8 + 4);
#ifdef BB_CPP_CACHED_STATE_SLOT
    BB_U32(frame - 36) = function;
#endif
    if (function)
      bb_crt_cpp_call_frame_entry((void *)function, BB_POINTER(frame + 8),
                                  0x103);
    BB_I32(frame - 4) = -1;
    advance_state(frame);
  }
  if (BB_I32(frame - 28) != BB_I32(frame + 20))
    bb_crt_signal_inconsistent_entry();
#ifdef BB_CPP_CACHED_STATE_SLOT
  BB_U32(BB_U32(frame - 32)) = BB_U32(frame - 28);
#else
  BB_U32(BB_U32(frame + 8) + 8) = BB_U32(frame - 28);
#endif
}
void __attribute__((used, noinline)) bb_crt_cpp_frame_catch_c(uintptr_t frame) {
  BB_I32(frame - 4) = -1;
  advance_state(frame);
  bb_crt_cpp_frame_unwind_c(frame, 1);
}
void __attribute__((used, noinline))
bb_crt_cpp_terminate_catch_c(uintptr_t frame) {
  bb_crt_signal_terminate_entry();
  BB_I32(frame - 4) = -1;
}
void __attribute__((used, noinline)) bb_crt_cpp_filter_c(uint32_t *r,
                                                         uint32_t kind) {
  uintptr_t frame = r[2];
  r[7] = kind == 0   ? 1
         : kind == 1 ? !!BYTE(frame + 12)
                     : bb_crt_cpp_unwind_filter_entry(BB_POINTER(frame - 20));
}
#ifdef BB_MODEL_RUNTIME
static void build_body(uint32_t *r) { bb_crt_cpp_build_c(r[2]); }
static void destroy_body(uint32_t *r) { bb_crt_cpp_destroy_c(r[2]); }
static void unwind_body(uint32_t *r) { bb_crt_cpp_frame_unwind_c(r[2], 0); }
static void guarded(void (*body)(uint32_t *), uintptr_t frame,
                    unsigned policy) {
  uint32_t r[10] = {0}, error;
  r[2] = frame;
  if (!bb_model_try_body(body, r, &error)) {
    struct bb_model_exception exception = bb_model_caught_exception();
    if (policy == 1 || (policy == 2 && exception.code == 0xe06d7363))
      bb_crt_signal_terminate_entry();
    bb_model_raise_exception(&exception);
  }
}
void bb_crt_cpp_build_entry(void *record, void *node, const void *handler,
                             const void *type) {
  struct bb_model_scratch scratch;
  uint32_t frame = bb_crt_model_frame(&scratch);
  BB_U32(frame + 8) = bb_address_encode(record);
  BB_U32(frame + 12) = bb_address_encode(node);
  BB_U32(frame + 16) = bb_address_encode(handler);
  BB_U32(frame + 20) = bb_address_encode(type);
  guarded(build_body, frame, 1);
  bb_model_scratch_end(&scratch);
}
void bb_crt_cpp_destroy_entry(void *record, uint32_t unwinding) {
  struct bb_model_scratch scratch;
  uint32_t frame = bb_crt_model_frame(&scratch);
  BB_U32(frame + 8) = bb_address_encode(record);
  BB_U32(frame + 12) = unwinding;
  guarded(destroy_body, frame, !!(uint8_t)unwinding);
  bb_model_scratch_end(&scratch);
}
void bb_crt_cpp_frame_unwind_entry(void *node, void *dispatcher,
                                    const void *info, int32_t stop) {
  struct bb_model_scratch scratch;
  uint32_t frame = bb_crt_model_frame(&scratch);
  BB_U32(frame + 8) = bb_address_encode(node);
  BB_U32(frame + 12) = bb_address_encode(dispatcher);
  BB_U32(frame + 16) = bb_address_encode(info);
  BB_U32(frame + 20) = stop;
  ++bb_model_exceptions()->unwinding;
  guarded(unwind_body, frame, 2);
  --bb_model_exceptions()->unwinding;
  bb_model_scratch_end(&scratch);
}
void BB_STDCALL bb_crt_cpp_member0_entry(void *self, void *function) {
  bb_crt_model_call(bb_address_encode(function), 0, bb_address_encode(self), NULL, 0);
}
void BB_STDCALL bb_crt_cpp_member1_entry(void *self, void *function, void *source) {
  uint32_t args[] = {bb_address_encode(source)};
  bb_crt_model_call(bb_address_encode(function), 0, bb_address_encode(self), args, 1);
}
void BB_STDCALL bb_crt_cpp_member2_entry(void *self, void *function, void *source,
                                        uint32_t virtual_base) {
  uint32_t args[] = {bb_address_encode(source), virtual_base};
  bb_crt_model_call(bb_address_encode(function), 0, bb_address_encode(self), args, 2);
}
uintptr_t BB_STDCALL bb_crt_cpp_call_frame_entry(void *function, void *node,
                                                 uint32_t kind) {
  uint32_t target = bb_address_encode(function), owner = bb_address_encode(node) + 12u;
  bb_crt_model_notice(kind, target, owner);
  uint32_t result = bb_crt_model_call(target, owner, kind, NULL, 0);
  bb_crt_model_notice(kind == 0x100 ? 2 : kind, result, owner);
  return result;
}
#else
#ifdef _WIN32
#define ASM_NAME_INNER(n) "_" #n
#define ASM_STDCALL_INNER(n, bytes) "_" #n "@" #bytes
#else
#define ASM_NAME_INNER(n) #n
#define ASM_STDCALL_INNER(n, bytes) #n
#endif
#define ASM_NAME(n) ASM_NAME_INNER(n)
#define ASM_STDCALL(n, bytes) ASM_STDCALL_INNER(n, bytes)
#define ASM_NUMBER_INNER(n) #n
#define ASM_NUMBER(n) ASM_NUMBER_INNER(n)
/* The member bridges transfer this/arguments and reuse the caller's return
 * address. All construction/destruction decisions remain in the C worker. */
#define MEMBER_ENTRY(name, args)                                               \
  void __attribute__((stdcall, naked, noinline)) name args {                   \
    __asm__ volatile("popl %eax;popl %ecx;xchgl %eax,(%esp);" BB_JUMP_EAX);    \
  }
MEMBER_ENTRY(bb_crt_cpp_member0_entry,
             (void *a __attribute__((unused)), void *b __attribute__((unused))))
MEMBER_ENTRY(bb_crt_cpp_member1_entry,
             (void *a __attribute__((unused)), void *b __attribute__((unused)),
              void *c __attribute__((unused))))
MEMBER_ENTRY(bb_crt_cpp_member2_entry,
             (void *a __attribute__((unused)), void *b __attribute__((unused)),
              void *c __attribute__((unused)),
              uint32_t d __attribute__((unused))))
uintptr_t __attribute__((stdcall, naked, noinline))
bb_crt_cpp_call_frame_entry(void *function __attribute__((unused)),
                            void *registration __attribute__((unused)),
                            uint32_t kind __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;subl $4,%esp;pushl %ebx;pushl %ecx;"
      "movl 12(%ebp),%eax;addl $12,%eax;movl %eax,-4(%ebp);movl 8(%ebp),%eax;"
      "pushl %ebp;pushl 16(%ebp);movl 16(%ebp),%ecx;movl "
      "-4(%ebp),%ebp;call " ASM_STDCALL(
          bb_crt_seh_notice_entry,
          4) ";" BB_CALL_EAX ";movl %ebp,%ebx;popl %ebp;"
             "movl 16(%ebp),%ecx;pushl %ebp;movl %ebx,%ebp;cmpl $0x100,%ecx;"
             "jne 1f;movl $2,%ecx;1:pushl %ecx;call " ASM_STDCALL(
                 bb_crt_seh_notice_entry,
                 4) ";popl %ebp;popl %ecx;popl %ebx;leave;ret $12");
}
#define RESTORE_FRAME                                                          \
  "movl -16(%ebp),%eax;movl %eax,%fs:0;popl %edi;popl %esi;popl "              \
  "%ebx;leave;ret"
/* Public prototypes give the legacy stack arguments; the adapter itself uses
 * no C parameters and the worker reads their live frame slots. */
#define GUARDED(name, args, table, extra, worker, more)                        \
  void __attribute__((naked, noinline)) name args {                            \
    __asm__ volatile(                                                          \
        "pushl %ebp;movl %esp,%ebp;pushl $-1;pushl $" ASM_NUMBER(table)        \
        ";pushl $" ASM_NUMBER(BB_CPP_SEH_HANDLER)                             \
        ";pushl %fs:0;movl %esp,%fs:0;subl $8,%esp;" extra                     \
        "pushl %ebx;pushl %esi;pushl %edi;movl %esp,-24(%ebp);"                \
        "lea -2048(%esp),%esp;" more "pushl %ebp;call " ASM_NAME(              \
            worker) ";movl -24(%ebp),%esp;" RESTORE_FRAME);                    \
  }
GUARDED(bb_crt_cpp_build_entry,
        (void *a __attribute__((unused)), void *b __attribute__((unused)),
         const void *c __attribute__((unused)),
         const void *d __attribute__((unused))),
        BB_CPP_BUILD_TABLE, "subl $4,%esp;", bb_crt_cpp_build_c, "")
GUARDED(bb_crt_cpp_destroy_entry,
        (void *a __attribute__((unused)), uint32_t b __attribute__((unused))),
        BB_CPP_DESTROY_TABLE, "", bb_crt_cpp_destroy_c, "")
GUARDED(bb_crt_cpp_frame_unwind_entry,
        (void *a __attribute__((unused)), void *b __attribute__((unused)),
         const void *c __attribute__((unused)),
         int32_t d __attribute__((unused))),
        BB_CPP_FRAME_TABLE, BB_CPP_FRAME_EXTRA, bb_crt_cpp_frame_unwind_c, "pushl $0;")
#define FILTER_ENTRY(name, kind)                                               \
  int32_t __attribute__((naked, noinline)) name(void) {                        \
    __asm__ volatile("lea -128(%esp),%esp;pushfl;pushal;cld;pushl $" #kind     \
                     ";leal 4(%esp),%eax;pushl %eax;call " ASM_NAME(           \
                         bb_crt_cpp_filter_c) ";addl $8,%esp;popal;popfl;lea " \
                                              "128(%esp),%esp;ret");           \
  }
FILTER_ENTRY(bb_crt_cpp_build_filter_entry, 0)
FILTER_ENTRY(bb_crt_cpp_destroy_filter_entry, 1)
FILTER_ENTRY(bb_crt_cpp_frame_filter_entry, 2)
#define CATCH_ENTRY(name, worker)                                              \
  void __attribute__((naked, noinline)) name(void) {                           \
    __asm__ volatile("movl -24(%ebp),%esp;lea -2048(%esp),%esp;"               \
                     "pushl %ebp;call " ASM_NAME(                              \
                         worker) ";movl -24(%ebp),%esp;" RESTORE_FRAME);       \
  }
CATCH_ENTRY(bb_crt_cpp_build_catch_entry, bb_crt_cpp_terminate_catch_c)
CATCH_ENTRY(bb_crt_cpp_destroy_catch_entry, bb_crt_cpp_terminate_catch_c)
CATCH_ENTRY(bb_crt_cpp_frame_catch_entry, bb_crt_cpp_frame_catch_c)

#endif
