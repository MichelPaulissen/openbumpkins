/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "bootstrap_resources.h"
#include "dialog.h"
#include "interop.h"
#include "game_calls.h"
#include "bootstrap_models.h"
#include "media_devices.h"
#include "session_controls.h"
#include "worker_runtime.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#define REF(p) bb_address_encode((const void *)(uintptr_t)(p))
void *bb_archive_close(void *);
void *bb_archive_member(void *, const char *);
uint32_t bb_archive_release(void *);
uint32_t bb_archive_seek(void *, uint32_t, uint32_t, uint32_t);
uint32_t bb_archive_read(void *, void *, uint32_t);

/* The original string and PCM copies run forward in DWORDs, then bytes.
 * Keep this ordering for overlapping storage rather than using memcpy. */
static void copy_bytes(uintptr_t out, uintptr_t in, uint32_t count) {
  while (count >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    count -= 4;
  }
  while (count--)
    BYTE(out++) = BYTE(in++);
}
static uint32_t text_length(uintptr_t p) {
  uint32_t n = 0;
  while (BYTE(p + n))
    ++n;
  return n;
}
static void copy_text(uintptr_t out, uintptr_t in) {
  copy_bytes(out, in, text_length(in) + 1);
}
static void *allocate(uint32_t size) {
  return PTR(BB_GAME_CALL(0x46c060u, 0, size));
}
static void remove_buffer(void *buffer) {
  BB_GAME_CALL(0x46c110u, 0, REF(buffer));
}
static void report(uintptr_t format, const char *name) {
  BB_GAME_CALL(0x46bae0u, 0, 0x6033d8u, (uint32_t)format, REF(name));
}
static void clear_packets(void) {
  BB_U32(0x48e740) = 0;
  BB_U32(0x48e754) = 0;
  BB_U32(0x48e744) = 0;
  BB_U32(0x48e748) = 0;
  BB_U32(0x48e74c) = 0;
  BB_U32(0x48e750) = 0;
}

/* 0x401000..0x4011f5: shared session modes, names and packet queues. */
void *BB_THISCALL bb_shared_construct_entry(void *object) {
  uintptr_t p = REF(object);
  BB_U32(p + 0x18) = 0;
  BB_U32(p + 0x1c) = 0;
  BB_U32(p + 0x10020) = 0;
  BB_U32(p + 0x10024) = 0;
  BB_U32(p + 0x20028) = 0;
  BB_U32(p + 0xc) = 0;
  BB_U32(p) = 0;
  return object;
}
void BB_THISCALL bb_shared_initialize_entry(void *object,
                                                          uint32_t window) {
  uintptr_t p = REF(object);
  BB_U32(p + 4) = window;
  clear_packets();
  BB_U32(p) = 0;
}
uint32_t BB_THISCALL bb_shared_create_entry(void *object,
                                                          uint32_t kind,
                                                          const char *a,
                                                          const char *b) {
  (void)object;
  (void)kind;
  (void)a;
  (void)b;
  return 1;
}
void BB_THISCALL bb_shared_stop_entry(void *object) {
  uint32_t mode = BB_U32(REF(object));
  if (mode == 3 || mode == 4)
    BB_U32(REF(object)) = 2;
}
uint32_t BB_THISCALL bb_shared_start_entry(void *object,
                                                         uint32_t unused) {
  (void)unused;
  uint32_t mode = BB_U32(REF(object));
  if (mode != 0 && mode != 1)
    return 0;
  bb_shared_create_entry(object, 0, PTR(0x484104), PTR(0x484104));
  bb_shared_stop_entry(object);
  return 1;
}
void BB_THISCALL bb_shared_reset_entry(void *object) {
  if (BB_U32(REF(object)) == 2)
    BB_U32(REF(object)) = 1;
  BB_U32(0x48e754) = 0;
  BB_U32(0x48e740) = 0;
  BB_U32(0x48e744) = 0;
  BB_U32(0x48e748) = 0;
  BB_U32(0x48e74c) = 0;
  BB_U32(0x48e750) = 0;
}
void BB_THISCALL
bb_shared_configure_entry(void *object, const char *a, const char *b) {
  uintptr_t p = REF(object);
  BB_U32(p) = 0;
  BB_U32(p + 0x20028) = 1;
  copy_text(p + 0x2002c, REF(a));
  copy_text(p + 0x2012c, REF(b));
  BB_U32(p + 0x2022e) = 0;
  BB_U32(p + 0x20232) = 0;
  BB_U32(p + 0xc) = 0;
}
void BB_THISCALL bb_shared_clear_entry(void *object) {
  uintptr_t p = REF(object);
  uint32_t mode = BB_U32(p);
  if (mode == 3)
    BB_U32(p + 0x20028) = 0;
  else if (mode == 2)
    BB_U32(p + 0x10020) = 0;
}
void BB_THISCALL bb_shared_net_start_entry(void *object) {
  if (BB_U32(REF(object)) == 3) {
    bb_shared_clear_entry(object);
    BB_U32(REF(object)) = 4;
    BB_GAME_CALL(0x41de40u, 0x662c98u, 0x484108u);
  }
}

/* 0x401db0..0x402207: audio-manager and resource ownership. Failed resource
 * loads remain linked, and empty/failed compressed allocations still succeed
 * just as in the reference. These are visible original behaviours. */
void *BB_THISCALL bb_audio_manager_construct_entry(void *object) {
  BB_U32(REF(object) + 0xc) = 0;
  BB_U32(REF(object) + 0x10) = 0;
  return object;
}
uint32_t BB_THISCALL bb_audio_manager_walk_entry(void *object) {
  uintptr_t p = BB_U32(REF(object) + 0xc);
  while (p)
    p = BB_U32(p);
  return 0;
}
void BB_THISCALL
bb_audio_manager_initialize_entry(void *object, uint32_t window) {
  BB_GAME_CALL(0x463560u, 0, window);
  BB_U32(REF(object)) = BB_U32(0x48bc70);
  BB_U32(REF(object) + 4) = BB_U32(0x48bc74);
  if (BB_GAME_CALL(0x459930u, 0))
    BB_GAME_CALL(BB_U32(0x6ca374u), 0, 0x4841fcu);
}
void BB_THISCALL bb_audio_manager_shutdown_entry(void *object) {
  BB_GAME_CALL(0x4599f0u, 0);
  if (BB_U32(REF(object) + 0x10)) {
    BB_GAME_CALL(0x46a970u, 0);
    BB_U32(REF(object) + 0x10) = 0;
  }
  BB_GAME_CALL(0x46376fu, 0);
}
void *BB_THISCALL bb_audio_asset_construct_entry(void *object,
                                                               void *manager) {
  uintptr_t p = REF(object);
  BB_U32(p + 4) = REF(manager);
  BB_U32(p + 0x10) = 0;
  BB_U32(p + 0x14) = 0;
  BB_U32(p + 0x18) = 0;
  BB_U32(p + 0x1c) = 0;
  BYTE(p + 0x34) = 0;
  BB_U32(p + 0x30) = 0;
  BB_U32(p + 0x2c) = 0;
  return object;
}
void *BB_THISCALL
bb_audio_resource_create_entry(void *object, const char *name, uint32_t flags) {
  void *resource = allocate(0x86);
  if (!resource)
    return NULL;
  uintptr_t p = REF(resource), manager = REF(object);
  bb_audio_asset_construct_entry(resource, object);
  BB_U32(p) = BB_U32(manager + 0xc);
  BB_U32(manager + 0xc) = p;
  if (!bb_audio_asset_load_entry(resource, name, flags))
    return NULL;
  BB_GAME_CALL(0x406a60u, 0);
  return resource;
}
void *bb_audio_resource_find_c(void *object, const char *name, uint32_t scratch) {
  uintptr_t node = BB_U32(REF(object) + 0xc), match = 0;
  while (node) {
    copy_text(scratch, node + 0x3e);
    uint32_t dot = BB_GAME_CALL(0x46c260u, 0, scratch, '.');
    if (dot)
      BYTE(dot) = 0;
    if (!BB_GAME_CALL(0x46c1c0u, 0, scratch, REF(name)))
      match = node;
    node = BB_U32(node);
  }
  return PTR(match);
}
uint32_t bb_audio_asset_load_c(void *object, const char *name, uint32_t flags, uint32_t scratch) {
  uintptr_t p = REF(object);
  if (!BB_U32(p + 4))
    return 0;
  uint32_t text = scratch;
  BB_U32(p + 0x36) = flags;
  copy_text((uintptr_t)text, REF(name));
  copy_text((uintptr_t)text + text_length((uintptr_t)text),
            flags & 1 ? 0x48424c : 0x484228);
  copy_text(p + 0x3e, (uintptr_t)text);
  if (flags & 1) {
    void *member = bb_archive_member(PTR(0x5f7360), PTR(text));
    if (!member) {
      report(0x484230, PTR(text));
      bb_fatal_teardown_entry(3);
      return 0;
    }
    uint32_t size = bb_archive_seek(member, 0, 2, 0);
    BB_U32(p + 0x24) = size;
    if (size) {
      bb_archive_seek(member, 0, 0, 0);
      void *data = allocate(BB_U32(p + 0x24));
      BB_U32(p + 0x20) = REF(data);
      if (data)
        bb_archive_read(member, data, BB_U32(p + 0x24));
    }
    bb_archive_release(member);
    return 1;
  }
  void *buffer =
      bb_audio_pcm_create_entry(object, BB_POINTER(BB_U32(p + 4)), PTR(text));
  BB_U32(p + 0x10) = REF(buffer);
  if (!buffer)
    return 0;
  for (unsigned i = 0; i < 3; ++i) {
    void *device = BB_POINTER(BB_U32(p + 4));
    BB_GAME_METHOD(REF(device), 0x14, BB_U32(p + 0x10), p + 0x14 + i * 4);
  }
  return 1;
}

/* 0x402410..0x4027af: WAV archive views, ordered RIFF chunk discovery and
 * DirectSound buffer creation/fill. Duration uses the original wrapped DWORD
 * arithmetic and sample rate/bits; its denominator omits channel count. */
uint32_t bb_audio_wave_find_entry(const void *input, void *format,
                                  void *samples, void *size) {
  uintptr_t f = (uintptr_t)format, s = (uintptr_t)samples, n = (uintptr_t)size;
  if (f)
    BB_U32(f) = 0;
  if (s)
    BB_U32(s) = 0;
  if (n)
    BB_U32(n) = 0;
  uint32_t p = REF(input);
  uint32_t length = BB_U32(p + 4), kind = BB_U32(p + 8);
  if (BB_U32(p) != 0x46464952 || kind != 0x45564157)
    return 0;
  p += 12;
  uint32_t end = p + length - 4;
  while (p < end) {
    uint32_t tag = BB_U32(p), count = BB_U32(p + 4);
    p += 8;
    if (tag == 0x20746d66 && f && !BB_U32(f)) {
      if (count < 14)
        return 0;
      BB_U32(f) = p;
      if ((!s || BB_U32(s)) && (!n || BB_U32(n)))
        return 1;
    } else if (tag == 0x61746164 && ((s && !BB_U32(s)) || (n && !BB_U32(n)))) {
      if (s)
        BB_U32(s) = p;
      if (n)
        BB_U32(n) = count;
      if (!f || BB_U32(f))
        return 1;
    }
    p += (count + 1) & ~1u;
  }
  return 0;
}
uint32_t bb_audio_wav_load_entry(uint32_t unused, const char *name,
                                 void *format, void *samples, void *size) {
  (void)unused;
  void *member = bb_archive_member(PTR(0x5f7360), name);
  if (!member) {
    report(0x484230, name);
    bb_fatal_teardown_entry(3);
    return 0;
  }
  uint32_t bytes = bb_archive_seek(member, 0, 2, 0);
  if (bytes) {
    bb_archive_seek(member, 0, 0, 0);
    void *data = allocate(bytes);
    BB_U32(0x4841f8) = REF(data);
    if (data) {
      bb_archive_read(member, data, bytes);
      bb_archive_release(member);
      if (bb_audio_wave_find_entry(BB_POINTER(0x4841f8), format, samples, size))
        return 1;
    } else {
      bb_archive_release(member);
      bb_fatal_teardown_entry(2);
    }
  }
  report(0x484254, name);
  bb_fatal_teardown_entry(3);
  return 0;
}
uint32_t bb_audio_buffer_fill_c(void *buffer, const void *input,
                                uint32_t bytes, uint32_t scratch) {
  if (!buffer || !input || !bytes)
    return 0;
  uint32_t object = REF(buffer), source = REF(input);
  uint32_t lock = BB_U32(BB_U32(object) + 0x2cu);
  if ((int32_t)BB_GAME_CALL(lock, 0, object, 0, bytes, scratch, scratch + 4,
                            scratch + 8, scratch + 12, 0) < 0)
    return 0;
  copy_bytes(BB_U32(scratch), source, BB_U32(scratch + 4));
  if (BB_U32(scratch + 12))
    copy_bytes(BB_U32(scratch + 8), source + BB_U32(scratch + 4),
               BB_U32(scratch + 12));
  BB_GAME_METHOD(object, 0x4c, BB_U32(scratch), BB_U32(scratch + 4),
                 BB_U32(scratch + 8), BB_U32(scratch + 12));
  return 1;
}

void *bb_audio_pcm_create_c(void *object, void *device, const char *name,
                             uint32_t scratch) {
  uint32_t descriptor = scratch, buffer = scratch + 20, samples = scratch + 24;
  memset(BB_SPAN(scratch, 28), 0, 28);
  if (bb_audio_wav_load_entry(0, name, PTR(descriptor + 16), PTR(samples),
                              PTR(descriptor + 8))) {
    BB_U32(descriptor) = 20;
    BB_U32(descriptor + 4) = 0xe2;
    if ((int32_t)BB_GAME_METHOD(REF(device), 0xc, descriptor, buffer, 0) >= 0) {
      uint32_t duration =
          (BB_U32(descriptor + 8) * 1000u) /
          ((WORD(BB_U32(descriptor + 16) + 14) >> 3) *
           BB_U32(BB_U32(descriptor + 16) + 4));
      BB_U32(REF(object) + 0x7e) = duration;
      BB_U32(REF(object) + 0x82) = (duration * 36u) / 1000u;
      if (!bb_audio_buffer_fill_entry(BB_POINTER(buffer), BB_POINTER(samples),
                                       BB_U32(descriptor + 8))) {
        BB_GAME_METHOD(BB_U32(buffer), 8);
        BB_U32(buffer) = 0;
      }
    } else {
      BB_U32(buffer) = 0;
    }
  }
  void *data = BB_POINTER(0x4841f8);
  if (data) {
    remove_buffer(data);
    BB_U32(0x4841f8) = 0;
  }
  return BB_POINTER(buffer);
}

/* 0x4027b0: DEC followed by a sign test, including unsigned wrap, with the
 * object address advanced before each ECX-only constructor callback. */
void BB_STDCALL bb_object_array_construct_entry(
    void *start, uint32_t stride, uint32_t count,
    void(BB_THISCALL * construct)(void *)) {
  uint32_t p = REF(start);
  --count;
  while ((int32_t)count >= 0) {
    void *object = PTR(p);
    p += stride;
    BB_GAME_CALL(REF(construct), REF(object));
    --count;
  }
}

/* 0x403a40: ordered fatal teardown. The real exit boundary does not return;
 * retaining its original continuation permits isolated boundary comparisons. */
void bb_fatal_teardown_entry(uint32_t reason) {
  bb_palette_black(PTR(0x4eebc8));
  BB_GAME_CALL(0x42e060u, 0);
  bb_audio_manager_shutdown_entry(PTR(0x5590e8));
  bb_display_release(PTR(0x4eebc8));
  bb_archive_close(PTR(0x5e4198));
  bb_archive_close(PTR(0x5f6e20));
  BB_GAME_CALL(0x42a870u, 0, 0);
  if (reason >= 1 && reason <= 3) {
    const char *caption = BB_POINTER(0x6038ac);
    if (!caption)
      caption = PTR(0x48481c);
    const char *text = reason == 3 ? PTR(0x6033d8) : BB_POINTER(0x6038a8);
    if (!text)
      text = PTR(0x48481c);
    BB_GAME_CALL(BB_U32(0x6ca558u), 0, BB_U32(0x5cd7c4u), REF(text), REF(caption), 0x10);
  }
  BB_GAME_CALL(BB_U32(0x6ca548u), 0, BB_U32(0x5cd7c4u), 0x10, 0, 0);
  BB_GAME_CALL(0x46bee0u, 0, 1);
}
void BB_THISCALL bb_display_flush_entry(void *object) {
  uintptr_t p = REF(object);
  BYTE(p + 0x20ebd) = 0;
  while (!bb_palette_ready(object)) {
    bb_palette_fade(object, 0);
    bb_display_wait(object, 3);
  }
  BYTE(p + 0x20ebc) = 0xff;
  BYTE(p + 0x20ebd) = 0xff;
}
void BB_THISCALL bb_display_present_wait_entry(void *object) {
  BYTE(REF(object) + 0x20ebd) = 0xff;
  while (!bb_palette_ready(object)) {
    bb_palette_fade(object, 0);
    bb_display_wait(object, 3);
  }
}

/* Typed entries borrow mapped locals from the dispatcher on portable hosts.
 * The i386 path keeps caller-visible locals on its native compatibility stack. */
void *BB_THISCALL bb_audio_resource_find_entry(void *object, const char *name) {
#ifdef BB_MODEL_RUNTIME
  return PTR(BB_GAME_CALL(0x401f50u, REF(object), REF(name)));
#else
  char text[BB_AUDIO_NAME_BYTES];
  return bb_audio_resource_find_c(object, name, REF(text));
#endif
}
uint32_t BB_THISCALL bb_audio_asset_load_entry(void *object, const char *name,
                                              uint32_t flags) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(0x402000u, REF(object), REF(name), flags);
#else
  char text[BB_AUDIO_NAME_BYTES];
  return bb_audio_asset_load_c(object, name, flags, REF(text));
#endif
}
uint32_t bb_audio_buffer_fill_entry(void *buffer, const void *input,
                                    uint32_t bytes) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(0x402600u, 0, REF(buffer), REF(input), bytes);
#else
  uint32_t outputs[BB_AUDIO_FILL_BYTES / 4];
  return bb_audio_buffer_fill_c(buffer, input, bytes, REF(outputs));
#endif
}
void *BB_THISCALL bb_audio_pcm_create_entry(void *object, void *device,
                                           const char *name) {
#ifdef BB_MODEL_RUNTIME
  return PTR(BB_GAME_CALL(0x402410u, REF(object), REF(device), REF(name)));
#else
  uint32_t outputs[BB_AUDIO_CREATE_BYTES / 4];
  return bb_audio_pcm_create_c(object, device, name, REF(outputs));
#endif
}
