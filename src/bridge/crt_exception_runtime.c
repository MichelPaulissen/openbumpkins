/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_exception_runtime.h"
#include <string.h>
uint32_t bb_crt_model_frame(struct bb_model_scratch *scope) {
  bb_model_scratch_begin(scope, 160);
  memset(BB_SPAN(scope->address, 160), 0, 160);
  return scope->address + 80u;
}
uint32_t bb_crt_model_call(uint32_t target, uint32_t owner, uint32_t self,
                           const uint32_t *arguments, unsigned count) {
  struct bb_call_state call = {target, self, 0, 0x202};
  struct bb_model_saved saved = {0, 0, owner, 0};
  bb_model_invoke_frame(&call, target, arguments, count, &saved, NULL);
  return call.eax;
}
void bb_crt_model_notice(uint32_t kind, uint32_t function, uint32_t owner) {
  BB_U32(0x48ccb8) = kind;
  BB_U32(0x48ccb4) = function;
  BB_U32(0x48ccbc) = owner;
}
void bb_crt_model_unwind(uint32_t stop, uint32_t record) {
  struct bb_model_exception_state *state = bb_model_exceptions();
  struct bb_model_scratch scope;
  bb_model_scratch_begin(&scope, 808);
  uint32_t at = scope.address, context = at + 84, dispatcher = at + 804;
  memset(BB_SPAN(at, 808), 0, 808);
  if (!record) {
    record = at;
    BB_U32(record) = 0xc0000027;
  }
  uint32_t flags = BB_U32(record + 4);
  BB_U32(record + 4) |= 2u | (stop ? 0u : 4u);
  ++state->unwinding;
  unsigned count = 0;
  while (state->registration != stop && state->registration &&
         state->registration != UINT32_MAX && ++count <= 65536) {
    uint32_t node = state->registration;
    BB_U32(dispatcher) = node;
    uint32_t args[] = {record, node, context, dispatcher};
    uint32_t disposition = bb_crt_model_call(BB_U32(node + 4), 0, 0, args, 4);
    if (disposition == 3)
      node = BB_U32(dispatcher);
    else if (disposition != 1)
      bb_model_raise(0xc0000027);
    state->registration = BB_U32(node);
  }
  if ((stop && state->registration != stop) || count > 65536)
    bb_model_raise(0xc0000027);
  --state->unwinding;
  BB_U32(record + 4) = flags;
  bb_model_scratch_end(&scope);
}
void bb_crt_model_continue(uint32_t function, uint32_t owner) {
  struct bb_model_exception_state *state = bb_model_exceptions();
  state->pending_target = function;
  state->pending_owner = owner;
}
void bb_crt_model_finish(uint32_t *r) {
  struct bb_model_exception_state *state = bb_model_exceptions();
  if (--state->dispatch_depth)
    return;
  /* A C continuation runs after every enclosing catch scope has restored its
   * exception context and released its caught object. No saved machine stack
   * or instruction pointer is required. */
  while (state->pending_target) {
    uint32_t target = state->pending_target, owner = state->pending_owner;
    state->pending_target = state->pending_owner = 0;
    state->registration = owner - 12u;
    r[7] = bb_crt_model_call(target, owner, 0, NULL, 0);
  }
}
