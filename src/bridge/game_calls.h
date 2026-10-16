/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GAME_CALLS_H
#define BB_GAME_CALLS_H
#include "interop.h"
#include <stdlib.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
#endif

/* Mixed legacy arguments may be a signed DWORD or a mapped host pointer.
 * Preserve sign-extended 32-bit scalars without narrowing real host pointers. */
static inline uint32_t bb_game_word(uintptr_t value) {
  if (value <= UINT32_MAX || value >= (uintptr_t)(intptr_t)INT32_MIN)
    return (uint32_t)value;
  return bb_address_encode((const void *)value);
}

/* Packed game imports and virtual methods exchange only four-byte words.
 * The full i386 runtime keeps its established call backend. Portable callers
 * dispatch C targets on the attached logical stack. No native pointer is
 * stored in either argument window. */
static inline uint32_t bb_game_call(uint32_t id, uint32_t self,
                                    const uint32_t *args, size_t words) {
  uint32_t padded[16] = {0};
  if (words > 16)
    abort();
  if (words)
    memcpy(padded, args, words * sizeof *args);
#ifdef BB_MODEL_RUNTIME
  /* Variadic CRT services consume a padded window; fixed-signature targets
   * still copy only their declared argument words. */
  struct bb_call_state state = {0, self, 0, 0x202};
  bb_model_invoke(&state, id, padded, 16);
  return state.eax;
#else
  struct bb_call_state state = {0, self, 0, 0x202};
  if (words <= 6)
    bb_call_words6(&state, BB_CODE(id), padded);
  else if (words <= 10)
    bb_call_words10(&state, BB_CODE(id), padded);
  else
    bb_call_words16(&state, BB_CODE(id), padded);
  return state.eax;
#endif
}
static inline uint32_t bb_game_method(uint32_t object, uint32_t offset,
                                      const uint32_t *tail, size_t words) {
  uint32_t args[16] = {object};
  if (words > 15)
    abort();
  if (words)
    memcpy(args + 1, tail, words * sizeof *tail);
  return bb_game_call(BB_U32(BB_U32(object) + offset), 0, args, words + 1);
}
#define BB_GAME_CALL(id, self, ...)                                            \
  bb_game_call(id, self, (uint32_t[]){__VA_ARGS__},                            \
               sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t))
#define BB_GAME_METHOD(object, offset, ...)                                    \
  bb_game_method(object, offset, (uint32_t[]){__VA_ARGS__},                    \
                 sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t))
#endif
