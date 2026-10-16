/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MODEL_RUNTIME_H
#define BB_MODEL_RUNTIME_H
#include "call_abi.h"

/* C forms of pushal state: EDI, ESI, EBP, saved ESP, EBX, EDX, ECX,
 * EAX, flags, and (when requested) one reserved local word. No host pointer
 * is stored in these words. The stack and code identities stay logical.
 */
/* OR into arguments for a packed variadic tail. Explicit translated call sites
 * already wrote that tail to their logical stack; ordinary C entries copy the
 * supplied window into their newly owned frame. */
enum { BB_MODEL_VARIADIC = 1u << 30 };
struct bb_model_target {
  uint32_t identity;
  void (*body)(uint32_t *);
  unsigned arguments;
  unsigned reserved_bytes;
  unsigned local_word;
};
typedef const struct bb_model_target *(*bb_model_lookup)(void *, uint32_t);
/* Borrowed dispatch environment for callbacks arriving on native worker
 * threads. Its lookup/context must outlive all callback owners. No stack/TLS
 * address is captured; a foreign thread supplies its own mapped stack. */
struct bb_model_context {
  bb_model_lookup lookup;
  void *context;
};
int bb_model_capture_context(struct bb_model_context *);
int bb_model_context_invoke(const struct bb_model_context *, uint32_t stack,
                            size_t size, struct bb_call_state *, uint32_t,
                            const uint32_t *, size_t, uint32_t *error);

/* Floating results are native C storage, never a host pointer packed into a
 * register word. Nested calls have independent result slots. */
struct bb_model_result {
  long double floating;
  unsigned has_floating;
};
/* Operand values for the remaining register-style numeric boundaries. These
 * are C values, owned by the dispatch environment and restored by C recovery.
 * Signaling-NaN classification survives hosts that quiet it on conversion. */
struct bb_numeric_value {
  long double value;
  unsigned signaling;
};
struct bb_numeric_operands {
  struct bb_numeric_value values[8];
  unsigned count;
  uint16_t comparison;
};
struct bb_numeric_operands *bb_model_operands(void);
/* Error payloads contain only original-width data references. Copies survive
 * recovery even when the raising worker's native/logical frame is discarded. */
struct bb_model_exception {
  uint32_t code, flags, nested, address, count, arguments[15];
};
_Static_assert(sizeof(struct bb_model_exception) == 80, "packed exception data");
struct bb_model_exception_state {
  uint32_t registration, catch_frame, pending_target, pending_owner;
  unsigned dispatch_depth, unwinding;
};
struct bb_model_exception_state *bb_model_exceptions(void);
struct bb_model_exception bb_model_caught_exception(void);
_Noreturn void bb_model_raise_exception(const struct bb_model_exception *);
struct bb_model_saved {
  uint32_t edi, esi, ebp, ebx;
};
void bb_model_return_float(long double);

/* Each thread borrows an already mapped, private logical stack and a lookup
 * context. Keep them alive until detach. Stack size must include 512 bytes
 * below each active call, in addition to its declared locals. Large text-screen
 * frames are allowed when the mapped stack has room. Attach/detach
 * return zero on an invalid configuration or while a call or temporary scope
 * is active.
 */
int bb_model_attach(bb_model_lookup, void *, uint32_t stack, size_t size);
int bb_model_detach(void);
uint32_t bb_model_current_identity(void);

/* Temporary packed data on the attached logical stack. Nested calls deepen
 * below this storage; end scopes in reverse order. C recovery restores the
 * cursor automatically when it bypasses a scope, with no borrowed host mapping
 * or heap allocation left behind. */
struct bb_model_scratch {
  uint64_t cursor;
  size_t depth;
  uint32_t address;
};
void bb_model_scratch_begin(struct bb_model_scratch *, size_t bytes);
void bb_model_scratch_end(const struct bb_model_scratch *);

/* Used by translated callers with an explicit logical ESP. Argument buffers
 * are native pointers; only the target's declared fixed words are copied. A
 * variadic tail remains in the caller's logical stack. Unknown
 * targets and stack exhaustion fail explicitly, never enter original code.
 */
void bb_model_call_at(struct bb_call_state *, uint32_t identity,
                      const uint32_t *, size_t words, uint32_t argument_base,
                      uint32_t edi, uint32_t esi, uint32_t ebp, uint32_t ebx);
void bb_model_call_result_at(struct bb_call_state *, uint32_t identity,
                             const uint32_t *, size_t words,
                             uint32_t argument_base, uint32_t edi, uint32_t esi,
                             uint32_t ebp, uint32_t ebx,
                             uint32_t return_identity,
                             struct bb_model_result *);

/* Ordinary C entry: allocate a logical frame on the attached stack. Incoming
 * register state is explicit so register-dependent adapters need no assembly.
 */
void bb_model_invoke(struct bb_call_state *, uint32_t identity,
                     const uint32_t *, size_t words);
void bb_model_invoke_result(struct bb_call_state *, uint32_t identity,
                            const uint32_t *, size_t words,
                            struct bb_model_result *);
void bb_model_invoke_frame(struct bb_call_state *, uint32_t identity,
                           const uint32_t *, size_t words,
                           const struct bb_model_saved *,
                           struct bb_model_result *);
/* C recovery boundaries restore the dispatch cursor, depth and floating slot
 * after a raised error. Component cleanup runs before raising to this boundary.
 * Failed calls restore incoming registers and clear result storage. */
int bb_model_try_invoke(struct bb_call_state *, uint32_t, const uint32_t *,
                        size_t, struct bb_model_result *, uint32_t *error);
/* Protect a C worker in its existing active frame. On failure restore its
 * ten input words, dispatch context and prior floating value; logical memory is
 * retained for the component's recovery action. No extra frame is allocated. */
int bb_model_try_body(void (*body)(uint32_t *), uint32_t *input,
                      uint32_t *error);
_Noreturn void bb_model_raise(uint32_t error);
#endif
