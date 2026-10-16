/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_present.h"
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#define WORD(a) BB_U16(a)
typedef uint32_t(BB_STDCALL *lock_fn)(void *, void *, void *, uint32_t, void *);
typedef uint32_t(BB_STDCALL *unlock_fn)(void *, void *);
static void *method(void *object, uint32_t offset) {
  return BB_POINTER((uintptr_t)BB_POINTER((uintptr_t)object) + offset);
}
static uint32_t half(uint32_t value) { return (uint32_t)((int32_t)value >> 1); }

/* 0x45bad0/0x45be30: source geometry and pointer are captured before Lock.
 * Flags/offsets and the live surface are re-read after callbacks. Copy units
 * follow the original packed DWORD frame representation, not pixel counts.
 */
static uint32_t present(void *object, unsigned bits) {
  uint32_t p = bb_address_encode(object);
  uint32_t width = BB_U32(p + 8), rows = BB_U32(p + 4);
  uint32_t source = BB_U32(p + 0x39c);
#ifdef BB_MODEL_RUNTIME
  MOVIE_LOCAL(bb_memory_u32, descriptor, 27, 108);
#else
  uint32_t descriptor[27] = {108};
#endif
  uint32_t result;
  do {
    void *surface = BB_POINTER(p + 0x408);
#ifdef BB_MODEL_RUNTIME
    result = MV5(MV_WORD(method(surface, 0x64)), surface, 0, descriptor, 1, 0);
#else
    result =
        ((lock_fn)BB_CODE(method(surface, 0x64)))(surface, 0, descriptor, 1, 0);
#endif
  } while (result);
  uint32_t flags = BB_U32(p + 0x3b4), output = descriptor[9];
  uint32_t pitch = descriptor[4];
  if (flags & 1)
    output += BB_U32(p + 0x3b8) * (bits == 16 ? 2u : 4u);
  if (flags & 2)
    output += BB_U32(p + 0x3bc) * pitch;
  if (flags & 4) {
    uint32_t screen = BB_U32(0x6be1ac);
    if (flags & 0xc0)
      output += (half(screen) - width) * 4;
    else if (bits == 16)
      output += screen - width;
    else
      output += half(screen - width) * 4;
  }
  if (flags & 8) {
    uint32_t screen = BB_U32(0x6be1a8);
    output +=
        ((flags & 0x30) ? half(screen) - rows : half(screen - rows)) * pitch;
  }
  if (flags & 0x10) {
    if (flags & 0x40) {
      uint32_t stride = (pitch >> 2) * 4, second = output + stride;
      while (rows--) {
        uint32_t count = width;
        /* Unrolled groups preserve forward-overlap and duplicate-store order.
         */
        while (count) {
          count -= 16;
          for (unsigned i = 0; i < 16; ++i) {
            uint32_t value = BB_U32(source);
            BB_U32(second) = value;
            BB_U32(output) = value;
            source += 4;
            output += 4;
            second += 4;
          }
        }
        output += stride * 2 - width * 4;
        second += stride * 2 - width * 4;
      }
    }
  } else {
    uint32_t stride =
        bits == 16 && !(flags & 0x40) ? (pitch >> 1) * 2 : (pitch >> 2) * 4;
    if (flags & 0x20)
      stride *= 2;
    while (rows--) {
      uint32_t count = width;
      uint32_t copied = 0;
      while (count) {
        unsigned group = bits == 32 && (flags & 0x40) ? 8 : 16;
        count -= group;
        for (unsigned i = 0; i < group; ++i) {
          if (bits == 16 && !(flags & 0x40)) {
            uint16_t value = WORD(source);
            WORD(output) = value;
            source += 4;
            output += 2;
            copied += 2;
          } else {
            uint32_t value = BB_U32(source);
            if (bits == 32 && (flags & 0x40)) {
              BB_U32(output + 4) = value;
              BB_U32(output) = value;
              output += 8;
              copied += 8;
            } else {
              BB_U32(output) = value;
              output += 4;
              copied += 4;
            }
            source += 4;
          }
        }
      }
      output += stride - copied;
    }
  }
  do {
    void *surface = BB_POINTER(p + 0x408);
#ifdef BB_MODEL_RUNTIME
    result = MV2(MV_WORD(method(surface, 0x80)), surface, 0);
#else
    result = ((unlock_fn)BB_CODE(method(surface, 0x80)))(surface, 0);
#endif
  } while (result);
  return 0;
}
uint32_t BB_THISCALL __attribute__((noinline))
bb_movie_present_16_entry(void *object) {
  return present(object, 16);
}
uint32_t BB_THISCALL __attribute__((noinline))
bb_movie_present_32_entry(void *object) {
  return present(object, 32);
}
