/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_handle.h"
#include "native.h"
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "../bridge/callback_runtime.h"
#include "../bridge/heap.h"
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Mmio {
  FILE *file;
  Handle file_handle;
  const unsigned char *memory;
  size_t length, position;
#ifdef BB_MODEL_RUNTIME
  U32 reference, file_reference;
#endif
};
struct WaveHeader {
#ifdef BB_MODEL_RUNTIME
  U32 data;
#else
  void *data;
#endif
  U32 length, recorded, user, flags, loops;
#ifdef BB_MODEL_RUNTIME
  U32 next;
#else
  struct WaveHeader *next;
#endif
  U32 reserved;
};
_Static_assert(sizeof(struct WaveHeader) == 32,
               "Windows WAVEHDR stays 32 bytes");
struct Wave {
  SDL_AudioDeviceID device;
  U32 callback, user, flags;
  struct WaveHeader *headers[256];
  U32 ends[256];
  unsigned first, count;
  U32 written;
#ifdef BB_MODEL_RUNTIME
  U32 reference;
  struct bb_callback_context context;
#endif
};
static struct Wave *waves[16];
static unsigned wave_count;
static pthread_mutex_t wave_mutex;
static pthread_once_t wave_once = PTHREAD_ONCE_INIT;
static void wave_initialize(void) {
  pthread_mutexattr_t attributes;
  if (pthread_mutexattr_init(&attributes) ||
      pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE) ||
      pthread_mutex_init(&wave_mutex, &attributes))
    bb_die("cannot initialize wave mutex");
  pthread_mutexattr_destroy(&attributes);
}
static SDL_TimerID completion_timer;
/* Window/no-callback wave devices share the completion worker. Function
 * callbacks retain their existing game-timer path and recursive call behavior.
 */
static int lock_wave(struct Wave *wave) {
  int locked = (wave->flags & 0x70000) != 0x30000;
  if (locked)
    pthread_mutex_lock(&wave_mutex);
  return locked;
}
static void unlock_wave(int locked) {
  if (locked)
    pthread_mutex_unlock(&wave_mutex);
}
static U32 wave_reference(struct Wave *wave) {
#ifdef BB_MODEL_RUNTIME
  return wave->reference;
#else
  return (U32)(uintptr_t)wave;
#endif
}
static void wave_dispose(struct Wave *wave) {
#ifdef BB_MODEL_RUNTIME
  bb_callback_destroy(&wave->context);
  if (wave->reference && !bb_heap_release(wave->reference))
    bb_die("wave handle lost its owner");
#endif
  free(wave);
}
static void wave_notify(struct Wave *wave, U32 message, U32 parameter) {
  if ((wave->flags & 0x70000) == 0x30000 && wave->callback) {
#ifdef BB_MODEL_RUNTIME
    U32 arguments[] = {wave->reference, message, wave->user, parameter, 0};
    bb_callback_invoke(&wave->context, wave->callback, arguments, 5);
#else
    void(W * callback)(Handle, U32, U32, U32, U32) =
        (void *)bb_native_entry(wave->callback);
    callback(wave, message, wave->user, parameter, 0);
#endif
  } else if ((wave->flags & 0x70000) == 0x10000 && wave->callback) {
    int(W * post)(Handle, U32, U32, I32) = bb_window_api("PostMessageA");
    post((void *)(uintptr_t)wave->callback, message, wave_reference(wave),
         (I32)parameter);
  }
}
static void pump_wave(struct Wave *wave) {
  U32 played = wave->written - SDL_GetQueuedAudioSize(wave->device);
  while (wave->count && (I32)(played - wave->ends[wave->first]) >= 0) {
    struct WaveHeader *header = wave->headers[wave->first];
    wave->first = (wave->first + 1) % 256;
    wave->count--;
    header->flags = (header->flags & ~0x10U) | 1;
#ifdef BB_MODEL_RUNTIME
    wave_notify(wave, 0x3bd, bb_address_encode(header));
#else
    wave_notify(wave, 0x3bd, (U32)(uintptr_t)header);
#endif
  }
}
static void timer_wave_pump(void) {
  for (unsigned n = 0; n < 16; n++) {
    pthread_mutex_lock(&wave_mutex);
    struct Wave *wave = waves[n];
    int function_callback = wave && (wave->flags & 0x70000) == 0x30000;
    pthread_mutex_unlock(&wave_mutex);
    if (function_callback)
      pump_wave(wave);
  }
}
static U32 wave_completion(U32 interval, void *opaque) {
  pthread_mutex_lock(&wave_mutex);
  for (unsigned n = 0; n < wave_count; n++)
    if (waves[n] && (waves[n]->flags & 0x70000) != 0x30000)
      pump_wave(waves[n]);
  pthread_mutex_unlock(&wave_mutex);
  return interval;
}
static int audio_init(void) {
  return !SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
}
static U32 W waveOutGetNumDevs(void) { return audio_init() ? 1 : 0; }
static U32 W waveOutGetDevCapsA(U32 device, void *output, U32 size) {
  if (size < 52)
    return 11;
  memset(output, 0, size);
  unsigned char *p = output;
  U16 version = 1;
  memcpy(p, &version, 2);
  memcpy(p + 2, &version, 2);
  U32 driver = 0x100;
  memcpy(p + 4, &driver, 4);
  snprintf((char *)p + 8, 32, "Linux SDL PCM audio");
  U32 formats = 0xfff;
  memcpy(p + 40, &formats, 4);
  U16 channels = 2;
  memcpy(p + 44, &channels, 2);
  return 0;
}
#ifdef BB_MODEL_RUNTIME
typedef U32 WaveOutput;
#else
typedef struct Wave **WaveOutput;
#endif
static U32 W waveOutOpen(WaveOutput output, U32 device,
                         const unsigned char *format, U32 callback, U32 user,
                         U32 flags) {
  U16 tag, channels, bits;
  U32 rate;
  memcpy(&tag, format, 2);
  memcpy(&channels, format + 2, 2);
  memcpy(&rate, format + 4, 4);
  memcpy(&bits, format + 14, 2);
  if (tag != 1 || (bits != 8 && bits != 16) || (channels != 1 && channels != 2))
    return 32;
  if (flags & 1)
    return 0;
  if (!audio_init())
    return 2;
  pthread_once(&wave_once, wave_initialize);
  bb_timer_set_wave_pump(timer_wave_pump);
  pthread_mutex_lock(&wave_mutex);
  unsigned slot = 0;
  while (slot < 16 && waves[slot])
    slot++;
  if (slot == 16) {
    pthread_mutex_unlock(&wave_mutex);
    return 4;
  }
  struct Wave *wave = calloc(1, sizeof(*wave));
  if (!wave) {
    pthread_mutex_unlock(&wave_mutex);
    return 7;
  }
#ifdef BB_MODEL_RUNTIME
  /* Allocate before publication; callbacks observe exactly the same handle
   * already stored in the caller's four-byte output slot. */
  wave->reference = bb_heap_zeroed(1, 16);
  if (!wave->reference || ((flags & 0x70000) == 0x30000 && callback &&
                           !bb_callback_initialize(&wave->context))) {
    wave_dispose(wave);
    pthread_mutex_unlock(&wave_mutex);
    return 7;
  }
  BB_U32(wave->reference) = 0x57415645u;
  BB_U64(wave->reference + 8u) = (uintptr_t)wave;
#endif
  SDL_AudioSpec wanted = {0}, obtained;
  wanted.freq = (int)rate;
  wanted.channels = (unsigned char)channels;
  wanted.format = bits == 8 ? AUDIO_U8 : AUDIO_S16LSB;
  wanted.samples = 512;
  wave->device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
  if (!wave->device) {
    wave_dispose(wave);
    pthread_mutex_unlock(&wave_mutex);
    return 2;
  }
  wave->callback = callback;
  wave->user = user;
  wave->flags = flags;
  if (!completion_timer) {
    completion_timer = SDL_AddTimer(1, wave_completion, NULL);
    if (!completion_timer) {
      SDL_CloseAudioDevice(wave->device);
      wave_dispose(wave);
      pthread_mutex_unlock(&wave_mutex);
      return 7;
    }
  }
  waves[slot] = wave;
  if (slot >= wave_count)
    wave_count = slot + 1;
#ifdef BB_MODEL_RUNTIME
  BB_U32(output) = wave->reference;
#else
  *output = wave;
#endif
  SDL_PauseAudioDevice(wave->device, 0);
  wave_notify(wave, 0x3bb, 0);
  pthread_mutex_unlock(&wave_mutex);
  return 0;
}
static U32 W waveOutPrepareHeader(struct Wave *wave, struct WaveHeader *header,
                                  U32 size) {
  int locked = lock_wave(wave);
  header->flags |= 2;
  unlock_wave(locked);
  return 0;
}
static U32 W waveOutUnprepareHeader(struct Wave *wave,
                                    struct WaveHeader *header, U32 size) {
  int locked = lock_wave(wave);
  pump_wave(wave);
  if (header->flags & 0x10) {
    unlock_wave(locked);
    return 33;
  }
  header->flags &= ~2U;
  unlock_wave(locked);
  return 0;
}
static U32 W waveOutWrite(struct Wave *wave, struct WaveHeader *header,
                          U32 size) {
  int locked = lock_wave(wave);
  pump_wave(wave);
  if (wave->count == 256) {
    unlock_wave(locked);
    return 4;
  }
  if (!(header->flags & 2)) {
    unlock_wave(locked);
    return 34;
  }
#ifdef BB_MODEL_RUNTIME
  const void *samples =
      header->data ? BB_SPAN(header->data, header->length) : NULL;
#else
  const void *samples = header->data;
#endif
  if (SDL_QueueAudio(wave->device, samples, header->length)) {
    unlock_wave(locked);
    return 1;
  }
  wave->written += header->length;
  unsigned slot = (wave->first + wave->count++) % 256;
  wave->headers[slot] = header;
  wave->ends[slot] = wave->written;
  header->flags = (header->flags & ~1U) | 0x10;
  unlock_wave(locked);
  return 0;
}
static U32 W waveOutPause(struct Wave *wave) {
  int locked = lock_wave(wave);
  SDL_PauseAudioDevice(wave->device, 1);
  unlock_wave(locked);
  return 0;
}
static U32 W waveOutRestart(struct Wave *wave) {
  int locked = lock_wave(wave);
  SDL_PauseAudioDevice(wave->device, 0);
  unlock_wave(locked);
  return 0;
}
static U32 W waveOutReset(struct Wave *wave) {
  int locked = lock_wave(wave);
  SDL_ClearQueuedAudio(wave->device);
  pump_wave(wave);
  unlock_wave(locked);
  return 0;
}
static U32 W waveOutClose(struct Wave *wave) {
  pthread_mutex_lock(&wave_mutex);
  waveOutReset(wave);
  SDL_CloseAudioDevice(wave->device);
  wave_notify(wave, 0x3bc, 0);
  for (unsigned n = 0; n < wave_count; n++)
    if (waves[n] == wave)
      waves[n] = NULL;
  wave_dispose(wave);
  pthread_mutex_unlock(&wave_mutex);
  return 0;
}
static void mmio_dispose(struct Mmio *io) {
#ifdef BB_MODEL_RUNTIME
  bb_kernel_handle_unborrow(io->file_handle);
  if (io->reference && !bb_heap_release(io->reference))
    bb_die("MMIO handle lost its owner");
#endif
  free(io);
}
static struct Mmio *W mmioOpenA(const char *name, U32 *info, U32 flags) {
  struct Mmio *io = calloc(1, sizeof(*io));
  if (!io)
    return NULL;
  if (name) {
    char *path = bb_path(name);
    io->file = fopen(path, "rb");
    free(path);
    if (!io->file) {
      mmio_dispose(io);
      return NULL;
    }
  } else if (info && info[1] == 0x20534f44) {
#ifdef BB_MODEL_RUNTIME
    io->file_reference = info[12];
    io->file_handle = bb_kernel_handle_acquire(io->file_reference);
#else
    io->file_handle = (void *)(uintptr_t)info[12];
#endif
    int descriptor = dup(bb_file_descriptor(io->file_handle));
    if (descriptor < 0) {
      mmio_dispose(io);
      return NULL;
    }
    io->file = fdopen(descriptor, "rb");
    if (!io->file) {
      close(descriptor);
      mmio_dispose(io);
      return NULL;
    }
  } else if (info) {
#ifdef BB_MODEL_RUNTIME
    io->memory = info[6] ? BB_SPAN(info[6], info[5]) : NULL;
#else
    io->memory = (void *)(uintptr_t)info[6];
#endif
    io->length = info[5];
    if (!io->memory) {
      mmio_dispose(io);
      return NULL;
    }
  } else {
    mmio_dispose(io);
    return NULL;
  }
  if (io->file) {
    long position = ftell(io->file);
    if (position < 0 || fseek(io->file, 0, SEEK_END)) {
      fclose(io->file);
      mmio_dispose(io);
      return NULL;
    }
    long length = ftell(io->file);
    if (length < 0 || fseek(io->file, position, SEEK_SET)) {
      fclose(io->file);
      mmio_dispose(io);
      return NULL;
    }
    io->length = (size_t)length;
    io->position = (size_t)position;
  }
  return io;
}
static I32 W mmioRead(struct Mmio *io, void *output, I32 size) {
  if (size < 0)
    return -1;
  size_t amount = (size_t)size;
  if (amount > io->length - io->position)
    amount = io->length - io->position;
  if (!amount)
    return 0;
  if (io->file)
    amount = fread(output, 1, amount, io->file);
  else
    memcpy(output, io->memory + io->position, amount);
  io->position += amount;
  return (I32)amount;
}
static I32 mmio_seek(struct Mmio *io, size_t position) {
  if (position > io->length)
    return -1;
  io->position = position;
  if (io->file)
    fseek(io->file, (long)position, SEEK_SET);
  return (I32)position;
}
static U32 W mmioClose(struct Mmio *io, U32 flags) {
  if (io->file)
    fclose(io->file);
  if (io->file_handle && !(flags & 0x10)) {
#ifdef BB_MODEL_RUNTIME
    bb_kernel_handle_close(io->file_reference);
#else
    int(W * close_handle)(Handle) = bb_kernel_api("CloseHandle");
    close_handle(io->file_handle);
#endif
  }
  mmio_dispose(io);
  return 0;
}
static U32 W mmioSetBuffer(struct Mmio *io, void *buffer, I32 size, U32 flags) {
  return 0;
}
static U32 W mmioAscend(struct Mmio *io, U32 *chunk, U32 flags) {
  return mmio_seek(io, (U32)(chunk[3] + chunk[1] + (chunk[1] & 1))) < 0 ? 272
                                                                        : 0;
}
static U32 W mmioDescend(struct Mmio *io, U32 *chunk, const U32 *parent,
                         U32 flags) {
  U32 desired = chunk[0], desired_type = chunk[2];
  size_t end = parent ? (U32)(parent[3] + parent[1]) : io->length;
  while (io->position + 8 <= end) {
    U32 header[3];
    if (mmioRead(io, header, 8) != 8)
      return 272;
    size_t position = io->position;
    int riff = header[0] == 0x46464952 || header[0] == 0x5453494c;
    header[2] = 0;
    if (riff && mmioRead(io, header + 2, 4) != 4)
      return 272;
    if (!flags || ((flags & 0x10) && header[0] == desired) ||
        ((flags & 0x60) && riff && header[2] == desired_type)) {
      chunk[0] = header[0];
      chunk[1] = header[1];
      chunk[2] = header[2];
      chunk[3] = (U32)position;
      chunk[4] = 0;
      return 0;
    }
    if (mmio_seek(io, (U32)(position + header[1] + (header[1] & 1))) < 0)
      return 272;
  }
  return 272;
}
static I32 W DirectSoundCreate(const void *guid, Handle *output, Handle outer) {
  return bb_sound_create(guid, output, outer);
}
static U32 W midiOutOpen(Handle *output, U32 device, U32 callback, U32 user,
                         U32 flags) {
  return 2;
}
static U32 W midiOutClose(Handle device) { return 0; }
static U32 W midiOutShortMsg(Handle device, U32 message) { return 0; }
static U32 W midiOutSetVolume(Handle device, U32 volume) { return 0; }
static U32 W mciSendStringA(const char *command, char *result, U32 capacity,
                            Handle window) {
  return bb_cd_command(command, result, capacity, window);
}
void *bb_audio_api(const char *name) {
  void *timer = bb_timer_api(name);
  if (timer)
    return timer;
  static const struct ApiEntry table[] = {API_ENTRY(waveOutGetNumDevs),
                                          API_ENTRY(waveOutGetDevCapsA),
                                          API_ENTRY(waveOutOpen),
                                          API_ENTRY(waveOutPrepareHeader),
                                          API_ENTRY(waveOutUnprepareHeader),
                                          API_ENTRY(waveOutWrite),
                                          API_ENTRY(waveOutPause),
                                          API_ENTRY(waveOutRestart),
                                          API_ENTRY(waveOutReset),
                                          API_ENTRY(waveOutClose),
                                          API_ENTRY(mmioOpenA),
                                          API_ENTRY(mmioRead),
                                          API_ENTRY(mmioClose),
                                          API_ENTRY(mmioSetBuffer),
                                          API_ENTRY(mmioDescend),
                                          API_ENTRY(mmioAscend),
                                          API_ENTRY(DirectSoundCreate),
                                          API_ENTRY(midiOutOpen),
                                          API_ENTRY(midiOutClose),
                                          API_ENTRY(midiOutShortMsg),
                                          API_ENTRY(midiOutSetVolume),
                                          API_ENTRY(mciSendStringA)};
  API_RETURN(table);
}

#ifdef BB_MODEL_RUNTIME
#include "audio_models.inc"
#endif
