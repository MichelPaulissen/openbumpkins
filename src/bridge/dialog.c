/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "bootstrap_resources.h"
#include "dialog.h"
#include "interop.h"
#include "session_controls.h"
#include <string.h>
#define BYTE(a) BB_U8(a)
#define TEXT(a) ((const char *)(uintptr_t)(a))
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#include "model_buffer.h"
#include "crt_output.h"
#define C0(id) BB_GAME_CALL(id,0)
#define C1(id,a0) BB_GAME_CALL(id,0,bb_game_word((uintptr_t)(a0)))
#define C2(id,a0,a1) BB_GAME_CALL(id,0,bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)))
#define C3(id,a0,a1,a2) BB_GAME_CALL(id,0,bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)))
#define C4(id,a0,a1,a2,a3) BB_GAME_CALL(id,0,bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)),bb_game_word((uintptr_t)(a3)))
#define M0(id,p) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)))
#define M1(id,p,a0) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)))
#define M2(id,p,a0,a1) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)))
#define M3(id,p,a0,a1,a2) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)))
#define M4(id,p,a0,a1,a2,a3) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)),bb_game_word((uintptr_t)(a3)))
#define M5(id,p,a0,a1,a2,a3,a4) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)),bb_game_word((uintptr_t)(a3)),bb_game_word((uintptr_t)(a4)))
#define M6(id,p,a0,a1,a2,a3,a4,a5) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)),bb_game_word((uintptr_t)(a3)),bb_game_word((uintptr_t)(a4)),bb_game_word((uintptr_t)(a5)))
#define M7(id,p,a0,a1,a2,a3,a4,a5,a6) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)),bb_game_word((uintptr_t)(a3)),bb_game_word((uintptr_t)(a4)),bb_game_word((uintptr_t)(a5)),bb_game_word((uintptr_t)(a6)))
#define M8(id,p,a0,a1,a2,a3,a4,a5,a6,a7) BB_GAME_CALL(id,bb_game_word((uintptr_t)(p)),bb_game_word((uintptr_t)(a0)),bb_game_word((uintptr_t)(a1)),bb_game_word((uintptr_t)(a2)),bb_game_word((uintptr_t)(a3)),bb_game_word((uintptr_t)(a4)),bb_game_word((uintptr_t)(a5)),bb_game_word((uintptr_t)(a6)),bb_game_word((uintptr_t)(a7)))
#define FORMAT(out,fmt,...) bb_crt_output_words(out,256,fmt,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4)
#define FORMAT_WORD(p) ((uint32_t)(p))
#else
typedef uint32_t (*c0)(void);
typedef uint32_t (*c1)(uint32_t);
typedef uint32_t (*c2)(uint32_t, uint32_t);
typedef uint32_t (*c3)(uint32_t, uint32_t, uint32_t);
typedef uint32_t (*c4)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m0)(void *);
typedef uint32_t(BB_THISCALL * m1)(void *, uint32_t);
typedef uint32_t(BB_THISCALL * m2)(void *, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m3)(void *, uint32_t, uint32_t,
                                                 uint32_t);
typedef uint32_t(BB_THISCALL * m4)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m5)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m6)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t, uint32_t,
                                                 uint32_t);
typedef uint32_t(BB_THISCALL * m8)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t, uint32_t,
                                                 uint32_t, uint32_t, uint32_t);
#define C0(a) ((c0)BB_CODE((uintptr_t)(a)))()
#define C1(a, x) ((c1)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x))
#define C2(a, x, y)                                                            \
  ((c2)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x), (uint32_t)(uintptr_t)(y))
#define C3(a, x, y, z)                                                         \
  ((c3)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x), (uint32_t)(uintptr_t)(y),     \
                       (uint32_t)(uintptr_t)(z))
#define C4(a, x, y, z, w)                                                      \
  ((c4)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x), (uint32_t)(uintptr_t)(y),     \
                       (uint32_t)(uintptr_t)(z), (uint32_t)(uintptr_t)(w))
#define M0(a, p) ((m0)BB_CODE((uintptr_t)(a)))(PTR(p))
#define M1(a, p, x) ((m1)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x))
#define M2(a, p, x, y)                                                         \
  ((m2)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),                       \
                       (uint32_t)(uintptr_t)(y))
#define M3(a, p, x, y, z)                                                      \
  ((m3)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),                       \
                       (uint32_t)(uintptr_t)(y), (uint32_t)(uintptr_t)(z))
#define M4(a, p, x, y, z, w)                                                   \
  ((m4)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),                       \
                       (uint32_t)(uintptr_t)(y), (uint32_t)(uintptr_t)(z),     \
                       (uint32_t)(uintptr_t)(w))
#define M5(a, p, x, y, z, w, v) ((m5)BB_CODE((uintptr_t)(a)))(PTR(p), x, y, z, w, v)
#define M6(a, p, x, y, z, w, v, u)                                             \
  ((m6)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x), y, z, w, v, u)
#define M8(a, p, x, y, z, w, v, u, t, s)                                       \
  ((m8)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x), y, z, w,              \
                       (uint32_t)(uintptr_t)(v), (uint32_t)(uintptr_t)(u),     \
                       (uint32_t)(uintptr_t)(t), (uint32_t)(uintptr_t)(s))
typedef int (*format_fn)(char *, const char *, ...);
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#define FORMAT_WORD(p) TEXT(p)
#endif
static void copy(uintptr_t out, uintptr_t in, size_t n) {
  while (n >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    n -= 4;
  }
  while (n--)
    BYTE(out++) = BYTE(in++);
}
static void string_copy(uintptr_t out, uintptr_t in) {
  copy(out, in, strlen(BB_DATA(in)) + 1);
}
static void append(uintptr_t out, uintptr_t in) {
  size_t n = strlen(BB_DATA(in)) + 1;
  copy(out + strlen(BB_DATA(out)), in, n);
}
static uintptr_t localized(uintptr_t slot) {
  uintptr_t p = BB_U32(slot);
  return p ? p : 0x0048481c;
}
static int32_t signed_add(uint32_t a, uint32_t b) { return (int32_t)(a + b); }
static void sound(uintptr_t slot) { C1(0x00415860, BB_U32(slot)); }
static void select_menu(uintptr_t self, uint32_t mode) {
  M1(0x0041f7a0, self, mode);
}

/* 0041dd80: wrapped signed mouse projection and the interior map bounds. */
void bb_mouse_map_position_entry(int32_t x, int32_t y, int32_t map_x,
                                 int32_t map_y, uint8_t *out_x,
                                 uint8_t *out_y) {
  int32_t d = (int32_t)(BB_U32(0x004846b8) - (uint32_t)x) / 2;
  int32_t a = (int32_t)((uint32_t)d - (uint32_t)y + BB_U32(0x004846bc)) / 2;
  int32_t px = signed_add((uint32_t)map_x, (uint32_t)a);
  d = (int32_t)((uint32_t)x - BB_U32(0x004846b8)) / 2;
  a = (int32_t)((uint32_t)d - (uint32_t)y + BB_U32(0x004846bc)) / 2;
  int32_t py = signed_add((uint32_t)map_y, (uint32_t)a);
  BYTE(out_x) = (uint8_t)(px < 1 ? 1 : px > 190 ? 190 : px);
  BYTE(out_y) = (uint8_t)(py < 1 ? 1 : py > 190 ? 190 : py);
}

/* The dialog contains 32 records with separate type, text, geometry, spacing
 * and enabled fields. Initialization only clears the original three fields. */
void BB_THISCALL bb_dialog_build_entry(void *object,
                                                     uint32_t mode) {
  uintptr_t p = (uintptr_t)object;
  for (unsigned i = 0; i < 32; ++i) {
    uintptr_t r = p + 0x11e + i * 0x11c;
    BB_U32(r) = 0;
    BB_U32(r + 0x114) = 0;
    BB_U32(r + 0x118) = 1;
  }
  BYTE(p + 4) = 0;
  BB_U32(p + 6) = mode;
  if (mode == 1 || mode == 5) {
    string_copy(p + 0x1e, 0x0048481c);
    BB_U32(p + 0x16) = 5;
    for (unsigned i = 0; i < 5; ++i)
      BB_U32(p + 0x11e + i * 0x11c) = 0;
    static const uintptr_t slots[2][5] = {
        {0x00603794, 0x00603798, 0x0060379c, 0x006037a0, 0x006037a4},
        {0x006037a8, 0x00603798, 0x006037ac, 0x006037b0, 0x006037b4}};
    for (unsigned i = 0; i < 5; ++i)
      string_copy(p + 0x122 + i * 0x11c, localized(slots[mode == 5][i]));
  } else if (mode == 8 || mode == 9 || mode == 13) {
    string_copy(p + 0x1e, localized(mode == 9 ? 0x006037ac : 0x00603798));
    C0(0x00420180);
    BB_U32(p + 0x16) = 11;
    for (unsigned i = 0; i < 10; ++i) {
      uintptr_t saved = 0x005c9558 + i * 0x144, r = p + 0x122 + i * 0x11c;
      string_copy(r, saved);
      BB_U32(r + 0x114) = mode == 9 ? 1 : BB_U32(saved + 0x140);
      BB_U32(r + 0x110) = 0xfffffff6;
    }
    BB_U32(p + 0xc2e) = 0;
    string_copy(p + 0xc3a, localized(0x006037b8));
    for (uint32_t i = 0; (int32_t)i < BB_I32(p + 0x16); ++i)
      BB_U32(p + 0x11e + i * 0x11c) = 1;
  } else if (mode == 10 || mode == 11 || mode == 14) {
    uintptr_t title = mode == 10   ? 0x006037a4
                      : mode == 11 ? 0x006037b4
                                   : 0x006037b0;
    uintptr_t prompt = mode == 10   ? 0x006037c0
                       : mode == 11 ? 0x006037c4
                                    : 0x006037c8;
    string_copy(p + 0x1e, localized(title));
    string_copy(p + 0x122, localized(prompt));
    BB_U32(p + 0x236) = 0;
    BB_U32(p + 0x11e) = 2;
    string_copy(p + 0x23e, localized(0x00603914));
    string_copy(p + 0x35a, localized(0x00603918));
    BB_U32(p + 0x16) = 3;
    for (uint32_t i = 1; (int32_t)i < BB_I32(p + 0x16); ++i)
      BB_U32(p + 0x11e + i * 0x11c) = 1;
  } else if (mode == 15) {
    string_copy(p + 0x1e, localized(0x006037ac));
    string_copy(p + 0x122, localized(0x006037bc));
    BB_U32(p + 0x11e) = 2;
    BB_U32(p + 0x236) = 0;
    string_copy(p + 0x23e, 0x006044e8);
    BB_U32(p + 0x16) = 3;
    string_copy(p + 0x35a, localized(0x006037b8));
    for (uint32_t i = 1; (int32_t)i < BB_I32(p + 0x16); ++i)
      BB_U32(p + 0x11e + i * 0x11c) = 1;
  } else if (mode == 16) {
    string_copy(p + 0x1e, localized(0x006037a4));
    BB_U32(p + 0x16) = 0;
  }
}
void BB_THISCALL bb_dialog_select_entry(void *object,
                                                      uint32_t mode) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p + 6) != mode)
    BB_U32(p + 10) = 0;
  M1(0x0041ed10, p, mode);
  while (!BB_U32(p + 0x236 + BB_U32(p + 10) * 0x11c)) {
    int32_t next = signed_add(BB_U32(p + 10), 1);
    BB_I32(p + 10) = next;
    if (next >= BB_I32(p + 0x16))
      BB_U32(p + 10) = (uint32_t)next - BB_U32(p + 0x16);
  }
}
void BB_THISCALL bb_dialog_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (BYTE(p + 4)) {
    if (BYTE(p + 4) == 1)
      M4(0x0042fd90, BB_U32(0x006ab57c), 0, 0, 255, 1);
    return;
  }
  M4(0x0042fd90, BB_U32(0x005c77b8), 0, 0, 255, 1);
  M4(0x0042fd90, BB_U32(0x006aff34), 202, 36, 255, 1);
  M0(0x00420900, 0x006af100);
  uint32_t x = BB_U32(p + 14), y = BB_U32(p + 18), first = y;
  y += 32;
  M4(0x00430560, 0x0069c758, p + 30, x, first, 1);
  for (uint32_t i = 0; (int32_t)i < BB_I32(p + 22); ++i) {
    uintptr_t r = p + 0x122 + i * 0x11c, font = 0;
    uint32_t type = BB_U32(r - 4);
    if (type == 0)
      font = BB_U32(p + 10) == i ? 0x0069c758 : 0x005c78f0;
    else if (type == 1)
      font = BB_U32(p + 10) == i ? 0x0050faa8 : 0x005fe420;
    else if (type == 2)
      font = 0x005117e0;
    if (font) {
      M4(0x00430560, font, r, x, y, 1);
      M8(0x004309b0, font, r, x, y, 1, r + 0x100, r + 0x104, r + 0x108,
         r + 0x10c);
    }
    y += BB_U32(r + 0x110) + 32;
  }
  int32_t chosen = BB_I32(p + 10);
  if (chosen >= 0 && chosen < BB_I32(p + 22)) {
    uintptr_t r = p + (uint32_t)chosen * 0x11c;
    int32_t left = BB_I32(r + 0x222), top = BB_I32(r + 0x226);
    uint32_t right = BB_U32(r + 0x22a), bottom = BB_U32(r + 0x22e);
    if (left > 0 && top > 0) {
      M4(0x00420a00, 0x006af100, (uint32_t)left - 5, (uint32_t)top - 5,
         right - (uint32_t)left + 11, bottom - (uint32_t)top + 11);
    } else
      M1(0x00420a60, 0x006af100, 0);
  } else
    M1(0x00420a60, 0x006af100, 0);
  if (BB_U32(p + 0x249e))
    M6(0x004306e0, 0x006a9968, p + 0x24ae, BB_U32(p + 0x24a2) - 300,
       BB_U32(p + 0x24a6), 600, 1, 0);
}
void BB_THISCALL bb_dialog_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_I32(0x0048446c) = signed_add(BB_U32(0x0048446c), 1) % 24;
  M0(0x00420950, 0x006af100);
  int32_t mode = BB_I32(p + 6);
  if (mode >= 3 && mode <= 4) {
    BB_U32(p + 26) -= 1;
    if (!BB_U32(p + 26))
      select_menu(p, mode);
  } else if (mode == 15)
    string_copy(p + 0x23e, 0x006044e8);
  if (BB_U32(p + 0x249e)) {
    int32_t x = BB_I32(p + 0x24a2);
    if (x < 317)
      BB_U32(p + 0x24a2) =
          (uint32_t)x + (uint32_t)((int32_t)(320u - (uint32_t)x) / 4) + 3;
    else if (x <= 323)
      BB_U32(p + 0x24a2) = 320;
    else
      BB_U32(p + 0x24a2) =
          (uint32_t)x + (uint32_t)((int32_t)(320u - (uint32_t)x) / 4) - 3;
    BB_U32(p + 0x24aa) -= 1;
    if (!BB_U32(p + 0x24aa))
      bb_session_info_entry();
  }
}
uint32_t BB_THISCALL bb_dialog_key_entry(void *object,
                                                       uint32_t key) {
  uintptr_t p = (uintptr_t)object;
  switch ((uint16_t)key) {
  case 8:
    if (BB_U32(p + 6) == 15) {
      size_t n = strlen(BB_DATA(0x006044e8));
      if (n)
        BYTE(0x006044e8 + n - 1) = 0;
    }
    break;
  case 13:
    sound(0x004842e4);
    M1(0x00420a60, 0x006af100, 0);
    M0(0x0041f810, p);
    break;
  case 27:
    if (BB_U32(p + 6) == 5) {
      sound(0x004842e4);
      C0(0x00403190);
      M1(0x0041ffc0, p, 0);
      M1(0x00420a60, 0x006af100, 0);
    } else {
      sound(0x004842e4);
      BB_U32(p + 10) = BB_U32(p + 22) - 1;
      M0(0x0041f810, p);
      M1(0x00420a60, 0x006af100, 0);
    }
    break;
  case 38:
    sound(0x00484304);
    do {
      int32_t n = signed_add(BB_U32(p + 10), 0xffffffff);
      BB_I32(p + 10) = n;
      if (n < 0)
        BB_U32(p + 10) += BB_U32(p + 22);
    } while (!BB_U32(p + 0x236 + BB_U32(p + 10) * 0x11c));
    return 1;
  case 40: {
    sound(0x00484304);
    int32_t count = BB_I32(p + 22);
    do {
      int32_t n = signed_add(BB_U32(p + 10), 1);
      BB_I32(p + 10) = n;
      if (n >= count)
        BB_U32(p + 10) -= (uint32_t)count;
    } while (!BB_U32(p + 0x236 + BB_U32(p + 10) * 0x11c));
    return 1;
  }
  }
  return 0;
}
uint32_t BB_THISCALL
bb_dialog_activate_entry(void *object, int32_t x, int32_t y) {
  (void)x;
  (void)y;
  sound(0x004842e4);
  M1(0x00420a60, 0x006af100, 0);
  M0(0x0041f810, object);
  return 1;
}
uint32_t BB_THISCALL bb_dialog_hover_entry(void *object,
                                                         int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  uint32_t old = BB_U32(p + 10);
  for (uint32_t i = 0; (int32_t)i < BB_I32(p + 22); ++i) {
    uintptr_t r = p + 0x222 + i * 0x11c;
    if (BB_I32(r) <= x && BB_I32(r + 4) <= y && x <= BB_I32(r + 8) &&
        y <= BB_I32(r + 12) && BB_U32(r + 20)) {
      BB_U32(p + 10) = i;
      if (i != old)
        sound(0x00484304);
    }
  }
  return 1;
}

/* The first historical label copied by the original save scan is uninitialized
 * stack residue. Use an empty initial label; subsequent prior labels retain
 * the exact scan order. Displayed names, filenames and availability are read
 * from the original file records, including short-read behavior. */
void bb_save_slots_refresh_entry(void) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_scratch scratch;
  bb_model_scratch_begin(&scratch, 64 + 256 + 12);
  char *name = BB_SPAN(scratch.address, 64);
  char *file_name = BB_SPAN(scratch.address + 64, 256);
  uint8_t *header = BB_SPAN(scratch.address + 320, 12);
  memset(name, 0, 64);
#else
  char name[64] = {0}, file_name[256];
  uint8_t header[12];
#endif
  for (uint32_t i = 0; i < 10; ++i) {
    uintptr_t r = 0x005c9558 + i * 0x144;
    FORMAT(file_name, TEXT(0x00486fac), i);
    append((uintptr_t)file_name, 0x00486fa4);
    string_copy(r + 0x40, (uintptr_t)name);
    uint32_t file = C2(0x0046bdd0, file_name, 0x0048416c);
    if (file) {
      C4(0x0046cf40, header, 8, 1, file);
      C4(0x0046cf40, name, 64, 1, file);
      C1(0x0046bbd0, file);
      BB_U32(r + 0x140) = 1;
    } else {
      string_copy((uintptr_t)name, 0x00486fdc);
      BB_U32(r + 0x140) = 0;
    }
    string_copy(r, (uintptr_t)name);
  }
#ifdef BB_MODEL_RUNTIME
  bb_model_scratch_end(&scratch);
#endif
}
static void clear_entities(void) {
  for (uint32_t offset = 0; offset < 0x17f4d0; offset += 314)
    BB_U32(BB_U32(0x00553f90) + offset) = 0;
}
void BB_THISCALL bb_dialog_visibility_entry(void *object,
                                                          uint32_t visible) {
  uintptr_t p = (uintptr_t)object;
  if (!visible && BB_U32(p)) {
    if (BB_U32(p)) {
      M0(0x0042f200, 0x004eebc8);
      M1(0x0042ef20, 0x004eebc8, 0x00486dd8);
      M0(0x0042dc00, 0x00554fbc + BYTE(0x004843f0) * 0xd00u);
      BB_U32(p) = 0;
    }
    return;
  }
  M0(0x0042dbd0, 0x00554fbc + BYTE(0x004843f0) * 0xd00u);
  BB_U32(p + 10) = 0;
  BB_U32(p) = 1;
  while (!BB_U32(p + 0x236 + BB_U32(p + 10) * 0x11c)) {
    int32_t n = signed_add(BB_U32(p + 10), 1);
    BB_I32(p + 10) = n;
    if (n >= BB_I32(p + 22))
      BB_U32(p + 10) -= BB_U32(p + 22);
  }
  M0(0x0042f200, 0x004eebc8);
  bb_session_disc_check_entry();
  string_copy(0x006033d8, 0x006afd08);
  append(0x006033d8, 0x00486fd0);
  BB_U32(0x006a9894) = C2(0x0046bdd0, 0x006033d8, 0x0048416c);
  if (!BB_U32(0x006a9894))
    clear_entities();
  C3(0x0046cba0, BB_U32(0x006a9894), 0, 2);
  uint32_t size = C1(0x0046c8f0, BB_U32(0x006a9894));
  BB_U32(0x00604634) = size;
  if (size % 1972u != 676u)
    for (;;) {
    }
  C1(0x0046bbd0, BB_U32(0x006a9894));
  M1(0x0042ef20, 0x004eebc8, 0x00486be8);
}
static void save_filename(uint32_t slot) {
  FORMAT((char *)0x006033d8, TEXT(0x00486fac), slot);
  append(0x006033d8, 0x00486fa4);
}
static uint32_t random_movie(void) {
  int32_t n = (int32_t)C0(0x0046be20) % 30 + 1;
  FORMAT((char *)0x006033d8, TEXT(0x00484170), FORMAT_WORD(0x006afd08), n);
  uint32_t file = C2(0x0046bdd0, 0x006033d8, 0x0048416c);
  BB_U32(0x006a9894) = file;
  return file;
}
static uint32_t start_network(uintptr_t menu, int selected) {
#ifndef BB_MODEL_RUNTIME
  typedef uint32_t(BB_STDCALL * restore_fn)(void *);
#endif
  uintptr_t surface = BB_U32(0x004eebf8);
#ifdef BB_MODEL_RUNTIME
  BB_GAME_METHOD(surface, 0x28);
#else
  ((restore_fn)BB_CODE((uintptr_t)BB_U32(BB_U32(surface) + 0x28)))(PTR(surface));
#endif
  return bb_shared_create_entry(PTR(0x00513458),
                                selected ? BB_U32(menu + 10) + 1 : 0,
                                TEXT(0x005ca200), TEXT(0x006afbe8));
}
/* 0041f810: the menu command dispatcher retains every menu-specific branch,
 * callbacks and rereads. Current menu/selection are captured by the switch. */
void BB_THISCALL bb_dialog_command_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  switch (BB_U32(p + 6)) {
  case 1:
    switch (BB_U32(p + 10)) {
    case 0:
      bb_shared_configure_entry(PTR(0x00513458), TEXT(0x005ca200), TEXT(0x006afbe8));
      BB_U32(0x0048426c) = 2;
      BB_U32(0x00484274) = 0;
      M0(0x0042f200, 0x004eebc8);
      C1(0x00414a20, 1);
      M1(0x0041ffc0, p, 0);
      BB_U32(0x00484278) = 1;
      return;
    case 1:
      select_menu(p, 13);
      return;
    case 2:
      M1(0x00432df0, 0x004eebc8,
         BB_U32(0x00484264) == 2 ? 0x00484b68 : 0x00484b5c);
      return;
    case 3:
      M1(0x00432df0, 0x004eebc8, 0x00486fc4);
      return;
    case 4:
      select_menu(p, 10);
      return;
    }
    break;
  case 2:
    if (BB_I32(p + 10) >= signed_add(BB_U32(p + 22), 0xffffffff))
      select_menu(p, 1);
    else {
      uint32_t result = bb_shared_start_entry(PTR(0x00513458), BB_U32(p + 10));
      select_menu(p, result ? 3 : 1);
    }
    return;
  case 3:
    if (BB_I32(p + 10) < signed_add(BB_U32(p + 22), 0xfffffffe)) {
      select_menu(p, start_network(p, 1) ? 4 : 1);
    } else if (BB_U32(p + 22) - BB_U32(p + 10) == 2)
      select_menu(p, start_network(p, 0) ? 4 : 1);
    else {
      bb_shared_reset_entry(PTR(0x00513458));
      select_menu(p, 2);
    }
    return;
  case 4:
    if (BB_I32(p + 10) < signed_add(BB_U32(p + 22), 0xffffffff)) {
      BB_U32(0x0048426c) = 2;
      BB_U32(0x00484274) = 0;
      C2(0x00415ed0, 0x00486fb8, 0);
    } else {
      bb_shared_stop_entry(PTR(0x00513458));
      select_menu(p, 3);
    }
    return;
  case 5:
    switch (BB_U32(p + 10)) {
    case 0:
      C0(0x00403190);
      M1(0x0041ffc0, p, 0);
      return;
    case 1:
      select_menu(p, 8);
      return;
    case 2:
      select_menu(p, 9);
      return;
    case 3:
      select_menu(p, 14);
      return;
    case 4:
      select_menu(p, 11);
      return;
    }
    break;
  case 8:
    if (BB_I32(p + 10) >= signed_add(BB_U32(p + 22), 0xffffffff)) {
      select_menu(p, 5);
      return;
    }
    save_filename(BB_U32(p + 10));
    M0(0x0042f200, 0x004eebc8);
    C2(0x00415ed0, 0x006033d8, 0);
    if (BYTE(0x004843dc))
      bb_session_pause_tick_entry();
    M1(0x0041ffc0, p, 0);
    M1(0x0042b480, 0x006a9950, BYTE(0x005e5cc3));
    bb_session_disc_check_entry();
    if (!random_movie()) {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(BB_U32(0x6ca328), 0, 0x80000001u, 0x484134);
#else
      typedef uint32_t(BB_STDCALL * delete_fn)(void *,
                                                             const char *);
      ((delete_fn)BB_CODE((uintptr_t)BB_U32(0x006ca328)))(PTR(0x80000001),
                                                 TEXT(0x00484134));
#endif
    }
    C1(0x0046bbd0, BB_U32(0x006a9894));
    return;
  case 9:
    if (BB_I32(p + 10) >= signed_add(BB_U32(p + 22), 0xffffffff)) {
      select_menu(p, 5);
      return;
    }
    save_filename(BB_U32(p + 10));
    string_copy(0x005e42d0, 0x006033d8);
    FORMAT((char *)0x006044e8, TEXT(0x00486ebc), (uint32_t)BYTE(0x005e5cc3));
    BB_U32(0x004847b4) = 3;
    select_menu(p, 15);
    return;
  case 10:
    if (BB_U32(p + 10) != 1) {
      select_menu(p, 1);
      return;
    }
    if (!BB_U32(0x0048474c)) {
      M0(0x0042f200, 0x004eebc8);
      bb_fatal_teardown_entry(0);
      return;
    }
    select_menu(p, 16);
    M1(0x00420a60, 0x006af100, 0);
    BB_U32(p + 0x249e) = 1;
    sound(0x004842ec);
    BB_U32(p + 0x24a2) = 940;
    BB_U32(p + 0x24a6) = 420;
    BB_U32(p + 0x24aa) = 1;
    string_copy(p + 0x24ae, localized(0x00603938));
    return;
  case 11:
    if (BB_U32(p + 10) == 1) {
      bb_shared_stop_entry(PTR(0x00513458));
      bb_shared_reset_entry(PTR(0x00513458));
      BB_U32(0x0048426c) = 1;
      BB_U32(0x00484274) = 0;
      select_menu(p, 1);
    } else
      select_menu(p, 5);
    return;
  case 13:
    if (BB_I32(p + 10) >= signed_add(BB_U32(p + 22), 0xffffffff)) {
      select_menu(p, 1);
      return;
    }
    bb_shared_configure_entry(PTR(0x00513458), TEXT(0x005ca200), TEXT(0x006afbe8));
    BB_U32(0x0048426c) = 2;
    BB_U32(0x00484274) = 0;
    save_filename(BB_U32(p + 10));
    M0(0x0042f200, 0x004eebc8);
    C2(0x00415ed0, 0x006033d8, 0);
    C0(0x00403190);
    M1(0x0041ffc0, p, 0);
    BB_U32(0x00484278) = 0;
    bb_session_music_stop_entry();
    if (BYTE(0x004843dc))
      bb_session_pause_tick_entry();
    M1(0x0042b480, 0x006a9950, BYTE(0x005e5cc3));
    return;
  case 14:
    if (BB_U32(p + 10) == 1) {
      M0(0x0042f200, 0x004eebc8);
      C1(0x00414a20, BYTE(0x005e5cc3));
      C0(0x00403190);
      M1(0x0041ffc0, p, 0);
      bb_session_disc_check_entry();
      if (!random_movie())
        clear_entities();
      C1(0x0046bbd0, BB_U32(0x006a9894));
    } else
      select_menu(p, 5);
    return;
  case 15:
    if (BB_I32(p + 10) < signed_add(BB_U32(p + 22), 0xffffffff)) {
      C2(0x004159c0, 0x005e42d0, 0x006044e8);
      C0(0x00403190);
      M1(0x0041ffc0, p, 0);
    } else
      select_menu(p, 9);
    BB_U32(0x004847b4) = 0;
    break;
  }
}

void BB_THISCALL bb_highlight_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  for (uint32_t i = 0; (int32_t)i < BB_I32(p + 0xa14); ++i) {
    uintptr_t r = p + i * 20;
    if (BB_U32(r + 20))
      M4(0x0042fd90, BB_U32(0x005f91ac), BB_U32(r + 24), BB_U32(r + 28),
         BB_U32(r + 36), 1);
  }
}
void BB_THISCALL bb_highlight_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  for (uint32_t i = 0; (int32_t)i < BB_I32(p + 0xa14); ++i) {
    uintptr_t r = p + i * 20;
    if (BB_U32(r + 20)) {
      BB_U32(r + 32) += 1;
      BB_U32(r + 36) =
          (uint32_t)(BB_I32(r + 32) / 2) % BB_U32(BB_U32(0x005f91ac) + 0x4c);
      if (BB_U32(r + 32) == 12)
        BB_U32(r + 20) = 0;
    }
  }
  if (BB_U32(p)) {
    int32_t limit = BB_I32(p + 0xa18), i = 0;
    while (i < limit && BB_U32(p + 20 + (uint32_t)i * 20))
      ++i;
    if (i < limit) {
      uintptr_t r = p + (uint32_t)i * 20;
      BB_U32(r + 20) = 1;
      BB_U32(r + 32) = 0;
      BB_U32(r + 36) = 0;
      int32_t random = (int32_t)C0(0x0046be20);
      BB_U32(r + 24) = BB_U32(p + 4) + (uint32_t)(random % BB_I32(p + 12));
      random = (int32_t)C0(0x0046be20);
      BB_U32(r + 28) = BB_U32(p + 8) + (uint32_t)(random % BB_I32(p + 16));
    }
  }
}
void BB_THISCALL bb_highlight_region_entry(void *object,
                                                         int32_t x, int32_t y,
                                                         int32_t width,
                                                         int32_t height) {
  uintptr_t p = (uintptr_t)object;
  BB_I32(p + 4) = x;
  BB_I32(p + 8) = y;
  BB_I32(p + 12) = width;
  BB_I32(p + 16) = height;
  int32_t area = (int32_t)((uint32_t)height * (uint32_t)width);
  M1(0x00420a60, p, 1);
  int32_t n = area / 40;
  BB_I32(p + 0xa18) = n;
  if (BB_I32(p + 0xa14) < n)
    BB_U32(p + 0xa18) = BB_U32(p + 0xa14);
}
void BB_THISCALL bb_highlight_enabled_entry(void *object,
                                                          uint32_t value) {
  BB_U32((uintptr_t)object) = value;
}

void BB_THISCALL bb_demo_rewind_entry(void *object) {
  BB_U32((uintptr_t)object + 0x20004) = 0;
}
void BB_THISCALL bb_demo_load_entry(void *object,
                                                  const char *path) {
  uintptr_t p = (uintptr_t)object;
  uint32_t file = C2(0x0046bdd0, path, 0x0048416c);
  if (!file)
    return;
  C4(0x0046cf40, p + 0x20000, 4, 1, file);
  for (uint32_t i = 0; i < BB_U32(p + 0x20000); ++i)
    C4(0x0046cf40, p + i * 16, 16, 1, file);
  C1(0x0046bbd0, file);
  BB_U32(p + 0x20004) = 0;
  FORMAT((char *)0x006033d8, TEXT(0x00486fe0), BB_U32(p + 0x20000));
  M1(0x0041de40, 0x00662c98, 0x006033d8);
}
void BB_THISCALL bb_demo_save_entry(void *object,
                                                  const char *path) {
  uintptr_t p = (uintptr_t)object;
  uint32_t file = C2(0x0046bdd0, path, 0x00486ed4);
  if (!file)
    return;
  C4(0x0046cd20, p + 0x20000, 4, 1, file);
  for (uint32_t i = 0; i < BB_U32(p + 0x20000); ++i)
    C4(0x0046cd20, p + i * 16, 16, 1, file);
  C1(0x0046bbd0, file);
  FORMAT((char *)0x006033d8, TEXT(0x00486ff4), BB_U32(p + 0x20000));
  M1(0x0041de40, 0x00662c98, 0x006033d8);
}
void BB_THISCALL bb_demo_append_entry(void *object,
                                                    uint32_t message,
                                                    uint32_t key,
                                                    uint32_t data) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + BB_U32(p + 0x20000) * 16) = message;
  BB_U32(p + 4 + BB_U32(p + 0x20000) * 16) = key;
  BB_U32(p + 8 + BB_U32(p + 0x20000) * 16) = data;
  BB_U32(p + 12 + BB_U32(p + 0x20000) * 16) = BB_U32(0x004842c4);
  BB_U32(p + 0x20000) += 1;
}
void BB_THISCALL bb_demo_pump_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
#ifndef BB_MODEL_RUNTIME
  typedef uint32_t(BB_STDCALL * post_fn)(void *, uint32_t,
                                                       uint32_t, uint32_t);
#endif
  if (BB_U32(p + 0x20004) >= BB_U32(p + 0x20000))
    return;
#ifndef BB_MODEL_RUNTIME
  post_fn post = (post_fn)BB_CODE((uintptr_t)BB_U32(0x006ca548));
#endif
  while (BB_U32(p + 0x20004) < BB_U32(p + 0x20000)) {
    uintptr_t r = p + BB_U32(p + 0x20004) * 16;
    if (BB_U32(0x004842c4) < BB_U32(r + 12))
      return;
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(BB_U32(0x6ca548), 0, BB_U32(0x5cd7c4), BB_U32(r), BB_U32(r + 4), BB_U32(r + 8));
#else
    post(PTR(BB_U32(0x005cd7c4)), BB_U32(r), BB_U32(r + 4), BB_U32(r + 8));
#endif
    BB_U32(p + 0x20004) += 1;
  }
}
