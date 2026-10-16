/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <string.h>

/* The bridge's registration records and CRT handlers already implement scope
 * selection and cleanup in C. Supply the native search/unwind boundary they
 * need; caught continuations leave this stack through the existing ABI shim. */
struct ExceptionRecord {
  U32 code, flags, nested, address, count, arguments[15];
};
struct Registration {
  struct Registration *previous;
  I32 (*handler)(struct ExceptionRecord *, struct Registration *, void *,
                 void *);
};
static struct Registration *head(void) {
  struct Registration *p;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(p));
  return p;
}
static void link(struct Registration *p) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(p) : "memory");
}
static int terminal(struct Registration *p) {
  return !p || (uintptr_t)p == UINT32_MAX;
}
static I32 dispatch_guard(struct ExceptionRecord *record,
                          struct Registration *registration, void *context,
                          void *dispatcher) {
  (void)registration;
  (void)context;
  (void)dispatcher;
  return record->flags & 6 ? 1 : 2;
}
void bb_exception_raise(U32 code, U32 flags, U32 count, const U32 *arguments,
                        void *filter) {
  struct ExceptionRecord record = {
      .code = code,
      .flags = flags,
      .address = (U32)(uintptr_t)__builtin_return_address(0),
      .count = count > 15 ? 15 : count};
  if (record.count)
    memcpy(record.arguments, arguments, record.count * sizeof(U32));
  U32 context[180] = {0};
  struct Registration *registration = head();
  for (unsigned n = 0; !terminal(registration) && n < 65536; ++n) {
    struct Registration guard = {head(), dispatch_guard};
    struct Registration *dispatcher = registration;
    link(&guard);
    I32 (*handler)(struct ExceptionRecord *, struct Registration *, void *,
                   void *) =
        (void *)bb_native_entry((uintptr_t)registration->handler);
    I32 result = handler(&record, registration, context, &dispatcher);
    link(guard.previous);
    if (!result) {
      if (!(flags & 1))
        return;
      bb_die("continued noncontinuable exception 0x%08x", code);
    }
    if (result != 1 && result != 2)
      bb_die("invalid exception search disposition %d", result);
    registration = registration->previous;
  }
  if (filter) {
    void *pair[2] = {&record, context};
    I32(W * unhandled)(void *) = (void *)bb_native_entry((uintptr_t)filter);
    if (unhandled(pair) == -1 && !(flags & 1))
      return;
  }
  bb_die("unhandled Win32 exception 0x%08x", code);
}
void bb_exception_unwind(void *target, void *continuation, void *exception,
                         void *value) {
  (void)continuation;
  (void)value;
  struct ExceptionRecord local = {.code = 0xc0000027};
  struct ExceptionRecord *record = exception ? exception : &local;
  U32 previous_flags = record->flags;
  record->flags |= 2 | (target ? 0 : 4);
  U32 context[180] = {0};
  struct Registration *registration = head();
  unsigned n = 0;
  while (registration != target && !terminal(registration) && n++ < 65536) {
    struct Registration *dispatcher = registration;
    I32 (*handler)(struct ExceptionRecord *, struct Registration *, void *,
                   void *) =
        (void *)bb_native_entry((uintptr_t)registration->handler);
    I32 result = handler(record, registration, context, &dispatcher);
    if (result == 3)
      registration = dispatcher;
    else if (result != 1)
      bb_die("invalid exception unwind disposition %d", result);
    registration = registration->previous;
    link(registration);
  }
  if (target && registration != target)
    bb_die("exception unwind target is absent");
  if (!target && !terminal(registration))
    bb_die("cyclic exception unwind chain");
  link(registration);
  record->flags = previous_flags;
  /* Our CRT ABI callers place their continuation immediately after this call;
   * a normal return resumes it after all required frame cleanup. */
}
