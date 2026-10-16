/* SPDX-License-Identifier: GPL-3.0-only */
#include "model_runtime.h"
#include "address.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

struct recovery;

struct model_thread {
  bb_model_lookup lookup;
  void *context;
  uint32_t low;
  uint64_t high, cursor;
  size_t depth;
  struct bb_model_result *result;
  struct recovery *recovery;
  uint32_t error, identity;
  struct bb_numeric_operands operands;
  struct bb_model_exception_state exceptions;
  struct bb_model_exception exception;
};
struct recovery {
  jmp_buf jump;
};
static _Thread_local struct model_thread thread;
struct context_scope {
  struct model_thread saved;
  struct context_scope *previous;
};
static _Thread_local struct context_scope *callback_contexts;

static void fail(uint32_t identity, const char *reason) {
  fprintf(stderr, "bumpkins: C model %08x: %s\n", identity, reason);
  abort();
}

int bb_model_attach(bb_model_lookup lookup, void *context, uint32_t stack,
                    size_t size) {
  if (thread.lookup || !lookup || !stack || (stack & 15u) || size < 1024 ||
      size > UINT32_MAX || (uint64_t)stack + size > UINT64_C(0x100000000))
    return 0;
#ifdef BB_PORTABLE_RUNTIME
  void *mapped;
  if (!bb_address_try_pointer(stack, size, &mapped))
    return 0;
#endif
  thread = (struct model_thread){.lookup = lookup,
                                 .context = context,
                                 .low = stack,
                                 .high = (uint64_t)stack + size,
                                 .cursor = (uint64_t)stack + size,
                                 .exceptions = {.registration = UINT32_MAX}};
  return 1;
}

int bb_model_detach(void) {
  if (!thread.lookup || thread.depth || thread.cursor != thread.high)
    return 0;
  thread = (struct model_thread){0};
  return 1;
}

int bb_model_capture_context(struct bb_model_context *context) {
  if (!context || !thread.lookup)
    return 0;
  *context = (struct bb_model_context){thread.lookup, thread.context};
  return 1;
}

int bb_model_context_invoke(const struct bb_model_context *context,
                            uint32_t stack, size_t size,
                            struct bb_call_state *state, uint32_t identity,
                            const uint32_t *arguments, size_t words,
                            uint32_t *error) {
  if (!context || !context->lookup)
    fail(identity, "missing callback dispatch environment");
  struct context_scope scope = {thread, callback_contexts};
  /* Synchronous/reentrant notifications share the current environment's deeper
   * frame; resetting that stack would overwrite their caller's live locals. */
  if (thread.lookup != context->lookup || thread.context != context->context) {
    struct context_scope *ancestor = callback_contexts;
    while (ancestor && (ancestor->saved.lookup != context->lookup ||
                        ancestor->saved.context != context->context))
      ancestor = ancestor->previous;
    if (ancestor) {
      /* A -> B -> A must deepen A's suspended frame, not restart its stack. */
      thread = ancestor->saved;
    } else {
      thread = (struct model_thread){0};
      if (!bb_model_attach(context->lookup, context->context, stack, size))
        fail(identity, "invalid callback logical stack");
      for (struct context_scope *active = &scope; active;
           active = active->previous)
        if (active->saved.depth && thread.low < active->saved.high &&
            active->saved.low < thread.high)
          fail(identity, "callback stack overlaps an active dispatch frame");
    }
  }
  callback_contexts = &scope;
  int success =
      bb_model_try_invoke(state, identity, arguments, words, NULL, error);
  callback_contexts = scope.previous;
  thread = scope.saved;
  return success;
}

uint32_t bb_model_current_identity(void) { return thread.identity; }
struct bb_numeric_operands *bb_model_operands(void) {
  if (!thread.lookup)
    fail(0, "numeric operands outside an attached C environment");
  return &thread.operands;
}
struct bb_model_exception_state *bb_model_exceptions(void) {
  if (!thread.lookup)
    fail(0, "exception state outside an attached C environment");
  return &thread.exceptions;
}
struct bb_model_exception bb_model_caught_exception(void) {
  return thread.exception;
}

void bb_model_scratch_begin(struct bb_model_scratch *scope, size_t bytes) {
  if (!scope || !thread.lookup || !bytes || bytes > UINT32_MAX ||
      thread.cursor < (uint64_t)thread.low + bytes + 512)
    fail(thread.identity, "logical temporary storage exhausted");
  uint64_t base = (thread.cursor - bytes) & ~UINT64_C(15);
  if (base < (uint64_t)thread.low + 512 || base > UINT32_MAX)
    fail(thread.identity, "logical temporary storage exhausted");
  *scope =
      (struct bb_model_scratch){thread.cursor, thread.depth, (uint32_t)base};
  thread.cursor = base;
}
void bb_model_scratch_end(const struct bb_model_scratch *scope) {
  if (!scope || !thread.lookup || scope->depth != thread.depth ||
      scope->address != thread.cursor || scope->cursor > thread.high ||
      scope->cursor < thread.cursor)
    fail(thread.identity, "unbalanced logical temporary storage");
  thread.cursor = scope->cursor;
}

void bb_model_return_float(long double value) {
  if (!thread.result)
    fail(0, "floating result outside an active C frame");
  thread.result->floating = value;
  thread.result->has_floating = 1;
}

static void call_result_at(struct bb_call_state *state, uint32_t identity,
                           const uint32_t *arguments, size_t words,
                           uint32_t argument_base, uint32_t edi, uint32_t esi,
                           uint32_t ebp, uint32_t ebx, uint32_t return_identity,
                           struct bb_model_result *result,
                           unsigned owned_frame) {
  if (!thread.lookup)
    fail(identity, "thread has no logical stack/dispatch table");
  ++thread.depth;
  const struct bb_model_target *target =
      thread.lookup(thread.context, identity);
  if (!target || target->identity != identity || !target->body)
    fail(identity, "unregistered function identity");
  unsigned fixed = target->arguments & ~BB_MODEL_VARIADIC;
  size_t copied =
      owned_frame && (target->arguments & BB_MODEL_VARIADIC) ? words : fixed;
  if (!state || fixed > words || (copied && !arguments) || fixed > 16 ||
      copied > 16 || (target->reserved_bytes & 3u) ||
      (target->local_word && target->reserved_bytes < 4))
    fail(identity, "invalid frame description/argument window");
  if ((argument_base & 3u) ||
      (uint64_t)argument_base <
          (uint64_t)thread.low + target->reserved_bytes + 512 ||
      (uint64_t)argument_base + copied * 4 > thread.high)
    fail(identity, "logical stack exhausted");

  uint64_t cursor = thread.cursor;
  uint32_t return_slot = argument_base - 4;
  /* A call-site identity may be inspected by a translated owner-frame helper;
   * it is data, never a host return address to execute. */
  BB_U32(return_slot) = return_identity;
  if (copied)
    memmove(BB_SPAN(argument_base, copied * 4), arguments, copied * 4);
  uint32_t input[10] = {
      edi,          esi,
      ebp,          argument_base - target->reserved_bytes - 8,
      ebx,          state->edx,
      state->ecx,   state->eax,
      state->flags, 0};
  if (target->local_word)
    input[9] = BB_U32(return_slot - target->reserved_bytes);
  uint64_t below_frame = (uint64_t)argument_base - target->reserved_bytes - 512;
  if (below_frame < thread.cursor)
    thread.cursor = below_frame;
  struct bb_model_result value = {0};
  struct bb_model_result *previous_result = thread.result;
  thread.result = &value;
  uint32_t previous_identity = thread.identity;
  thread.identity = identity;
  target->body(input);
  thread.identity = previous_identity;
  thread.result = previous_result;
  --thread.depth;
  thread.cursor = cursor;
  *state = (struct bb_call_state){input[7], input[6], input[5], input[8]};
  if (result)
    *result = value;
}

void bb_model_call_result_at(struct bb_call_state *state, uint32_t identity,
                             const uint32_t *arguments, size_t words,
                             uint32_t argument_base, uint32_t edi, uint32_t esi,
                             uint32_t ebp, uint32_t ebx,
                             uint32_t return_identity,
                             struct bb_model_result *result) {
  call_result_at(state, identity, arguments, words, argument_base, edi, esi,
                 ebp, ebx, return_identity, result, 0);
}

void bb_model_call_at(struct bb_call_state *state, uint32_t identity,
                      const uint32_t *arguments, size_t words,
                      uint32_t argument_base, uint32_t edi, uint32_t esi,
                      uint32_t ebp, uint32_t ebx) {
  bb_model_call_result_at(state, identity, arguments, words, argument_base, edi,
                          esi, ebp, ebx, 0, NULL);
}

void bb_model_invoke_frame(struct bb_call_state *state, uint32_t identity,
                           const uint32_t *arguments, size_t words,
                           const struct bb_model_saved *saved,
                           struct bb_model_result *result) {
  if (!thread.lookup || words > 16 || thread.cursor < words * 4 + 16)
    fail(identity, "missing stack or invalid C argument window");
  uint64_t base = (thread.cursor - words * 4 - 16) & ~UINT64_C(15);
  if (base > UINT32_MAX || base < thread.low)
    fail(identity, "logical stack exhausted");
  call_result_at(state, identity, arguments, words, (uint32_t)base,
                 saved ? saved->edi : 0, saved ? saved->esi : 0,
                 saved ? saved->ebp : 0, saved ? saved->ebx : 0, 0, result, 1);
}

void bb_model_invoke_result(struct bb_call_state *state, uint32_t identity,
                            const uint32_t *arguments, size_t words,
                            struct bb_model_result *result) {
  bb_model_invoke_frame(state, identity, arguments, words, NULL, result);
}

void bb_model_invoke(struct bb_call_state *state, uint32_t identity,
                     const uint32_t *arguments, size_t words) {
  bb_model_invoke_result(state, identity, arguments, words, NULL);
}

int bb_model_try_invoke(struct bb_call_state *state, uint32_t identity,
                        const uint32_t *arguments, size_t words,
                        struct bb_model_result *result, uint32_t *error) {
  if (!state || !thread.lookup)
    fail(identity, "invalid recovery boundary");
  struct model_thread saved = thread;
  struct bb_call_state registers = *state;
  struct recovery recovery;
  thread.recovery = &recovery;
  if (!setjmp(recovery.jump)) {
    bb_model_invoke_result(state, identity, arguments, words, result);
    thread.recovery = saved.recovery;
    if (error)
      *error = 0;
    return 1;
  }
  uint32_t raised = thread.error;
  struct bb_model_exception exception = thread.exception;
  thread = saved;
  thread.exception = exception;
  *state = registers;
  if (result)
    *result = (struct bb_model_result){0};
  if (error)
    *error = raised;
  return 0;
}

int bb_model_try_body(void (*body)(uint32_t *), uint32_t *input,
                      uint32_t *error) {
  if (!body || !input || !thread.lookup || !thread.depth || !thread.result)
    fail(0, "invalid protected worker");
  struct model_thread saved = thread;
  uint32_t registers[10];
  memcpy(registers, input, sizeof registers);
  struct bb_model_result result = *thread.result;
  struct recovery recovery;
  thread.recovery = &recovery;
  if (!setjmp(recovery.jump)) {
    body(input);
    thread.recovery = saved.recovery;
    if (error)
      *error = 0;
    return 1;
  }
  uint32_t raised = thread.error;
  struct bb_model_exception exception = thread.exception;
  thread = saved;
  thread.exception = exception;
  memcpy(input, registers, sizeof registers);
  *thread.result = result;
  if (error)
    *error = raised;
  return 0;
}

_Noreturn void bb_model_raise(uint32_t error) {
  struct bb_model_exception exception = {
      .code = error, .address = thread.identity};
  bb_model_raise_exception(&exception);
}
_Noreturn void bb_model_raise_exception(const struct bb_model_exception *exception) {
  if (!thread.recovery)
    fail(exception->code, "uncaught C model error");
  thread.exception = *exception;
  thread.error = exception->code;
  longjmp(thread.recovery->jump, 1);
}
