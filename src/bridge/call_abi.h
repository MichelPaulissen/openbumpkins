/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CALL_ABI_H
#define BB_CALL_ABI_H
#include <stddef.h>
#include <stdint.h>

/* Fixed-width values describe the legacy call, independently of host pointers.
 * Packed data and translated state machines retain four-byte words while host
 * entry targets and argument-buffer pointers use uintptr_t and native pointers.
 */
struct bb_call_state {
  uint32_t eax, ecx, edx, flags;
};
_Static_assert(sizeof(struct bb_call_state) == 16, "legacy register state");
_Static_assert(offsetof(struct bb_call_state, flags) == 12,
               "legacy flags slot");

/* The counts are padded stack windows, not the callee's C argument count.
 * Keep each caller's existing window: reading extra words can leave its array.
 * Callee stack cleanup is independent of the translated caller's logical ESP.
 */
void bb_call_words6(struct bb_call_state *, uintptr_t, const uint32_t *);
void bb_call_words10(struct bb_call_state *, uintptr_t, const uint32_t *);
void bb_call_words16(struct bb_call_state *, uintptr_t, const uint32_t *);
void bb_call_float10(struct bb_call_state *, uintptr_t, const uint32_t *,
                     long double *);
#endif
