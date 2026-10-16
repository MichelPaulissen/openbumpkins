/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "bootstrap_resources.h"
#include "capture_video.h"
#include "input.h"
#include "interop.h"
#include "world_controls.h"
#include "session_controls.h"
#include "worker_runtime.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#include "model_buffer.h"
#endif
#include <string.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#define TEXT(a) ((const char *)(uintptr_t)(a))
#ifndef BB_MODEL_RUNTIME
typedef uint32_t (*c0)(void);
typedef uint32_t (*c1)(uint32_t);
typedef uint32_t (*c2)(uint32_t, uint32_t);
typedef uint32_t (*c4)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*c5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m0)(void *);
typedef uint32_t(BB_THISCALL * m1)(void *, uint32_t);
typedef uint32_t(BB_THISCALL * m2)(void *, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m3)(void *, uint32_t, uint32_t,
                                                 uint32_t);
typedef uint32_t(BB_THISCALL * m4)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m5)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t, uint32_t);
#define C0(a) ((c0)BB_CODE((uintptr_t)(a)))()
#define C1(a, x) ((c1)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x))
#define C2(a, x, y)                                                            \
  ((c2)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x), (uint32_t)(uintptr_t)(y))
#define C4(a, w, x, y, z) ((c4)BB_CODE((uintptr_t)(a)))(w, x, y, z)
#define C5(a, v, w, x, y, z) ((c5)BB_CODE((uintptr_t)(a)))(v, w, x, y, z)
#define M0(a, p) ((m0)BB_CODE((uintptr_t)(a)))(PTR(p))
#define M1(a, p, x) ((m1)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x))
#define M2(a, p, x, y)                                                         \
  ((m2)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),                       \
                       (uint32_t)(uintptr_t)(y))
#define M3(a, p, x, y, z) ((m3)BB_CODE((uintptr_t)(a)))(PTR(p), x, y, z)
#define M4(a, p, w, x, y, z) ((m4)BB_CODE((uintptr_t)(a)))(PTR(p), w, x, y, z)
#define M5(a, p, v, w, x, y, z) ((m5)BB_CODE((uintptr_t)(a)))(PTR(p), v, w, x, y, z)
typedef int (*format_fn)(char *, const char *, ...);
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#define PRINT(fmt, ...) FORMAT((char *)0x006033d8, TEXT(fmt), __VA_ARGS__)
#define FORMAT_TEXT(a) TEXT(a)
#else
#define C0(a) BB_GAME_CALL(a, 0)
#define C1(a, x) BB_GAME_CALL(a, 0, bb_game_word((uintptr_t)(x)))
#define C2(a, x, y) BB_GAME_CALL(a, 0, bb_game_word((uintptr_t)(x)), bb_game_word((uintptr_t)(y)))
#define C4(a, w, x, y, z) BB_GAME_CALL(a, 0, w, x, y, z)
#define C5(a, v, w, x, y, z) BB_GAME_CALL(a, 0, v, w, x, y, z)
#define M0(a, p) BB_GAME_CALL(a, bb_game_word((uintptr_t)(p)))
#define M1(a, p, x) BB_GAME_CALL(a, bb_game_word((uintptr_t)(p)), bb_game_word((uintptr_t)(x)))
#define M2(a, p, x, y) BB_GAME_CALL(a, bb_game_word((uintptr_t)(p)), bb_game_word((uintptr_t)(x)), bb_game_word((uintptr_t)(y)))
#define M3(a, p, x, y, z) BB_GAME_CALL(a, bb_game_word((uintptr_t)(p)), x, y, z)
#define M4(a, p, w, x, y, z) BB_GAME_CALL(a, bb_game_word((uintptr_t)(p)), w, x, y, z)
#define M5(a, p, v, w, x, y, z) BB_GAME_CALL(a, bb_game_word((uintptr_t)(p)), v, w, x, y, z)
#define PRINT(fmt, ...) BB_GAME_CALL(0x46bae0, 0, 0x6033d8, fmt, __VA_ARGS__)
#define FORMAT_TEXT(a) ((uint32_t)(uintptr_t)(a))
#endif
static size_t text_length(uintptr_t address) { return strlen(BB_DATA(address)); }
static uintptr_t faction(void) {
  return 0x00554fbc + BYTE(0x004843f0) * 0xd00u;
}
static uintptr_t tile(void) {
  return 0x00559b60 + (BB_U32(0x004846d4) * 192u + BB_U32(0x004846d0)) * 12u;
}
static void sample(uintptr_t slot) { C1(0x00415860, BB_U32(slot)); }
static void feedback(uintptr_t text) { M1(0x0041de40, 0x00662c98, text); }
static void feedback_buffer(void) { feedback(0x006033d8); }
static void format_only(uintptr_t format) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x46bae0, 0, 0x6033d8, format);
#else
  FORMAT((char *)0x006033d8, TEXT(format));
#endif
}
/* These string operations retain the original forward DWORD/byte copy order. */
static void copy(uintptr_t out, uintptr_t in, size_t n) {
  while (n >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    n -= 4;
  }
  while (n--) {
    BYTE(out++) = BYTE(in++);
  }
}
static void string_copy(uintptr_t out, uintptr_t in) {
  copy(out, in, text_length(in) + 1);
}
static void append(uintptr_t out, uintptr_t in) {
  size_t n = text_length(in) + 1;
  copy(out + text_length(out), in, n);
}
static void editor_cycle(void) {
  if (!BB_U32(0x004842a0))
    return;
  BYTE(0x004843e8) = (BYTE(0x004843e8) + 1) % 3;
  sample(0x004842d8);
  M0(0x0041ced0, BB_U32(0x004844a8));
  if (BYTE(0x004843e8) == 2)
    M2(0x004238a0, 0x005e47b0, 0, 0);
}
static void terrain_edit(uint32_t kind, uint32_t x, uint32_t y) {
  M3(0x0042dae0, faction(), kind, x, y);
}
static void entity_edit(void) {
  M5(0x0042da90, faction(), BYTE(0x004843f0), BB_U32(0x0048441c),
     BB_U32(0x004846d0), BB_U32(0x004846d4), BB_U32(0x004846d8));
  sample(0x004842d4);
}
static void town_text(void) {
  uintptr_t p = M4(0x004341a0, 0x00553f90, BYTE(0x004843f0), 0x62,
                   BB_U32(0x004846d0), BB_U32(0x004846d4));
  if (p)
    M2(0x0042dca0, faction(), BB_U32(p + 0x8e), 0x005e5cd0);
}
static void typing_key(uint32_t key) {
  if (key == 8) {
    size_t n = text_length(0x005e5cd0);
    if (n)
      BYTE(0x005e5cd0 + n - 1) = 0;
    if (BB_U32(0x004847b4) == 2)
      town_text();
  }
  if (key == 0x23) {
    if (BB_U32(0x004847b4) == 1) {
      if (BYTE(0x005e47b0) == 200)
        M1(0x00424c80, 0x005e47b0, 0x005e5cd0);
      else {
        M1(0x0042db30, faction(), 0x005e5cd0);
        C1(0x0042b260, 0);
      }
    }
    string_copy(0x005e5cd0, 0x0048481c);
    BB_U32(0x004847b4) = 0;
  }
  if (key == 13) {
    BYTE(0x006033d8) = 10;
    BYTE(0x006033d9) = 0;
    append(0x005e5cd0, 0x006033d8);
    if (BB_U32(0x004847b4) == 2)
      town_text();
  }
}
static void inspect_entity(void) {
  uintptr_t p = BB_U32(0x00484488);
  if (!p)
    return;
  if (BYTE(p + 0xd4) == 0x67) {
    M1(0x00452ea0, p, 0x006033d8);
    feedback_buffer();
    PRINT(0x004848ac, BYTE(p + 0xce));
    feedback_buffer();
    PRINT(0x00484890, BYTE(p + 0x111), BYTE(p + 0x11b));
    feedback_buffer();
    uintptr_t target = BB_U32(p + 0x18);
    if (target) {
      PRINT(0x0048487c, FORMAT_TEXT(0x00663538 + BYTE(target + 0xd4) * 914u),
            BB_U32(target + 0x8e));
      feedback_buffer();
    }
    if (BYTE(p + 0xf6)) {
      PRINT(0x00484870, BYTE(p + 0xf6));
      feedback_buffer();
    }
    PRINT(0x00484840, SHORT(p + 0xa0), SHORT(p + 0xa2), BYTE(p + 0x109),
          BYTE(p + 0x10a), BYTE(p + 0x10b), BYTE(p + 0x10c), BYTE(p + 0x10d),
          BYTE(p + 0x10e));
  } else
    PRINT(0x00484838, FORMAT_TEXT(0x00663538 + BYTE(p + 0xd4) * 914u),
          BB_U32(p + 0x8e));
  feedback_buffer();
  uintptr_t t = tile();
  PRINT(0x00484820, BB_U32(t + 8) & 1u,
        !!(BYTE(t + 4) & (1u << (BYTE(0x004843f0) & 31))));
  feedback_buffer();
}
static void editor_key(uint32_t key) {
  if (key >= 0x30 && key <= 0x39 && BB_I32(0x00484418) > 0) {
    uint32_t digit = key - 0x30;
    switch (BB_U32(0x00484418)) {
    case 1:
      BB_U32(0x00484418) = 2;
      BB_U32(0x0048441c) = digit * 100u;
      break;
    case 2:
      BB_U32(0x00484418) = 3;
      BB_U32(0x0048441c) += digit * 10u;
      break;
    case 3:
      BB_U32(0x0048441c) += digit;
      if (!BB_U32(0x00484420)) {
        int32_t count = BB_I32(BB_U32(0x006aff40) + 0x4c);
        if (count <= BB_I32(0x0048441c))
          BB_U32(0x0048441c) = (uint32_t)count - 1u;
        terrain_edit(BB_U32(0x0048441c), BB_U32(0x004846d0),
                     BB_U32(0x004846d4));
      } else
        entity_edit();
      BB_U32(0x00484418) = 0;
      break;
    }
  }
  uintptr_t p;
  switch (key) {
  case 9: {
    uint32_t row = BB_U32(0x00484428), offset = row * 192u;
    if ((int32_t)row > BB_I32(0x00484430))
      break;
    do {
      uint32_t col = BB_U32(0x00484424);
      if ((int32_t)col <= BB_I32(0x0048442c)) {
        uintptr_t t = 0x00559b60 + (offset + col) * 12u;
        do {
          uint8_t y = (uint8_t)row - BYTE(0x00484428) + BYTE(0x004846d4);
          uint8_t x = (uint8_t)col - BYTE(0x00484424) + BYTE(0x004846d0);
          uint32_t kind = BYTE(t);
          t += 12;
          ++col;
          terrain_edit(kind, x, y);
        } while ((int32_t)col <= BB_I32(0x0048442c));
      }
      offset += 192;
      ++row;
    } while ((int32_t)row <= BB_I32(0x00484430));
    break;
  }
  case 0x20:
    inspect_entity();
    break;
  case 0x23:
    BB_U32(0x0048442c) = BB_U32(0x004846d0);
    BB_U32(0x00484430) = BB_U32(0x004846d4);
    break;
  case 0x24:
    BB_U32(0x00484424) = BB_U32(0x004846d0);
    BB_U32(0x00484428) = BB_U32(0x004846d4);
    break;
  case 0x2d:
    BB_U32(0x00484418) = 1;
    BB_U32(0x00484420) = 0;
    break;
  case 0x2e:
    M2(0x004344b0, 0x00553f90, BB_U32(0x004846d0), BB_U32(0x004846d4));
    if (BYTE(tile() + 1))
      C2(0x0041abe0, BB_U32(0x004846d0), BB_U32(0x004846d4));
    break;
  case 0x41:
    p = M2(0x00433e20, 0x00553f90, BB_U32(0x004846d0), BB_U32(0x004846d4));
    if (p)
      M2(0x00435070, 0x00553f90, ((int32_t)(int8_t)BYTE(p + 0xf7) + 1) % 4, p);
    break;
  case 0x43:
    if (!BB_U32(0x00484738)) {
      if (BB_U32(0x00484420) == 1) {
        BB_U32(0x00484420) = 1;
        entity_edit();
        BB_U32(0x00484418) = 0;
      }
    } else if (BB_U32(0x00484488)) {
      int32_t value = bb_random() % 6;
      BYTE(BB_U32(0x00484488) + 0x115) = (uint8_t)value;
      BYTE(BB_U32(0x00484488) + 0x116) = 10;
      static const uintptr_t crimes[] = {0x00484914, 0x0048491c, 0x00484928,
                                         0x00484940, 0x00484938, 0x00484930};
      if (BYTE(BB_U32(0x00484488) + 0x115) < 6)
        feedback(crimes[BYTE(BB_U32(0x00484488) + 0x115)]);
    }
    break;
  case 0x44:
    if (BB_U32(0x0048473c)) {
      BB_U32(0x0048474c) = !BB_U32(0x0048474c);
      sample(0x004842dc);
    } else if (!BB_U32(0x00484738))
      M0(0x0042f200, 0x004eebc8);
    else if (BB_U32(0x00484488)) {
      int32_t value = bb_random() % 4;
      BYTE(BB_U32(0x00484488) + 0x11e) = (uint8_t)value;
    }
    break;
  case 0x45:
    p = M2(0x00433e20, 0x00553f90, BB_U32(0x004846d0), BB_U32(0x004846d4));
    if (p && BYTE(p + 0xd4) == 0x53) {
      BYTE(p + 0xf4) = (BYTE(p + 0xf4) + 1) % 7;
      M0(0x00437ee0, p);
    }
    break;
  case 0x46:
    if (!BB_U32(0x00484738))
      BB_U32(0x00484268) = !BB_U32(0x00484268);
    else
      {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(0x435180, 0x553f90);
#else
      bb_entity_depth_check_entry(PTR(0x00553f90));
#endif
    }
    break;
  case 0x47:
    if (BB_U32(0x00533480) == 1)
      M2(0x0041aef0, 0x00554fb8, BYTE(0x004843f0), 10000);
    break;
  case 0x48:
    {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(0x4214f0, 0x0, BB_U32(0x004846d0), BB_U32(0x004846d4));
#else
      bb_initial_buildings_entry(BB_U32(0x004846d0), BB_U32(0x004846d4));
#endif
    }
    break;
  case 0x49:
    bb_session_disc_check_entry();
    break;
  case 0x4b:
    if (!BB_U32(0x00484738)) {
      feedback(0x00484a6c);
      M0(0x0042b8c0, 0x006025d0);
    } else {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(BB_U32(0x6ca374), 0, 0x484a84);
#else
      typedef void(BB_STDCALL * debug_fn)(const char *);
      ((debug_fn)BB_CODE((uintptr_t)BB_U32(0x006ca374)))(TEXT(0x00484a84));
#endif
      BB_U32(0x00484458) = 1;
    }
    break;
  case 0x4c:
    if (BB_U32(0x00484738)) {
      for (uintptr_t f = 0x00554fbc; f < 0x005590bc; f += 0xd00)
        M0(0x00429120, f);
      feedback(0x00484a4c);
      for (uint32_t offset = 0; offset < 5000u * 314u; offset += 314u) {
        uintptr_t node = BB_U32(0x00553f90) + offset;
        if (BB_U32(node) &&
            (BYTE(node + 0xd4) == 0x67 || BYTE(node + 0xd4) == 0x68))
          M1(0x00437f30, node, BYTE(node + 0xf4));
      }
    } else if (BB_U32(0x0048473c)) {
      BB_U32(0x004842a8) = !BB_U32(0x004842a8);
      C0(0x00403190);
    } else
      BB_U32(0x00484758) = !BB_U32(0x00484758);
    break;
  case 0x4d:
    if (!BB_U32(0x00484738))
      M5(0x00433ba0, 0x00553f90, 4, 0x53, (BB_U32(0x004846d0) << 16) + 0x8000u,
         (BB_U32(0x004846d4) << 16) + 0x8000u, BB_U32(0x004846d8) << 16);
    else {
      p = M5(0x00433ba0, 0x00553f90, BYTE(0x004843f0), 0x67,
             BB_U32(0x004846d0) << 16, BB_U32(0x004846d4) << 16,
             BB_U32(0x004846d8) << 16);
      M2(0x00452f80, p, 0, 0);
      M0(0x00452c40, p);
      BYTE(p + 0xce) = 11;
    }
    break;
  case 0x4e:
    BB_U32(0x004847b4) = 2;
    BB_U32(0x004847b8) = 1;
    break;
  case 0x4f:
    BB_U32(0x00484418) = 1;
    BB_U32(0x00484420) = 1;
    break;
  case 0x50:
    BYTE(0x004843f0) = BB_U32(0x00484738) ? 4 : (BYTE(0x004843f0) + 1) & 3;
    break;
  case 0x52:
    if (BB_U32(0x00484738))
      C2(0x00415ed0, 0x0048494c, 0);
    else if (BB_I32(0x00484424) < BB_I32(0x0048442c) &&
             BB_I32(0x00484428) < BB_I32(0x00484430)) {
      uintptr_t offset = BYTE(0x004843f0) * 0xd00u;
      BB_U32(0x00555bf4 + offset) = BB_U32(0x00484424);
      BB_U32(0x00555bf8 + offset) = BB_U32(0x00484428);
      BB_U32(0x00555bfc + offset) = BB_U32(0x0048442c);
      BB_U32(0x00555c00 + offset) = BB_U32(0x00484430);
      sample(0x004842dc);
    } else
      sample(0x004842e0);
    break;
  case 0x53:
    p = BB_U32(0x00484488);
    if (p) {
      if (!BB_U32(0x00484738)) {
        uint32_t part = BYTE(p + 0xea);
        if (part < BB_U32(BB_U32(p + 0x58) + 0x4c)) {
          BYTE(p + 0xea) = part + 1;
          M0(0x00437ee0, p);
        }
      } else if (BYTE(p + 0xea)) {
        --BYTE(p + 0xea);
        M0(0x00437ee0, p);
      }
    }
    break;
  case 0x54:
    if (BYTE(0x0055540d)) {
      p = BB_U32(0x00553f90) + BB_U32(0x0055540e) * 314u;
      static const uint32_t edges[][2] = {
          {2, 2}, {188, 188}, {2, 188}, {188, 2}};
      int escaped = 0;
      for (unsigned i = 0; i < 4; ++i) {
        M2(0x0043fb50, p, edges[i][0], edges[i][1]);
        if (BYTE(p + 0xdd) == edges[i][0] && BYTE(p + 0xde) == edges[i][1]) {
          escaped = 1;
          break;
        }
      }
      if (escaped) {
        sample(0x004842e0);
        M3(0x0043fae0, p, BYTE(p + 0xdd), BYTE(p + 0xde), 0);
        format_only(0x004848dc);
      } else
        format_only(0x004848b4);
      feedback_buffer();
    }
    break;
  case 0x55:
    if (BB_U32(0x00484738))
      bb_display_flush_entry(PTR(0x004eebc8));
    else
      bb_display_present_wait_entry(PTR(0x004eebc8));
    break;
  case 0x56:
    p = M2(0x00433e20, 0x00553f90, BB_U32(0x004846d0), BB_U32(0x004846d4));
    if (p)
      M2(0x00435070, 0x00553f90, 4, p);
    break;
  case 0x60:
    sample(0x004842d8);
    {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(0x431af0, 0x4eebc8, 0x484aa0);
#else
      bb_screenshot_entry(PTR(0x004eebc8), PTR(0x00484aa0));
#endif
    }
    C1(0x00415730, BB_U32(0x006b0838));
    break;
  case 0x6a:
    terrain_edit(BB_U32(0x0048441c), BB_U32(0x004846d0), BB_U32(0x004846d4));
    break;
  case 0x6b:
    if (!BB_U32(0x00484738)) {
      uint32_t value = BYTE(tile());
      int32_t limit = (int32_t)(BB_U32(BB_U32(0x006aff40) + 0x4c) - 1u);
      if ((int32_t)value < limit)
        terrain_edit(value + 1, BB_U32(0x004846d0), BB_U32(0x004846d4));
      BB_U32(0x0048441c) = BYTE(tile()) + 1;
    } else {
      M1(0x00421670, 0x005e470c, (uint16_t)(WORD(0x005e470c) * 2 + 1));
      PRINT(0x00484904, SHORT(0x005e470c));
      feedback_buffer();
    }
    break;
  case 0x6d:
    if (!BB_U32(0x00484738)) {
      uint32_t value = BYTE(tile());
      if (value)
        terrain_edit(value - 1, BB_U32(0x004846d0), BB_U32(0x004846d4));
      BB_U32(0x0048441c) = BYTE(tile()) - 1u;
    } else {
      M1(0x00421670, 0x005e470c, SHORT(0x005e470c) / 2);
      PRINT(0x00484904, SHORT(0x005e470c));
      feedback_buffer();
    }
    break;
  case 0x6f:
    BB_U32(0x0048441c) = BYTE(tile());
    break;
  case 0x71:
    format_only(0x00484a40);
    feedback_buffer();
    PRINT(0x00484a24, BB_U32(0x00554fa0), 5000, 314);
    feedback_buffer();
    PRINT(0x00484a10, BB_U32(0x00484750));
    feedback_buffer();
    PRINT(0x004849fc, BB_U32(0x00484454));
    feedback_buffer();
    break;
  case 0x72:
    format_only(0x004849ec);
    feedback_buffer();
    PRINT(0x004849d8, BB_U32(0x00533480));
    feedback_buffer();
    PRINT(0x004849c8, BYTE(0x004842b8));
    feedback_buffer();
    for (unsigned i = 0; i < 5; ++i) {
      uintptr_t f = 0x00554fc0 + i * 0xd00u;
      PRINT(0x004849b8, i, FORMAT_TEXT(f), BB_U32(f + 0x46));
      feedback_buffer();
    }
    break;
  case 0x73:
    if (!BB_U32(0x0048475c)) {
      format_only(0x00484994);
      feedback_buffer();
      BB_U32(0x0048475c) = 1;
      M0(0x00420c90, 0x00642c48);
    } else {
      M1(0x00420b30, 0x00642c48, 0x004849ac);
      format_only(0x004849a0);
      feedback_buffer();
      BB_U32(0x0048475c) = 0;
    }
    break;
  case 0x74:
    if (!BB_U32(0x00484760)) {
      M1(0x00420a90, 0x00642c48, 0x004849ac);
      BB_U32(0x00484760) = 1;
      format_only(0x00484970);
      feedback_buffer();
      M0(0x00420ca0, 0x00642c48);
    } else {
      format_only(0x00484980);
      feedback_buffer();
      BB_U32(0x00484760) = 0;
    }
    break;
  case 0x75:
    if (BB_U32(0x00484738))
      M0(0x00421690, 0x005e470c);
    else
      M0(0x004216e0, 0x005e470c);
    break;
  case 0x76:
    PRINT(0x00484958, BB_U32(0x006024b8), BB_U32(0x00602484));
    feedback_buffer();
    break;
  case 0x7a:
    editor_cycle();
    break;
  case 0xbb:
    if (BB_U32(0x004842a8)) {
      ++BYTE(tile() + 6);
      C0(0x00403190);
    }
    break;
  case 0xbd:
    if (BB_U32(0x004842a8)) {
      --BYTE(tile() + 6);
      C0(0x00403190);
    }
    break;
  }
}
static void normal_key(uint32_t key) {
  if (key >= 0x30 && key <= 0x39) {
    if (BB_U32(0x00484738))
      M1(0x00427880, BB_U32(0x004844a8), key - 0x30);
    else {
      int shift = !!BB_U32(0x0048473c);
      M1(0x00427930, BB_U32(0x004844a8), key - 0x30);
      if (!shift)
        C1(0x00415730, BB_U32(0x006b0838));
      else {
        uintptr_t f = BB_U32(0x004844a8);
        if (BYTE(f + 0x451))
          C1(0x0041ad70, BB_U32(f + 0x452) * 314u + BB_U32(0x00553f90));
      }
    }
  }
  switch (key) {
  case 0x25:
    BB_U32(0x00484528) = 1;
    break;
  case 0x26:
    BB_U32(0x00484520) = 1;
    break;
  case 0x27:
    BB_U32(0x0048452c) = 1;
    break;
  case 0x28:
    BB_U32(0x00484524) = 1;
    break;
  case 0x7a:
    editor_cycle();
    break;
  }
}
static void character(uint32_t key) {
  if (BB_I32(0x004847b4) < 1 || BB_I32(0x004847b4) > 2) {
    BYTE(0x006033d8) = key;
    BYTE(0x006033d9) = 0;
    if (text_length(0x005d1960) >= 17)
      string_copy(0x005d1960, 0x005d1961);
    append(0x005d1960, 0x006033d8);
  } else if (BB_U32(0x004847b8))
    --BB_U32(0x004847b8);
  else if (key >= 32 && key < 127) {
    BYTE(0x006033d8) = key;
    BYTE(0x006033d9) = 0;
    if (text_length(0x005e5cd0) < 240)
      append(0x005e5cd0, 0x006033d8);
    if (BB_U32(0x004847b4) == 2)
      town_text();
  }
}
static void record_event(uint32_t message, uint32_t key, uint32_t data) {
  if (BB_U32(0x0048475c))
    M3(0x00420bc0, 0x00642c48, message, key, data);
}
/* Return true only for the window handler's early zero return. */
static int key_down(uint32_t key, uint32_t data) {
  record_event(0x100, key, data);
  if (BB_U32(0x005ca308)) {
    M1(0x0041eab0, 0x005ca308, key);
    if (!BB_U32(0x004847b4))
      return 0;
  }
  if (!BB_U32(0x004847b4)) {
    if (BB_U32(0x00484458)) {
      BB_U32(0x00484458) = key;
      PRINT(0x00484aa4, key);
      feedback_buffer();
      BB_U32(0x00484458) = 0;
    }
    switch (BYTE(0x004843e8)) {
    case 0:
      normal_key(key);
      break;
    case 1:
      editor_key(key);
      break;
    case 2:
      M1(0x004268d0, 0x005e47b0, key);
      break;
    }
  } else if (BB_I32(0x004847b4) > 0 && BB_I32(0x004847b4) < 3)
    typing_key(key);
  switch (key) {
  case 0x10:
    BB_U32(0x0048473c) = 1;
    break;
  case 0x11:
    BB_U32(0x00484738) = 1;
    break;
  case 0x13:
    bb_session_pause_tick_entry();
    break;
  case 0x1b:
  case 0x7b:
    if (!BB_U32(0x005ca308) && BB_U32(0x0048426c) == 2 &&
        BB_U32(0x00484274) == 2) {
      if (!BB_U32(0x0048443c)) {
        sample(0x004842e4);
        M1(0x0041ffc0, 0x005ca308, 1);
        M1(0x0041ed10, 0x005ca308, 5);
      } else if (BB_U32(0x0048443c) == 3) {
        sample(0x004842e4);
        C1(0x0042b260, 0);
      } else if (BB_U32(0x0048443c) == 4)
        M0(0x0042b400, 0x006a9950);
    }
    return 1;
  case 0x50:
    if (!BYTE(0x004843e8))
      bb_session_pause_tick_entry();
    break;
  }
  return 0;
}
static void left_down(uint32_t message, uint32_t key, uint32_t data) {
  record_event(message, key, data);
  if (BB_U32(0x005ca308))
    M2(0x0041ec60, 0x005ca308, BB_U32(0x004846b8), BB_U32(0x004846bc));
  else {
    if (BB_U32(0x0048443c) == 4) {
      M0(0x0042b400, 0x006a9950);
      goto finish;
    }
    static const struct {
      uintptr_t entry, object;
    } controls[] = {{0x00419d80, 0x005d19a0}, {0x0041e6a0, 0x005ecf38},
                    {0x0041c370, 0x00640ce8}, {0x0041a7b0, 0x005f90d8},
                    {0x00427290, 0x006b08c0}, {0x0042a7e0, 0x006a8ef8},
                    {0x0042c480, 0x006025d0}, {0x00422390, 0x005db050},
                    {0x0041b710, 0x006b0858},
#ifdef BB_MODEL_RUNTIME
                    {0x0042a480, 0x006a98e0},
#else
                    {(uintptr_t)bb_stock_panel_click_entry, 0x006a98e0},
#endif
                    {0x004295b0, 0x005eac80}};
    for (unsigned i = 0; i < sizeof(controls) / sizeof(*controls); ++i)
      if (M2(controls[i].entry, controls[i].object, BB_U32(0x004846b8),
             BB_U32(0x004846bc)))
        goto finish;
    if (BB_U32(0x00484700)) {
      M0(0x00427810, BB_U32(0x004844a8));
      BYTE(0x005c5b64) = 0;
      BB_U32(0x0063f07c) = 0;
      return;
    }
    if (BB_U32(0x0048443c))
      goto finish;
    if (BYTE(0x005e4740)) {
      M3(0x0041b5d0, 0x006b0858, BYTE(0x005e4740), BB_U32(0x004846d0),
         BB_U32(0x004846d4));
      goto finish;
    }
    if (!BYTE(0x005c5b64)) {
      BB_U32(0x0048470c) = BB_U32(0x004846b8);
      BB_U32(0x00484710) = BB_U32(0x004846bc);
      BB_U32(0x00484714) = BB_U32(0x004846b8);
      BB_U32(0x00484718) = BB_U32(0x004846bc);
      BB_U32(0x00484704) = 1;
      goto finish;
    }
    C4(0x00403950, BYTE(0x004843f0), BYTE(0x005c5b64), BB_U32(0x004846d0),
       BB_U32(0x004846d4));
    BB_U32(0x00484728) = C4(0x004033f0, BYTE(0x004843f0), BYTE(0x005c5b64),
                            BB_U32(0x004846d0), BB_U32(0x004846d4));
    if (BB_U32(0x00484728) || BYTE(0x004843e8) == 1) {
      if (BB_I32(BB_U32(0x004844a8) + 0x4a) <
          WORD(0x006638b8 + BYTE(0x005c5b64) * 914u))
        sample(0x004842e0);
      else {
        uint32_t kind = BYTE(0x005c5b64);
        if (BYTE(0x004843e8)) {
          uint32_t alternate = BYTE(0x006635bc + kind * 914u);
          if (alternate)
            kind = alternate;
        }
        C5(0x0042e4e0, BYTE(0x004843f0), kind, BB_U32(0x004846d0),
           BB_U32(0x004846d4), BB_U32(0x004846d8));
        sample(0x004842d4);
        if (!BB_U32(0x006638be + BYTE(0x005c5b64) * 914u))
          BYTE(0x005c5b64) = 0;
      }
    } else {
      sample(0x004842e0);
      uintptr_t slot = 0;
      switch (BB_U32(0x0048472c)) {
      case 1:
        slot = 0x006034f0;
        break;
      case 2:
        slot = 0x006034ec;
        break;
      case 3:
        slot = 0x006034e8;
        break;
      case 4:
        slot = 0x006034f4;
        break;
      }
      if (slot) {
        uintptr_t text = BB_U32(slot);
        string_copy(0x006033d8, text ? text : 0x0048481c);
      }
      M2(0x0041e310, 0x005ecf38, 0x006033d8, 1);
    }
  }
finish:
  BB_U32(0x004846c8) = BB_U32(0x004846b8);
  BB_U32(0x004846cc) = BB_U32(0x004846bc);
  BB_U32(0x0048448c) = BB_U32(0x00484488);
}
static int32_t difference(uintptr_t first, uintptr_t second) {
  return (int32_t)(BB_U32(first) - BB_U32(second));
}
static int outside(int32_t value, int32_t limit) {
  return value > limit || value < -limit;
}
static void mouse_move(uint32_t key, uint32_t data) {
  record_event(0x200, key, data);
  BB_U32(0x004846b8) = data & 0xffff;
  BB_U32(0x004846bc) = data >> 16;
  if (BB_U32(0x0048426c) == 1)
    M2(0x0041ec90, 0x005ca308, BB_U32(0x004846b8), BB_U32(0x004846bc));
  else if (BB_U32(0x0048426c) == 2) {
    if (BB_U32(0x005ca308))
      M2(0x0041ec90, 0x005ca308, BB_U32(0x004846b8), BB_U32(0x004846bc));
    if (BB_U32(0x0048443c) == 2)
      M2(0x0042d1f0, 0x006025d0, BB_U32(0x004846b8), BB_U32(0x004846bc));
    else if (BB_U32(0x0048443c) == 3)
      M2(0x00422410, 0x005db050, BB_U32(0x004846b8), BB_U32(0x004846bc));
    BB_U32(0x00484280) = 0;
    BB_U32(0x00484284) = 0;
    BB_U32(0x0048427c) = 36;
    M0(0x0042b250, 0x00559128);
    if (!BB_U32(0x00484700)) {
      if (BB_U32(0x00484704) &&
          outside(difference(0x004846b8, 0x004846c8), 20) &&
          outside(difference(0x004846bc, 0x004846cc), 20)) {
        BB_U32(0x00484714) = BB_U32(0x004846b8);
        BB_U32(0x00484718) = BB_U32(0x004846bc);
        BB_U32(0x004846f0) = 0;
        BB_U32(0x004846f4) = 0;
        BB_U32(0x004846f8) = 0;
        BB_U32(0x004846fc) = 0;
        BB_U32(0x006a8f28) = BB_U32(0x006024b8);
        BB_U32(0x00484704) = 2;
        BB_U32(0x006a8f24) = BB_U32(0x00602484);
      }
    } else if (!BB_U32(0x00484708)) {
      int32_t dx = difference(0x004846b8, 0x004846c8);
      if (BB_U32(0x004846b8) != BB_U32(0x004846c8) && outside(dx, 3)) {
        BB_U32(0x00484700) = 2;
        long double v =
            (long double)dx * BB_DOUBLE(0x00482120) + BB_DOUBLE(0x004846f0);
        BB_DOUBLE(0x004846f0) = (double)v;
      }
      int32_t dy = difference(0x004846bc, 0x004846cc);
      if (BB_U32(0x004846bc) != BB_U32(0x004846cc) && outside(dy, 3)) {
        BB_U32(0x00484700) = 2;
        long double v =
            (long double)dy * BB_DOUBLE(0x00482120) + BB_DOUBLE(0x004846f8);
        BB_DOUBLE(0x004846f8) = (double)v;
      }
    } else if (BB_U32(0x00484708) == 1) {
      M1(0x0041a9e0, 0x005f90d8, difference(0x004846b8, 0x004846c8));
      BB_U32(0x00484700) = 2;
    } else if (BB_U32(0x00484708) == 2) {
      int32_t dy = difference(0x004846bc, 0x004846cc) / 4;
      int32_t x = (int32_t)((uint32_t)dy +
                            (uint32_t)(difference(0x004846b8, 0x004846c8) / 4));
      int32_t y = (int32_t)((uint32_t)dy +
                            (uint32_t)(difference(0x004846c8, 0x004846b8) / 4));
      BB_U32(0x00484478) = y < 0;
      BB_U32(0x0048447c) = y > 0;
      BB_U32(0x00484480) = x > 0;
      BB_U32(0x00484484) = x < 0;
      BB_U32(0x00484470) += (uint32_t)x;
      BB_U32(0x00484474) += (uint32_t)y;
      if (BB_I32(0x00484474) < 64)
        BB_U32(0x00484474) = 64;
      if ((int32_t)(BB_U32(0x00484474) + 64u) > 191)
        BB_U32(0x00484474) = 127;
      if (BB_I32(0x00484470) < 64)
        BB_U32(0x00484470) = 64;
      if ((int32_t)(BB_U32(0x00484470) + 64u) > 191)
        BB_U32(0x00484470) = 127;
    }
  }
  M3(0x004193f0, 0x006b0830, BB_U32(0x004846b8), BB_U32(0x004846bc), 0);
}
/* 00403b40: complete game window-message routing. Packed mouse coordinates
 * are zero-extended WORDs; key capture stores a WORD, not the adjacent DWORD.
 * The final native/default procedure's result and stdcall cleanup are defined.
 */
int32_t BB_STDCALL bb_game_window_entry(void *window,
                                                      uint32_t message,
                                                      uint32_t key,
                                                      uint32_t data) {
#ifndef BB_MODEL_RUNTIME
  typedef int32_t(BB_STDCALL * default_fn)(void *, uint32_t,
                                                         uint32_t, uint32_t);
  typedef void(BB_STDCALL * quit_fn)(int32_t);
  typedef uintptr_t(BB_STDCALL * paint_fn)(void *, void *);
  typedef int32_t(BB_STDCALL * cursor_fn)(uint32_t, uint32_t);
#endif
  if (message == 0x112 &&
      ((key & 0xfff0) == 0xf140 || (key & 0xfff0) == 0xf170))
    return 0;
  if (BB_U32(0x004847c0)) {
    if (message == 0x100) {
      WORD(0x004847c4) = key;
      return 0;
    }
    if (message == 0x201 || message == 0x204) {
      WORD(0x004847c4) = 1;
      return 0;
    }
    goto finish;
  }
  if (BB_U32(0x004844a0)) {
    switch (message) {
    case 2:
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(BB_U32(0x6ca510), 0, 0);
#else
      ((quit_fn)BB_CODE((uintptr_t)BB_U32(0x006ca510)))(0);
#endif
      break;
    case 15: {
      unsigned char paint[64] = {0};
#ifdef BB_MODEL_RUNTIME
      struct bb_model_buffer buffer;
      bb_model_buffer_begin(&buffer, paint, sizeof paint);
      BB_GAME_CALL(BB_U32(0x6ca514), 0, bb_address_encode(window), buffer.reference);
      BB_GAME_CALL(BB_U32(0x6ca54c), 0, bb_address_encode(window), buffer.reference);
      bb_model_buffer_end(&buffer, 0);
#else
      ((paint_fn)BB_CODE((uintptr_t)BB_U32(0x006ca514)))(window, paint);
      ((paint_fn)BB_CODE((uintptr_t)BB_U32(0x006ca54c)))(window, paint);
#endif
      return 1;
    }
    case 20:
      return 1;
    case 28:
      BB_U32(0x00484734) = key;
      if (!key && BB_U32(0x00484734)) {
        if (BB_U32(0x0048426c)) {
          M0(0x0042f200, 0x004eebc8);
          M0(0x00401e60, 0x005590e8);
          M0(0x00401ea0, 0x005590e8);
        }
        BB_U32(0x00484734) = 0;
      }
      break;
    case 0x102:
      if (BB_U32(0x005ca308) && BB_U32(0x005ca30e) == 15 && key >= 32 &&
          key < 127) {
        BYTE(0x006033d8) = key;
        BYTE(0x006033d9) = 0;
        if (text_length(0x006044e8) < 32)
          append(0x006044e8, 0x006033d8);
        else
          sample(0x004842e0);
      }
      break;
    case 0x112:
      if (key == 0xf100)
        return 0;
      break;
    }
    switch (BB_U32(0x0048426c)) {
    case 0:
      if (key == 0x100)
        BB_U32(0x00484274) = 1;
      break;
    case 1:
      if (message == 0x100)
        M1(0x0041eab0, 0x005ca308, key);
      else if (message == 0x201)
        M2(0x0041ec60, 0x005ca308, BB_U32(0x004846b8), BB_U32(0x004846bc));
      break;
    case 2:
      switch (message) {
      case 0x100:
        if (key_down(key, data))
          return 0;
        break;
      case 0x101:
        record_event(message, key, data);
        switch (key) {
        case 0x10:
          BB_U32(0x0048473c) = 0;
          break;
        case 0x11:
          BB_U32(0x00484738) = 0;
          break;
        case 0x25:
          BB_U32(0x00484528) = 0;
          break;
        case 0x26:
          BB_U32(0x00484520) = 0;
          break;
        case 0x27:
          BB_U32(0x0048452c) = 0;
          break;
        case 0x28:
          BB_U32(0x00484524) = 0;
          break;
        }
        break;
      case 0x102:
        character(key);
        break;
      case 0x203:
        BB_U32(0x0048471c) = 1;
        left_down(message, key, data);
        break;
      case 0x201:
        left_down(message, key, data);
        break;
      case 0x202:
        record_event(message, key, data);
        BB_U32(0x00484700) = 0;
        if (BB_U32(0x00484704) == 2) {
#ifdef BB_MODEL_RUNTIME
          BB_GAME_CALL(0x434af0, 0x553f90, BB_U32(0x48470c), BB_U32(0x484710),
                         BB_U32(0x484714), BB_U32(0x484718));
#else
          bb_entity_select_rect_entry(
              PTR(0x00553f90), BB_U32(0x0048470c), BB_U32(0x00484710),
              BB_U32(0x00484714), BB_U32(0x00484718));
#endif
          C0(0x00403190);
        } else if (BB_U32(0x00484704) == 1) {
          BYTE(0x005c5b64) = 0;
          BB_U32(0x00484414) = 0;
          C0(0x00420cc0);
        }
        BB_U32(0x00484704) = 0;
        BB_U32(0x0048471c) = 0;
        M0(0x0041a080, 0x005d19a0);
        if (BB_U32(0x0048443c) == 2)
          M0(0x0042d770, 0x006025d0);
        break;
      case 0x204:
      case 0x206:
        record_event(message, key, data);
        BB_U32(0x004847a4) = 0;
        M1(0x0041c490, 0x00640ce8, 0);
        BB_U32(0x00484708) =
            !BB_U32(0x0048443c)
                ? (int32_t)(BB_U32(0x005f90e8) - 5u) < BB_I32(0x004846bc)
                : 2;
        BB_U32(0x004846c8) = BB_U32(0x004846b8);
        BB_U32(0x004846cc) = BB_U32(0x004846bc);
        BB_U32(0x00484700) = 1;
        BB_U32(0x00484704) = 0;
        break;
      case 0x205:
        record_event(message, key, data);
        if (BB_U32(0x00484700) == 1) {
          M0(0x00427810, BB_U32(0x004844a8));
          BYTE(0x005c5b64) = 0;
        }
        if (BB_U32(0x00484700)) {
          BB_U32(0x00484700) = 0;
#ifdef BB_MODEL_RUNTIME
          BB_GAME_CALL(BB_U32(0x6ca550), 0, BB_U32(0x4846c8), BB_U32(0x4846cc));
#else
          ((cursor_fn)BB_CODE((uintptr_t)BB_U32(0x006ca550)))(BB_U32(0x004846c8),
                                                     BB_U32(0x004846cc));
#endif
        }
        BB_U32(0x00484704) = 0;
        break;
      }
      break;
    }
  }
  if (message == 0x200)
    mouse_move(key, data);
finish:
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(0x6ca554), 0, bb_address_encode(window), message, key, data);
#else
  return ((default_fn)BB_CODE((uintptr_t)BB_U32(0x006ca554)))(window, message, key,
                                                     data);
#endif
}
