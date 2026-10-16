/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_runtime.h"
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#include "media_devices.h"
#include "movie_color.h"
#include "movie_dispatch.h"
#include "movie_present.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#define W BB_STDCALL
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
typedef uint32_t(W *api1_fn)(uint32_t);
typedef uint32_t(W *api2_fn)(uint32_t, uint32_t);
typedef uint32_t(W *api3_fn)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(W *api4_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(W *api5_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(W *api6_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t);
typedef uint32_t(W *api8_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *member_fn)(void *);

#ifdef BB_MODEL_RUNTIME
#define API1(id, ...) MV1(id, __VA_ARGS__)
#define API2(id, ...) MV2(id, __VA_ARGS__)
#define API3(id, ...) MV3(id, __VA_ARGS__)
#define API4(id, ...) MV4(id, __VA_ARGS__)
#define API5(id, ...) MV5(id, __VA_ARGS__)
#define API6(id, ...) MV6(id, __VA_ARGS__)
#define API8(id, ...) MV8(id, __VA_ARGS__)
#else
#define API1(id, ...) ((api1_fn)BB_CODE(id))(__VA_ARGS__)
#define API2(id, ...) ((api2_fn)BB_CODE(id))(__VA_ARGS__)
#define API3(id, ...) ((api3_fn)BB_CODE(id))(__VA_ARGS__)
#define API4(id, ...) ((api4_fn)BB_CODE(id))(__VA_ARGS__)
#define API5(id, ...) ((api5_fn)BB_CODE(id))(__VA_ARGS__)
#define API6(id, ...) ((api6_fn)BB_CODE(id))(__VA_ARGS__)
#define API8(id, ...) ((api8_fn)BB_CODE(id))(__VA_ARGS__)
#endif
static uintptr_t method(uintptr_t p, uint32_t o) {
  return BB_U32(BB_U32(p) + o);
}
static uint32_t exchange(uintptr_t p, uint32_t v) {
  return API2(BB_U32(0x6ca3b8), p, v);
}
static void increment(uintptr_t p) { API1(BB_U32(0x6ca3bc), p); }
static void decrement(uintptr_t p) { API1(BB_U32(0x6ca3c0), p); }
static void release(uintptr_t p) { API1(method(p, 8), p); }
#ifdef BB_MODEL_RUNTIME
static void free_memory(uintptr_t p) { MV1(0x46e110, p); }
#else
static void free_memory(uintptr_t p) {
  ((void (*)(void *))BB_CODE(0x46e110))(PTR(p));
}
#endif
static uint32_t read_bytes(uintptr_t p, void *out, uint32_t n) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, out, n);
  uint32_t result = MV3(BB_U32(0x6ca564), BB_U32(p), buffer.reference, n);
  bb_model_buffer_end(&buffer, 1);
  return result;
#else
  return API3(BB_U32(0x6ca564), BB_U32(p), (uintptr_t)out, n);
#endif
}
static void copy_bytes(uintptr_t out, uintptr_t in, uint32_t n) {
  while (n >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    n -= 4;
  }
  while (n--)
    BYTE(out++) = BYTE(in++);
}
static void zero_bytes(uintptr_t out, uint32_t n) {
  while (n >= 4) {
    BB_U32(out) = 0;
    out += 4;
    n -= 4;
  }
  while (n--)
    BYTE(out++) = 0;
}
static int32_t clamp_sample(uint32_t value) {
  int32_t v = (int32_t)value;
  return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}
static uint32_t clamp_index(uint32_t value) {
  int32_t v = (int32_t)value;
  return v < 0 ? 0 : v > 0x1600 ? 0x1600 : value;
}

/* 0x45a2e0: on-disk FourCC and byte count are little endian. These numerical
 * tags correspond to SEAD/SNDC/SEND/pQGT/kVGT/fVGT/1SNd/pIQT/UV2f/etc. */
uint32_t BB_MOVIE_M bb_movie_chunk_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  unsigned char header[8];
  uint32_t result = read_bytes(p, header, 8);
  if (!result)
    return UINT32_MAX;
  if (result != 8)
    return UINT32_MAX - 1;
  uint32_t tag = BB_U32(header), bytes = BB_U32(header + 4) - 8;
  switch (tag) {
  case 0x44414553:
    return 0;
  case 0x684e5331:
  case 0x6c484353:
    bb_movie_predictor_reset_entry(object);
    return 0;
  case 0x43444e53:
    if (BB_U32(p + 0x3ac))
      return bb_movie_audio_read_entry(object, bytes, 2);
    break;
  case 0x644e5331:
  case 0x6c444353:
    if (BB_U32(p + 0x3ac))
      return bb_movie_audio_read_entry(object, bytes, 1);
    break;
  case 0x444e4553:
  case 0x654e5331:
  case 0x6c454353:
    return 3;
  case 0x54475170:
  case 0x6656554d:
    return bb_movie_frame_read_entry(object, bytes, 3);
  case 0x5447566b:
  case 0x6b564754:
    return bb_movie_frame_read_entry(object, bytes, 1);
  case 0x54475666:
  case 0x66564754:
    return bb_movie_frame_read_entry(object, bytes, 2);
  case 0x54514970:
  case 0x66325655:
    return bb_movie_frame_read_entry(object, bytes, 4);
  }
  return UINT32_MAX - 1;
}

/* 0x45a4e0: retain live slot/counter re-reads between stores and atomics. */
uint32_t BB_MOVIE_F bb_movie_frame_read_entry(void *object, uint32_t bytes,
                                              uint32_t codec) {
  uint32_t p = bb_address_encode(object);
  uint32_t base = BB_U32(p + 0xc), cursor = BB_U32(p + 0x14);
  if (cursor + bytes >= base + 500000)
    BB_U32(p + 0x14) = base;
  uint32_t result = read_bytes(p, PTR(BB_U32(p + 0x14)), bytes);
  if (!result)
    return UINT32_MAX;
  if (result == UINT32_MAX)
    return UINT32_MAX - 1;
  BB_U32(p + 0x19c + (BB_U32(p + 0x434) & 31) * 16) = BB_U32(p + 0x14);
  BB_U32(p + 0x1a8 + (BB_U32(p + 0x434) & 31) * 16) = codec;
  BB_U32(p + 0x1a4 + (BB_U32(p + 0x434) & 31) * 16) = BB_U32(p + 0x440);
  BB_U32(p + 0x14) = BB_U32(p + 0x14) + bytes;
  increment(p + 0x434);
  increment(p + 0x438);
  increment(p + 0x440);
  return 1;
}

/* 0x45a5a0: scratch is the exact original incoming 0x8008-byte frame. */
uint32_t bb_movie_audio_read_c(unsigned char *frame, void *object,
                               uint32_t bytes, uint32_t codec) {
  uint32_t p = bb_address_encode(object), f = bb_address_encode(frame);
  uint32_t result = read_bytes(p, frame + 8, bytes);
  if (!result)
    return UINT32_MAX;
  if (result == UINT32_MAX)
    return UINT32_MAX - 1;
  uint32_t output = codec == 1   ? ((bytes - 12) / 30) * 112
                    : codec == 2 ? bytes * 4
                                 : BB_U32(f);
  if (BB_U32(p + 0x18) + output >= BB_U32(p + 0x10) + 150000)
    BB_U32(p + 0x18) = BB_U32(p + 0x10);
  if (codec == 1) {
    BB_U32(p + 0x454) = (int32_t)(int16_t)WORD(f + 12);
    BB_U32(p + 0x458) = (int32_t)(int16_t)WORD(f + 16);
    BB_U32(p + 0x45c) = (int32_t)(int16_t)WORD(f + 14);
    BB_U32(p + 0x460) = (int32_t)(int16_t)WORD(f + 18);
    BB_U32(f + 4) = BB_U32(p + 0x18);
    BB_U32(f) = 0;
    bb_movie_block_decode_entry(output >> 2, PTR(f), PTR(p + 0x454),
                                PTR(p + 0x45c), PTR(p + 0x458), PTR(p + 0x460),
                                PTR(f + 20), PTR(f + 4));
  } else if (codec == 2) {
    bb_movie_stereo_decode_entry(frame + 8, PTR(BB_U32(p + 0x18)), bytes,
                                 PTR(p + 0x7c8), PTR(p + 0x7d0));
  }
  BB_U32(p + 0x1c + (BB_U32(p + 0x42c) & 31) * 12) = BB_U32(p + 0x18);
  BB_U32(p + 0x20 + (BB_U32(p + 0x42c) & 31) * 12) = output;
  BB_U32(p + ((BB_U32(p + 0x42c) & 31) + 3) * 12) = BB_U32(p + 0x440);
  BB_U32(p + 0x18) = BB_U32(p + 0x18) + output;
  increment(p + 0x42c);
  increment(p + 0x43c);
  increment(p + 0x444);
  return 2;
}
#ifdef BB_MODEL_RUNTIME
uint32_t BB_MOVIE_F bb_movie_audio_read_entry(void *object, uint32_t bytes,
                                              uint32_t codec) {
  MOVIE_BUFFER(unsigned char, frame, 0x8008);
  return bb_movie_audio_read_c(frame, object, bytes, codec);
}
#else
uint32_t BB_MOVIE_F __attribute__((naked))
bb_movie_audio_read_entry(void *object __attribute__((unused)),
                          uint32_t bytes __attribute__((unused)),
                          uint32_t codec __attribute__((unused))) {
  __asm__ volatile(
      "movl 4(%esp),%eax;subl $0x8008,%esp;"
      "pushl %eax;pushl %edx;pushl %ecx;leal 12(%esp),%eax;"
      "pushl %eax;call " BB_ASM(
          bb_movie_audio_read_c) ";addl $16,%esp;addl $0x8008,%esp;ret $4");
}

#endif
/* 0x45ad20, 0x45ad40: wrapped predictor arithmetic, saturation and histories.
 */
uint32_t BB_MOVIE_M bb_movie_predictor_reset_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  for (uint32_t o = 0x454; o <= 0x460; o += 4)
    BB_U32(p + o) = 0;
  return 0;
}
static uint32_t predictor(uint32_t nibble, unsigned shift, uint32_t previous,
                          uint32_t older, uint32_t a, uint32_t b) {
  uint32_t signal = (uint32_t)((int32_t)(nibble << 28) >> (shift & 31));
  return (uint32_t)clamp_sample(
      (uint32_t)((int32_t)(signal + previous * a + older * b + 128) >> 8));
}
void BB_MOVIE_F bb_movie_block_decode_entry(
    uint32_t samples, uint32_t *position, uint32_t *left, uint32_t *left_older,
    uint32_t *right, uint32_t *right_older, const unsigned char *input,
    uint32_t *output) {
  int32_t offset = (int32_t)(BB_U32(position) * 30u) / 28;
  uint32_t in = bb_address_encode(input) + (uint32_t)offset,
           out = BB_U32(output);
  uint32_t l = BB_U32(left), lo = BB_U32(left_older), r = BB_U32(right),
           ro = BB_U32(right_older);
  if ((int32_t)samples > 0) {
    uint32_t blocks = (samples + 27) / 28;
    do {
      BB_U32(position) += 28;
      uint32_t code = BYTE(in), shifts = BYTE(in + 1);
      in += 2;
      uint32_t la = BB_U32(0x48a3d8 + (code >> 4) * 4);
      uint32_t lb = BB_U32(0x48a3e8 + (code >> 4) * 4);
      uint32_t ra = BB_U32(0x48a3d8 + (code & 15) * 4);
      uint32_t rb = BB_U32(0x48a3e8 + (code & 15) * 4);
      unsigned ls = (shifts >> 4) + 8, rs = (shifts & 15) + 8;
      for (unsigned i = 0; i < 28; i += 2) {
        uint32_t pair = BYTE(in++);
        lo = predictor(pair >> 4, ls, l, lo, la, lb);
        ro = predictor(pair & 15, rs, r, ro, ra, rb);
        WORD(out) = lo;
        WORD(out + 2) = ro;
        pair = BYTE(in++);
        l = predictor(pair >> 4, ls, lo, l, la, lb);
        r = predictor(pair & 15, rs, ro, r, ra, rb);
        WORD(out + 4) = l;
        WORD(out + 6) = r;
        out += 8;
      }
    } while (--blocks);
  }
  BB_U32(left) = l;
  BB_U32(left_older) = lo;
  BB_U32(right) = r;
  BB_U32(right_older) = ro;
  BB_U32(output) = out;
}

/* 0x45b8b0 and 0x45b953 preserve all incoming registers with PUSHAL/POPAL. */
void bb_movie_mono_decode_c(uint32_t start, const unsigned char *input,
                            uint16_t *output, uint32_t count, uint32_t *sample,
                            uint32_t *index) {
  uint32_t end = start + count, n = start, step = BB_U32(index);
  uint32_t value = (int32_t)(int16_t)BB_U32(sample);
  do {
    uint32_t code = BYTE(input);
    if (!(n & 1))
      code >>= 4;
    else {
      code &= 15;
      ++input;
    }
    value = clamp_sample(value + BB_U32(0x48a3fc + step + code * 4));
    WORD(output) = value;
    ++output;
    ++n;
    step = clamp_index(step + BB_U32(0x48ba3c + (code & 7) * 4));
  } while ((int32_t)n < (int32_t)end);
  BB_U32(sample) = value;
  BB_U32(index) = step;
}
void bb_movie_stereo_decode_c(const unsigned char *input, uint16_t *output,
                              uint32_t count, uint32_t *samples,
                              uint32_t *indices) {
  uint32_t packed = BB_U32(indices);
  BB_U32(0x48ba64) = (int32_t)(int16_t)packed;
  BB_U32(0x48ba68) = (int32_t)(int16_t)(packed >> 16);
  packed = BB_U32(samples);
  BB_U32(0x48ba5c) = (int32_t)(int16_t)packed;
  BB_U32(0x48ba60) = (int32_t)(int16_t)(packed >> 16);
  uint32_t n = 0;
  do {
    uint32_t code = BYTE(input) >> 4, step = BB_U32(0x48ba64);
    uint32_t value =
        clamp_sample(BB_U32(0x48ba5c) + BB_U32(0x48a3fc + step + code * 4));
    WORD((uintptr_t)output + n * 4) = value;
    BB_U32(0x48ba5c) = value;
    BB_U32(0x48ba64) = clamp_index(step + BB_U32(0x48ba3c + (code & 7) * 4));
    code = BYTE(input) & 15;
    step = BB_U32(0x48ba68);
    value = clamp_sample(BB_U32(0x48ba60) + BB_U32(0x48a3fc + step + code * 4));
    WORD((uintptr_t)output + n * 4 + 2) = value;
    BB_U32(0x48ba60) = value;
    BB_U32(0x48ba68) = clamp_index(step + BB_U32(0x48ba3c + (code & 7) * 4));
    ++n;
    ++input;
  } while ((int32_t)n < (int32_t)count);
  BB_U32(samples) = (BB_U32(0x48ba60) << 16) | (BB_U32(0x48ba5c) & 65535);
  BB_U32(indices) = (BB_U32(0x48ba68) << 16) | (BB_U32(0x48ba64) & 65535);
}
#ifdef BB_MODEL_RUNTIME
void bb_movie_mono_decode_entry(uint32_t start, const unsigned char *in,
                                uint16_t *out, uint32_t count, uint32_t *sample,
                                uint32_t *index) {
  bb_movie_mono_decode_c(start, in, out, count, sample, index);
}
void bb_movie_stereo_decode_entry(const unsigned char *in, uint16_t *out,
                                  uint32_t count, uint32_t *samples,
                                  uint32_t *indices) {
  bb_movie_stereo_decode_c(in, out, count, samples, indices);
}
#else
#define SAVED_ENTRY(name, body, n, offset, parameters)                         \
  void __attribute__((naked)) name parameters {                                \
    __asm__ volatile("pushal;.rept " #n ";pushl " #offset                      \
                     "(%esp);.endr;call " BB_ASM(body) ";addl $" #n            \
                                                       "*4,%esp;popal;ret");   \
  }
SAVED_ENTRY(bb_movie_mono_decode_entry, bb_movie_mono_decode_c, 6, 56,
            (uint32_t a __attribute__((unused)),
             const unsigned char *b __attribute__((unused)),
             uint16_t *c __attribute__((unused)),
             uint32_t d __attribute__((unused)),
             uint32_t *e __attribute__((unused)),
             uint32_t *f __attribute__((unused))))
SAVED_ENTRY(bb_movie_stereo_decode_entry, bb_movie_stereo_decode_c, 5, 52,
            (const unsigned char *a __attribute__((unused)),
             uint16_t *b __attribute__((unused)),
             uint32_t c __attribute__((unused)),
             uint32_t *d __attribute__((unused)),
             uint32_t *e __attribute__((unused))))

#endif
/* 0x45b3a0: borrowed or owned DirectSound and primary buffer setup. */
uint32_t BB_MOVIE_M bb_movie_sound_setup_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (!(BB_U32(p + 0x3b4) & 0x400000))
    return 0;
  BB_U32(0x6bb194) = 0;
  if (BB_U32(p + 0x3b4) & 0x100000) {
    BB_U32(0x6bb190) = 0;
    BB_U32(0x6bb188) = BB_U32(p + 0x3cc);
  }
  if (!BB_U32(0x6bb188)) {
    BB_U32(0x6bb190) = 1;
    uint32_t window = BB_U32(p + 0x400);
    if (bb_media_sound_create_entry(NULL, (void **)0x6bb188, NULL))
      BB_U32(0x6bb188) = 0;
    else {
      uintptr_t sound = BB_U32(0x6bb188);
      if (API3(method(sound, 0x18), sound, window, 3)) {
        release(BB_U32(0x6bb188));
        BB_U32(0x6bb188) = 0;
      }
    }
    if (!BB_U32(0x6bb188)) {
      BB_U32(0x6bb18c) = 0;
      return 0;
    }
  }
#ifdef BB_MODEL_RUNTIME
  MOVIE_LOCAL(bb_memory_u32, caps, 24, 0);
#else
  uint32_t caps[24] = {0};
#endif
  caps[0] = 0x60;
  uintptr_t sound = BB_U32(0x6bb188);
  API2(method(sound, 0x10), sound, (uintptr_t)caps);
  BB_U32(0x6bb198) = (caps[1] >> 5) & 1;
#ifdef BB_MODEL_RUNTIME
  MOVIE_LOCAL(bb_memory_u32, descriptor, 5, 20, 0, 0, 0, 0);
#else
  uint32_t descriptor[5] = {20, 0, 0, 0, 0};
#endif
  if (BB_U32(p + 0x3b4) & 0x200000) {
    BB_U32(0x6bb18c) = BB_U32(p + 0x3d0);
    uintptr_t primary = BB_U32(0x6bb18c);
    if (primary) {
      API2(method(primary, 0xc), primary, (uintptr_t)descriptor);
      BB_U32(0x6bb184) = descriptor[2];
    }
  }
  if (BB_U32(0x6bb18c))
    return 0;
  descriptor[0] = 20;
  descriptor[1] = 0x81;
  descriptor[2] = descriptor[3] = descriptor[4] = 0;
  sound = BB_U32(0x6bb188);
  if (API4(method(sound, 0xc), sound, (uintptr_t)descriptor, 0x6bb18c, 0))
    return 0;
#ifdef BB_MODEL_RUNTIME
  MOVIE_LOCAL(unsigned char, format, 18, 0);
#else
  unsigned char format[18] = {0};
#endif
  uintptr_t f = (uintptr_t)format;
  WORD(f) = 1;
  WORD(f + 2) = (caps[1] & 2) ? 2 : 1;
  BB_U32(f + 4) = 22050;
  WORD(f + 14) = (caps[1] & 8) ? 16 : 8;
  WORD(f + 12) = (WORD(f + 14) >> 3) * WORD(f + 2);
  BB_U32(f + 8) = (uint32_t)WORD(f + 12) * 22050;
  uintptr_t primary = BB_U32(0x6bb18c);
  if (API2(method(primary, 0x38), primary, f)) {
    release(BB_U32(0x6bb18c));
    BB_U32(0x6bb18c) = 0;
    return 1;
  }
#ifdef BB_MODEL_RUNTIME
  MOVIE_LOCAL(bb_memory_u32, primary_caps, 5, 20, 0, 0, 0, 0);
#else
  uint32_t primary_caps[5] = {20, 0, 0, 0, 0};
#endif
  primary = BB_U32(0x6bb18c);
  API2(method(primary, 0xc), primary, (uintptr_t)primary_caps);
  BB_U32(0x6bb194) = 1;
  BB_U32(0x6bb184) = primary_caps[2];
  return 0;
}
uint32_t BB_MOVIE_M bb_movie_interfaces_release_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (BB_U32(p + 0x3b4) & 0x400000) {
    if (BB_U32(0x6bb194) && BB_U32(0x6bb18c)) {
      release(BB_U32(0x6bb18c));
      BB_U32(0x6bb18c) = 0;
    }
    if (BB_U32(0x6bb190)) {
      if (BB_U32(0x6bb188))
        release(BB_U32(0x6bb188));
      BB_U32(0x6bb188) = 0;
    }
  }
  return 0;
}
/* Lock result slots are [second size, second pointer, first size, first
 * pointer]. */
static uint32_t lock_sound(uintptr_t sound, uint32_t position, uint32_t bytes,
                           bb_memory_u32 *parts) {
  return API8(method(sound, 0x2c), sound, position, bytes,
              (uintptr_t)(parts + 3), (uintptr_t)(parts + 2),
              (uintptr_t)(parts + 1), (uintptr_t)parts, 0);
}
static void unlock_sound(uintptr_t sound, const bb_memory_u32 *parts) {
  API5(method(sound, 0x4c), sound, parts[3], parts[2], parts[1], parts[0]);
}
uint32_t BB_MOVIE_M bb_movie_sound_start_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (BB_U32(0x6bb188) && BB_U32(0x6bb18c)) {
#ifdef BB_MODEL_RUNTIME
    MOVIE_LOCAL(unsigned char, format, 18, 0);
#else
    unsigned char format[18] = {0};
#endif
    uintptr_t f = (uintptr_t)format;
    WORD(f) = 1;
    WORD(f + 2) = 2;
    BB_U32(f + 4) = 22050;
    BB_U32(f + 8) = 88200;
    WORD(f + 12) = 4;
    WORD(f + 14) = 16;
#ifdef BB_MODEL_RUNTIME
    MOVIE_LOCAL(bb_memory_u32, descriptor, 5, 20, 0x100e0, BB_U32(0x6bb184), 0,
                MV_WORD(f));
#else
    uint32_t descriptor[5] = {20, 0x100e0, BB_U32(0x6bb184), 0, f};
#endif
    uintptr_t sound = BB_U32(0x6bb188);
    if (!API4(method(sound, 0xc), sound, (uintptr_t)descriptor, p + 0x410, 0)) {
      sound = BB_U32(p + 0x410);
      API2(method(sound, 0x44), sound, 22050);
#ifdef BB_MODEL_RUNTIME
      MOVIE_LOCAL(bb_memory_u32, caps, 5, 20, 0, 0, 0, 0);
      MOVIE_BUFFER(bb_memory_u32, parts, 4);
#else
      uint32_t caps[5] = {20, 0, 0, 0, 0}, parts[4];
#endif
      sound = BB_U32(p + 0x410);
      if (!API2(method(sound, 0xc), sound, (uintptr_t)caps)) {
        BB_U32(p + 0x420) = caps[2];
        BB_U32(p + 0x424) = 0;
        BB_U32(p + 0x450) = 0;
        sound = BB_U32(p + 0x410);
        if (!lock_sound(sound, BB_U32(p + 0x424), BB_U32(p + 0x420), parts)) {
          zero_bytes(parts[3], parts[2]);
          if (parts[0])
            zero_bytes(parts[1], parts[0]);
          unlock_sound(BB_U32(p + 0x410), parts);
          sound = BB_U32(p + 0x410);
          if (!API4(method(sound, 0x30), sound, 0, 0, 1)) {
            BB_U32(p + 0x41c) = 1;
            return 0;
          }
        } else
          release(BB_U32(p + 0x410));
      } else
        release(BB_U32(p + 0x410));
    }
  }
  BB_U32(p + 0x44c) = 0;
  BB_U32(p + 0x420) = 0x8000;
  BB_U32(p + 0x410) = 0;
  BB_U32(p + 0x424) = 0;
  BB_U32(p + 0x450) = 1;
  BB_U32(p + 0x41c) = 1;
  return 0;
}
uint32_t BB_MOVIE_M bb_movie_sound_release_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (BB_U32(p + 0x410)) {
    release(BB_U32(p + 0x410));
    BB_U32(p + 0x410) = 0;
  }
  return 0;
}

/* 0x45afc0 / 0x45b310 and callback 0x45b060. */
uint32_t BB_MOVIE_M bb_movie_timer_start_entry(void *object) {
  uint32_t p = bb_address_encode(object);
#ifdef BB_MODEL_RUNTIME
  MOVIE_BUFFER(bb_memory_u32, caps, 2);
#else
  uint32_t caps[2];
#endif
  BB_U32(p + 0x448) = 0;
  if (API2(BB_U32(0x6ca57c), (uintptr_t)caps, 8))
    return 1;
  uint32_t period = caps[0] > 1 ? caps[0] : 1;
  if (period >= caps[1])
    period = caps[1];
  BB_U32(p + 0x474) = period;
  API1(BB_U32(0x6ca578), period);
  exchange(p + 0x46c, 0);
#ifdef BB_MODEL_RUNTIME
  uint32_t id = MV5(BB_U32(0x6ca574), 10, BB_U32(p + 0x474), 0x45b060, p, 1);
#else
  uint32_t id = API5(BB_U32(0x6ca574), 10, BB_U32(p + 0x474),
                     (uintptr_t)bb_movie_timer_entry, p, 1);
#endif
  BB_U32(p + 0x470) = id;
  return id ? 0 : 2;
}
uint32_t BB_MOVIE_M bb_movie_timer_stop_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (!BB_U32(p + 0x470))
    return 0;
  for (unsigned i = 0; i < 500; ++i) {
    if (exchange(p + 0x46c, 1) != 1)
      break;
    API1(BB_U32(0x6ca388), 2);
  }
  if (API1(BB_U32(0x6ca580), BB_U32(p + 0x470)))
    return 1;
  BB_U32(p + 0x470) = 0;
  exchange(p + 0x46c, 0);
  API1(BB_U32(0x6ca560), BB_U32(p + 0x474));
  return 0;
}
void W bb_movie_timer_entry(uint32_t id, uint32_t message, void *object,
                            uint32_t a, uint32_t b) {
  (void)id;
  (void)message;
  (void)a;
  (void)b;
  uint32_t p = bb_address_encode(object);
  if (exchange(p + 0x46c, 1))
    return;
  if (BB_U32(p + 0x41c)) {
    uint32_t cursor;
    if (BB_U32(p + 0x450)) {
      BB_U32(p + 0x44c) = BB_U32(p + 0x44c) + 882;
      cursor = BB_U32(p + 0x44c);
      if (BB_U32(p + 0x44c) >= BB_U32(p + 0x420))
        BB_U32(p + 0x44c) = cursor - BB_U32(p + 0x420);
      cursor = BB_U32(p + 0x44c);
    } else {
#ifdef BB_MODEL_RUNTIME
      MOVIE_BUFFER(bb_memory_u32, cursors, 2);
      uint32_t sound = BB_U32(p + 0x410);
      MV3(method(sound, 0x10), sound, cursors, cursors + 1);
      cursor = cursors[0];
#else
      uint32_t write_cursor;
      uintptr_t sound = BB_U32(p + 0x410);
      API3(method(sound, 0x10), sound, (uintptr_t)&cursor,
           (uintptr_t)&write_cursor);
#endif
    }
    uint32_t position = BB_U32(p + 0x424);
    uint32_t available = cursor - position;
    if (position > cursor)
      available += BB_U32(p + 0x420);
    uint32_t slot = p + 0x1c + (BB_U32(p + 0x428) & 31) * 12;
    if ((int32_t)available >= BB_I32(slot + 4)) {
      if (BB_U32(p + 0x43c)) {
        if (!BB_U32(p + 0x450)) {
#ifdef BB_MODEL_RUNTIME
          MOVIE_BUFFER(bb_memory_u32, parts, 4);
#else
          uint32_t parts[4];
#endif
          if (!lock_sound(BB_U32(p + 0x410), BB_U32(p + 0x424),
                          BB_U32(slot + 4), parts)) {
            copy_bytes(parts[3], BB_U32(slot), parts[2]);
            if (parts[0])
              copy_bytes(parts[1], BB_U32(slot) + parts[2], parts[0]);
            unlock_sound(BB_U32(p + 0x410), parts);
          }
        }
        BB_U32(p + 0x424) =
            (BB_U32(p + 0x424) + BB_U32(slot + 4)) % BB_U32(p + 0x420);
        increment(p + 0x428);
        decrement(p + 0x43c);
        BB_U32(p + 0x418) = BB_U32(slot + 8);
        BB_U32(slot) = 0;
        BB_U32(slot + 4) = 0;
      } else {
        if (!BB_U32(p + 0x450)) {
#ifdef BB_MODEL_RUNTIME
          MOVIE_BUFFER(bb_memory_u32, parts, 4);
#else
          uint32_t parts[4];
#endif
          if (!lock_sound(BB_U32(p + 0x410), BB_U32(p + 0x424), available,
                          parts)) {
            zero_bytes(parts[3], parts[2]);
            if (parts[0])
              zero_bytes(parts[1], parts[0]);
            unlock_sound(BB_U32(p + 0x410), parts);
          }
        }
        BB_U32(p + 0x424) = (available + BB_U32(p + 0x424)) % BB_U32(p + 0x420);
        increment(p + 0x418);
      }
    }
  }
  BB_U32(p + 0x448) = BB_U32(p + 0x448) + 1;
  exchange(p + 0x46c, 0);
}

/* 0x45a740: version gate, context copy, file/MMIO allocation and prebuffering.
 */
void *bb_movie_open_entry(const char *path, uint32_t window,
                          const uint32_t *context) {
  uintptr_t name = BB_U32(0x488d34), expected = 0x488d3c;
  while (BYTE(name) == BYTE(expected)) {
    if (!BYTE(name))
      break;
    ++name;
    ++expected;
  }
  if (BYTE(name) != BYTE(expected))
    return NULL;
#ifdef BB_MODEL_RUNTIME
#define allocate(count, bytes) ((void *)(uintptr_t)MV2(0x46daa0, count, bytes))
  uint32_t p = bb_address_encode(allocate(1, 0x7d8));
#else
  typedef void *(*calloc_fn)(uint32_t, uint32_t);
  calloc_fn allocate = (calloc_fn)BB_CODE(0x46daa0);
  uintptr_t p = (uintptr_t)allocate(1, 0x7d8);
#endif
  if (!p)
    return NULL;
  copy_bytes(p + 0x3b4, bb_address_encode(context), 44);
  BB_U32(p + 0x404) = BB_U32(p + 0x3c0);
  BB_U32(p + 0x408) = BB_U32(p + 0x3c4);
  BB_U32(p + 0x40c) = BB_U32(p + 0x3c8);
  BB_U32(p + 0x400) = window;
  BB_U32(p + 0x3a8) = 0;
  BB_U32(p + 0x39c) = 0;
  BB_U32(p + 0x3a0) = 0;
  BB_U32(p + 0x414) = 0;
#ifdef BB_MODEL_RUNTIME
  void *file = bb_game_open(path, 0x48416c);
#else
  void *file = ((void *(*)(const char *, const char *))BB_CODE(0x46bdd0))(
      path, PTR(0x48416c));
#endif
  if (file) {
    uint32_t tag = bb_address_encode(context);
#ifdef BB_MODEL_RUNTIME
    bb_game_read(&tag, 4, 1, file, 0);
    bb_game_close(file);
#else
    ((uint32_t (*)(void *, uint32_t, uint32_t, void *))BB_CODE(0x46cf40))(
        &tag, 4, 1, file);
    ((uint32_t (*)(void *))BB_CODE(0x46bbd0))(file);
#endif
    BB_U32(p + 0x3ac) =
        tag == 0x44414553 || tag == 0x6c484353 || tag == 0x684e5331;
  }
  bb_movie_sound_setup_entry(PTR(p));
  uint32_t mmio = API3(BB_U32(0x6ca56c), (uintptr_t)path, 0, 0);
  BB_U32(p) = mmio;
  if (mmio && !API4(BB_U32(0x6ca568), mmio, 0, 0x4000, 0)) {
    for (unsigned i = 0; i < 32; ++i) {
      BB_U32(p + 0x1c + i * 12) = 0;
      BB_U32(p + 0x19c + i * 16) = 0;
    }
    uint32_t arena = (uintptr_t)allocate(1, 500000);
    BB_U32(p + 0xc) = arena;
    BB_U32(p + 0x14) = arena;
    if (arena) {
      arena = (uintptr_t)allocate(1, 150000);
      BB_U32(p + 0x10) = arena;
      BB_U32(p + 0x18) = arena;
      if (arena) {
        BB_U32(p + 0x430) = 0;
        BB_U32(p + 0x440) = 0;
        BB_U32(p + 0x444) = 0;
        BB_U32(p + 0x434) = 0;
        BB_U32(p + 0x438) = 0;
        BB_U32(p + 0x43c) = 0;
        BB_U32(0x488d38) = 4;
        uint32_t counter = p + (BB_U32(p + 0x3ac) ? 0x43c : 0x438);
        while (BB_U32(counter) < 16)
          if (bb_movie_chunk_entry(PTR(p)) == UINT32_MAX)
            break;
        bb_movie_frame_setup_entry(PTR(p));
        return PTR(p);
      }
    }
  }
  free_memory(p);
  return NULL;
}

/* 0x45c150/0x45c540/0x45d060/0x45d080. */
void BB_MOVIE_M bb_movie_present_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t mode = BB_U32(p + 0x3fc);
  if (mode == 16 || mode == 15)
    bb_movie_present_16_entry(object);
  else
    bb_movie_present_32_entry(object);
}
void BB_MOVIE_M bb_movie_buffers_swap_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t a = BB_U32(p + 0x39c);
  BB_U32(p + 0x39c) = BB_U32(p + 0x3a0);
  BB_U32(p + 0x3a0) = a;
}
uint32_t BB_MOVIE_M bb_movie_bits_entry(uint32_t bits) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 31; ++i)
    if (bits & (1u << i))
      ++n;
  return n;
}
void BB_MOVIE_M bb_movie_buffers_release_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (BB_U32(p + 0x3a8)) {
    const uint32_t offsets[] = {0x39c, 0x3a0, 0x4bc, 0x4c0, 0x4c4,
                                0x4ac, 0x4b0, 0x4b8, 0x4b4};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i)
      if (BB_U32(p + offsets[i]))
        free_memory(BB_U32(p + offsets[i]));
    BB_U32(p + 0x3a8) = 0;
  }
}

uint32_t bb_movie_run_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t stop = 0, delay = 0;
  exchange(p + 0x464, 1);
#ifdef BB_MODEL_RUNTIME
  uint32_t thread = MV0(BB_U32(0x6ca3c8));
#else
  uint32_t thread = ((uint32_t(W *)(void))BB_CODE(BB_POINTER(0x6ca3c8)))();
#endif
  API2(BB_U32(0x6ca3c4), thread, 1);
  if (!bb_movie_timer_start_entry(object)) {
    do {
      uint32_t slot = p + 0x19c + (BB_U32(p + 0x430) & 31) * 16;
      if (BB_U32(p + 0x3b4) & 0x20000)
#ifdef BB_MODEL_RUNTIME
        stop = MV0(BB_U32(p + 0x3dc));
#else
        stop = ((uint32_t (*)(void))BB_CODE(BB_POINTER(p + 0x3dc)))();
#endif
      if (!BB_U32(slot) || stop)
        stop = 1;
      else {
        uint32_t result = bb_movie_chunk_entry(object);
        if (result == 2 || (result == 1 && BB_U32(p + 0x3ac)))
          BB_U32(0x488d38) = bb_movie_chunk_entry(object);
        if (!BB_U32(p + 0x414) && BB_U32(p + 0x3ac)) {
          bb_movie_sound_start_entry(object);
          BB_U32(p + 0x418) = 0;
        }
        if (BB_U32(p + 0x3ac)) {
          uint32_t counter = BB_U32(p + 0x414), cursor = BB_U32(p + 0x418) - 5;
          while ((int32_t)(counter - cursor) >= 1) {
            API1(BB_U32(0x6ca388), 0);
            cursor = BB_U32(p + 0x418) - 5;
          }
          delay = (int32_t)(cursor - counter) < 0 ? 0 : cursor - counter;
        }
        if ((int32_t)delay <= 1) {
          bb_movie_frame_dispatch_entry(object, PTR(slot));
          bb_movie_present_entry(object);
        } else if (BB_U32(slot + 12) == 1 || BB_U32(slot + 12) == 2)
          bb_movie_frame_dispatch_entry(object, PTR(slot));
        bb_movie_buffers_swap_entry(object);
        BB_U32(slot) = 0;
        increment(p + 0x430);
        decrement(p + 0x438);
        increment(p + 0x414);
        if (BB_U32(slot + 8) == BB_U32(p + 0x440))
          stop = 1;
      }
    } while (!stop);
    bb_movie_timer_stop_entry(object);
  }
  bb_movie_sound_release_entry(object);
  exchange(p + 0x464, 0);
  return 0;
}
static uint32_t run_and_destroy(void *object) {
  uint32_t p = bb_address_encode(object);
  exchange(0x6bb180, 1);
  bb_movie_run_entry(object);
  if (BB_U32(p))
    API2(BB_U32(0x6ca570), BB_U32(p), 0);
  if (BB_U32(p + 0xc))
    free_memory(BB_U32(p + 0xc));
  if (BB_U32(p + 0x10))
    free_memory(BB_U32(p + 0x10));
  for (unsigned i = 0; i < 32; ++i) {
    uint32_t slot = p + 0x19c + i * 16;
    if (BB_U32(slot))
      BB_U32(slot) = 0;
  }
  if (BB_U32(p + 0x3ac))
    for (unsigned i = 0; i < 32; ++i) {
      uint32_t slot = p + 0x1c + i * 12;
      if (BB_U32(slot))
        BB_U32(slot) = 0;
    }
  bb_movie_interfaces_release_entry(object);
  bb_movie_buffers_release_entry(object);
  free_memory(p);
  exchange(0x6bb180, 0);
  return 0;
}
uint32_t bb_movie_worker_entry(void *object) { return run_and_destroy(object); }
#ifdef BB_MODEL_RUNTIME

#else
static uint32_t W movie_worker_win32(void *object) {
  return bb_movie_worker_entry(object);
}
#endif
uint32_t bb_movie_play_entry(const char *path, uint32_t window,
                             const uint32_t *context) {
  void *object = bb_movie_open_entry(path, window, context);
  if (!object)
    return UINT32_MAX;
  uint32_t p = bb_address_encode(object);
  if (BB_U32(p + 0x3b4) & 0x10000) {
#ifdef BB_MODEL_RUNTIME
    uint32_t thread = MV6(BB_U32(0x6ca3cc), 0, 0, 0x45ac60, p, 0, p + 0x3d4);
#else
    uint32_t thread = API6(BB_U32(0x6ca3cc), 0, 0,
                           (uintptr_t)movie_worker_win32, p, 0, p + 0x3d4);
#endif
    BB_U32(p + 0x3d8) = thread;
    return thread ? p : UINT32_MAX - 1;
  }
  return run_and_destroy(object);
}
