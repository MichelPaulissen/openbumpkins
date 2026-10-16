/* SPDX-License-Identifier: GPL-3.0-only */
/* Only the Windows argument/object representation belongs here. The SDL
 * device, buffers, format validation and mixing remain in the shared C code. */
#include "../bridge/heap.h"
#include "sound_models.h"
#include <pthread.h>

static uint32_t sound_identities[11], buffer_identities[21];
static uint32_t sound_references[2];
static pthread_once_t sound_model_once = PTHREAD_ONCE_INIT;
static void model_tables(void) {
  uint32_t *tables[] = {sound_identities, buffer_identities};
  const unsigned counts[] = {11, 21};
  const uint32_t bases[] = {BB_SOUND_MODEL_SOUND, BB_SOUND_MODEL_BUFFER};
  for (unsigned i = 0; i < 2; ++i) {
    for (unsigned n = 0; n < counts[i]; ++n)
      tables[i][n] = bases[i] + n * 4u;
#ifdef BB_PORTABLE_RUNTIME
    if (!bb_address_bind_auto(0xf0000000u, UINT64_C(0x100000000), tables[i],
                              counts[i] * 4u, 16, &sound_references[i]))
      bb_die("cannot map native sound method table");
#else
    sound_references[i] = bb_address_encode(tables[i]);
#endif
  }
}
static uint32_t model_object(struct Sound *host) {
  if (!host->reference) {
    uint32_t p = bb_heap_zeroed(1, 16);
    if (!p)
      return 0;
    pthread_once(&sound_model_once, model_tables);
    BB_U32(p) = sound_references[host->kind - 1];
    BB_U32(p + 4) = 0x534e0000u | host->kind;
    BB_U64(p + 8) = (uintptr_t)host;
    host->reference = p;
  }
  return host->reference;
}
static struct Sound *model_host(uint32_t p, unsigned kind) {
  uint32_t tag = BB_U32(p + 4);
  if ((kind && tag != (0x534e0000u | kind)) ||
      (!kind && tag != 0x534e0001u && tag != 0x534e0002u))
    bb_die("invalid native sound object %08x", p);
  uintptr_t host = (uintptr_t)BB_U64(p + 8);
  if (!host)
    bb_die("invalid native sound host pointer");
  return (struct Sound *)host;
}
static void model_release_proxy(struct Sound *host) {
  if (host->reference && !bb_heap_release(host->reference))
    bb_die("native sound proxy lost its owner");
}
static uint32_t model_samples(struct Buffer *buffer) {
  struct BufferData *data = buffer->storage;
  if (!data->reference) {
#ifdef BB_PORTABLE_RUNTIME
    if (!bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000), data->samples,
                              buffer->size, 16, &data->reference))
      return 0;
#else
    data->reference = bb_address_encode(data->samples);
#endif
  }
  return data->reference;
}
static void model_release_samples(struct BufferData *data) {
#ifdef BB_PORTABLE_RUNTIME
  if (data->reference && !bb_address_unbind(data->reference))
    bb_die("native sound sample mapping lost its owner");
#else
  (void)data;
#endif
}
static uint32_t sound_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void *sound_data(uint32_t *r, unsigned n, size_t size) {
  uint32_t p = sound_arg(r, n);
  return p ? BB_SPAN(p, size) : NULL;
}
#define A(n) sound_arg(r, n)
#define P(n, bytes) sound_data(r, n, bytes)
#define HOST(n, kind) model_host(A(n), kind)
#define BUFFER(n) ((struct Buffer *)HOST(n, 2))
static uint32_t model_output(uint32_t slot, struct Sound *object) {
  uint32_t p = model_object(object);
  if (!p) {
    release(object);
    return 0x8007000eu;
  }
  BB_U32(slot) = p;
  return 0;
}
static void model_create(uint32_t *r) {
  Handle output = NULL;
  r[7] = bb_sound_create((void *)(uintptr_t)A(0), &output,
                         (Handle)(uintptr_t)A(2));
  if (!r[7])
    r[7] = model_output(A(1), output);
}
static void model_query(uint32_t *r) {
  void *output = NULL;
  r[7] = query(HOST(0, 0), P(1, 16), &output);
  if (!r[7])
    r[7] = model_output(A(2), output);
  else
    BB_U32(A(2)) = 0;
}
static void model_addref(uint32_t *r) { r[7] = addref(HOST(0, 0)); }
static void model_release(uint32_t *r) { r[7] = release(HOST(0, 0)); }
static void model_create_buffer(uint32_t *r) {
  struct Buffer *output = NULL;
  r[7] = create_buffer(HOST(0, 1), P(1, 20), &output, (Handle)(uintptr_t)A(3));
  if (!r[7])
    r[7] = model_output(A(2), &output->sound);
}
static void model_duplicate(uint32_t *r) {
  struct Buffer *output = NULL;
  struct Buffer *source = A(1) ? (struct Buffer *)HOST(1, 0) : NULL;
  r[7] = duplicate_buffer(HOST(0, 1), source, A(2) ? &output : NULL);
  if (!r[7]) {
    BB_U32(A(2)) = 0;
    r[7] = model_output(A(2), &output->sound);
  } else if (source && A(2) && source->sound.kind == 2)
    BB_U32(A(2)) = 0;
}
static void model_sound_caps(uint32_t *r) {
  uint32_t size = BB_U32(A(1));
  r[7] = sound_caps(HOST(0, 1), P(1, size < 4 ? 4 : size));
}
static void model_cooperative(uint32_t *r) {
  r[7] = cooperative(HOST(0, 1), (Handle)(uintptr_t)A(1), A(2));
}
static void model_compact(uint32_t *r) { r[7] = compact(HOST(0, 1)); }
static void model_speaker_get(uint32_t *r) {
  r[7] = speaker_get(HOST(0, 1), P(1, 4));
}
static void model_speaker_set(uint32_t *r) {
  r[7] = speaker_set(HOST(0, 1), A(1));
}
static void model_initialize(uint32_t *r) {
  r[7] = initialize(HOST(0, 1), (void *)(uintptr_t)A(1));
}
static void model_buffer_caps(uint32_t *r) {
  r[7] = buffer_caps(BUFFER(0), P(1, 20));
}
static void model_position(uint32_t *r) {
  r[7] = position(BUFFER(0), P(1, 4), P(2, 4));
}
static void model_format(uint32_t *r) {
  uint32_t size = A(2) < 18 ? A(2) : 18;
  r[7] = get_format(BUFFER(0), size ? P(1, size) : NULL, A(2), P(3, 4));
}
static void model_volume(uint32_t *r) { r[7] = get_volume(BUFFER(0), P(1, 4)); }
static void model_pan(uint32_t *r) { r[7] = get_pan(BUFFER(0), P(1, 4)); }
static void model_frequency(uint32_t *r) {
  r[7] = get_frequency(BUFFER(0), P(1, 4));
}
static void model_status(uint32_t *r) { r[7] = status(BUFFER(0), P(1, 4)); }
static void model_lock(uint32_t *r) {
  struct Buffer *b = BUFFER(0);
  uint32_t samples = model_samples(b);
  if (!samples) {
    r[7] = 0x8007000eu;
    return;
  }
  uint32_t offset = A(1), bytes = A(2);
  r[7] = lock_range(b, &offset, &bytes, A(7));
  if (r[7])
    return;
  BB_U32(A(3)) = samples + offset;
  BB_U32(A(4)) = bytes < b->size - offset ? bytes : b->size - offset;
  if (A(5))
    BB_U32(A(5)) = bytes > BB_U32(A(4)) ? samples : 0;
  if (A(6))
    BB_U32(A(6)) = bytes - BB_U32(A(4));
  b->locked = 1;
}
static void model_unlock(uint32_t *r) {
  r[7] = unlock_buffer(BUFFER(0), (void *)(uintptr_t)A(1), A(2),
                       (void *)(uintptr_t)A(3), A(4));
}
static void model_play(uint32_t *r) {
  r[7] = play_buffer(BUFFER(0), A(1), A(2), A(3));
}
static void model_set_position(uint32_t *r) {
  r[7] = set_position(BUFFER(0), A(1));
}
static void model_set_format(uint32_t *r) {
  r[7] = set_format(BUFFER(0), P(1, 16));
}
static void model_set_volume(uint32_t *r) {
  r[7] = set_volume(BUFFER(0), (int32_t)A(1));
}
static void model_set_pan(uint32_t *r) {
  r[7] = set_pan(BUFFER(0), (int32_t)A(1));
}
static void model_set_frequency(uint32_t *r) {
  r[7] = set_frequency(BUFFER(0), A(1));
}
static void model_stop(uint32_t *r) { r[7] = stop_buffer(BUFFER(0)); }
static void model_restore(uint32_t *r) { r[7] = restore_buffer(BUFFER(0)); }
#undef A
#undef P
#undef HOST
#undef BUFFER
static const struct bb_model_target sound_targets[] = {
    {BB_SOUND_MODEL_API, model_create, 3, 0, 0},
#define S(slot, body, words)                                                   \
  {BB_SOUND_MODEL_SOUND + slot * 4u, body, words, 0, 0}
    S(0, model_query, 3),
    S(1, model_addref, 1),
    S(2, model_release, 1),
    S(3, model_create_buffer, 4),
    S(4, model_sound_caps, 2),
    S(5, model_duplicate, 3),
    S(6, model_cooperative, 3),
    S(7, model_compact, 1),
    S(8, model_speaker_get, 2),
    S(9, model_speaker_set, 2),
    S(10, model_initialize, 2),
#undef S
#define B(slot, body, words)                                                   \
  {BB_SOUND_MODEL_BUFFER + slot * 4u, body, words, 0, 0}
    B(0, model_query, 3),
    B(1, model_addref, 1),
    B(2, model_release, 1),
    B(3, model_buffer_caps, 2),
    B(4, model_position, 3),
    B(5, model_format, 4),
    B(6, model_volume, 2),
    B(7, model_pan, 2),
    B(8, model_frequency, 2),
    B(9, model_status, 2),
    B(11, model_lock, 8),
    B(12, model_play, 4),
    B(13, model_set_position, 2),
    B(14, model_set_format, 2),
    B(15, model_set_volume, 2),
    B(16, model_set_pan, 2),
    B(17, model_set_frequency, 2),
    B(18, model_stop, 1),
    B(19, model_unlock, 5),
    B(20, model_restore, 1)
#undef B
};
uint32_t bb_sound_model_export(const char *name) {
  return !strcmp(name, "DirectSoundCreate") ? BB_SOUND_MODEL_API : 0;
}
const struct bb_model_target *bb_sound_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(sound_targets) / sizeof(*sound_targets); ++i)
    if (sound_targets[i].identity == id)
      return sound_targets + i;
  return NULL;
}
