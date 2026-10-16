/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "native.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Windows DirectSound buffers are mixed into a single native SDL device. */
struct Sound {
  void **table;
  U32 references, kind;
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
#endif
};
struct BufferData {
  U32 references;
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
#endif
  unsigned char samples[];
};
struct Buffer {
  struct Sound sound;
  unsigned char *data;
  U32 size, caps, rate, frequency;
  U16 channels, bits;
  I32 volume, pan;
  double position;
  int playing, looping, locked;
  struct Buffer *next;
  struct BufferData *storage;
};
#ifdef BB_MODEL_RUNTIME
static void model_release_proxy(struct Sound *);
static void model_release_samples(struct BufferData *);
#endif
static void *sound_table[11], *buffer_table[21];
static SDL_AudioDeviceID device;
static SDL_AudioSpec output_format;
static struct Buffer *buffers;
static void mix(void *opaque, unsigned char *stream, int length) {
  int16_t *output = (int16_t *)stream;
  memset(stream, 0, (size_t)length);
  for (int frame = 0; frame < length / 4; frame++) {
    double sum[2] = {0, 0};
    for (struct Buffer *b = buffers; b; b = b->next) {
      if (!b->playing || !b->size || (b->caps & 1))
        continue;
      unsigned stride = b->channels * b->bits / 8;
      U32 position = (U32)b->position * stride;
      if (position >= b->size) {
        if (!b->looping) {
          b->playing = 0;
          b->position = 0;
          continue;
        }
        b->position = fmod(b->position, b->size / stride);
        position = (U32)b->position * stride;
      }
      double gain = pow(10, b->volume / 2000.0);
      for (unsigned channel = 0; channel < 2; channel++) {
        unsigned offset =
            position + (b->channels == 1 ? 0 : channel) * b->bits / 8;
        int value;
        if (b->bits == 8)
          value = ((int)b->data[offset] - 128) * 256;
        else {
          int16_t sample;
          memcpy(&sample, b->data + offset, 2);
          value = sample;
        }
        double pan_gain = channel == 0 && b->pan > 0 ? pow(10, -b->pan / 2000.0)
                          : channel == 1 && b->pan < 0
                              ? pow(10, b->pan / 2000.0)
                              : 1;
        sum[channel] += value * gain * pan_gain;
      }
      b->position += (double)b->frequency / output_format.freq;
    }
    for (unsigned channel = 0; channel < 2; channel++) {
      if (sum[channel] > 32767)
        sum[channel] = 32767;
      if (sum[channel] < -32768)
        sum[channel] = -32768;
      output[frame * 2 + channel] = (int16_t)sum[channel];
    }
  }
  bb_cd_mix(output, (unsigned)length / 4);
}
static U32 W addref(struct Sound *sound) { return ++sound->references; }
static U32 W release(struct Sound *sound) {
  if (--sound->references)
    return sound->references;
  if (sound->kind == 2) {
    SDL_LockAudioDevice(device);
    struct Buffer *b = (struct Buffer *)sound;
    struct Buffer **link = &buffers;
    while (*link && *link != b)
      link = &(*link)->next;
    if (*link)
      *link = b->next;
    if (!--b->storage->references) {
#ifdef BB_MODEL_RUNTIME
      model_release_samples(b->storage);
#endif
      free(b->storage);
    }
    SDL_UnlockAudioDevice(device);
  }
#ifdef BB_MODEL_RUNTIME
  model_release_proxy(sound);
#endif
  free(sound);
  return 0;
}
static I32 W query(struct Sound *sound, const U32 *guid, void **result) {
  if (guid[0] != 0 && guid[0] != 0x279afa83 && guid[0] != 0x279afa85) {
    *result = NULL;
    return (I32)0x80004002;
  }
  *result = sound;
  addref(sound);
  return 0;
}
static I32 W set_format(struct Buffer *b, const unsigned char *format) {
  U16 tag, channels, bits;
  U32 rate;
  memcpy(&tag, format, 2);
  memcpy(&channels, format + 2, 2);
  memcpy(&rate, format + 4, 4);
  memcpy(&bits, format + 14, 2);
  if (tag != 1 || (channels != 1 && channels != 2) ||
      (bits != 8 && bits != 16) || !rate)
    return (I32)0x80070057;
  SDL_LockAudioDevice(device);
  b->channels = channels;
  b->bits = bits;
  b->rate = b->frequency = rate;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W create_buffer(struct Sound *sound, const U32 *description,
                           struct Buffer **result, Handle outer) {
  struct Buffer *b = calloc(1, sizeof(*b));
  if (!b)
    return (I32)0x8007000e;
  b->sound = (struct Sound){.table = buffer_table, .references = 1, .kind = 2};
  b->caps = description[1];
  b->size = description[2] ? description[2] : 16384;
  size_t storage_size;
  if (__builtin_add_overflow(sizeof(*b->storage), (size_t)b->size,
                             &storage_size)) {
    free(b);
    return (I32)0x8007000e;
  }
  b->storage = calloc(1, storage_size);
  if (!b->storage) {
    free(b);
    return (I32)0x8007000e;
  }
  b->storage->references = 1;
  b->data = b->storage->samples;
  b->channels = 2;
  b->bits = 16;
  b->rate = b->frequency = 22050;
  if (description[4] && set_format(b, BB_DATA(description[4]))) {
    free(b->storage);
    free(b);
    return (I32)0x80070057;
  }
  if (b->bits == 8)
    memset(b->data, 128, b->size);
  SDL_LockAudioDevice(device);
  b->next = buffers;
  buffers = b;
  SDL_UnlockAudioDevice(device);
  *result = b;
  return 0;
}
/* DuplicateSoundBuffer shares sample memory while each voice keeps its own
 * controls and playback state. Storage survives until the last voice release.
 * https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mt708944(v=vs.85)
 */
static I32 W duplicate_buffer(struct Sound *sound, struct Buffer *source,
                              struct Buffer **result) {
  if (!source || !result || source->sound.kind != 2)
    return (I32)0x80070057;
  *result = NULL;
  if (source->caps & 1)
    return (I32)0x88780032;
  struct Buffer *b = calloc(1, sizeof(*b));
  if (!b)
    return (I32)0x8007000e;
  SDL_LockAudioDevice(device);
  *b = *source;
  b->sound.references = 1;
#ifdef BB_MODEL_RUNTIME
  b->sound.reference = 0;
#endif
  b->position = 0;
  b->playing = b->looping = b->locked = 0;
  ++b->storage->references;
  b->next = buffers;
  buffers = b;
  SDL_UnlockAudioDevice(device);
  *result = b;
  return 0;
}
static I32 W buffer_caps(struct Buffer *b, U32 *caps) {
  caps[0] = 20;
  caps[1] = b->caps;
  caps[2] = b->size;
  caps[3] = caps[4] = 0;
  return 0;
}
static I32 W position(struct Buffer *b, U32 *play, U32 *write) {
  SDL_LockAudioDevice(device);
  unsigned stride = b->channels * b->bits / 8;
  U32 cursor = (U32)b->position * stride % b->size;
  if (play)
    *play = cursor;
  if (write)
    *write = (cursor + b->frequency / 100 * stride) % b->size;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W get_format(struct Buffer *b, void *output, U32 capacity,
                        U32 *written) {
  unsigned char format[18] = {0};
  U16 tag = 1, stride = b->channels * b->bits / 8;
  U32 bytes = b->rate * stride;
  memcpy(format, &tag, 2);
  memcpy(format + 2, &b->channels, 2);
  memcpy(format + 4, &b->rate, 4);
  memcpy(format + 8, &bytes, 4);
  memcpy(format + 12, &stride, 2);
  memcpy(format + 14, &b->bits, 2);
  if (written)
    *written = 18;
  if (output)
    memcpy(output, format, capacity < 18 ? capacity : 18);
  return 0;
}
static I32 W get_volume(struct Buffer *b, I32 *value) {
  *value = b->volume;
  return 0;
}
static I32 W get_pan(struct Buffer *b, I32 *value) {
  *value = b->pan;
  return 0;
}
static I32 W get_frequency(struct Buffer *b, U32 *value) {
  *value = b->frequency;
  return 0;
}
static I32 W status(struct Buffer *b, U32 *value) {
  SDL_LockAudioDevice(device);
  *value = b->playing ? 1 | (b->looping ? 4 : 0) : 0;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 lock_range(struct Buffer *b, U32 *offset_value, U32 *bytes_value,
                      U32 flags) {
  U32 offset = *offset_value, bytes = *bytes_value;
  SDL_LockAudioDevice(device);
  if (flags & 1) {
    unsigned stride = b->channels * b->bits / 8;
    offset =
        ((U32)b->position * stride + b->frequency / 100 * stride) % b->size;
  }
  if (flags & 2) {
    offset = 0;
    bytes = b->size;
  }
  if (offset >= b->size || bytes > b->size) {
    SDL_UnlockAudioDevice(device);
    return (I32)0x80070057;
  }
  *offset_value = offset;
  *bytes_value = bytes;
  return 0;
}
static I32 W lock_buffer(struct Buffer *b, U32 offset, U32 bytes, void **first,
                         U32 *first_bytes, void **second, U32 *second_bytes,
                         U32 flags) {
  I32 error = lock_range(b, &offset, &bytes, flags);
  if (error)
    return error;
  *first = b->data + offset;
  *first_bytes = bytes < b->size - offset ? bytes : b->size - offset;
  if (second)
    *second = bytes > *first_bytes ? b->data : NULL;
  if (second_bytes)
    *second_bytes = bytes - *first_bytes;
  b->locked = 1;
  return 0;
}
static I32 W unlock_buffer(struct Buffer *b, void *first, U32 first_bytes,
                           void *second, U32 second_bytes) {
  if (!b->locked)
    return (I32)0x80070057;
  b->locked = 0;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W play_buffer(struct Buffer *b, U32 reserved1, U32 reserved2,
                         U32 flags) {
  SDL_LockAudioDevice(device);
  b->looping = !!(flags & 1);
  b->playing = 1;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W set_position(struct Buffer *b, U32 value) {
  if (value >= b->size)
    return (I32)0x80070057;
  SDL_LockAudioDevice(device);
  b->position = (double)value / (b->channels * b->bits / 8);
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W set_volume(struct Buffer *b, I32 value) {
  if (value < -10000 || value > 0)
    return (I32)0x80070057;
  SDL_LockAudioDevice(device);
  b->volume = value;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W set_pan(struct Buffer *b, I32 value) {
  if (value < -10000 || value > 10000)
    return (I32)0x80070057;
  SDL_LockAudioDevice(device);
  b->pan = value;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W set_frequency(struct Buffer *b, U32 value) {
  SDL_LockAudioDevice(device);
  b->frequency = value ? value : b->rate;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W stop_buffer(struct Buffer *b) {
  SDL_LockAudioDevice(device);
  b->playing = 0;
  SDL_UnlockAudioDevice(device);
  return 0;
}
static I32 W restore_buffer(struct Buffer *b) { return 0; }
static I32 W sound_caps(struct Sound *sound, U32 *caps) {
  U32 size = caps[0];
  memset(caps, 0, size);
  caps[0] = size;
  if (size >= 16) {
    caps[1] = 0x0000000f;
    caps[2] = 100;
    caps[3] = 200000;
  }
  return 0;
}
static I32 W cooperative(struct Sound *sound, Handle window, U32 level) {
  return 0;
}
static I32 W compact(struct Sound *sound) { return 0; }
static I32 W speaker_get(struct Sound *sound, U32 *configuration) {
  *configuration = 4;
  return 0;
}
static I32 W speaker_set(struct Sound *sound, U32 configuration) { return 0; }
static I32 W initialize(struct Sound *sound, const void *guid) {
  return (I32)0x88780082;
}
int bb_sound_ready(void) {
  if (device)
    return 1;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    return 0;
  SDL_AudioSpec requested = {0};
  requested.freq = 44100;
  requested.format = AUDIO_S16LSB;
  requested.channels = 2;
  requested.samples = 256;
  requested.callback = mix;
  device = SDL_OpenAudioDevice(NULL, 0, &requested, &output_format, 0);
  if (!device)
    return 0;
  SDL_PauseAudioDevice(device, 0);
  return 1;
}
I32 W bb_sound_create(const void *guid, Handle *result, Handle outer) {
  if (!bb_sound_ready())
    return (I32)0x88780078;
  if (!sound_table[0]) {
#ifndef BB_MODEL_RUNTIME
    for (unsigned n = 0; n < 11; n++)
      sound_table[n] = bb_api("IDirectSound", "unsupported_method");
    for (unsigned n = 0; n < 21; n++)
      buffer_table[n] = bb_api("IDirectSoundBuffer", "unsupported_method");
#endif
    void **tables[] = {sound_table, buffer_table};
    for (unsigned n = 0; n < 2; n++) {
      tables[n][0] = query;
      tables[n][1] = addref;
      tables[n][2] = release;
    }
    sound_table[3] = create_buffer;
    sound_table[4] = sound_caps;
    sound_table[5] = duplicate_buffer;
    sound_table[6] = cooperative;
    sound_table[7] = compact;
    sound_table[8] = speaker_get;
    sound_table[9] = speaker_set;
    sound_table[10] = initialize;
    buffer_table[3] = buffer_caps;
    buffer_table[4] = position;
    buffer_table[5] = get_format;
    buffer_table[6] = get_volume;
    buffer_table[7] = get_pan;
    buffer_table[8] = get_frequency;
    buffer_table[9] = status;
    buffer_table[11] = lock_buffer;
    buffer_table[12] = play_buffer;
    buffer_table[13] = set_position;
    buffer_table[14] = set_format;
    buffer_table[15] = set_volume;
    buffer_table[16] = set_pan;
    buffer_table[17] = set_frequency;
    buffer_table[18] = stop_buffer;
    buffer_table[19] = unlock_buffer;
    buffer_table[20] = restore_buffer;
  }
  struct Sound *sound = calloc(1, sizeof(*sound));
  if (!sound)
    return (I32)0x8007000e;
  *sound = (struct Sound){.table = sound_table, .references = 1, .kind = 1};
  *result = sound;
  return 0;
}

#ifdef BB_MODEL_RUNTIME
#include "sound_models.inc"
#endif
