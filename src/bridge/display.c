/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include <stddef.h>
#ifdef BB_MODEL_RUNTIME
#include "render_models.h"
#else

typedef uint32_t(BB_STDCALL *lock_fn)(void *, void *, void *, uint32_t, void *);
typedef uint32_t(BB_STDCALL *unlock_fn)(void *, void *);
typedef uint32_t(BB_STDCALL *flip_fn)(void *, void *, uint32_t);
typedef uint32_t(BB_STDCALL *surface_fn)(void *);
typedef uint32_t(BB_STDCALL *wait_fn)(void *, uint32_t, void *);
typedef uint32_t(BB_STDCALL *debug_fn)(const char *);

static void *method(void *object, uint32_t offset) {
  return BB_POINTER((uintptr_t)BB_POINTER((uintptr_t)object) + offset);
}
#endif

/* 0x0042f150..0x0042f17b: set the embedded descriptor's size before Lock.
 * A failed lock returns null without resetting fields written by the callback.
 */
uint32_t BB_THISCALL bb_display_lock(void *object) {
  uint32_t owner = bb_address_encode(object);
  void *surface = BB_POINTER(owner + 0x38);
  BB_U32(owner + 0x4c) = 108;
#ifdef BB_MODEL_RUNTIME
  uint32_t args[] = {0, owner + 0x4c, 1, 0};
  if (bb_render_model_method(bb_address_encode(surface), 0x64, args, 4))
#else
  if (((lock_fn)BB_CODE(method(surface, 0x64)))(
          surface, NULL, (void *)(owner + 0x4c), 1, NULL))
#endif
    return 0;
  return BB_U32(owner + 0x70);
}

/* 0x0042f180..0x0042f190. Re-read the surface after each external call. */
uint32_t BB_THISCALL bb_display_unlock(void *object) {
  void *surface = BB_POINTER(bb_address_encode(object) + 0x38);
#ifdef BB_MODEL_RUNTIME
  return bb_render_model_method(bb_address_encode(surface), 0x80,
                                (uint32_t[]){0}, 1);
#else
  return ((unlock_fn)BB_CODE(method(surface, 0x80)))(surface, NULL);
#endif
}

/* 0x0042f1a0..0x0042f1f9: clear the global screen dimensions, including an
 * unconditional unlock after a failed lock. Pitch arithmetic wraps at 32 bits.
 */
uint32_t BB_THISCALL bb_display_clear(void *object) {
  uint32_t owner = bb_address_encode(object);
  uint32_t row = bb_display_lock(object);
  if (row)
    for (int32_t y = 0; y < BB_I32(0x00484448); ++y) {
      for (int32_t x = 0; x < BB_I32(0x00484444); ++x)
        BB_U8(row + (uint32_t)x) = 0;
      row += BB_U32(owner + 0x5c);
    }
  uint32_t result = bb_display_unlock(object);
  BB_U32(owner + 0x1c) = 0xffffff00;
  return result;
}

/* 0x0042f240..0x0042f2bd. Lost surfaces are restored for any nonzero
 * IsLost result. Retry once, retaining its upper EAX bytes during invalidation.
 */
uint32_t BB_THISCALL bb_display_flip(void *object) {
  uint32_t owner = bb_address_encode(object);
  bb_render_queue_advance((void *)(uintptr_t)0x005d19e0);
  bb_palette_fade(object, 0);
  void *surface = BB_POINTER(owner + 0x34);
#ifdef BB_MODEL_RUNTIME
#define FLIP(s)                                                                \
  bb_render_model_method(bb_address_encode(s), 0x2c, (uint32_t[]){0, 1}, 2)
#define SURFACE(s, offset)                                                     \
  bb_render_model_method(bb_address_encode(s), offset, NULL, 0)
#else
#define FLIP(s) ((flip_fn)BB_CODE(method(s, 0x2c)))(s, NULL, 1)
#define SURFACE(s, offset) ((surface_fn)BB_CODE(method(s, offset)))(s)
#endif
  uint32_t result = FLIP(surface);
  if (result == 0x887601c2) {
    for (uint32_t offset = 0x34; offset <= 0x38; offset += 4) {
      surface = BB_POINTER(owner + offset);
      if (SURFACE(surface, 0x60)) {
        surface = BB_POINTER(owner + offset);
        SURFACE(surface, 0x6c);
      }
    }
    surface = BB_POINTER(owner + 0x34);
    result = FLIP(surface);
    return bb_display_invalidate(result);
  }
  if (result) {
#ifdef BB_MODEL_RUNTIME
    return bb_render_model_service(BB_U32(0x006ca374), 0,
                                   (uint32_t[]){0x00488830}, 1);
#else
    return ((debug_fn)BB_CODE(BB_POINTER(0x006ca374)))(
        (const char *)(uintptr_t)0x00488830);
#endif
  }
  return result;
}
#undef FLIP
#undef SURFACE

/* 0x00432dc0..0x00432ded. Its boundary handles the nonpositive count, for
 * which the original leaves all caller registers, including EAX, unchanged.
 */
uint32_t BB_THISCALL bb_display_wait(void *object, int32_t count) {
  uint32_t result = 0;
  for (int32_t i = 0; i < count; ++i) {
    void *draw = BB_POINTER(bb_address_encode(object) + 0x30);
#ifdef BB_MODEL_RUNTIME
    bb_render_model_method(bb_address_encode(draw), 0x58, (uint32_t[]){1, 0},
                           2);
    draw = BB_POINTER(bb_address_encode(object) + 0x30);
    result = bb_render_model_method(bb_address_encode(draw), 0x58,
                                    (uint32_t[]){4, 0}, 2);
#else
    ((wait_fn)BB_CODE(method(draw, 0x58)))(draw, 1, NULL);
    draw = BB_POINTER(bb_address_encode(object) + 0x30);
    result = ((wait_fn)BB_CODE(method(draw, 0x58)))(draw, 4, NULL);
#endif
  }
  return result;
}

/* 0x0042f200..0x0042f23c: clear and present both buffers, then three complete
 * vertical blanks. Only the original four cursor fields are reset.
 */
uint32_t BB_THISCALL bb_display_reset(void *object) {
  uint32_t owner = bb_address_encode(object);
  bb_display_clear(object);
  bb_display_flip(object);
  bb_display_clear(object);
  bb_display_flip(object);
  bb_display_wait(object, 3);
  BB_U32(owner + 0x10) = 0;
  BB_U32(owner + 0x14) = 0xffffff00;
  BB_U32(owner + 0x18) = 0xffffff00;
  BB_U32(owner + 0x1c) = 0xffffff00;
  return 0xffffff00;
}

/* 0x004324c0..0x004324fb: restore remapped raw sprites except the globally
 * excluded one. Read each next link after restoration, which can relink it.
 * The flag lets the boundary preserve ECX/EDX when no restoration was called.
 */
uint32_t bb_display_restore(void *object, uint32_t *called) {
  uint32_t node = BB_U32(bb_address_encode(object) + 0x20ec2);
  uint32_t result = 0;
  *called = 0;
  while (node) {
    result = BB_U32(node + 0xf0);
    if (result) {
      result = BB_U32(node + 0x60);
      if (!result) {
        result = BB_U32(0x006aff40);
        if (result != node) {
          *called = 1;
#ifdef BB_MODEL_RUNTIME
          result = bb_render_model_service(0x0042f680u, node, NULL, 0);
#else
          result = bb_sprite_restore((void *)node);
#endif
        }
      }
    }
    node = BB_U32(node + 0x2f6);
  }
  return result;
}

/* 0x00432530..0x004325e8: inclusive screen clipping, independent of viewport.
 * The fifth argument is ignored: the original always uses the object's shade
 * map. Preserve its early return values and unconditional unlock once locked.
 */
uint32_t BB_THISCALL bb_display_shade_rect(void *object, int32_t left,
                                           int32_t top, int32_t right,
                                           int32_t bottom,
                                           const unsigned char *ignored_map) {
  (void)ignored_map;
  uint32_t owner = bb_address_encode(object);
  if (right < 0 || bottom < 0)
    return right;
  int32_t max_x = BB_U32(0x00484444) - 1;
  if (left > max_x)
    return right;
  int32_t max_y = BB_U32(0x00484448) - 1;
  if (top > max_y)
    return max_y;
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  if (right > max_x)
    right = max_x;
  if (bottom > max_y)
    bottom = max_y;
  uint32_t pixels = bb_display_lock(object);
  uint32_t row = pixels + BB_U32(owner + 0x5c) * (uint32_t)top;
  if (top <= bottom) {
    uint32_t rows = (uint32_t)bottom - (uint32_t)top + 1;
    do {
      if (left <= right) {
        uint32_t at = row + (uint32_t)left;
        uint32_t count = (uint32_t)right - (uint32_t)left + 1;
        do {
          unsigned char value = BB_U8(at);
          BB_U8(at) = BB_U8(owner + 0xcbc + value);
          ++at;
        } while (--count);
      }
      row += BB_U32(owner + 0x5c);
    } while (--rows);
  }
  return bb_display_unlock(object);
}

/* 0x00432500..0x00432524. */
uint32_t BB_THISCALL bb_display_shade(void *object, int32_t left, int32_t top,
                                      int32_t right, int32_t bottom) {
  return bb_display_shade_rect(
      object, left, top, right, bottom,
      (const unsigned char *)(uintptr_t)(bb_address_encode(object) + 0xcbc));
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_DISPLAY_VIEWPORT "_bb_display_viewport"
#define BB_DISPLAY_INTERSECT "_bb_display_intersect"
#define BB_DISPLAY_WAIT "_bb_display_wait"
#define BB_DISPLAY_RESTORE "_bb_display_restore"
#else
#define BB_DISPLAY_VIEWPORT "bb_display_viewport"
#define BB_DISPLAY_INTERSECT "bb_display_intersect"
#define BB_DISPLAY_WAIT "bb_display_wait"
#define BB_DISPLAY_RESTORE "bb_display_restore"
#endif

/* Thin ABI boundaries retain caller-visible registers of the leaf routines. */
void __attribute__((naked)) bb_display_viewport_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "pushl 20(%esp)\n\t"
                   "pushl 20(%esp)\n\t"
                   "pushl 20(%esp)\n\t"
                   "pushl 20(%esp)\n\t"
                   "call " BB_DISPLAY_VIEWPORT "\n\t"
                   "popl %ecx\n\t"
                   "movl 4(%ecx), %edx\n\t"
                   "ret $16");
}
void __attribute__((naked)) bb_display_intersect_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "pushl 20(%esp)\n\t"
                   "pushl 20(%esp)\n\t"
                   "pushl 20(%esp)\n\t"
                   "pushl 20(%esp)\n\t"
                   "call " BB_DISPLAY_INTERSECT "\n\t"
                   "popl %ecx\n\t"
                   "movl 4(%ecx), %edx\n\t"
                   "ret $16");
}
void __attribute__((naked)) bb_display_wait_entry(void) {
  __asm__ volatile("cmpl $0, 4(%esp)\n\t"
                   "jle 1f\n\t"
                   "jmp " BB_DISPLAY_WAIT "\n"
                   "1: ret $4");
}
void __attribute__((naked)) bb_display_restore_entry(void) {
  __asm__ volatile("cmpl $0, 0x20ec2(%ecx)\n\t"
                   "je 2f\n\t"
                   "pushl %edx\n\t"
                   "pushl %ecx\n\t"
                   "subl $4, %esp\n\t"
                   "pushl %esp\n\t"
                   "pushl 8(%esp)\n\t"
                   "call " BB_DISPLAY_RESTORE "\n\t"
                   "addl $8, %esp\n\t"
                   "cmpl $0, (%esp)\n\t"
                   "jne 1f\n\t"
                   "movl 4(%esp), %ecx\n\t"
                   "movl 8(%esp), %edx\n"
                   "1: addl $12, %esp\n"
                   "2: ret");
}
#endif
