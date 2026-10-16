/* SPDX-License-Identifier: GPL-3.0-only */
#include "callback_runtime.h"
#include "heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int bb_callback_initialize(struct bb_callback_context *context) {
  memset(context, 0, sizeof(*context));
  atomic_init(&context->stack_busy, 0);
  if (!bb_model_capture_context(&context->dispatch))
    return 0;
  context->stack = bb_heap_zeroed(1, BB_CALLBACK_STACK_BYTES);
  return context->stack != 0;
}
void bb_callback_destroy(struct bb_callback_context *context) {
  if (context->stack && !bb_heap_release(context->stack))
    abort();
  memset(context, 0, sizeof(*context));
}
uint32_t bb_callback_invoke(struct bb_callback_context *context,
                            uint32_t identity, const uint32_t *arguments,
                            size_t words) {
  if (!context->stack)
    abort();
  struct bb_call_state state = {0, 0, 0, 0x202};
  struct bb_model_context current;
  int attached = bb_model_capture_context(&current) &&
                 current.lookup == context->dispatch.lookup &&
                 current.context == context->dispatch.context;
  uint32_t stack = context->stack;
  int primary = 0;
  if (!attached) {
    primary = !atomic_exchange(&context->stack_busy, 1);
    if (!primary) {
      stack = bb_heap_zeroed(1, BB_CALLBACK_STACK_BYTES);
      if (!stack)
        abort();
    }
  }
  uint32_t error;
  int success = bb_model_context_invoke(&context->dispatch, stack,
                                        BB_CALLBACK_STACK_BYTES, &state,
                                        identity, arguments, words, &error);
  if (primary)
    atomic_store(&context->stack_busy, 0);
  else if (!attached && !bb_heap_release(stack))
    abort();
  if (!success) {
    fprintf(stderr, "bumpkins: unhandled callback error %08x at %08x\n", error,
            identity);
    abort();
  }
  return state.eax;
}
