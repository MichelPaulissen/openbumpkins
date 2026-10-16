/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "statistics.h"
#include "interop.h"
#include "world_controls.h"
#include "session_controls.h"
#include "worker_runtime.h"
#define BYTE(a) BB_U8(a)
#define TEXT(a) ((const char *)(uintptr_t)(a))
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
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
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)

static int32_t subtract(uint32_t a, uint32_t b) { return (int32_t)(a - b); }
static uint32_t truncation(long double value) {
  /* The original QWORD conversion produces integer-indefinite for invalid
   * inputs; callers retain its zero low DWORD. Avoid an undefined C cast. */
  if (!(value >= -0x1p63L && value < 0x1p63L))
    return 0;
  return (uint32_t)(int64_t)value;
}
static uintptr_t faction_offset(void) { return BYTE(0x004843f0) * 0xd00u; }
static void history_store(uintptr_t p, uint32_t offset, uint32_t value) {
  BB_U32(p + offset + (uint32_t)(int32_t)SHORT(p) * 4u) = value;
}

/* Complete mission popup theme selection, including default font refresh. */
void BB_THISCALL bb_mission_theme_entry(void *object,
                                                      uint32_t theme) {
  (void)object;
  if (BB_U32(0x004842d0)) {
    M1(0x00433120, 0x004eebc8, BB_U32(0x004842d0));
    BB_U32(0x004842d0) = 0;
  }
  static const uintptr_t palette[] = {0x004885ec, 0x004886e8, 0x004886cc,
                                      0x004886b0, 0x00488694, 0x00488678,
                                      0x0048865c, 0x00488640, 0x00488624};
  static const uintptr_t bitmap[] = {0x004886f8, 0x004886dc, 0x004886c0,
                                     0x004886a4, 0x00488688, 0x0048866c,
                                     0x00488650, 0x00488634, 0x00488618};
  if (theme < 9) {
    M1(0x0042ef20, 0x004eebc8, palette[theme]);
    BB_U32(0x004842d0) = M2(0x0042f320, 0x004eebc8, bitmap[theme], 1);
  }
  M2(0x00430310, 0x006ab590, 0x004eebc8, 0x00486c00);
  M1(0x00430550, 0x006ab590, 0xffffffff);
}
void BB_THISCALL bb_mission_popup_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  M0(0x0042f5f0, BB_U32(0x006afb20));
  uint32_t surface = BB_U32(0x006afb20), text = BB_U32(p + 4);
  M6(0x004306e0, 0x006ab590, text, 20, 30, 600, 0, surface);
}
void BB_THISCALL
bb_mission_popup_visibility_entry(void *object, uint32_t visible) {
  uintptr_t p = (uintptr_t)object;
  if (visible) {
    BB_U32(p) = 1;
    BB_U32(p + 8) = 520;
    C1(0x0042b260, 4);
    bb_faction_music_reset_entry();
    bb_session_music_start_entry(3);
    M1(0x0042b5a0, p, BYTE(0x005e5ccc));
    M0(0x0042b710, p);
  } else {
    BB_U32(p) = 0;
    C1(0x0042b260, 0);
    bb_session_music_stop_entry();
    BB_U32(0x00484274) = 2;
  }
}

/* Human averages retain the unsigned accumulation and integer division
 * preceding each extended floating-point scaling/truncation boundary. */
void BB_THISCALL bb_faction_statistics_entry(void *object) {
  uintptr_t p = (uintptr_t)object, node = BB_U32(0x00554f98);
  uint32_t count = 0, morale = 0, water = 0, resistance = 0, age = 0;
  if (node) {
    uint8_t owner = BYTE(p + 0x48);
    do {
      if (BYTE(node + 0xf7) == owner && BYTE(node + 0xd4) == 0x67) {
        ++count;
        morale += BYTE(node + 0x111);
        water += BYTE(node + 0x101);
        resistance += BYTE(node + 0x11b);
        age += BYTE(node + 0xce);
      }
      node = BB_U32(node + 4);
    } while (node);
  }
  (void)water;
  if (count) {
    morale /= count;
    resistance /= count;
    age /= count;
  }
  long double n = (long double)morale;
  n *= BB_DOUBLE(0x004821e8);
  n *= BB_DOUBLE(0x004821f0);
  BB_U32(p + 0xc4c) = truncation(n);
  n = (long double)resistance;
  n *= BB_DOUBLE(0x004821e8);
  n *= BB_DOUBLE(0x004821f0);
  BB_U32(p + 0xc50) = truncation(n);
  BB_U32(p + 0xc48) = (age * 62u) / 96u;
}

static uint32_t graph_y(uint32_t value, uint32_t maximum) {
  if (!maximum)
    return 227;
  long double n = (long double)value / (long double)maximum;
  n *= BB_DOUBLE(0x004821f8);
  return 227u - truncation(n);
}
static void graph_label(uintptr_t text, uint32_t x, uint32_t y) {
  int32_t left, top, right, bottom;
#ifdef BB_MODEL_RUNTIME
  bb_font_bounds(PTR(0x63f0d0),TEXT(text),x,y,1,&left,&top,&right,&bottom);
#else
  M8(0x004309b0, 0x0063f0d0, text, x, y, 1, &left, &top, &right, &bottom);
#endif
  uintptr_t font =
      subtract((uint32_t)right, (uint32_t)left) > 164 ? 0x005cd7d0 : 0x0063f0d0;
  M4(0x00430560, font, text, x, y, 1);
}
void BB_THISCALL bb_statistics_graph_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0xcc2 + BYTE(p + 0xb62) * 4u) = 0;
  for (uint32_t i = 0; i < 5; ++i) {
    uintptr_t row = p + BYTE(p + 0xb63 + BYTE(p + 0xb62) * 5u + i) * 0x74u;
    if (BB_U32(row + 2))
      for (int32_t j = 0; j < SHORT(p); ++j) {
        uint32_t value = BB_U32(row + 0x46 + (uint32_t)j * 4u);
        uintptr_t maximum = p + 0xcc2 + BYTE(p + 0xb62) * 4u;
        if (BB_U32(maximum) < value)
          BB_U32(maximum) = value;
      }
  }
  for (uint32_t i = 0, y = 295; i < 5; ++i, y += 31) {
    if (BYTE(p + 0xb62) == i)
      M4(0x0042fd90, BB_U32(0x005e4768), 191, y - 19, 255, 1);
    graph_label(p + 0xb7c + i * 64u, 280, y);
  }
  for (uint32_t i = 0, y = 295; i < 5; ++i, y += 31) {
    uintptr_t row = p + BYTE(p + 0xb63 + BYTE(p + 0xb62) * 5u + i) * 0x74u;
    uint32_t x = 226;
    if (BB_U32(row + 2)) {
      uint32_t at =
          graph_y(BB_U32(row + 0x46), BB_U32(p + 0xcc2 + BYTE(p + 0xb62) * 4u));
      if (SHORT(p) <= 1) {
        bb_display_line_entry(PTR(0x004eebc8), PTR(BB_U32(0x004eec00)), x,
                              at, x, at, BYTE(p + 0xcbc + i));
        FORMAT((char *)0x006033d8, TEXT(0x00486ebc), BB_U32(row + 0x46));
        M4(0x00430560, 0x005c5b70, 0x006033d8, x, at, 1);
      } else
        for (int32_t j = 0; j < SHORT(p); ++j, x += 32) {
          uint32_t previous = at;
          if (j < SHORT(p) - 1) {
            at = graph_y(BB_U32(row + 0x4a + (uint32_t)j * 4u),
                         BB_U32(p + 0xcc2 + BYTE(p + 0xb62) * 4u));
            bb_display_line_entry(
                PTR(0x004eebc8), PTR(BB_U32(0x004eec00)), x, previous,
                (uint16_t)(x + 32), at, BYTE(p + 0xcbc + i));
          }
          if (!j || BB_U32(row + 0x42 + (uint32_t)j * 4u) !=
                        BB_U32(row + 0x46 + (uint32_t)j * 4u)) {
            FORMAT((char *)0x006033d8, TEXT(0x00486ebc),
                   BB_U32(row + 0x46 + (uint32_t)j * 4u));
            M4(0x00430560, 0x005c5b70, 0x006033d8, x, previous, 1);
          }
        }
    }
    if (BB_U32(row + 2))
      M4(0x0042fd90, BB_U32(0x005e4768), 442, y - 19, 255, 1);
    graph_label(row + 6, 531, y);
  }
  if (SHORT(p)) {
    uint32_t weather = BYTE(p + 0xb56);
    for (uint32_t i = 0, x = 226; i < 12; ++i, x += 32) {
      M4(0x0042fd90, BB_U32(0x005fe3f0), x - 11, 232, weather, 1);
      weather = (weather + 1u) % 4u;
    }
  }
}
void BB_THISCALL bb_statistics_draw_entry(void *object) {
  M4(0x0042fd90, BB_U32(0x00600174), 0, 39, 255, 1);
  M0(0x0042bff0, object);
  M0(0x0042cad0, object);
}
void BB_THISCALL bb_statistics_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  int32_t count = SHORT(p);
  if (!count || BYTE(p + 0xb55 + (uint32_t)count) != BB_U32(0x00484768))
    M0(0x0042c5b0, p);
}
uint32_t BB_THISCALL
bb_statistics_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(0x0048443c) != 2 || BB_U32(0x0048474c))
    return 0;
  if (x < BB_I32(p + 0xcde))
    M2(0x0042d070, p, (uint32_t)x, (uint32_t)y);
  if (x > 190 && x < 370) {
    for (uint32_t i = 0, top = 276; i < 5; ++i, top += 31)
      if (y >= (int32_t)top && y <= (int32_t)(top + 25)) {
        C1(0x00415860, BB_U32(0x004842d8));
        BYTE(p + 0xb62) = i;
      }
    return 1;
  }
  if (x > 441 && x < 621)
    for (uint32_t i = 0, top = 276; i < 5; ++i, top += 31)
      if (y >= (int32_t)top && y <= (int32_t)(top + 25) &&
          BYTE(p + 0xb63 + BYTE(p + 0xb62) * 5u + i) != 8) {
        C1(0x00415860, BB_U32(0x004842d8));
        uintptr_t enabled =
            p + 2 + BYTE(p + 0xb63 + BYTE(p + 0xb62) * 5u + i) * 0x74u;
        BB_U32(enabled) = !BB_U32(enabled);
      }
  return 1;
}
void BB_THISCALL bb_statistics_history_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (SHORT(p) < 12)
    WORD(p) += 1;
  else {
    for (uint32_t row = 0; row < 25; ++row)
      for (int32_t j = 0; j < SHORT(p) - 1; ++j)
        BB_U32(p + 0x46 + row * 0x74u + (uint32_t)j * 4u) =
            BB_U32(p + 0x4a + row * 0x74u + (uint32_t)j * 4u);
    for (int32_t j = 0; j < SHORT(p) - 1; ++j)
      BYTE(p + 0xb56 + (uint32_t)j) = BYTE(p + 0xb57 + (uint32_t)j);
  }
  M0(0x0042b7b0, BB_U32(0x004844a8));
  history_store(p, 0x286, WORD(0x00555108 + faction_offset()));
  history_store(p, 0x2fa, BB_U32(0x00555c10 + faction_offset()));
  history_store(p, 0x36e, BB_U32(0x00555c14 + faction_offset()));
  history_store(p, 0x456, BB_U32(0x00555c1c + faction_offset()));
  history_store(p, 0x42, WORD(0x005559be + faction_offset()));
  history_store(p, 0xb6, WORD(0x005559c0 + faction_offset()));
  uintptr_t offset = faction_offset();
  history_store(p, 0x12a,
                (uint32_t)WORD(0x005559d6 + offset) +
                    WORD(0x005559d8 + offset));
  offset = faction_offset();
  history_store(p, 0x19e,
                (uint32_t)WORD(0x005559d2 + offset) +
                    WORD(0x005559d4 + offset));
  uintptr_t faction = BB_U32(0x004844a8);
  uint32_t free = WORD(faction + 0x14c);
  for (uint32_t i = 0; i < 6; ++i)
    free -= WORD(faction + (i < 2 ? 0xa02 + i * 2u : 0xa16 + (i - 2) * 2u));
  history_store(p, 0x212, free);
  history_store(p, 0x4ca, BB_U32(0x00555006 + faction_offset()));
  static const uint32_t source[] = {
      0x00555c20, 0x00555c24, 0x00555c28, 0x00555c2c, 0x00555c08,
      0x00555c0c, 0x00555c30, 0x00555c34, 0x00555c04, 0x00555c38,
      0x00555c3c, 0x00555c40, 0x00555c44, 0x00555c48};
  static const uint32_t output[] = {0x53e, 0x5b2, 0x626, 0x69a, 0x70e,
                                    0x782, 0x86a, 0x8de, 0x7f6, 0x952,
                                    0x9c6, 0xa3a, 0xaae, 0xb22};
  for (uint32_t i = 0; i < 14; ++i)
    history_store(p, output[i], BB_U32(source[i] + faction_offset()));
  BYTE(p + 0xb55 + (uint32_t)(int32_t)SHORT(p)) = BYTE(0x00484768);
  for (uint32_t i = 0; i < 14; ++i)
    BB_U32(BB_U32(0x004844a8) +
           (i < 2 ? 0xc54 + i * 4u : 0xc60 + (i - 2) * 4u)) = 0;
  bb_session_disc_check_entry();
  int32_t n = (int32_t)C0(0x0046be20) % 30 + 1;
  FORMAT((char *)0x006033d8, TEXT(0x00484170), FORMAT_WORD(0x006afd08), n);
  BB_U32(0x006a9894) = C2(0x0046bdd0, 0x006033d8, 0x0048416c);
  if (!BB_U32(0x006a9894))
    for (uint32_t offset = 0; offset < 0x17f4d0; offset += 314)
      BB_U32(BB_U32(0x00553f90) + offset) = 0;
  C1(0x0046bbd0, BB_U32(0x006a9894));
}

void BB_THISCALL bb_economy_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0xcd6) = 0;
  BB_U32(p + 0xcda) = 41;
  BB_U32(p + 0xcde) = 200;
  BB_U32(p + 0xce2) = BB_U32(0x00484448) + 4;
  uintptr_t costs = BB_U32(0x004844a8) + 0x4e;
  static const uint32_t enabled[] = {0x15c, 0xdc, 0xe8, 0xe4,
                                     0xea,  0x80, 0x102};
  static const uintptr_t reference[] = {0x005e44ac, 0x005e441c, 0x005e44f4,
                                        0x005e4680, 0x005e453c, 0x005e45a8,
                                        0x005e4584};
  static const uint32_t value[] = {10, 2, 14, 36, 18, 24, 22};
  for (uint32_t i = 0; i < 7; ++i)
    if (WORD(BB_U32(0x004844a8) + enabled[i])) {
      long double n = (long double)WORD(reference[i]);
      n *= BB_DOUBLE(0x00482200);
      n = (long double)WORD(costs + value[i]) / n;
      n *= BB_DOUBLE(0x00482208);
      uint32_t x = 47u + truncation(n), y = 83u + i * 31u;
      M4(0x0042fd90, BB_U32(0x005f9254), 48, y, 255, 1);
      M4(0x0042fd90, BB_U32(0x006b0840), x, y - 1, 0, 1);
      char text[16];
      FORMAT(text, TEXT(0x00486ebc), (uint32_t)WORD(costs + value[i]));
      M4(0x00430560, 0x005c5b70, text, x + 13, y + 7, 1);
    }
  if (WORD(BB_U32(0x004844a8) + 0x1b2)) {
    uint32_t y = BB_U32(p + 0xcda) + 307;
    static const uint32_t offset[] = {0x2b, 0x2d, 0x2e, 0x2f};
    for (uint32_t i = 0; i < 4; ++i, y += 31) {
      uint32_t frame = (uint32_t)BYTE(costs + offset[i]) + 1;
      M4(0x0042fd90, BB_U32(0x006b0840), 47u + (frame * 3u - 3u) * 9u, y, frame,
         1);
    }
  }
}
static void economy_set(uintptr_t p, double value) {
  union {
    double real;
    uint32_t words[2];
  } bits = {.real = value};
  M3(0x0042d780, p, BB_U32(p + 0xce6), bits.words[0], bits.words[1]);
  BB_U32(p + 0xcea) = 1;
}
static double slider_value(int32_t delta, int goods, int drag) {
  uintptr_t base = drag ? 0x00482238 : 0x00482210;
  long double n = (long double)delta * BB_DOUBLE(base + (goods ? 0x18 : 0));
  volatile double value = (double)n;
  if (goods) {
    if (!(n >= BB_DOUBLE(base + 0x10)))
      value = 0;
    if (value > BB_DOUBLE(base + 0x20))
      value = 3;
  } else {
    if (n > BB_DOUBLE(base + 8))
      value = 2;
    if (value < 0)
      value = 0;
  }
  return value;
}
uint32_t BB_THISCALL
bb_economy_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (x < BB_I32(p + 0xcd6) || y < BB_I32(p + 0xcda) || x > BB_I32(p + 0xcde) ||
      y > BB_I32(p + 0xce2))
    return 0;
  int32_t rx = subtract((uint32_t)x, BB_U32(p + 0xcd6));
  int32_t ry = subtract((uint32_t)y, BB_U32(p + 0xcda));
  int32_t width = BB_I32(BB_U32(0x006b0840) + 0x44) / 2;
  int32_t delta =
      subtract((uint32_t)subtract((uint32_t)rx, (uint32_t)width), 47);
  int32_t row = subtract((uint32_t)ry, 41) / 31;
  BB_I32(p + 0xce6) = row;
  double value = slider_value(delta, 0, 0);
  if (row < 0)
    BB_U32(p + 0xce6) = 0;
  else if (row > 6) {
    row = (int32_t)((uint32_t)(subtract((uint32_t)ry, 307) / 31) + 7u);
    BB_I32(p + 0xce6) = row;
    if (row > 10)
      BB_U32(p + 0xce6) = 10;
    value = slider_value(delta, 1, 0);
  }
  economy_set(p, value);
  return 1;
}
void BB_THISCALL bb_economy_drag_entry(void *object, int32_t x,
                                                     int32_t y) {
  (void)y;
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p + 0xcea))
    return;
  int32_t width = BB_I32(BB_U32(0x006b0840) + 0x44) / 2;
  int32_t delta =
      subtract((uint32_t)subtract((uint32_t)x, (uint32_t)width), 47);
  double value = slider_value(delta, 0, 1);
  int32_t row = BB_I32(p + 0xce6);
  if (row < 0)
    BB_U32(p + 0xce6) = 0;
  else if (row > 6)
    value = slider_value(delta, 1, 1);
  economy_set(p, value);
}
