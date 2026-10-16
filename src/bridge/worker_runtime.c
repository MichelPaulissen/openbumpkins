/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "worker_runtime.h"
#include "command_builders.h"
#include "interop.h"
#include <math.h>
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
typedef int (*format_fn)(char *, const char *, ...);
typedef int32_t (*integer_fn)(const char *);
typedef char *(*find_fn)(const char *, int);
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#include "crt_primitives.h"
#define FIND(text,c) strchr(BB_DATA(text),c)
static int32_t integer(const char *text) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer,text,strlen(BB_DATA(text))+1);
  int32_t result = BB_GAME_CALL(0x46d230,0,buffer.reference);
  bb_model_buffer_end(&buffer,0);
  return result;
}
#define INTEGER integer
#else
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x46bae0))
#define INTEGER ((integer_fn)BB_CODE((uintptr_t)0x46d230))
#define FIND ((find_fn)BB_CODE((uintptr_t)0x46c260))
#endif
void *bb_archive_member(void *, const char *);
uint32_t bb_archive_read(void *, void *, uint32_t);
uint32_t bb_archive_release(void *);

static uint32_t text_size(uintptr_t p) {
  uint32_t n = 1;
  while (BYTE(p + n - 1))
    ++n;
  return n;
}
static void text_copy(uintptr_t out, uintptr_t in) {
  uint32_t n = text_size(in);
  while (n >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    n -= 4;
  }
  while (n--)
    BYTE(out++) = BYTE(in++);
}
static uintptr_t faction(void) { return 0x554fbcu + BYTE(0x4843f0) * 0xd00u; }
static void message(void) {
  bb_message_append_entry(PTR(0x5ecf38), 0x6033d8, 0);
}
static const char *event_format(uint32_t value) {
  return (const char *)PTR(value ? value : 0x48481c);
}

void bb_random_event_entry(void) {
  if (!BB_U32(0x4842c4))
    return;
  int32_t y0 = bb_random() % 192;
  int32_t x0 = bb_random() % 192;
  uintptr_t city =
      (uintptr_t)bb_entity_nearest(PTR(0x553f90), BYTE(0x4843f0), 0x62, x0, y0);
  if (!city)
    return;
  int32_t offset = bb_random() % 20;
  uint8_t x = (uint8_t)(BYTE(city + 0xd8) - 10 + offset);
  offset = bb_random() % 20;
  uint8_t y = (uint8_t)(BYTE(city + 0xd9) - 10 + offset);
  if (x > 191)
    x = 191;
  if (y > 191)
    y = 191;
  int32_t event = bb_random() % 8;
  switch (event) {
  case 0: {
    if (!BYTE(0x5e5cc1) || BB_U32(0x484768) == 3)
      return;
    if (bb_random() % 100 >= BYTE(0x5e5cc1))
      return;
    uint32_t px = (uint32_t)x << 16, py = (uint32_t)y << 16;
    uint32_t height = (uint32_t)BYTE(0x559b65u + (y * 192u + x) * 12u) << 16;
    bb_entity_create(PTR(0x553f90), 4, 7, px, py, height);
    for (unsigned i = 0; i < 4; ++i) {
      int32_t dy = bb_random() % 4;
      uint32_t ny =
          py + (i < 2 ? (uint32_t)dy << 16 : 0u - ((uint32_t)dy << 16));
      int32_t dx = bb_random() % 4;
      uint32_t nx =
          px + (i < 2 ? (uint32_t)dx << 16 : 0u - ((uint32_t)dx << 16));
      bb_entity_create(PTR(0x553f90), 4, 7, nx, ny, height);
    }
    FORMAT(PTR(0x6033d8), event_format(BB_U32(0x6037cc)), BB_U32(city + 0x3c));
    message();
    return;
  }
  case 1: {
    if (!BYTE(0x5e5cc1) || bb_random() % 100 >= BYTE(0x5e5cc1))
      return;
    uint8_t subtype = (uint8_t)(bb_random() % 4);
    for (uintptr_t p = BB_U32(0x554f98); p; p = BB_U32(p + 4)) {
      uint8_t kind = BYTE(p + 0xd4);
      if ((kind == 0x6e || kind == 0x6f || kind == 0x67) &&
          (int32_t)bb_entity_distance(BYTE(p + 0xd8), BYTE(p + 0xd9), x, y) <
              0x50000) {
        BYTE(p + 0x11e) = subtype;
        return;
      }
    }
    return;
  }
  case 2: {
    if (!(BB_U32(0x555c4c) & 1))
      return;
    for (uintptr_t p = BB_U32(0x554f98); p; p = BB_U32(p + 4))
      if (BYTE(p + 0xd4) == 0x6e &&
          (int32_t)bb_entity_distance(BYTE(p + 0xd8), BYTE(p + 0xd9), x, y) <
              0x50000) {
        BYTE(p + 0x11e) = 2;
        int32_t health = (int16_t)WORD(p + 0xa0);
        if (health > 10)
          bb_entity_damage_entry(PTR(p), health - 10, 0);
        FORMAT(PTR(0x6033d8), event_format(BB_U32(0x6037d0)));
        message();
        return;
      }
    return;
  }
  case 3:
  case 4:
  case 5: {
    uint32_t flags = BB_U32(0x555c4c), mask = 1u << (event - 2);
    if (!(flags & mask))
      return;
    uint32_t format = BB_U32(0x6037d4u + (event - 3u) * 4u);
    BB_U32(0x555c4c) = flags & ~mask;
    FORMAT(PTR(0x6033d8), event_format(format));
    message();
    for (uintptr_t p = BB_U32(0x554f98); p; p = BB_U32(p + 4))
      if (BYTE(p + 0xd4) == 0x67 && BYTE(p + 0xf7) == BYTE(0x4843f0))
        BYTE(p + 0x12e) = event - 2;
    return;
  }
  case 6:
    if (BYTE(0x685e99) > BYTE(0x5e5cc0))
      return;
    for (uintptr_t p = BB_U32(0x554f98); p; p = BB_U32(p + 4))
      if (BYTE(p + 0xd4) == 0x67 && BYTE(p + 0xf4) == 1 && !(bb_random() % 2)) {
        BYTE(p + 0x111) = 0;
        BYTE(p + 0x10d) = 0;
        return;
      }
  }
}

void bb_initial_buildings_entry(uint32_t x, uint32_t y) {
  bb_command_build_entry(PTR(faction()), BYTE(0x4843f0), 0x62, (uint8_t)(x + 2),
                         (uint8_t)(y - 3), 0);
  bb_command_build_entry(PTR(faction()), BYTE(0x4843f0), 0x8f, (uint8_t)x,
                         (uint8_t)y, 0);
  bb_command_build_entry(PTR(faction()), BYTE(0x4843f0), 0x90, (uint8_t)(x - 1),
                         (uint8_t)(y + 2), 0);
}

void bb_faction_music_reset_entry(void) {
  bb_sound_event_entry(7, 0);
  bb_sound_event_entry(9, 0);
  bb_sound_event_entry(6, 0);
  bb_sound_event_entry(10, 0);
  bb_sound_event_entry(0, 1);
}
void BB_THISCALL bb_faction_end_state_entry(void *object,
                                                          uint32_t state) {
  uintptr_t p = (uintptr_t)object;
  uint8_t next = state;
  if (BYTE(p + 0x450) == next)
    return;
  uint8_t owner = BYTE(p + 0x48);
  BYTE(p + 0x450) = next;
  if (owner != BYTE(0x4843f0) || (next != 1 && next != 2))
    return;
  static const uintptr_t slots[2][3] = {{0x602440, 0x602324, 0x602330},
                                        {0x55910c, 0x559100, 0x559104}};
  int32_t choice = bb_random() % 3;
  if (choice >= 0 && choice <= 2)
    bb_sound_play_entry(BB_U32(slots[next - 1][choice]));
  BB_U32(0x5e5cc8) = 120;
  BB_U32(0x5e5cc4) = next - 1;
  bb_faction_music_reset_entry();
}
uint32_t bb_clock_text_parse_entry(const char *input) {
  char text[256];
  text_copy((uintptr_t)text, (uintptr_t)input);
  uint32_t fraction = 0;
  if (FIND(input, '.'))
    fraction = INTEGER(FIND(input, '.') + 1);
  char *p = text;
  uint32_t hours = 0, minutes = 0;
  if (FIND(text, ':')) {
    if (FIND(FIND(text, ':') + 1, ':')) {
      hours = INTEGER(text);
      minutes = INTEGER(FIND(text, ':') + 1);
      p = FIND(text, ':') + 1;
    } else {
      minutes = INTEGER(text);
    }
    p = FIND(p, ':') + 1;
  }
  uint32_t seconds = INTEGER(p);
  return ((hours * 60u + minutes) * 60u + seconds) * 36u + fraction;
}

void BB_THISCALL
bb_stock_panel_animation_entry(void *object, uint32_t animation) {
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p + 8)) {
    BB_U32(p + 12) = 0;
    BB_U32(p + 16) = 550;
    BB_U32(p + 8) = animation;
    BB_U32(p + 20) = BB_U32(p + 4) + 20;
  }
}
void BB_THISCALL bb_stock_panel_needs_entry(void *object) {
  uintptr_t e =
      BB_U32(0x553f90) + BB_U32(0x55540e + BYTE(0x4843f0) * 0xd00u) * 314u;
  uint32_t choices[32], count = 0;
  uint8_t food = BYTE(e + 0x100), water = BYTE(e + 0x101);
  if (food < 32)
    choices[count++] = 2;
  if (!food)
    choices[count++] = 2;
  if (water < 32)
    choices[count++] = 11;
  if (!water)
    choices[count++] = 11;
  if (BYTE(e + 0x113) > 20)
    choices[count++] = 7;
  uint8_t morale = BYTE(e + 0x111);
  if (morale > 96)
    choices[count++] = 5;
  if (morale >= 32 && morale < 64)
    choices[count++] = 6;
  if (morale < 32)
    choices[count++] = 12;
  if (!morale) {
    choices[count++] = 12;
    choices[count++] = 3;
    choices[count++] = 4;
  }
  if (count) {
    int32_t choice = bb_random() % (int32_t)count;
    bb_stock_panel_animation_entry(object, choices[choice]);
  }
}
static uint32_t stock_sine(int32_t tick) {
  long double angle = (long double)tick * BB_DOUBLE(0x4821d8);
  return (uint32_t)(int64_t)(sinl(angle) * BB_DOUBLE(0x4821e0));
}
void BB_THISCALL bb_stock_panel_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 32) = 0;
  uintptr_t f = faction();
  if (BYTE(f + 0x451) == 1) {
    uintptr_t e = BB_U32(0x553f90) + BB_U32(f + 0x452) * 314u;
    BB_U32(p + 40) = e;
    uint8_t kind = BYTE(e + 0xd4);
    if (kind == 0x67 ? BYTE(e + 0xf4) != 17 && BYTE(e + 0xf4) != 10
                     : BB_U32(e + 0x38) || BYTE(0x6635bd + kind * 914u))
      BB_U32(p + 32) = 1;
  }
  if (!BB_U32(p + 32) || BB_U32(p + 40) != BB_U32(p + 44))
    BB_U32(p + 8) = 0;
  BB_U32(p + 44) = BB_U32(p + 40);
  if (!BB_U32(p + 8)) {
    if (BB_U32(p + 32)) {
      if (BB_U32(p + 24))
        --BB_U32(p + 24);
      else {
        bb_stock_panel_needs_entry(object);
        BB_U32(p + 24) = 72;
      }
    }
    return;
  }
  int32_t tick = BB_I32(p + 12);
  if (tick < 20) {
    --BB_U32(p + 20);
    BB_U32(p + 28) = BB_U32(p + 20);
  } else if (tick < 100)
    BB_U32(p + 20) = BB_U32(p + 28) - stock_sine(tick - 20);
  else
    --BB_U32(p + 20);
  uint32_t position = stock_sine(tick) + 600;
  BB_U32(p + 12) = (uint32_t)tick + 1;
  BB_U32(p + 16) = position;
  if (BB_U32(p + 12) == 120)
    BB_U32(p + 8) = 0;
}
uint32_t BB_THISCALL
bb_stock_panel_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p + 32) || BB_I32(p) > x || BB_I32(p + 4) > y)
    return 0;
  y = (int32_t)((uint32_t)y - BB_U32(p + 4));
  uintptr_t e = BB_U32(p + 40);
  if (!e)
    return 1;
  if (y > 60) {
    uint8_t profession = BYTE(e + 0xf4);
    BB_U32(0x4885e0) += 1;
    if (BB_I32(0x4885e0) > 10) {
      if (profession == 2) {
        BB_U32(0x4885e0) = 0;
        int32_t choice = bb_random() % 3;
        static const uintptr_t slots[] = {0x6634cc, 0x63f024, 0x5fe3d0,
                                          0x602304};
        if ((uint32_t)choice <= 3) {
          uint32_t sound = BB_U32(slots[choice]);
          bb_entity_feedback_c(PTR(BB_U32(p + 40)), PTR(sound), 0, 0, sound);
        }
      } else if (profession == 12) {
        uint32_t sound = BB_U32(0x66350c);
        BB_U32(0x4885e0) = 0;
        bb_entity_feedback_c(PTR(BB_U32(p + 40)), PTR(sound), 0, 0, sound);
      } else {
        bb_entity_idle_action(PTR(BB_U32(p + 40)));
        BB_U32(0x4885e0) = 0;
      }
    }
  }
  e = BB_U32(p + 40);
  if (BYTE(e + 0x11a))
    bb_entity_cargo(PTR(e), NULL);
  return 1;
}
