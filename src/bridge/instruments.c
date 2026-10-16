/* SPDX-License-Identifier: GPL-3.0-only */
#include "instruments.h"
#include "abi.h"
#include "interop.h"
#include "legacy_audio_calls.h"
#include "midi.h"
#include "wav_sample.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
typedef uint32_t(BB_STDCALL *tick_fn)(void) __attribute__((unused));
typedef uint32_t(BB_STDCALL *wait_fn)(void *, uint32_t) __attribute__((unused));
typedef uint32_t(BB_STDCALL *midi_fn)(void *, uint32_t) __attribute__((unused));
static uintptr_t bank(void) { return BB_U32(0x48c400); }
static void debug(const char *text) { LA_DEBUG(BB_POINTER(0x6ca374), text); }
static void read(void *out, uint32_t size, void *file) {
  LA_READ(0x46cf40, out, size, 1, file);
}
static void write(const void *in, uint32_t size, void *file) {
  LA_WRITE(0x46cd20, in, size, 1, file);
}
void BB_INSTRUMENT_M bb_instrument_song_start_entry(void *song) {
  uint32_t p = bb_address_encode(song);
  BB_U32(bank() + 0xa610) = p;
  if (BB_U32(bank() + 0xa610)) {
    for (uint32_t i = 0; i < WORD(p + 0x1602); ++i)
      bb_midi_track_begin_entry(PTR(p + i * 0x58));
    BB_U32(0x6c1b7c) = WORD(BB_U32(bank() + 0xa610) + 0x1604);
    BB_U32(0x6c1c88) = BB_U32(BB_U32(bank() + 0xa610) + 0x160c);
    BB_U32(0x48c3e4) = 1;
    BB_U32(0x6c1b80) = LA_CLOCK(BB_POINTER(0x6ca378));
  }
}
void BB_INSTRUMENT_M bb_instrument_song_rewind_entry(void *song) {
  uint32_t p = bb_address_encode(song);
  for (uint32_t i = 0; i < WORD(p + 0x1602); ++i)
    bb_midi_track_rewind_entry(PTR(p + i * 0x58));
  BB_U32(0x48c3e4) = 0;
}
void BB_INSTRUMENT_M bb_instrument_song_tick_entry(void *song, uint32_t time) {
  uint32_t p = bb_address_encode(song);
  for (uint32_t i = 0; i < WORD(p + 0x1602); ++i)
    bb_midi_track_tick_entry(PTR(p + i * 0x58), time);
}
void BB_INSTRUMENT_M bb_instrument_device_initialize_entry(void *object,
                                                           void *window) {
  uint32_t p = bb_address_encode(object);
  BB_SET_POINTER(p + 0xa618, window);
  bb_wav_device_initialize_entry(PTR(p + 4), window);
  bb_instrument_midi_open_entry(object);
}
void BB_INSTRUMENT_M bb_instrument_device_attach_entry(void *object,
                                                       void *window,
                                                       void *sound,
                                                       void *primary) {
  uint32_t p = bb_address_encode(object);
  BB_SET_POINTER(p + 0xa618, window);
  bb_wav_device_attach_entry(PTR(p + 4), sound, primary);
  bb_instrument_midi_open_entry(object);
}
uint32_t BB_INSTRUMENT_M bb_instrument_runtime_initialize_entry(void *object) {
  typedef void *(BB_STDCALL * event_fn)(void *, uint32_t, uint32_t,
                                        const char *)__attribute__((unused));
  typedef void *(BB_STDCALL * thread_fn)(
      void *, uint32_t, uint32_t(BB_STDCALL *)(void *), void *, uint32_t,
      uint32_t *)__attribute__((unused));
  typedef uint32_t(BB_STDCALL * priority_fn)(void *, int32_t)
      __attribute__((unused));
  BB_U32(bb_address_encode(object)) = 0;
  BB_SET_POINTER(0x48c400, object);
  BB_SET_POINTER(0x48c3f0,
                 LA_CREATE_EVENT(BB_POINTER(0x6ca4a4), NULL, 1, 0, NULL));
  if (!BB_U32(0x48c3f0))
    return 0;
  BB_SET_POINTER(0x48c3ec, LA_CREATE_THREAD(BB_POINTER(0x6ca3cc), NULL, 0,
                                            bb_instrument_worker_entry, NULL, 0,
                                            (uint32_t *)0x48c3e8));
  if (!BB_U32(0x48c3ec)) {
    BB_U32(0x48c3e8) = 0;
    return 0;
  }
  LA_PRIORITY(BB_POINTER(0x6ca3c4), BB_POINTER(0x48c3ec), 2);
  return 1;
}
void BB_INSTRUMENT_M bb_instrument_runtime_release_entry(void *object) {
  BB_U32(0x48c3e0) = 0;
  LA_WAIT(BB_POINTER(0x6ca478), BB_POINTER(0x48c3f0), 500);
  uint32_t song = BB_U32(bb_address_encode(object));
  bb_instrument_midi_close_entry(object);
  while (song) {
    uint32_t next = BB_U32(song + 0x1618);
    bb_midi_release_entry(PTR(song));
    LA_RELEASE(0x46c110, PTR(song));
    song = next;
  }
}
void BB_INSTRUMENT_M bb_instrument_gain_set_entry(void *object, double value) {
  (void)object;
  BB_DOUBLE(0x48c3f8) = value;
  if (value > BB_DOUBLE(0x4823e0))
    BB_DOUBLE(0x48c3f8) = 1.0;
  else if (!(value >= BB_DOUBLE(0x4823e8)))
    BB_DOUBLE(0x48c3f8) = 0.0; /* Unordered comparison also clamps to zero. */
}
long double BB_INSTRUMENT_M bb_instrument_gain_get_entry(void *object) {
  (void)object;
  return BB_DOUBLE(0x48c3f8);
}
void BB_INSTRUMENT_M bb_instrument_song_load_entry(void *object,
                                                   const char *filename) {
  void *song = LA_ALLOCATE(0x46c060, 0x161c);
  if (song)
    song = bb_midi_construct_entry(song);
  uint32_t p = bb_address_encode(object), s = bb_address_encode(song);
  BB_U32(s + 0x1618) = BB_U32(p);
  BB_U32(p) = s;
  BB_U32(bank() + 0xa610) = s;
  bb_midi_load_entry(song, filename);
}
uint32_t BB_INSTRUMENT_M bb_instrument_bank_read_entry(void *object,
                                                       void *file) {
  uint32_t p = bb_address_encode(object);
  char name[256];
  uint16_t count = 0;
  read(name, 3, file);
  read(name, 3, file); /* Header/version are consumed without validation. */
  read(&count, 2, file);
  for (uint32_t i = 0; i < count; ++i) {
    read(name, 64, file);
    uint32_t sample = (uintptr_t)bb_wav_acquire_entry(PTR(p + 4), name);
    read(PTR(sample + 0x92), 1, file);
    read(PTR(sample + 0x94), 1, file);
    read(PTR(sample + 0x98), 1, file);
    read(PTR(sample + 0xa0), 1, file);
    read(PTR(sample + 0xa1), 1, file);
  }
  read(&count, 2, file);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t program = p + 0x10 + i * 0x14c;
    read(PTR(program), 64, file);
    read(PTR(program + 0x144), 4, file);
    read(PTR(program + 0x148), 1, file);
    read(PTR(program + 0x40), 4, file);
    for (uint32_t j = 0; (int32_t)j < BB_I32(program + 0x40); ++j) {
      read(name, 64, file);
      BB_SET_POINTER(program + 0x44 + j * 4,
                     bb_wav_lookup_entry(PTR(p + 4), name));
    }
  }
  return 1;
}
uint32_t BB_INSTRUMENT_M bb_instrument_bank_write_entry(void *object,
                                                        void *file) {
  uint32_t p = bb_address_encode(object), sample;
  uint16_t count = 0;
  write((const void *)0x48c65c, 3, file);
  write((const void *)0x48c660, 3, file);
  for (sample = BB_U32(p + 4); sample; sample = BB_U32(sample + 0xa8))
    ++count;
  write(&count, 2, file);
  for (sample = BB_U32(p + 4); sample; sample = BB_U32(sample + 0xa8)) {
    write(PTR(sample), 64, file);
    write(PTR(sample + 0x92), 1, file);
    write(PTR(sample + 0x94), 1, file);
    write(PTR(sample + 0x98), 1, file);
    write(PTR(sample + 0xa0), 1, file);
    write(PTR(sample + 0xa1), 1, file);
  }
  count = 128;
  write(&count, 2, file);
  for (uint32_t i = 0; i < 128; ++i) {
    uint32_t program = p + 0x10 + i * 0x14c;
    write(PTR(program), 64, file);
    write(PTR(program + 0x144), 4, file);
    write(PTR(program + 0x148), 1, file);
    write(PTR(program + 0x40), 4, file);
    for (uint32_t j = 0; (int32_t)j < BB_I32(program + 0x40); ++j)
      write(BB_POINTER(program + 0x44 + j * 4), 64, file);
  }
  return 1;
}
static uint32_t file_bank(void *object, const char *filename, uintptr_t mode,
                          unsigned saving) {
  void *file = LA_OPEN(0x46bdd0, filename, (const char *)mode);
  if (file) {
    if (saving)
      bb_instrument_bank_write_entry(object, file);
    else
      bb_instrument_bank_read_entry(object, file);
    LA_FILE_ONE(0x46bbd0, file);
  }
  return 1;
}
uint32_t BB_INSTRUMENT_M bb_instrument_bank_load_entry(void *object,
                                                       const char *filename) {
  return file_bank(object, filename, 0x48c658, 0);
}
uint32_t BB_INSTRUMENT_M bb_instrument_bank_save_entry(void *object,
                                                       const char *filename) {
  return file_bank(object, filename, 0x48c664, 1);
}
uint32_t BB_INSTRUMENT_M bb_instrument_bank_import_entry(void *object,
                                                         const char *filename) {
  return file_bank(object, filename, 0x48c668, 0);
}
uint32_t BB_INSTRUMENT_M bb_instrument_bank_export_entry(void *object,
                                                         const char *filename) {
  return file_bank(object, filename, 0x48c66c, 1);
}
uint32_t BB_STDCALL bb_instrument_worker_entry(void *argument) {
  (void)argument;
  BB_U32(0x6c1b80) = LA_CLOCK(BB_POINTER(0x6ca378));
  while (BB_U32(0x48c3e0)) {
    if (BB_U32(0x48c3e4)) {
      BB_U32(0x6c1b78) = LA_CLOCK(BB_POINTER(0x6ca378));
      uint32_t time = (BB_U32(0x6c1b78) - BB_U32(0x6c1b80)) * BB_U32(0x6c1b7c);
      time /= BB_U32(0x6c1c88);
      BB_U32(0x6c1c8c) = time;
      /* The original ticks the list head, not the +0xa610 selected-song field.
       */
      bb_instrument_song_tick_entry(BB_POINTER(bank()), time);
      LA_WAIT(BB_POINTER(0x6ca478), BB_POINTER(0x48c3f0), 10);
    } else
      LA_WAIT(BB_POINTER(0x6ca478), BB_POINTER(0x48c3f0), 1000);
  }
  debug((const char *)0x48c670);
  BB_U32(0x48c3e8) = 0;
  LA_HANDLE_ONE(BB_POINTER(0x6ca37c), BB_POINTER(0x48c3ec));
  BB_U32(0x48c3ec) = 0;
  return 0;
}
void BB_INSTRUMENT_M bb_instrument_program_append_entry(void *program,
                                                        void *sample) {
  uint32_t p = bb_address_encode(program);
  if (sample) {
    BB_SET_POINTER(p + 0x44 + BB_U32(p + 0x40) * 4, sample);
    ++BB_U32(p + 0x40);
  }
}
void BB_INSTRUMENT_M bb_instrument_note_on_entry(void *program, uint8_t channel,
                                                 uint8_t note,
                                                 uint8_t velocity) {
  uint32_t p = bb_address_encode(program);
  volatile long double scaled = (long double)velocity * BB_DOUBLE(0x48c3f8);
  velocity = (uint8_t)(int64_t)scaled;
  for (uint32_t i = 0; (int32_t)i < BB_I32(p + 0x40); ++i)
    bb_wav_note_entry(BB_POINTER(p + 0x44 + i * 4), note, velocity);
  if (BB_U32(p + 0x144)) {
    uint32_t message = ((uint32_t)BYTE(p + 0x148) << 8) + channel + 0xc0;
    LA_MIDI_MESSAGE(BB_POINTER(0x6ca584), BB_POINTER(bank() + 0xa624), message);
    message =
        ((uint32_t)velocity << 16) + ((uint32_t)note << 8) + channel + 0x90;
    LA_MIDI_MESSAGE(BB_POINTER(0x6ca584), BB_POINTER(bank() + 0xa624), message);
  }
}
void BB_INSTRUMENT_M bb_instrument_note_off_entry(void *program,
                                                  uint8_t channel, uint8_t note,
                                                  uint8_t velocity) {
  uint32_t p = bb_address_encode(program);
  if (BB_U32(p + 0x144)) {
    uint32_t message =
        ((uint32_t)velocity << 16) + ((uint32_t)note << 8) + channel + 0x80;
    LA_MIDI_MESSAGE(BB_POINTER(0x6ca584), BB_POINTER(bank() + 0xa624), message);
  }
}
void BB_INSTRUMENT_M bb_instrument_midi_open_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  typedef uint32_t(BB_STDCALL * open_fn)(void **, uint32_t, uint32_t, uint32_t,
                                         uint32_t) __attribute__((unused));
  BB_U32(p + 0xa61c) = 1;
  if (!LA_MIDI_OPEN(BB_POINTER(0x6ca58c), (void **)(uintptr_t)(p + 0xa624),
                    BB_U32(p + 0xa61c), BB_U32(p + 0xa618), 0, 0x10000))
    LA_MIDI_MESSAGE(BB_POINTER(0x6ca588), BB_POINTER(p + 0xa624), 0xffffffff);
}
void BB_INSTRUMENT_M bb_instrument_midi_close_entry(void *object) {
  LA_HANDLE_ONE(BB_POINTER(0x6ca590),
                BB_POINTER(bb_address_encode(object) + 0xa624));
}
