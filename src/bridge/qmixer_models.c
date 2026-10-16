/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include "qmixer_lifetime.h"
#include <stdio.h>
#include <stdlib.h>

const struct bb_model_target *bb_qmix_model_find(void *context,
                                                 uint32_t identity) {
  (void)context;
  switch (identity) {
#define MODEL(address, method, count, frame, floating)                         \
  case address: {                                                              \
    static const struct bb_model_target target = {address, method, count,      \
                                                  frame, 0};                   \
    return &target;                                                            \
  }
    BB_QMIX_CONTROL_METHODS(MODEL)
    BB_QMIX_PLAYBACK_METHODS(MODEL)
    BB_QMIX_PCM_METHODS(MODEL)
    BB_QMIX_INTEGER_METHODS(MODEL)
    BB_QMIX_RENDER_METHODS(MODEL)
    BB_QMIX_OBJECTS_METHODS(MODEL)
    BB_QMIX_RUNTIME_METHODS(MODEL)
    BB_QMIX_HEAP_METHODS(MODEL)
    BB_QMIX_CRT_METHODS(MODEL)
    BB_QMIX_NUMERIC_METHODS(MODEL)
    BB_QMIX_DSP_METHODS(MODEL)
    BB_QMIX_FILTERS_METHODS(MODEL)
    BB_QMIX_DELAY_METHODS(MODEL)
    BB_QMIX_TAP_DELAY_METHODS(MODEL)
    BB_QMIX_SOURCE_METHODS(MODEL)
    BB_QMIX_LOADING_METHODS(MODEL)
    BB_QMIX_STARTUP_METHODS(MODEL)
    BB_QMIX_DRIVERS_METHODS(MODEL)
    BB_QMIX_LATENCY_METHODS(MODEL)
    BB_QMIX_DEVICE_METHODS(MODEL)
    BB_QMIX_SESSION_METHODS(MODEL)
    BB_QMIX_API_METHODS(MODEL)
    MODEL(0x10011710u, bb_qmix_model_malloc_c, 1, 2048, 0)
    MODEL(0x100117c0u, bb_qmix_model_free_c, 1, 2048, 0)
    MODEL(0x100116c0u, bb_qmix_model_raise_c, 2, 2048, 0)
    /* Spatial arithmetic, published descriptor/base methods and CRT terminal
     * callbacks remain reachable after replacing the old exception machinery. */
    MODEL(0x100195e0u, bb_qmix_crt_hypot_c, 4, 2048, 1)
    MODEL(0x10011c40u, bb_qmix_pure_virtual_failure_c, 0, 2048, 0)
    MODEL(0x10012030u, bb_qmix_fatal_error_c, 1, 2048, 0)
    MODEL(0x100148a0u, bb_qmix_quick_exit_c, 1, 2048, 0)
    MODEL(0x10011680u, bb_qmix_exception_object_destructor_c, 0, 2048, 0)
    MODEL(0x100116a0u, bb_qmix_exception_object_delete_c, 1, 2048, 0)
    MODEL(0x10015f80u, bb_qmix_fp_unavailable_c, 0, 2048, 0)
#undef MODEL
  default:
    return NULL;
  }
}

void bb_qmix_model_call(struct bb_call_state *state, uintptr_t identity,
                        const uint32_t *args, uint32_t esp, uint32_t edi,
                        uint32_t esi, uint32_t ebp, uint32_t ebx,
                        uint32_t returned, long double *floating) {
  if (identity > UINT32_MAX) {
    fputs("bumpkins: native pointer used as a mixer code identity\n", stderr);
    abort();
  }
  struct bb_model_result result;
  bb_model_call_result_at(state, (uint32_t)identity, args, 16, esp, edi, esi,
                          ebp, ebx, returned, &result);
  *floating = result.floating;
}
