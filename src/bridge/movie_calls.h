/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MOVIE_CALLS_H
#define BB_MOVIE_CALLS_H
#include "crt_output.h"
#include "game_file.h"
static inline void bb_movie_scope_end(struct bb_model_scratch *scope) {
  bb_model_scratch_end(scope);
}
#define MOVIE_BUFFER(type, name, count)                                        \
  struct bb_model_scratch name##_scope                                         \
      __attribute__((cleanup(bb_movie_scope_end)));                            \
  bb_model_scratch_begin(&name##_scope, sizeof(type) * (count));               \
  type *name = BB_SPAN(name##_scope.address, sizeof(type) * (count))
#define MOVIE_LOCAL(type, name, count, ...)                                    \
  MOVIE_BUFFER(type, name, count);                                             \
  memcpy(name, (type[count]){__VA_ARGS__}, sizeof(type) * (count))
#define MV_WORD(p) bb_game_word((uintptr_t)(p))
#define MV0(id) BB_GAME_CALL(id, 0)
#define MV1(id, a0) BB_GAME_CALL(id, 0, MV_WORD(a0))
#define MV2(id, a0, a1) BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1))
#define MV3(id, a0, a1, a2)                                                    \
  BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1), MV_WORD(a2))
#define MV4(id, a0, a1, a2, a3)                                                \
  BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1), MV_WORD(a2), MV_WORD(a3))
#define MV5(id, a0, a1, a2, a3, a4)                                            \
  BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1), MV_WORD(a2), MV_WORD(a3),      \
               MV_WORD(a4))
#define MV6(id, a0, a1, a2, a3, a4, a5)                                        \
  BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1), MV_WORD(a2), MV_WORD(a3),      \
               MV_WORD(a4), MV_WORD(a5))
#define MV7(id, a0, a1, a2, a3, a4, a5, a6)                                    \
  BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1), MV_WORD(a2), MV_WORD(a3),      \
               MV_WORD(a4), MV_WORD(a5), MV_WORD(a6))
#define MV8(id, a0, a1, a2, a3, a4, a5, a6, a7)                                \
  BB_GAME_CALL(id, 0, MV_WORD(a0), MV_WORD(a1), MV_WORD(a2), MV_WORD(a3),      \
               MV_WORD(a4), MV_WORD(a5), MV_WORD(a6), MV_WORD(a7))
#define MV_FORMAT(out, fmt, ...)                                               \
  bb_crt_output_words(out, 256, fmt, (uint32_t[]){__VA_ARGS__},                \
                      sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#endif
