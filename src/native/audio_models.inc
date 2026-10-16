/* SPDX-License-Identifier: GPL-3.0-only */
/* Windows fields are logical words; SDL devices and stdio objects stay native.
 * Queueing, RIFF parsing, callbacks and completion behavior remain shared. */
#include "audio_models.h"
static void *audio_host(uint32_t reference, uint32_t tag) {
  if (BB_U32(reference) != tag || !BB_U64(reference + 8u))
    bb_die("invalid audio handle %08x", reference);
  return (void *)(uintptr_t)BB_U64(reference + 8u);
}
static uint32_t audio_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void *audio_data(uint32_t *r, unsigned n, size_t size) {
  uint32_t reference = audio_arg(r, n);
  return reference ? BB_SPAN(reference, size) : NULL;
}
#define A(n) audio_arg(r, n)
#define P(n, bytes) audio_data(r, n, bytes)
#define WAVE() ((struct Wave *)audio_host(A(0), 0x57415645u))
#define MMIO() ((struct Mmio *)audio_host(A(0), 0x4d4d494fu))
static void model_num_devs(uint32_t *r) { r[7] = waveOutGetNumDevs(); }
static void model_caps(uint32_t *r) {
  r[7] = waveOutGetDevCapsA(A(0), A(2) >= 52 ? P(1, A(2)) : NULL, A(2));
}
static void model_open(uint32_t *r) {
  r[7] = waveOutOpen(A(0), A(1), P(2, 16), A(3), A(4), A(5));
}
static void model_prepare(uint32_t *r) {
  r[7] = waveOutPrepareHeader(WAVE(), P(1, 32), A(2));
}
static void model_unprepare(uint32_t *r) {
  r[7] = waveOutUnprepareHeader(WAVE(), P(1, 32), A(2));
}
static void model_write(uint32_t *r) {
  r[7] = waveOutWrite(WAVE(), P(1, 32), A(2));
}
static void model_pause(uint32_t *r) { r[7] = waveOutPause(WAVE()); }
static void model_restart(uint32_t *r) { r[7] = waveOutRestart(WAVE()); }
static void model_reset(uint32_t *r) { r[7] = waveOutReset(WAVE()); }
static void model_close(uint32_t *r) { r[7] = waveOutClose(WAVE()); }
static void model_mmio_open(uint32_t *r) {
  struct Mmio *io = mmioOpenA(P(0, 1), A(0) ? NULL : P(1, 52), A(2));
  r[7] = 0;
  if (!io)
    return;
  io->reference = bb_heap_zeroed(1, 16);
  if (!io->reference) {
    mmioClose(io, 0x10); /* A failed open cannot consume its source handle. */
    return;
  }
  BB_U32(io->reference) = 0x4d4d494fu;
  BB_U64(io->reference + 8u) = (uintptr_t)io;
  r[7] = io->reference;
}
static void model_read(uint32_t *r) {
  struct Mmio *io = MMIO();
  int32_t size = (int32_t)A(2);
  size_t amount = size > 0 ? (uint32_t)size : 0;
  if (amount > io->length - io->position)
    amount = io->length - io->position;
  r[7] = mmioRead(io, amount ? P(1, amount) : NULL, size);
}
static void model_mmio_close(uint32_t *r) { r[7] = mmioClose(MMIO(), A(1)); }
static void model_set_buffer(uint32_t *r) {
  r[7] = mmioSetBuffer(MMIO(), (void *)(uintptr_t)A(1), (int32_t)A(2), A(3));
}
static void model_ascend(uint32_t *r) {
  r[7] = mmioAscend(MMIO(), P(1, 20), A(2));
}
static void model_descend(uint32_t *r) {
  r[7] = mmioDescend(MMIO(), P(1, 20), P(2, 20), A(3));
}
static void model_midi_open(uint32_t *r) {
  /* The reference reports no MIDI device and never touches this output. */
  r[7] = midiOutOpen((void *)(uintptr_t)A(0), A(1), A(2), A(3), A(4));
}
static void model_midi_close(uint32_t *r) {
  r[7] = midiOutClose((Handle)(uintptr_t)A(0));
}
static void model_midi_message(uint32_t *r) {
  r[7] = midiOutShortMsg((Handle)(uintptr_t)A(0), A(1));
}
static void model_midi_volume(uint32_t *r) {
  r[7] = midiOutSetVolume((Handle)(uintptr_t)A(0), A(1));
}
#undef A
#undef P
#undef WAVE
#undef MMIO
static const struct {
  const char *name;
  struct bb_model_target target;
} audio_targets[] = {
#define T(index, name, body, words)                                            \
  {name, {BB_AUDIO_MODEL_API + index * 4u, body, words, 0, 0}}
    T(0, "waveOutGetNumDevs", model_num_devs, 0),
    T(1, "waveOutGetDevCapsA", model_caps, 3),
    T(2, "waveOutOpen", model_open, 6),
    T(3, "waveOutPrepareHeader", model_prepare, 3),
    T(4, "waveOutUnprepareHeader", model_unprepare, 3),
    T(5, "waveOutWrite", model_write, 3),
    T(6, "waveOutPause", model_pause, 1),
    T(7, "waveOutRestart", model_restart, 1),
    T(8, "waveOutReset", model_reset, 1),
    T(9, "waveOutClose", model_close, 1),
    T(10, "mmioOpenA", model_mmio_open, 3),
    T(11, "mmioRead", model_read, 3),
    T(12, "mmioClose", model_mmio_close, 2),
    T(13, "mmioSetBuffer", model_set_buffer, 4),
    T(14, "mmioDescend", model_descend, 4),
    T(15, "mmioAscend", model_ascend, 3),
    T(16, "midiOutOpen", model_midi_open, 5),
    T(17, "midiOutClose", model_midi_close, 1),
    T(18, "midiOutShortMsg", model_midi_message, 2),
    T(19, "midiOutSetVolume", model_midi_volume, 2)
#undef T
};
uint32_t bb_audio_model_export(const char *name) {
  for (unsigned i = 0; i < sizeof(audio_targets) / sizeof(*audio_targets); ++i)
    if (!strcmp(name, audio_targets[i].name))
      return audio_targets[i].target.identity;
  return 0;
}
const struct bb_model_target *bb_audio_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(audio_targets) / sizeof(*audio_targets); ++i)
    if (id == audio_targets[i].target.identity)
      return &audio_targets[i].target;
  return NULL;
}
