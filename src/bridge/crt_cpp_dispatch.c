/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_cpp_dispatch.h"
#include "crt_cpp_objects.h"
#include "crt_seh.h"
#include "crt_signals.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "crt_exception_runtime.h"
#include <string.h>
#endif
#define BYTE(a) BB_U8(a)
#ifndef BB_CPP_RECORD
#define BB_CPP_RECORD 0x48e01c
#define BB_CPP_CONTEXT 0x48e020
#define BB_CPP_TRANSLATOR 0x48e030
#define BB_CPP_CATCH_GUARD 0x46f990
#define BB_CPP_TRANSLATE_GUARD 0x46fab0
#define BB_CPP_TRANSLATE_RESUME 0x46fa67
#define BB_CPP_CATCH_TABLE 0x4833d0
#define BB_CPP_SEH_HANDLER 0x476478
#define BB_CC_RESULT -40
#define BB_CC_SAVED_STACK -36
#define BB_CC_PREVIOUS_RECORD -28
#define BB_CC_PREVIOUS_CONTEXT -32
#define BB_CC_RETURN_TEMP -44
#define BB_CC_FRAME_EXTRA "subl $20,%esp;"
#endif
static int32_t cpp_record(uintptr_t r) {
  return BB_U32(r) == 0xe06d7363 && BB_U32(r + 16) == 3 &&
         BB_U32(r + 20) == 0x19930520;
}
static uintptr_t head(void) {
#ifdef BB_MODEL_RUNTIME
  return bb_model_exceptions()->registration;
#else
  uintptr_t r;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(r));
  return r;
#endif
}
static void set_head(uintptr_t r) {
#ifdef BB_MODEL_RUNTIME
  bb_model_exceptions()->registration = r;
#else
  __asm__ volatile("movl %0,%%fs:0" ::"r"(r) : "memory");
#endif
}
uint32_t bb_crt_cpp_internal_entry(void *record, void *node, void *context,
                                   void *dispatcher, const void *info,
                                   int32_t depth, void *guard,
                                   uint32_t translated) {
  uintptr_t r = (uintptr_t)record, i = (uintptr_t)info;
  if (BB_U32(i) != 0x19930520)
    bb_crt_signal_inconsistent_entry();
  if (BYTE(r + 4) & 0x66) {
    if (BB_U32(i + 4) && !depth)
      bb_crt_cpp_frame_unwind_entry(node, dispatcher, info, -1);
    return 1;
  }
  if (BB_U32(i + 12)) {
    if (BB_U32(r) == 0xe06d7363 && BB_U32(r + 20) > 0x19930520 &&
        BB_U32(BB_U32(r + 28) + 8)) {
#ifdef BB_MODEL_RUNTIME
      uint32_t args[] = {bb_address_encode(record), bb_address_encode(node),
                         bb_address_encode(context), bb_address_encode(dispatcher),
                         bb_address_encode(info), depth, bb_address_encode(guard), translated};
      return bb_crt_model_call(BB_U32(BB_U32(r + 28) + 8), 0, 0, args, 8);
#else
      return (
          (uint32_t (*)(void *, void *, void *, void *, const void *, int32_t,
                        void *, uint32_t))BB_CODE(BB_POINTER(BB_U32(r + 28) + 8)))(
          record, node, context, dispatcher, info, depth, guard, translated);
#endif
    }
    bb_crt_cpp_find_entry(record, node, context, dispatcher, info, translated,
                          depth, guard);
  }
  return 1;
}
void bb_crt_cpp_find_entry(void *record, void *node, void *context,
                           void *dispatcher, const void *info,
                           uint32_t translated, int32_t depth, void *guard) {
  uintptr_t r = (uintptr_t)record, n = (uintptr_t)node, i = (uintptr_t)info;
  int32_t state = BB_I32(n + 8);
  if (state < -1 || BB_I32(i + 4) <= state)
    bb_crt_signal_inconsistent_entry();
  if (cpp_record(r) && !BB_U32(r + 28)) {
    if (!BB_U32(BB_CPP_RECORD))
      return;
    r = BB_U32(BB_CPP_RECORD);
    context = BB_POINTER(BB_CPP_CONTEXT);
    if (!bb_crt_signal_validate_read_entry((void *)r, 1))
      bb_crt_signal_inconsistent_entry();
    if (cpp_record(r) && !BB_U32(r + 28))
      bb_crt_signal_inconsistent_entry();
  }
  if (cpp_record(r)) {
    uint32_t first, last;
    uintptr_t row = (uintptr_t)bb_crt_cpp_try_range_entry(info, depth, state,
                                                          &first, &last);
    for (; first < last; ++first, row += 20) {
      if (BB_I32(row) > state || state > BB_I32(row + 4))
        continue;
      uintptr_t handler = BB_U32(row + 16);
      int32_t handlers = BB_I32(row + 12);
      int32_t matched = 0;
      for (; handlers && !matched; --handlers, handler += 16) {
        uintptr_t types = BB_U32(BB_U32(r + 28) + 12) + 4;
        int32_t count = BB_I32(BB_U32(BB_U32(r + 28) + 12));
        for (; count; --count, types += 4)
          if (bb_crt_cpp_type_match_entry((void *)handler, BB_POINTER(types),
                                          BB_POINTER(r + 28))) {
            bb_crt_cpp_catch_entry((void *)r, node, context, dispatcher, info,
                                   (void *)handler, BB_POINTER(types),
                                   (void *)row, depth, guard);
#ifdef BB_MODEL_RUNTIME
            if (bb_model_exceptions()->pending_target)
              return;
#endif
            matched = 1;
            break;
          }
      }
    }
    if ((uint8_t)translated)
      bb_crt_cpp_destroy_entry((void *)r, 1);
  } else if (!(uint8_t)translated)
    bb_crt_cpp_foreign_entry((void *)r, node, context, dispatcher, info, state,
                             depth, guard);
  else
    bb_crt_signal_terminate_entry();
}
void bb_crt_cpp_foreign_entry(void *record, void *node, void *context,
                              void *dispatcher, const void *info, int32_t state,
                              int32_t depth, void *guard) {
  if (BB_U32(BB_CPP_TRANSLATOR) &&
      bb_crt_cpp_translate_entry(record, node, context, dispatcher, info, depth,
                                 guard))
    return;
  uint32_t first, last;
  uintptr_t row =
      (uintptr_t)bb_crt_cpp_try_range_entry(info, depth, state, &first, &last);
  for (; first < last; ++first, row += 20) {
    if (BB_I32(row) > state || state > BB_I32(row + 4))
      continue;
    uintptr_t handler = BB_U32(row + 16) + (BB_U32(row + 12) - 1) * 16;
    if (!BB_U32(handler + 4) || !BYTE(BB_U32(handler + 4) + 8)) {
      bb_crt_cpp_catch_entry(record, node, context, dispatcher, info,
                             (void *)handler, (void *)0, (void *)row, depth,
                             guard);
#ifdef BB_MODEL_RUNTIME
      if (bb_model_exceptions()->pending_target)
        return;
#endif
    }
  }
}
void bb_crt_cpp_catch_entry(void *record, void *node, void *context,
                            void *dispatcher, const void *info,
                            const void *handler, const void *type,
                            const void *row, int32_t depth, void *guard) {
  if (type)
    bb_crt_cpp_build_entry(record, node, handler, type);
  bb_crt_cpp_nested_unwind_entry(guard ? guard : node, record);
  bb_crt_cpp_frame_unwind_entry(node, dispatcher, info, BB_I32(row));
  BB_U32((uintptr_t)node + 8) = BB_U32((uintptr_t)row + 4) + 1;
  uintptr_t continuation = bb_crt_cpp_call_catch_entry(
      record, node, context, info, BB_POINTER((uintptr_t)handler + 12), depth,
      0x100);
#ifdef BB_MODEL_RUNTIME
  if (bb_model_exceptions()->pending_target)
    return;
#endif
  if (continuation)
    bb_crt_cpp_continue_entry((void *)continuation, node);
}
uint32_t bb_crt_cpp_catch_guard_entry(void *record, void *registration,
                                      void *context,
                                      void *dispatcher
                                      __attribute__((unused))) {
#ifndef BB_MODEL_RUNTIME
  __asm__ volatile("cld" ::: "cc");
#endif
  uintptr_t g = (uintptr_t)registration;
  return bb_crt_cpp_internal_entry(record, BB_POINTER(g + 12), context,
                                   (void *)0, BB_POINTER(g + 8), BB_I32(g + 16),
                                   registration, 0);
}
void __attribute__((used, noinline)) bb_crt_cpp_call_catch2_c(uintptr_t f) {
  BB_U32(f - 16) = BB_CPP_CATCH_GUARD;
  BB_U32(f - 12) = BB_U32(f + 12);
  BB_U32(f - 8) = BB_U32(f + 8);
  BB_U32(f - 4) = BB_U32(f + 20) + 1;
  BB_U32(f - 20) = head();
  set_head(f - 20);
  BB_U32(f - 24) = bb_crt_cpp_call_frame_entry(
      BB_POINTER(f + 16), BB_POINTER(f + 8), BB_U32(f + 24));
  set_head(BB_U32(f - 20));
}
#ifdef BB_MODEL_RUNTIME
static unsigned consumed(uint32_t frame) {
  uint32_t scope = bb_model_exceptions()->catch_frame;
  for (unsigned n = 0; scope && n < 65536; ++n, scope = BB_U32(scope - 48))
    if (scope == frame)
      return BB_U32(scope - 52);
  return 0;
}
static void consume(uint32_t record) {
  uint32_t scope = bb_model_exceptions()->catch_frame;
  for (unsigned n = 0; scope && n < 65536; ++n, scope = BB_U32(scope - 48))
    if (BB_U32(scope + 8) == record)
      BB_U32(scope - 52) = 1;
}
#endif
void __attribute__((used, noinline)) bb_crt_cpp_catch_cleanup_c(uintptr_t f) {
#ifdef BB_CC_CACHED_NODE_SLOT
  BB_U32(BB_U32(f + BB_CC_CACHED_NODE_SLOT)) = BB_U32(f + BB_CC_SAVED_STACK);
#else
  BB_U32(BB_U32(f + 12) - 4) = BB_U32(f + BB_CC_SAVED_STACK);
#endif
  BB_U32(BB_CPP_RECORD) = BB_U32(f + BB_CC_PREVIOUS_RECORD);
  BB_U32(BB_CPP_CONTEXT) = BB_U32(f + BB_CC_PREVIOUS_CONTEXT);
  if (cpp_record(BB_U32(f + 8)) && BB_U32(f + BB_CC_RESULT)
#ifdef BB_MODEL_RUNTIME
      && !consumed(f)
#endif
  ) {
#ifdef BB_MODEL_RUNTIME
    consume(BB_U32(f + 8));
#endif
    bb_crt_cpp_destroy_entry(BB_POINTER(f + 8), bb_crt_seh_abnormal_entry());
  }
}
void __attribute__((used, noinline)) bb_crt_cpp_call_catch_c(uintptr_t f) {
  BB_U32(f + BB_CC_RESULT) = BB_U32(f + 24);
#ifdef BB_CC_CACHED_NODE_SLOT
  BB_U32(f + BB_CC_CACHED_NODE_SLOT) = BB_U32(f + 12) - 4;
#endif
  BB_U32(f + BB_CC_SAVED_STACK) = BB_U32(BB_U32(f + 12) - 4);
  BB_U32(f + BB_CC_PREVIOUS_RECORD) = BB_U32(BB_CPP_RECORD);
  BB_U32(f + BB_CC_PREVIOUS_CONTEXT) = BB_U32(BB_CPP_CONTEXT);
  BB_U32(BB_CPP_RECORD) = BB_U32(f + 8);
  BB_U32(BB_CPP_CONTEXT) = BB_U32(f + 16);
  BB_I32(f - 4) = 0;
  BB_I32(f - 4) = 1;
  BB_U32(f + BB_CC_RESULT) = bb_crt_cpp_call_catch2_entry(
      BB_POINTER(f + 12), BB_POINTER(f + 20), BB_POINTER(f + 24),
      BB_I32(f + 28), BB_U32(f + 32));
  BB_I32(f - 4) = 0;
  BB_I32(f - 4) = -1;
  bb_crt_cpp_catch_cleanup_c(f);
}
void __attribute__((used, noinline)) bb_crt_cpp_catch_filter_c(uint32_t *r) {
  r[7] = bb_crt_cpp_rethrow_filter_entry(BB_POINTER(r[2] - 20));
}
void __attribute__((used, noinline)) bb_crt_cpp_nested_finish_c(uintptr_t f) {
  BB_U32(BB_U32(f + 12) + 4) &= ~2u;
  uintptr_t previous = BB_U32(f - 8);
  BB_U32(previous) = head();
  set_head(previous);
}
void __attribute__((used, noinline)) bb_crt_cpp_translate_c(uintptr_t f) {
  BB_U32(f - 36) = BB_CPP_TRANSLATE_GUARD;
  BB_U32(f - 32) = BB_U32(f + 24);
  BB_U32(f - 28) = BB_U32(f + 12);
  BB_U32(f - 24) = BB_U32(f + 28);
  BB_U32(f - 20) = BB_U32(f + 32);
#ifdef BB_MODEL_RUNTIME
  BB_U32(f - 16) = BB_CPP_TRANSLATE_RESUME;
#else
  BB_U32(f - 16) = BB_CODE(BB_CPP_TRANSLATE_RESUME);
#endif
  BB_U32(f - 12) = f - 64;
  BB_U32(f - 8) = f;
  BB_U32(f - 4) = 0;
  BB_U32(f - 40) = head();
  set_head(f - 40);
  BB_U32(f - 52) = 1;
  BB_U32(f - 48) = BB_U32(f + 8);
  BB_U32(f - 44) = BB_U32(f + 16);
#ifdef BB_MODEL_RUNTIME
  uint32_t args[] = {BB_U32(BB_U32(f + 8)), f - 48};
  bb_crt_model_call(BB_U32(BB_CPP_TRANSLATOR), 0, 0, args, 2);
#else
  ((void (*)(uint32_t, const void *))BB_CODE(BB_POINTER(BB_CPP_TRANSLATOR)))(
      BB_U32(BB_U32(f + 8)), (void *)(f - 48));
#endif
  BB_U32(f - 52) = 0;
}
void __attribute__((used, noinline))
bb_crt_cpp_translate_resume_c(uintptr_t f) {
  if (BB_U32(f - 4)
#ifdef BB_MODEL_RUNTIME
      && BB_U32(f - 40) && BB_U32(f - 40) != UINT32_MAX
#endif
  ) {
    BB_U32(BB_U32(f - 40)) = BB_U32(head());
    set_head(BB_U32(f - 40));
  } else
    set_head(BB_U32(f - 40));
}
#ifdef BB_MODEL_RUNTIME
static void catch2_body(uint32_t *r) { bb_crt_cpp_call_catch2_c(r[2]); }
static void catch_body(uint32_t *r) { bb_crt_cpp_call_catch_c(r[2]); }
static void translate_body(uint32_t *r) { bb_crt_cpp_translate_c(r[2]); }
static uint32_t event_record(struct bb_model_scratch *scope,
                              const struct bb_model_exception *event) {
  bb_model_scratch_begin(scope, 800);
  memset(BB_SPAN(scope->address, 800), 0, 800);
  memcpy(BB_SPAN(scope->address, sizeof *event), event, sizeof *event);
  return scope->address;
}
uintptr_t bb_crt_cpp_call_catch2_entry(void *node, const void *info, void *function,
                                        int32_t depth, uint32_t kind) {
  struct bb_model_scratch scope;
  uint32_t f = bb_crt_model_frame(&scope), r[10] = {0}, error;
  BB_U32(f + 8) = bb_address_encode(node);
  BB_U32(f + 12) = bb_address_encode(info);
  BB_U32(f + 16) = bb_address_encode(function);
  BB_U32(f + 20) = depth;
  BB_U32(f + 24) = kind;
  r[2] = f;
  uintptr_t result;
  if (bb_model_try_body(catch2_body, r, &error))
    result = BB_U32(f - 24);
  else {
    struct bb_model_exception event = bb_model_caught_exception();
    struct bb_model_scratch raised;
    uint32_t record = event_record(&raised, &event), previous = head();
    set_head(f - 20);
    bb_crt_cpp_catch_guard_entry((void *)(uintptr_t)record,
                                  (void *)(uintptr_t)(f - 20),
                                  (void *)(uintptr_t)(record + 80), NULL);
    set_head(previous);
    if (!bb_model_exceptions()->pending_target)
      bb_model_raise_exception(&event);
    /* Preserve ownership while enclosing C catches restore their context. A
     * nested rethrow may already have consumed this same exception object. */
    result = bb_address_encode(function);
    bb_model_scratch_end(&raised);
  }
  bb_model_scratch_end(&scope);
  return result;
}
uintptr_t bb_crt_cpp_call_catch_entry(void *record, void *node, void *context,
                                       const void *info, void *function,
                                       int32_t depth, uint32_t kind) {
  struct bb_model_scratch scope;
  uint32_t f = bb_crt_model_frame(&scope), r[10] = {0}, error;
  BB_U32(f + 8) = bb_address_encode(record);
  BB_U32(f + 12) = bb_address_encode(node);
  BB_U32(f + 16) = bb_address_encode(context);
  BB_U32(f + 20) = bb_address_encode(info);
  BB_U32(f + 24) = bb_address_encode(function);
  BB_U32(f + 28) = depth;
  BB_U32(f + 32) = kind;
  BB_U32(f - 48) = bb_model_exceptions()->catch_frame;
  bb_model_exceptions()->catch_frame = f;
  r[2] = f;
  unsigned success = bb_model_try_body(catch_body, r, &error);
  struct bb_model_exception event = bb_model_caught_exception();
  unsigned rethrow = !success && event.code == 0xe06d7363 && event.count == 3 &&
                     event.arguments[0] == 0x19930520 && !event.arguments[2];
  if (!success) {
    if (rethrow)
      BB_U32(f + BB_CC_RESULT) = 0;
    ++bb_model_exceptions()->unwinding;
    bb_crt_cpp_catch_cleanup_c(f);
    --bb_model_exceptions()->unwinding;
  }
  uintptr_t result = BB_U32(f + BB_CC_RESULT);
  bb_model_exceptions()->catch_frame = BB_U32(f - 48);
  bb_model_scratch_end(&scope);
  if (!success && !rethrow)
    bb_model_raise_exception(&event);
  return rethrow || bb_model_exceptions()->pending_target ? 0 : result;
}
void BB_STDCALL bb_crt_cpp_nested_unwind_entry(void *node, void *record) {
  uint32_t previous = head(), stop = bb_address_encode(node);
  bb_crt_model_unwind(stop, bb_address_encode(record));
  BB_U32((uintptr_t)record + 4) &= ~2u;
  if (previous && previous != UINT32_MAX && previous != stop) {
    BB_U32(previous) = head();
    set_head(previous);
  }
}
void BB_STDCALL bb_crt_cpp_continue_entry(void *continuation, void *node) {
  bb_crt_model_continue(bb_address_encode(continuation), bb_address_encode(node) + 12u);
}
uint32_t bb_crt_cpp_translate_guard_entry(void *record, void *registration,
                                           void *context, void *dispatcher) {
  (void)dispatcher;
  uintptr_t g = (uintptr_t)registration;
  if (BYTE((uintptr_t)record + 4) & 0x66) {
    BB_U32(g + 36) = 1;
    return 1;
  }
  bb_crt_cpp_internal_entry(record, BB_POINTER(g + 12), context, NULL,
                              BB_POINTER(g + 8), BB_I32(g + 16),
                              BB_POINTER(g + 20), 1);
  if (!BB_U32(g + 36))
    bb_crt_cpp_nested_unwind_entry(registration, record);
  bb_crt_cpp_translate_resume_c(g + 40);
  return 1;
}
int32_t bb_crt_cpp_translate_entry(void *record, void *node, void *context,
                                    void *dispatcher, const void *info,
                                    int32_t depth, void *guard) {
  struct bb_model_scratch scope;
  uint32_t f = bb_crt_model_frame(&scope), r[10] = {0}, error;
  uint32_t args[] = {bb_address_encode(record), bb_address_encode(node),
                     bb_address_encode(context), bb_address_encode(dispatcher),
                     bb_address_encode(info), depth, bb_address_encode(guard)};
  for (unsigned i = 0; i < 7; ++i) BB_U32(f + 8 + i * 4u) = args[i];
  r[2] = f;
  if (!bb_model_try_body(translate_body, r, &error)) {
    struct bb_model_exception event = bb_model_caught_exception();
    struct bb_model_scratch raised;
    uint32_t translated = event_record(&raised, &event), previous = head();
    set_head(f - 40);
    bb_crt_cpp_translate_guard_entry((void *)(uintptr_t)translated,
                                      (void *)(uintptr_t)(f - 40),
                                      (void *)(uintptr_t)(translated + 80), NULL);
    set_head(previous);
    bb_model_scratch_end(&raised);
  }
  bb_crt_cpp_translate_resume_c(f);
  int32_t result = BB_I32(f - 52);
  bb_model_scratch_end(&scope);
  return result;
}
void bb_crt_cpp_handler_c(uint32_t *r) {
  uintptr_t a = r[3] + 136u;
  r[7] = bb_crt_cpp_internal_entry(BB_POINTER(a), BB_POINTER(a + 4),
                                    BB_POINTER(a + 8), BB_POINTER(a + 12),
                                    (void *)(uintptr_t)r[7], 0, NULL, 0);
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
static void __attribute__((naked, noinline, noreturn))
resume_guard(void *registration __attribute__((unused))) {
  __asm__ volatile(
      "movl 4(%esp),%ebx;movl 28(%ebx),%esp;movl 32(%ebx),%ebp;jmp *24(%ebx)");
}
uint32_t bb_crt_cpp_translate_guard_entry(void *record, void *registration,
                                          void *context,
                                          void *dispatcher
                                          __attribute__((unused))) {
  __asm__ volatile("cld" ::: "cc");
  uintptr_t g = (uintptr_t)registration;
  if (BYTE((uintptr_t)record + 4) & 0x66) {
    BB_U32(g + 36) = 1;
    return 1;
  }
  bb_crt_cpp_internal_entry(record, BB_POINTER(g + 12), context, (void *)0,
                            BB_POINTER(g + 8), BB_I32(g + 16),
                            BB_POINTER(g + 20), 1);
  if (!BB_U32(g + 36))
    bb_crt_cpp_nested_unwind_entry(registration, record);
  resume_guard(registration);
}
void __attribute__((used, noinline)) bb_crt_cpp_handler_c(uint32_t *r) {
  uintptr_t a = r[3] + 136;
  r[7] = bb_crt_cpp_internal_entry(BB_POINTER(a), BB_POINTER(a + 4),
                                   BB_POINTER(a + 8), BB_POINTER(a + 12),
                                   (void *)r[7], 0, (void *)0, 0);
}
uint32_t __attribute__((naked, noinline)) bb_crt_cpp_handler_entry(
    void *a __attribute__((unused)), void *b __attribute__((unused)),
    void *c __attribute__((unused)), void *d __attribute__((unused))) {
  __asm__ volatile(
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_cpp_handler_c) ";addl $4,%esp;popal;popfl;cld;lea "
                                "128(%esp),%esp;ret");
}
void __attribute__((stdcall, naked, noinline))
bb_crt_cpp_continue_entry(void *continuation __attribute__((unused)),
                          void *node __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;subl $4,%esp;pushl %ebx;pushl %esi;"
      "pushl %edi;movl 12(%ebp),%eax;addl $12,%eax;movl %eax,-4(%ebp);"
      "movl %fs:0,%ebx;movl (%ebx),%eax;movl %eax,%fs:0;movl 8(%ebp),%eax;"
      "movl 12(%ebp),%ebx;movl -4(%ebx),%esp;movl -4(%ebp),%ebp;" BB_JUMP_EAX);
}
void __attribute__((stdcall, naked, noinline))
bb_crt_cpp_nested_unwind_entry(void *node __attribute__((unused)),
                               void *record __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;subl $8,%esp;pushl %ebx;pushl %esi;"
      "pushl %edi;movl %fs:0,%eax;movl %eax,-8(%ebp);call 0f;0:popl %eax;"
      "addl $(1f-0b),%eax;movl %eax,-4(%ebp);pushl $0;pushl 12(%ebp);"
      "pushl -4(%ebp);pushl 8(%ebp);call " ASM_STDCALL(bb_crt_seh_rtl_unwind_entry, 16) ";1:lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_cpp_nested_finish_c) ";addl $4,%esp;lea 2048(%esp),%esp;popl "
                                      "%edi;popl %esi;popl %ebx;leave;ret $8");
}
uintptr_t __attribute__((naked, noinline)) bb_crt_cpp_call_catch2_entry(
    void *a __attribute__((unused)), const void *b __attribute__((unused)),
    void *c __attribute__((unused)), int32_t d __attribute__((unused)),
    uint32_t e __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;subl $24,%esp;pushl %ebx;pushl %esi;"
      "pushl %edi;lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_cpp_call_catch2_c) ";addl $4,%esp;lea 2048(%esp),%esp;movl "
                                    "-24(%ebp),%eax;popl %edi;popl %esi;popl "
                                    "%ebx;leave;ret");
}
#define RESTORE_CATCH                                                          \
  "movl -16(%ebp),%eax;movl %eax,%fs:0;popl %edi;popl %esi;popl %ebx;movl "    \
  ASM_NUMBER(BB_CC_RESULT) "(%ebp),%eax;leave;ret"
uintptr_t __attribute__((naked, noinline)) bb_crt_cpp_call_catch_entry(
    void *a __attribute__((unused)), void *b __attribute__((unused)),
    void *c __attribute__((unused)), const void *d __attribute__((unused)),
    void *e __attribute__((unused)), int32_t f __attribute__((unused)),
    uint32_t g __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl $-1;pushl $" ASM_NUMBER(BB_CPP_CATCH_TABLE) ";"
      "pushl $" ASM_NUMBER(BB_CPP_SEH_HANDLER) ";pushl %fs:0;movl %esp,%fs:0;subl $8,%esp;" BB_CC_FRAME_EXTRA
      "pushl %ebx;pushl %esi;pushl %edi;movl %esp,-24(%ebp);"
      "lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_cpp_call_catch_c) ";movl -24(%ebp),%esp;" RESTORE_CATCH);
}
int32_t __attribute__((naked, noinline)) bb_crt_cpp_catch_filter_entry(void) {
  __asm__ volatile(
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_cpp_catch_filter_c) ";addl $4,%esp;popal;popfl;lea "
                                     "128(%esp),%esp;ret");
}
void __attribute__((naked, noinline)) bb_crt_cpp_catch_cleanup_entry(void) {
  __asm__ volatile(
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %ebp;call " ASM_NAME(
          bb_crt_cpp_catch_cleanup_c) ";addl $4,%esp;popal;popfl;lea "
                                      "128(%esp),%esp;ret");
}
uintptr_t __attribute__((naked, noinline))
bb_crt_cpp_catch_landing_entry(void) {
  __asm__ volatile(
      "movl -24(%ebp),%esp;movl $0," ASM_NUMBER(BB_CC_RESULT) "(%ebp);movl $0," ASM_NUMBER(BB_CC_RETURN_TEMP) "(%ebp);"
      "pushl $-1;leal -16(%ebp),%eax;pushl %eax;call " ASM_NAME(
          bb_crt_seh_local_unwind_entry) ";addl $8,%esp;movl "
                                         "-16(%ebp),%eax;movl %eax,%fs:0;popl "
                                         "%edi;popl %esi;"
                                         "popl %ebx;movl "
                                         ASM_NUMBER(BB_CC_RETURN_TEMP) "(%ebp),%eax;leave;ret");
}
#define RESTORE_TRANSLATE                                                      \
  "popl %edi;popl %esi;popl %ebx;movl -52(%ebp),%eax;leave;ret"
int32_t __attribute__((naked, noinline)) bb_crt_cpp_translate_entry(
    void *a __attribute__((unused)), void *b __attribute__((unused)),
    void *c __attribute__((unused)), void *d __attribute__((unused)),
    const void *e __attribute__((unused)), int32_t f __attribute__((unused)),
    void *g __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;subl $52,%esp;pushl %ebx;pushl %esi;"
      "pushl %edi;lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_cpp_translate_c) ";addl $4,%esp;lea 2048(%esp),%esp;"
                                  "lea -2048(%esp),%esp;pushl "
                                  "%ebp;call " ASM_NAME(
                                      bb_crt_cpp_translate_resume_c) ";addl "
                                                                     "$4,%esp;"
                                                                     "lea "
                                                                     "2048(%"
                                                                     "esp),%"
                                                                     "esp"
                                                                     ";" RESTORE_TRANSLATE);
}
int32_t __attribute__((naked, noinline))
bb_crt_cpp_translate_resume_entry(void) {
  __asm__ volatile(
      "movl -12(%ebp),%esp;lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_cpp_translate_resume_c) ";addl $4,%esp;lea "
                                         "2048(%esp),%esp;" RESTORE_TRANSLATE);
}

#endif
