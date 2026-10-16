/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_LIFETIME_H
#define BB_QMIXER_LIFETIME_H
#include "model_runtime.h"
uint32_t bb_qmix_model_exception_head(void);
void bb_qmix_model_exception_link(uint32_t);
_Noreturn void bb_qmix_model_raise(uint32_t information);
struct bb_qmix_model_error {
  uint32_t information, value;
};
/* Keep error words in native C storage; success clears both output words.
 * The throw adapter copies WaveError's value before its logical frame unwinds. */
_Noreturn void bb_qmix_model_raise_error(struct bb_qmix_model_error);
int bb_qmix_model_try_invoke_detailed(struct bb_call_state *, uint32_t,
                                     const uint32_t *, size_t,
                                     struct bb_model_result *,
                                     struct bb_qmix_model_error *);
int bb_qmix_model_try_invoke(struct bb_call_state *, uint32_t, const uint32_t *,
                             size_t, struct bb_model_result *, uint32_t *error);
/* A translated worker links this logical owner before it can throw. Failed
 * execution cleans nested owners and leaves this record for its C catch action;
 * success must restore the registration that preceded the worker. */
int bb_qmix_model_try_body(void (*)(uint32_t *), uint32_t *input,
                           uint32_t owner, struct bb_qmix_model_error *);
/* Execute a catch with a native, nested error value. A rethrow or replacement
 * error restores this context before propagating to the enclosing boundary. */
void bb_qmix_model_catch_body(void (*)(uint32_t *), uint32_t *input,
                             struct bb_qmix_model_error);
_Noreturn void bb_qmix_model_rethrow(void);
struct bb_qmix_model_error bb_qmix_model_caught_error(void);
/* Release resources belonging to inner stages while retaining this owner for
 * its C catch action. Cleanup failures retain the normal terminal policy. */
void bb_qmix_model_cleanup_to(uint32_t owner, int32_t stage);
void bb_qmix_model_malloc_c(uint32_t *);
void bb_qmix_model_free_c(uint32_t *);
void bb_qmix_model_raise_c(uint32_t *);
#endif
