/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GAME_AUDIO_CALLS_H
#define BB_GAME_AUDIO_CALLS_H
#ifdef BB_MODEL_RUNTIME
#include "game_audio_models.h"
#define BB_AUDIO_TARGET(name) BB_AUDIO_##name
#define BB_AUDIO_CALL_WORDS(state, target, args, esp, edi, esi, ebp, ebx)        \
  bb_model_call_at(state, target, args, 10, esp, edi, esi, ebp, ebx)
static inline void bb_audio_call_float(struct bb_call_state *state,
                                       uint32_t identity, const uint32_t *args,
                                       uint32_t esp, uint32_t edi, uint32_t esi,
                                       uint32_t ebp, uint32_t ebx,
                                       long double *floating) {
  struct bb_model_result result;
  bb_model_call_result_at(state, identity, args, 10, esp, edi, esi, ebp, ebx,
                          0, &result);
  *floating = result.floating;
}
#define BB_AUDIO_CALL_FLOAT bb_audio_call_float
#else
#define BB_AUDIO_TARGET(name) ((uintptr_t)(name))
#define BB_AUDIO_CALL_WORDS(state, target, args, esp, edi, esi, ebp, ebx)        \
  bb_call_words10(state, BB_CODE(target), args)
#define BB_AUDIO_CALL_FLOAT(state, target, args, esp, edi, esi, ebp, ebx, out)   \
  bb_call_float10(state, target, args, out)
#endif
#endif
