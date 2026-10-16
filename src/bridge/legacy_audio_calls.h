/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_LEGACY_AUDIO_CALLS_H
#define BB_LEGACY_AUDIO_CALLS_H
#include "crt_primitives.h"
/* Keep the library's packed records and virtual-call arguments four bytes
 * wide. Only private strings/descriptors/output words need scoped logical
 * storage; CRT-owned streams, samples and device proxies keep their identities.
 */
#ifdef BB_MODEL_RUNTIME
#include "crt_output.h"
#include "game_file.h"
#include <math.h>
#define LA_WORD(v) bb_game_word((uintptr_t)(v))
static inline void la_debug(uint32_t id, const char *text) {
  struct bb_model_buffer p;
  bb_model_buffer_begin(&p, text, strlen(BB_DATA(text)) + 1);
  BB_GAME_CALL(id, 0, p.reference);
  bb_model_buffer_end(&p, 0);
}
static inline uint32_t la_create(uint32_t id, void *sound, const uint32_t *desc,
                                 void **out, void *outer) {
  struct bb_model_buffer d;
  bb_model_buffer_begin(&d, desc, 20);
  uint32_t value = BB_GAME_CALL(id, 0, LA_WORD(sound), d.reference,
                                LA_WORD(out), LA_WORD(outer));
  bb_model_buffer_end(&d, 0);
  return value;
}
static inline uint32_t la_status(uint32_t id, void *object, uint32_t *out) {
  struct bb_model_buffer p;
  bb_model_buffer_begin(&p, out, 4);
  uint32_t value = BB_GAME_CALL(id, 0, LA_WORD(object), p.reference);
  bb_model_buffer_end(&p, 1);
  return value;
}
static inline uint32_t la_position(uint32_t id, void *object, uint32_t *play,
                                   uint32_t *write) {
  struct bb_model_buffer p, w;
  bb_model_buffer_begin(&p, play, 4);
  bb_model_buffer_begin(&w, write, 4);
  uint32_t value =
      BB_GAME_CALL(id, 0, LA_WORD(object), p.reference, w.reference);
  bb_model_buffer_end(&w, 1);
  bb_model_buffer_end(&p, 1);
  return value;
}
static inline int la_format_text(char *out, uint32_t fmt, const char *text) {
  struct bb_model_buffer p;
  bb_model_buffer_begin(&p, text, strlen(BB_DATA(text)) + 1);
  int value = bb_crt_output_words(out, 256, (const char *)(uintptr_t)fmt,
                                  &p.reference, 1);
  bb_model_buffer_end(&p, 0);
  return value;
}
#define LA_FILE_ONE(id, a0) BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0))
#define LA_READ(id, out, size, count, file)                                    \
  bb_game_read(out, size, count, file, 0)
#define LA_DEBUG(id, text) la_debug(LA_WORD(id), text)
#define LA_FORMAT(id, out, fmt, ...)                                           \
  bb_crt_output_words(out, 256, fmt, (uint32_t[]){__VA_ARGS__},                \
                      sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#define LA_ALLOCATE(id, n) bb_game_allocate(n)
#define LA_RELEASE(id, a0) ((void)BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0)))
#define LA_OPEN(id, name, mode) bb_game_open(name, LA_WORD(mode))
#define LA_MESSAGE_BOX(id, a0, a1, a2, a3)                                     \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2),          \
               LA_WORD(a3))
#define LA_COOPERATIVE(id, a0, a1, a2)                                         \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2))
#define LA_CREATE_BUFFER(id, sound, desc, out, outer)                          \
  la_create(LA_WORD(id), sound, desc, out, outer)
#define LA_PLAY(id, a0, a1, a2, a3)                                            \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2),          \
               LA_WORD(a3))
#define LA_BUFFER_ZERO(id, a0) BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0))
#define LA_DUPLICATE(id, a0, a1, a2)                                           \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2))
#define LA_LOCK(id, a0, a1, a2, a3, a4, a5, a6, a7)                            \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2),          \
               LA_WORD(a3), LA_WORD(a4), LA_WORD(a5), LA_WORD(a6),             \
               LA_WORD(a7))
#define LA_UNLOCK(id, a0, a1, a2, a3, a4)                                      \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2),          \
               LA_WORD(a3), LA_WORD(a4))
#define LA_SEEK(id, a0, a1, a2)                                                \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2))
#define LA_STATUS(id, object, out) la_status(LA_WORD(id), object, out)
#define LA_BUFFER_WORD(id, a0, a1)                                             \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1))
#define LA_POSITION(id, object, play, write)                                   \
  la_position(LA_WORD(id), object, play, write)
#define LA_POWER(id, a, b) ((void)(id), (long double)pow(a, b))
#define LA_WRITE(id, data, size, count, file)                                  \
  bb_game_write(data, size, count, file)
#define LA_CLOCK(id) BB_GAME_CALL(LA_WORD(id), 0)
#define LA_CREATE_EVENT(id, a0, a1, a2, a3)                                    \
  ((void *)(uintptr_t)BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1),   \
                                   LA_WORD(a2), LA_WORD(a3)))
#define LA_CREATE_THREAD(id, sec, size, entry, arg, flags, tid)                \
  ((void *)(uintptr_t)BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(sec), size,         \
                                   0x469c41, LA_WORD(arg), flags,              \
                                   LA_WORD(tid)))
#define LA_PRIORITY(id, a0, a1)                                                \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1))
#define LA_WAIT(id, a0, a1)                                                    \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1))
#define LA_HANDLE_ONE(id, a0) BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0))
#define LA_MIDI_MESSAGE(id, a0, a1)                                            \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1))
#define LA_MIDI_OPEN(id, a0, a1, a2, a3, a4)                                   \
  BB_GAME_CALL(LA_WORD(id), 0, LA_WORD(a0), LA_WORD(a1), LA_WORD(a2),          \
               LA_WORD(a3), LA_WORD(a4))
#else
#define LA_FILE_ONE(id, ...) ((int (*)(void *))BB_CODE(id))(__VA_ARGS__)
#define LA_READ(id, ...)                                                       \
  ((uint32_t (*)(void *, uint32_t, uint32_t, void *))BB_CODE(id))(__VA_ARGS__)
#define LA_DEBUG(id, ...)                                                      \
  ((void(BB_STDCALL *)(const char *))BB_CODE(id))(__VA_ARGS__)
#define LA_FORMAT(id, ...)                                                     \
  ((int (*)(char *, const char *, ...))BB_CODE(id))(__VA_ARGS__)
#define LA_ALLOCATE(id, ...) ((void *(*)(uint32_t))BB_CODE(id))(__VA_ARGS__)
#define LA_RELEASE(id, ...) ((void (*)(void *))BB_CODE(id))(__VA_ARGS__)
#define LA_OPEN(id, ...)                                                       \
  ((void *(*)(const char *, const char *))BB_CODE(id))(__VA_ARGS__)
#define LA_MESSAGE_BOX(id, ...) ((box_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_COOPERATIVE(id, ...) ((coop_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_CREATE_BUFFER(id, ...) ((create_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_PLAY(id, ...) ((play_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_BUFFER_ZERO(id, ...) ((method0)BB_CODE(id))(__VA_ARGS__)
#define LA_DUPLICATE(id, ...) ((duplicate_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_LOCK(id, ...) ((lock_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_UNLOCK(id, ...) ((unlock_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_SEEK(id, ...)                                                       \
  ((int (*)(void *, uint32_t, int))BB_CODE(id))(__VA_ARGS__)
#define LA_STATUS(id, ...) ((status_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_BUFFER_WORD(id, ...) ((method1)BB_CODE(id))(__VA_ARGS__)
#define LA_POSITION(id, ...) ((position_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_POWER(id, ...)                                                      \
  ((long double (*)(double, double))BB_CODE(id))(__VA_ARGS__)
#define LA_WRITE(id, ...)                                                      \
  ((uint32_t (*)(const void *, uint32_t, uint32_t, void *))BB_CODE(id))(       \
      __VA_ARGS__)
#define LA_CLOCK(id, ...) ((tick_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_CREATE_EVENT(id, ...) ((event_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_CREATE_THREAD(id, ...) ((thread_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_PRIORITY(id, ...) ((priority_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_WAIT(id, ...) ((wait_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_HANDLE_ONE(id, ...)                                                 \
  ((uint32_t(BB_STDCALL *)(void *))BB_CODE(id))(__VA_ARGS__)
#define LA_MIDI_MESSAGE(id, ...) ((midi_fn)BB_CODE(id))(__VA_ARGS__)
#define LA_MIDI_OPEN(id, ...) ((open_fn)BB_CODE(id))(__VA_ARGS__)
#endif
#endif
