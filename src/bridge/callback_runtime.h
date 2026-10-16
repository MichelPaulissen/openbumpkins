/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CALLBACK_RUNTIME_H
#define BB_CALLBACK_RUNTIME_H
#include "model_runtime.h"
#include <stdatomic.h>
enum { BB_CALLBACK_STACK_BYTES = 256 * 1024 };
struct bb_callback_context {
  struct bb_model_context dispatch;
  uint32_t stack;
  atomic_uint stack_busy;
};
/* Owners must stop all callbacks before destruction. Attached/reentrant calls
 * deepen their existing frames. Concurrent foreign entries borrow the primary
 * stack or own temporary overflow stacks, so no live frame is overwritten. */
int bb_callback_initialize(struct bb_callback_context *);
void bb_callback_destroy(struct bb_callback_context *);
uint32_t bb_callback_invoke(struct bb_callback_context *, uint32_t,
                            const uint32_t *, size_t);
#endif
