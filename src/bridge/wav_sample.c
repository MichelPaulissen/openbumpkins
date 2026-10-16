/* SPDX-License-Identifier: GPL-3.0-only */
#include "wav_sample.h"
#include "abi.h"
#include "interop.h"
#include "legacy_audio_calls.h"
#include "media_devices.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#define METHOD(object, offset)                                                 \
  BB_POINTER(BB_U32((uintptr_t)(object)) + (offset))
typedef uint32_t(BB_STDCALL *method0)(void *) __attribute__((unused));
typedef uint32_t(BB_STDCALL *method1)(void *, uint32_t) __attribute__((unused));
typedef uint32_t(BB_STDCALL *play_fn)(void *, uint32_t, uint32_t, uint32_t)
    __attribute__((unused));
typedef uint32_t(BB_STDCALL *status_fn)(void *, uint32_t *)
    __attribute__((unused));
typedef uint32_t(BB_STDCALL *position_fn)(void *, uint32_t *, uint32_t *)
    __attribute__((unused));
typedef uint32_t(BB_STDCALL *create_fn)(void *, const uint32_t *, void **,
                                        void *) __attribute__((unused));
typedef uint32_t(BB_STDCALL *duplicate_fn)(void *, void *, void **)
    __attribute__((unused));
typedef uint32_t(BB_STDCALL *lock_fn)(void *, uint32_t, uint32_t, void **,
                                      uint32_t *, void **, uint32_t *, uint32_t)
    __attribute__((unused));
typedef uint32_t(BB_STDCALL *unlock_fn)(void *, void *, uint32_t, void *,
                                        uint32_t) __attribute__((unused));
static void debug(const char *text) { LA_DEBUG(BB_POINTER(0x6ca374), text); }
static void copy(char *out, const char *in) {
  bb_crt_string_copy_entry(out, in);
}
static void read(void *out, uint32_t size, uint32_t count, void *file) {
  LA_READ(0x46cf40, out, size, count, file);
}
static void *allocate(uint32_t size) { return LA_ALLOCATE(0x46c060, size); }
static void remove_object(void *object) { LA_RELEASE(0x46c110, object); }
void BB_WAV_M bb_wav_device_initialize_entry(void *object, void *window) {
  uint32_t p = bb_address_encode(object);
  typedef uint32_t(BB_STDCALL * coop_fn)(void *, void *, uint32_t)
      __attribute__((unused));
  typedef uint32_t(BB_STDCALL * box_fn)(void *, const char *, const char *,
                                        uint32_t) __attribute__((unused));
  uint32_t result =
      bb_media_sound_create_entry(NULL, (void **)(uintptr_t)(p + 4), NULL);
  if (result) {
    LA_MESSAGE_BOX(BB_POINTER(0x6ca558), window, (const char *)0x48c414,
                   (const char *)0x48c404, 16);
    return;
  }
  void *sound = BB_POINTER(p + 4);
  LA_COOPERATIVE(METHOD(sound, 0x18), sound, window, 1);
  uint32_t desc[5];
  bb_crt_memory_fill_entry(desc, 0, 20);
  desc[0] = 20;
  desc[1] = 1;
  sound = BB_POINTER(p + 4);
  if (LA_CREATE_BUFFER(METHOD(sound, 12), sound, desc,
                       (void **)(uintptr_t)(p + 8), NULL))
    LA_MESSAGE_BOX(BB_POINTER(0x6ca558), window, (const char *)0x48c448,
                   (const char *)0x48c434, 16);
  else {
    void *buffer = BB_POINTER(p + 8);
    LA_PLAY(METHOD(buffer, 0x30), buffer, 0, 0, 1);
  }
}
void BB_WAV_M bb_wav_device_attach_entry(void *object, void *sound,
                                         void *primary) {
  uint32_t p = bb_address_encode(object);
  BB_SET_POINTER(p + 4, sound);
  BB_SET_POINTER(p + 8, primary);
}
void *BB_WAV_M bb_wav_acquire_entry(void *object, const char *filename) {
  char name[256], base[256];
  copy(name, filename);
  typedef char *(*find_fn)(const char *, const char *)__attribute__((unused));
  typedef char *(*last_fn)(const char *, int)__attribute__((unused));
  if (bb_crt_string_search_entry(name, (const char *)0x48c468))
    *bb_crt_string_search_entry(name, (const char *)0x48c470) = 0;
  if (bb_crt_string_find_last_entry(name, '\\')) {
    copy(base, bb_crt_string_find_last_entry(name, '\\') + 1);
    copy(name, base);
  }
  void *sample = bb_wav_lookup_entry(object, name);
  if (sample)
    return sample;
  sample = allocate(0xac);
  if (sample)
    sample = bb_wav_construct_entry(sample);
  uint32_t p = bb_address_encode(object), s = bb_address_encode(sample);
  BB_U32(s + 0xa8) = BB_U32(p);
  BB_U32(s + 0xa4) = p;
  BB_U32(p) = s;
  copy(sample, name);
  if (!bb_wav_load_entry(sample, sample))
    debug((const char *)0x48c478);
  return sample;
}
void BB_WAV_M bb_wav_remove_entry(void *object, void *sample) {
  uint32_t p = bb_address_encode(object), current = BB_U32(p), previous = 0;
  if (current == bb_address_encode(sample)) {
    uint32_t next = BB_U32(current + 0xa8);
    remove_object(PTR(current));
    BB_U32(p) = next;
  } else {
    while (current && current != bb_address_encode(sample)) {
      previous = current;
      current = BB_U32(current + 0xa8);
    }
    if (current && previous)
      BB_U32(previous + 0xa8) = BB_U32(current + 0xa8);
    if (current)
      remove_object(PTR(current));
  }
}
void BB_WAV_M bb_wav_play_entry(void *object, void *sample) {
  (void)object;
  if (sample)
    bb_wav_trigger_entry(sample);
}
void *BB_WAV_M bb_wav_lookup_entry(void *object, const char *name) {
  debug(name);
  uint32_t sample = BB_U32(bb_address_encode(object));
  while (sample) {
    debug((const char *)(uintptr_t)sample);
    if (!bb_crt_string_compare_entry((const char *)(uintptr_t)sample, name))
      return PTR(sample);
    sample = BB_U32(sample + 0xa8);
  }
  return NULL;
}
void *BB_WAV_M bb_wav_construct_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  BYTE(p) = 0;
  for (unsigned i = 0; i < 8; ++i)
    BB_U32(p + 0x40 + i * 4) = 0;
  BB_U32(p + 0x78) = 0;
  BYTE(p + 0x98) = 60;
  BB_U32(p + 0x94) = 1;
  BYTE(p + 0xa0) = 0;
  BYTE(p + 0xa1) = 127;
  BYTE(p + 0x92) = 4;
  return object;
}
void BB_WAV_M bb_wav_buffers_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t desc[5] = {0};
  for (uint32_t i = 0; i < BYTE(p + 0x92); ++i) {
    void *buffer = BB_POINTER(p + 0x40 + i * 4);
    if (buffer)
      LA_BUFFER_ZERO(METHOD(buffer, 8), buffer);
    void *sound;
    if (i == 0) {
      desc[0] = 20;
      desc[1] = 0xe2;
      desc[4] = p + 0x80;
      desc[2] = BB_U32(p + 0x7c);
      sound = BB_POINTER(BB_U32(p + 0xa4) + 4);
      LA_CREATE_BUFFER(METHOD(sound, 12), sound, desc,
                       (void **)(uintptr_t)(p + 0x40), NULL);
    } else {
      sound = BB_POINTER(BB_U32(p + 0xa4) + 4);
      LA_DUPLICATE(METHOD(sound, 20), sound, BB_POINTER(p + 0x40),
                   (void **)(uintptr_t)(p + 0x40 + i * 4));
    }
  }
}
void BB_WAV_M bb_wav_fill_entry(void *object) {
  uint32_t p = bb_address_encode(object);
#ifdef BB_MODEL_RUNTIME
  uint32_t buffer = BB_U32(p + 0x40);
  if (!buffer)
    return;
  struct bb_model_scratch scratch;
  bb_model_scratch_begin(&scratch, 16);
  uint32_t at = scratch.address;
  memset(BB_DATA(at), 0, 16);
  if (!BB_GAME_METHOD(buffer, 0x2c, 0, BB_U32(p + 0x7c), at, at + 4, at + 8,
                      at + 12, 0)) {
    uint32_t first = BB_U32(at), first_bytes = BB_U32(at + 4),
             second = BB_U32(at + 8), second_bytes = BB_U32(at + 12);
    bb_crt_memory_copy_entry((void *)(uintptr_t)first, BB_POINTER(p + 0x78),
                             first_bytes);
    if (second_bytes)
      bb_crt_memory_copy_entry(
          (void *)(uintptr_t)second,
          (void *)(uintptr_t)(BB_U32(p + 0x78) + first_bytes), second_bytes);
    BB_GAME_METHOD(BB_U32(p + 0x40), 0x4c, first, first_bytes, second,
                   second_bytes);
  }
  bb_model_scratch_end(&scratch);
#else
  void *buffer = BB_POINTER(p + 0x40), *first, *second;
  uint32_t first_bytes, second_bytes;
  if (!buffer || LA_LOCK(METHOD(buffer, 0x2c), buffer, 0, BB_U32(p + 0x7c),
                         &first, &first_bytes, &second, &second_bytes, 0))
    return;
  typedef void *(*copy_fn)(void *, const void *,
                           uint32_t)__attribute__((unused));
  bb_crt_memory_copy_entry(first, BB_POINTER(p + 0x78), first_bytes);
  if (second_bytes)
    bb_crt_memory_copy_entry(second, PTR(BB_U32(p + 0x78) + first_bytes),
                             second_bytes);
  buffer = BB_POINTER(p + 0x40);
  LA_UNLOCK(METHOD(buffer, 0x4c), buffer, first, first_bytes, second,
            second_bytes);
#endif
}
uint32_t BB_WAV_M bb_wav_load_entry(void *object, const char *name) {
  uint32_t p = bb_address_encode(object);
  char filename[256], riff[5], wave[5], tag[5], fmt[5];
  riff[4] = wave[4] = tag[4] = fmt[4] = 0;
  copy(filename, name);
  bb_crt_string_append_entry(filename, (const char *)0x48c488);
  void *file = LA_OPEN(0x46bdd0, filename, (const char *)0x48c490);
  if (!file)
    return 0;
  uint32_t total, length, fmt_size;
  read(riff, 1, 4, file);
  read(&total, 4, 1, file);
  read(wave, 1, 4, file);
  typedef int (*compare_fn)(const char *, const char *, uint32_t)
      __attribute__((unused));
  if (!bb_crt_string_compare_count_entry(riff, (const char *)0x48c494, 4) &&
      !bb_crt_string_compare_count_entry(wave, (const char *)0x48c49c, 4)) {
    read(fmt, 1, 4, file);
    read(&fmt_size, 4, 1, file);
    read(PTR(p + 0x80), fmt_size, 1, file);
    while (!BB_U32(p + 0x78) && !(BYTE((uintptr_t)file + 12) & 16)) {
      read(tag, 1, 4, file);
      read(&length, 4, 1, file);
      if (!bb_crt_string_compare_count_entry(tag, (const char *)0x48c4a4, 4)) {
        unsigned mono = WORD(p + 0x82) == 1, eight = WORD(p + 0x8e) == 8;
        unsigned width = mono && eight ? 1 : (!mono && !eight ? 4 : 2);
        unsigned slot = mono ? (eight ? 0x60 : 0x68) : (eight ? 0x64 : 0x6c);
        BYTE(p + 0x74) = width;
        BB_U32(p + 0x70) = width == 1 ? length : length >> (width == 2 ? 1 : 2);
        BB_SET_POINTER(p + slot, allocate(BB_U32(p + 0x70) * width));
        read(BB_POINTER(p + slot), width, BB_U32(p + 0x70), file);
        BB_U32(p + 0x78) = BB_U32(p + slot);
        BB_U32(p + 0x7c) = length;
      } else
        LA_SEEK(0x46cba0, file, length, 1);
    }
  }
  LA_FILE_ONE(0x46bbd0, file);
  bb_wav_buffers_entry(object);
  bb_wav_fill_entry(object);
  BB_U32(p + 0x9c) = BB_U32(p + 0x84);
  return 1;
}
void BB_WAV_M bb_wav_trigger_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t status;
  for (uint32_t i = 0; i < BYTE(p + 0x92); ++i) {
    void *buffer = BB_POINTER(p + 0x40 + i * 4);
    LA_STATUS(METHOD(buffer, 0x24), buffer, &status);
    if (status != 1) {
      buffer = BB_POINTER(p + 0x40 + i * 4);
      LA_BUFFER_WORD(METHOD(buffer, 0x34), buffer, 0);
      buffer = BB_POINTER(p + 0x40 + i * 4);
      LA_PLAY(METHOD(buffer, 0x30), buffer, 0, 0, 0);
      return;
    }
  }
}
void BB_WAV_M bb_wav_root_note_entry(void *object, uint8_t note) {
  uint32_t p = bb_address_encode(object);
  BYTE(p + 0x98) = note;
  BB_U32(p + 0x94) = 0;
}
void BB_WAV_M bb_wav_note_range_entry(void *object, uint8_t low, uint8_t high) {
  uint32_t p = bb_address_encode(object);
  BYTE(p + 0xa0) = low;
  BYTE(p + 0xa1) = high;
  BB_U32(p + 0x94) = 0;
}
int32_t BB_WAV_M bb_wav_voice_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t best = 0, selected = 0, status, play, write;
  for (uint32_t i = 0; i < BYTE(p + 0x92); ++i) {
    void *buffer = BB_POINTER(p + 0x40 + i * 4);
    if (!buffer)
      continue;
    LA_STATUS(METHOD(buffer, 0x24), buffer, &status);
    if (status != 1)
      return i;
    buffer = BB_POINTER(p + 0x40 + i * 4);
    LA_POSITION(METHOD(buffer, 0x10), buffer, &play, &write);
    if (play > best) {
      best = play;
      selected = i;
    }
  }
  return selected; /* Includes no voices, null slots and equal cursor ties. */
}
void BB_WAV_M bb_wav_note_entry(void *object, uint8_t note, uint8_t velocity) {
  if (!velocity)
    return;
  uint32_t p = bb_address_encode(object);
  volatile long double attenuation =
      (long double)(100 - (int32_t)velocity) * BB_DOUBLE(0x4823c0);
  attenuation = attenuation / BB_DOUBLE(0x4823c8);
  int32_t gain = (uint32_t)(int64_t)attenuation;
  if (gain > 10000)
    gain = 10000;
  if (gain < 0)
    gain = 0;
  if (note < BYTE(p + 0xa0) || note > BYTE(p + 0xa1))
    return;
  uint32_t frequency;
  if (!BB_U32(p + 0x94)) {
    int32_t delta = (int32_t)note - BYTE(p + 0x98);
    LA_FORMAT(0x46bae0, (char *)(uintptr_t)0x6c1b88, (const char *)0x48c4ac,
              delta);
    debug((const char *)0x6c1b88);
    volatile double exponent = (long double)delta / BB_DOUBLE(0x4823d0);
    volatile long double power = LA_POWER(0x46ccd0, 2.0, exponent);
    power = power * (long double)BB_U32(p + 0x9c);
    frequency = (uint32_t)(int64_t)power;
  } else
    frequency = BB_U32(p + 0x9c);
  int32_t voice = bb_wav_voice_entry(object);
  if (voice >= 0) {
    uintptr_t slot = p + 0x40 + (uint32_t)voice * 4;
    void *buffer = BB_POINTER(slot);
    LA_BUFFER_ZERO(METHOD(buffer, 0x48), buffer);
    buffer = BB_POINTER(slot);
    LA_BUFFER_WORD(METHOD(buffer, 0x44), buffer, frequency);
    buffer = BB_POINTER(slot);
    LA_BUFFER_WORD(METHOD(buffer, 0x34), buffer, 0);
    buffer = BB_POINTER(slot);
    LA_BUFFER_WORD(METHOD(buffer, 0x3c), buffer, 0u - (uint32_t)gain);
    buffer = BB_POINTER(slot);
    LA_PLAY(METHOD(buffer, 0x30), buffer, 0, 0, 0);
  }
}
