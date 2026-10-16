/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "crt_seh.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "crt_exception_runtime.h"
#include <string.h>
#endif

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

/* The EXE and mixer use the same CRT scope algorithm with private module
 * state. A second translation unit supplies its addresses and symbol prefix. */
#ifndef BB_SEH_NESTED_ADDRESS
#define BB_SEH_NESTED_ADDRESS 0x46f73c
#define BB_SEH_NOTICE_KIND 0x48ccb8
#define BB_SEH_NOTICE_FUNCTION 0x48ccb4
#define BB_SEH_NOTICE_OWNER 0x48ccbc
#define BB_SEH_RTL_IMPORT 0x6ca444
#endif

static uintptr_t exception_head(void) {
#ifdef BB_MODEL_RUNTIME
  return bb_model_exceptions()->registration;
#else
  uintptr_t head;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(head));
  return head;
#endif
}
static void notice(uint32_t kind, uintptr_t function, uintptr_t owner) {
  BB_U32(BB_SEH_NOTICE_KIND) = kind;
  BB_U32(BB_SEH_NOTICE_FUNCTION) = function;
  BB_U32(BB_SEH_NOTICE_OWNER) = owner;
}
/* Scope funclets receive the owning function's EBP. They may transfer straight
 * to a catch landing and discard this entire dispatcher stack. */
static int32_t call_owner(uintptr_t function, uintptr_t owner) {
#ifdef BB_MODEL_RUNTIME
  return bb_crt_model_call(function, owner, 0, NULL, 0);
#else
  function = BB_CODE(function);
  int32_t result;
  __asm__ volatile("pushl %%ebp;movl %%esi,%%ebp;call *%%edi;popl %%ebp"
                   : "=a"(result)
                   : "S"(owner), "D"(function)
                   : "ecx", "edx", "cc", "memory");
  return result;
#endif
}
/* Only the adapter changes EBP/FS; C owns scope traversal and notification. */
#ifdef BB_MODEL_RUNTIME
extern void bb_crt_seh_local_unwind_c(uintptr_t, uintptr_t);
static void local_at(void *registration, int32_t level, uintptr_t owner) {
  struct bb_model_scratch scope;
  bb_model_scratch_begin(&scope, 40);
  uint32_t guard = scope.address, previous = bb_model_exceptions()->registration;
  memset(BB_SPAN(guard, 40), 0, 40);
  BB_U32(guard) = previous;
  BB_U32(guard + 4) = BB_SEH_NESTED_ADDRESS;
  BB_U32(guard + 8) = -2;
  BB_U32(guard + 12) = bb_address_encode(registration);
  BB_U32(guard + 32) = bb_address_encode(registration);
  BB_U32(guard + 36) = level;
  bb_model_exceptions()->registration = guard;
  bb_crt_seh_local_unwind_c(guard, owner);
  bb_model_exceptions()->registration = previous;
  bb_model_scratch_end(&scope);
}
void bb_crt_model_local_unwind(uint32_t registration, int32_t level, uint32_t owner) {
  local_at((void *)(uintptr_t)registration, level, owner);
}
#else
static void __attribute__((naked, noinline))
local_at(void *registration __attribute__((unused)),
         int32_t level __attribute__((unused)),
         uintptr_t owner __attribute__((unused))) {
  __asm__ volatile("pushl %ebp;movl 16(%esp),%ebp;pushl 12(%esp);"
                   "pushl 12(%esp);call " ASM_NAME(
                       bb_crt_seh_local_unwind_entry) ";addl $8,%esp;popl "
                                                      "%ebp;ret");
}
#endif
void __attribute__((used, noinline))
bb_crt_seh_local_unwind_c(uintptr_t guard, uintptr_t owner) {
  for (;;) {
    /* The old loop re-reads the caller's registration and target each time,
     * while the nested-unwind record retains the initial registration. */
    uintptr_t registration = BB_U32(guard + 32);
    uintptr_t table = BB_U32(registration + 8);
    int32_t level = BB_I32(registration + 12);
    if (level == -1 || level == BB_I32(guard + 36))
      return;
    uintptr_t row = table + (uint32_t)level * 12;
    BB_U32(guard + 8) = BB_U32(row);
    BB_U32(registration + 12) = BB_U32(guard + 8);
    if (!BB_U32(row + 4)) {
      uintptr_t function = BB_U32(row + 8);
      notice(BB_U32(owner + 8), function, owner);
      call_owner(function, owner);
    }
  }
}
int32_t bb_crt_seh_abnormal_entry(void) {
  uintptr_t head = exception_head();
#ifdef BB_MODEL_RUNTIME
  if (bb_model_exceptions()->unwinding || bb_model_exceptions()->pending_target)
    return 1;
  if (!head || head == UINT32_MAX)
    return 0;
#endif
  return BB_U32(head + 4) == BB_SEH_NESTED_ADDRESS &&
         BB_U32(head + 8) == BB_U32(BB_U32(head + 12) + 12);
}
void __attribute__((used, noinline)) bb_crt_seh_notice_c(uint32_t *r,
                                                         uint32_t from_frame) {
  notice(from_frame ? BB_U32(r[2] + 8) : r[6], r[7], r[2]);
}
int32_t bb_crt_seh_nested_entry(void *record, void *registration,
                                void *context __attribute__((unused)),
                                void *dispatcher) {
  if (BB_U32((uintptr_t)record + 4) & 6) {
    BB_U32(dispatcher) = bb_address_encode(registration);
    return 3;
  }
  return 1;
}
void __attribute__((used, noinline)) bb_crt_seh_handler_c(uintptr_t frame) {
  uintptr_t record = BB_U32(frame + 8);
  uintptr_t registration = BB_U32(frame + 12);
  if (BB_U32(record + 4) & 6) {
    local_at((void *)registration, -1, registration + 16);
    BB_U32(frame - 28) = 1;
    return;
  }
  BB_U32(frame - 8) = record;
  BB_U32(frame - 4) = BB_U32(frame + 16);
  BB_U32(registration - 4) = frame - 8;
  int32_t level = BB_I32(registration + 12);
  uintptr_t table = BB_U32(registration + 8);
  uintptr_t working_frame = frame;
  while (level != -1) {
    uintptr_t row = table + (uint32_t)level * 12;
    if (BB_U32(row + 4)) {
      int32_t disposition = call_owner(BB_U32(row + 4), registration + 16);
      registration = BB_U32(working_frame + 12);
      if (disposition < 0) {
        BB_U32(frame - 28) = 0;
        return;
      }
      if (disposition > 0) {
        table = BB_U32(registration + 8);
        bb_crt_seh_global_unwind_entry((void *)registration);
        working_frame = registration + 16;
        local_at((void *)registration, level, registration + 16);
        row = table + (uint32_t)level * 12;
        uintptr_t function = BB_U32(row + 8);
        notice(BB_U32(registration + 24), function, registration + 16);
        BB_U32(registration + 12) = BB_U32(row);
        call_owner(function, registration + 16);
#ifdef BB_MODEL_RUNTIME
        /* The converted landing is an ordinary C action. Its owner has already
         * been unwound; return a handled disposition instead of jumping to a
         * saved machine stack and instruction pointer. */
        BB_U32(frame - 28) = 0;
        return;
#endif
      }
    }
    table = BB_U32(registration + 8);
    level = BB_I32(table + (uint32_t)level * 12);
  }
  BB_U32(frame - 28) = 1;
}
void BB_STDCALL bb_crt_seh_context_unwind_entry(void *context) {
  uintptr_t p = (uintptr_t)context;
  local_at(BB_POINTER(p + 24), BB_I32(p + 28), BB_U32(p));
}
#ifdef BB_MODEL_RUNTIME
void BB_STDCALL bb_crt_seh_rtl_unwind_entry(void *registration, void *continuation,
                                           void *record, void *value) {
  (void)continuation;
  (void)value;
  bb_crt_model_unwind(bb_address_encode(registration), bb_address_encode(record));
}
void bb_crt_seh_global_unwind_entry(void *registration) {
  bb_crt_model_unwind(bb_address_encode(registration), 0);
}
void bb_crt_seh_local_unwind_entry(void *registration, int32_t level) {
  uint32_t node = bb_address_encode(registration);
  local_at(registration, level, node + 16u);
}
int32_t bb_crt_seh_handler_entry(void *record, void *registration, void *context,
                                 void *dispatcher) {
  struct bb_model_scratch scope;
  uint32_t frame = bb_crt_model_frame(&scope);
  BB_U32(frame + 8) = bb_address_encode(record);
  BB_U32(frame + 12) = bb_address_encode(registration);
  BB_U32(frame + 16) = bb_address_encode(context);
  BB_U32(frame + 20) = bb_address_encode(dispatcher);
  bb_crt_seh_handler_c(frame);
  int32_t result = BB_I32(frame - 28);
  bb_model_scratch_end(&scope);
  return result;
}
#else
/* RtlUnwind belongs to the platform. This tail adapter leaves the calling
 * frame intact for its continuation; Linux's full OS dispatcher is pending. */
void __attribute__((stdcall, naked, noinline))
bb_crt_seh_rtl_unwind_entry(void *registration __attribute__((unused)),
                            void *continuation __attribute__((unused)),
                            void *record __attribute__((unused)),
                            void *value __attribute__((unused))) {
  __asm__ volatile("jmp *" ASM_NUMBER(BB_SEH_RTL_IMPORT));
}
void __attribute__((naked, noinline))
bb_crt_seh_global_unwind_entry(void *registration __attribute__((unused))) {
  __asm__ volatile("pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;"
                   "pushl %edi;pushl %ebp;pushl $0;pushl $0;call 0f;"
                   "0:popl %eax;addl $(1f-0b),%eax;pushl %eax;"
                   "pushl 8(%ebp);call " ASM_STDCALL(
                       bb_crt_seh_rtl_unwind_entry, 16) ";1:popl %ebp;popl "
                                                        "%edi;popl %esi;popl "
                                                        "%ebx;leave;ret");
}
void __attribute__((naked, noinline))
bb_crt_seh_local_unwind_entry(void *registration __attribute__((unused)),
                              int32_t level __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebx;pushl %esi;pushl %edi;pushl 16(%esp);pushl $-2;"
      "pushl $" ASM_NUMBER(BB_SEH_NESTED_ADDRESS) ";pushl %fs:0;movl %esp,%fs:0;"
      "movl %esp,%eax;lea -2048(%esp),%esp;pushl %ebp;pushl "
      "%eax;call " ASM_NAME(
          bb_crt_seh_local_unwind_c) ";addl $8,%esp;lea 2048(%esp),%esp;"
                                     "popl %fs:0;addl $12,%esp;popl %edi;popl "
                                     "%esi;popl %ebx;ret");
}
#define NOTICE_ENTRY(name, from_frame)                                         \
  void __attribute__((stdcall, naked, noinline)) name(                         \
      uint32_t unused __attribute__((unused))) {                               \
    __asm__ volatile(                                                          \
        "lea -128(%esp),%esp;pushfl;pushal;cld;pushl $" #from_frame            \
        ";leal 4(%esp),%eax;pushl %eax;call " ASM_NAME(                        \
            bb_crt_seh_notice_c) ";addl $8,%esp;popal;popfl;lea "              \
                                 "128(%esp),%esp;ret $4");                     \
  }
NOTICE_ENTRY(bb_crt_seh_notice_entry, 0)
NOTICE_ENTRY(bb_crt_seh_frame_notice_entry, 1)
int32_t __attribute__((naked, noinline))
bb_crt_seh_handler_entry(void *record __attribute__((unused)),
                         void *registration __attribute__((unused)),
                         void *context __attribute__((unused)),
                         void *dispatcher __attribute__((unused))) {
  __asm__ volatile("pushl %ebp;movl %esp,%ebp;subl $8,%esp;pushl %ebx;"
                   "pushl %esi;pushl %edi;pushl %ebp;cld;"
                   "lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
                       bb_crt_seh_handler_c) ";addl $4,%esp;"
                                             "lea 2048(%esp),%esp;"
                                             "movl -28(%ebp),%eax;popl %ebp;"
                                             "popl %edi;popl %esi;popl %ebx;"
                                             "leave;ret");
}

#endif
