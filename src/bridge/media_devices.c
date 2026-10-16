/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "media_devices.h"
#include "interop.h"
#include <stddef.h>
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#include "model_buffer.h"
#endif
#define WORD(a) BB_U16(a)
#define METHOD(p, offset) BB_POINTER(BB_U32((uintptr_t)(p)) + (offset))
typedef uint32_t(BB_STDCALL * factory_fn)(const void *, void **,
                                                        void *);
typedef uint32_t(BB_STDCALL * caps_fn)(void *, void *);
typedef uint32_t(BB_STDCALL * coop_fn)(void *, void *, uint32_t);
typedef uint32_t(BB_STDCALL * create_fn)(void *, const uint32_t *,
                                                       void **, void *);
typedef uint32_t(BB_STDCALL * release_fn)(void *);
static void clear(void *p, uint32_t n) {
#ifdef BB_MODEL_RUNTIME
  memset(BB_DATA(p), 0, n);
#else
  ((void *(*)(void *, uint32_t, uint32_t))BB_CODE(0x46f490))(p, 0, n);
#endif
}
static uint32_t query(void *object, unsigned offset, void *data, size_t bytes) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, data, bytes);
  uint32_t result = BB_GAME_METHOD(bb_address_encode(object), offset, buffer.reference);
  bb_model_buffer_end(&buffer, 1);
  return result;
#else
  (void)bytes;
  return ((caps_fn)BB_CODE(METHOD(object, offset)))(object, data);
#endif
}
static uint32_t release(void *object) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_METHOD(bb_address_encode(object), 8);
#else
  return ((release_fn)BB_CODE(METHOD(object, 8)))(object);
#endif
}
#ifdef BB_MODEL_RUNTIME
static uint32_t factory(uint32_t slot, const void *guid, void **out, void *outer) {
  struct bb_model_buffer id, result;
  bb_model_buffer_begin(&id, guid, guid ? 16 : 0);
  bb_model_buffer_begin(&result, out, out ? 4 : 0);
  uint32_t status = BB_GAME_CALL(BB_U32(slot), 0, id.reference, result.reference,
                                  bb_address_encode(outer));
  bb_model_buffer_end(&result, 1);
  bb_model_buffer_end(&id, 0);
  return status;
}
#endif
/* C import boundaries replace the two executable JMP-through-IAT stubs. */
uint32_t BB_STDCALL
bb_media_draw_create_entry(const void *guid, void **out, void *outer) {
#ifdef BB_MODEL_RUNTIME
  return factory(0x6ca33c, guid, out, outer);
#else
  return ((factory_fn)BB_CODE(BB_POINTER(0x6ca33c)))(guid, out, outer);
#endif
}
uint32_t BB_STDCALL
bb_media_sound_create_entry(const void *guid, void **out, void *outer) {
#ifdef BB_MODEL_RUNTIME
  return factory(0x6ca344, guid, out, outer);
#else
  return ((factory_fn)BB_CODE(BB_POINTER(0x6ca344)))(guid, out, outer);
#endif
}
uint32_t bb_media_sound_device_entry(void **out, void *window) {
  BB_SET_POINTER(0x6c1338, window);
  if (!bb_media_sound_create_entry(NULL, out, NULL)) {
    void *sound = BB_POINTER(out);
#ifdef BB_MODEL_RUNTIME
    if (!BB_GAME_METHOD(bb_address_encode(sound), 0x18, bb_address_encode(window), 2))
#else
    if (!((coop_fn)BB_CODE(METHOD(sound, 0x18)))(sound, window, 2))
#endif
      return 0;
  }
  BB_SET_POINTER(out, NULL); /* Failed cooperative devices are not released. */
  return 1;
}
void bb_media_sound_initialize_entry(void *window) {
  bb_media_sound_device_entry((void **)0x48bc70, window);
  if (!BB_U32(0x48bc70))
    return;
  uint32_t caps[24];
  clear(caps, sizeof(caps));
  caps[0] = sizeof(caps);
  void *sound = BB_POINTER(0x48bc70);
  query(sound, 0x10, caps, sizeof caps);
  BB_U32(0x48bc78) = !!(caps[1] & 0x20);
  unsigned char format[18];
  clear(format, sizeof(format));
  uintptr_t f = (uintptr_t)format;
  WORD(f) = 1;
  BB_U32(f + 4) = 22050;
  WORD(f + 2) = caps[1] & 2 ? 2 : 1;
  WORD(f + 14) = caps[1] & 8 ? 16 : 8;
  WORD(f + 12) = (WORD(f + 14) >> 3) * WORD(f + 2);
  BB_U32(f + 8) = WORD(f + 12) * BB_U32(f + 4);
  uint32_t desc[5];
  clear(desc, sizeof(desc));
  desc[0] = 20;
  desc[1] = 0x81;
  desc[2] = desc[4] = 0;
  sound = BB_POINTER(0x48bc70);
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer descriptor;
  bb_model_buffer_begin(&descriptor, desc, sizeof desc);
  uint32_t status = BB_GAME_METHOD(bb_address_encode(sound), 12,
                                     descriptor.reference, 0x48bc74, 0);
  bb_model_buffer_end(&descriptor, 0);
  if (status)
#else
  if (((create_fn)BB_CODE(METHOD(sound, 12)))(sound, desc, (void **)0x48bc74, NULL))
#endif
    return;
  uint32_t buffer_caps[5];
  clear(buffer_caps, sizeof(buffer_caps));
  buffer_caps[0] = sizeof(buffer_caps);
  void *buffer = BB_POINTER(0x48bc74);
  query(buffer, 12, buffer_caps, sizeof buffer_caps);
  buffer = BB_POINTER(0x48bc74);
  query(buffer, 0x38, format, sizeof format);
}
/* Preserve the original write to the caller's by-value argument slot while
 * legacy callers still share its stack convention. Device release itself is C.
 */
void __attribute__((used, noinline)) bb_media_sound_release_c(uint32_t *r) {
  uintptr_t args = r[3] + 8;
  void *object = BB_POINTER(args);
  if (object) {
    release(object);
    BB_U32(args) = 0;
  }
  r[7] = 0;
  r[8] = (r[8] & ~0x8d5u) | 0x44;
}
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#ifdef BB_MODEL_RUNTIME
uint32_t bb_media_sound_release_entry(void *object) {
  return BB_GAME_CALL(0x4637b4, 0, bb_address_encode(object));
}
#else
uint32_t __attribute__((naked, noinline))
bb_media_sound_release_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(
      bb_media_sound_release_c) ";addl $4,%esp;popal;popfl;ret");
}
#endif
void bb_media_sound_shutdown_entry(void) {
  void *primary = BB_POINTER(0x48bc74);
  if (primary) {
    release(primary);
    BB_U32(0x48bc74) = 0;
  }
  bb_media_sound_release_entry(BB_POINTER(0x48bc70));
  /* The original device-global word is retained, including a stale pointer. */
}
