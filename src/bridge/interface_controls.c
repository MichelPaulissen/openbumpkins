/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "interface_controls.h"
#include "crt_errors.h"
#include "command_builders.h"
#include "dialog.h"
#include "interop.h"
#include "statistics.h"
#include "world.h"
#include <math.h>
#include <string.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#else
typedef uint32_t (*c0)(void);
typedef uint32_t (*c1)(uint32_t);
typedef uint32_t(BB_THISCALL * m0)(void *);
typedef uint32_t(BB_THISCALL * m1)(void *, uint32_t);
#define C0(a) ((c0)BB_CODE((uintptr_t)(a)))()
#define C1(a, x) ((c1)BB_CODE((uintptr_t)(a)))(x)
#define M0(a, p) ((m0)BB_CODE((uintptr_t)(a)))(PTR(p))
#define M1(a, p, x) ((m1)BB_CODE((uintptr_t)(a)))(PTR(p), x)
#endif
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

static void sprite(uintptr_t slot, uint32_t x, uint32_t y, uint32_t frame) {
  bb_sprite_draw(PTR(BB_U32(slot)), (int32_t)x, (int32_t)y, (int32_t)frame, 1);
}
static void rectangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
  bb_render_queue_insert(PTR(0x5d19e0), (int32_t)x1, (int32_t)y1, (int32_t)x2,
                         (int32_t)y2, 1);
}

/* 00415360: corners, horizontal chunks, vertical chunks, dirty rectangles. */
void bb_selection_border_entry(uint32_t x1, uint32_t y1, uint32_t x2,
                               uint32_t y2) {
  if ((int32_t)x2 < (int32_t)x1) {
    uint32_t t = x1;
    x1 = x2;
    x2 = t;
  }
  if ((int32_t)y2 < (int32_t)y1) {
    uint32_t t = y1;
    y1 = y2;
    y2 = t;
  }
  if ((int32_t)(x2 - x1) < 10)
    x2 = x1 + 10;
  if ((int32_t)(y2 - y1) < 10)
    y2 = y1 + 10;
  sprite(0x6b08a8, x1, y1, 255);
  sprite(0x6b08ac, x2 - 1, y1, 255);
  sprite(0x6b08b0, x1, y2 - 1, 255);
  sprite(0x6b08b4, x2 - 1, y2 - 1, 255);
  for (uint32_t x = x1 + 4; (int32_t)x < (int32_t)(x2 - 4);) {
    unsigned power = 5;
    while (power && (int32_t)(x2 - 4 - x) < (int32_t)(1u << power))
      --power;
    sprite(0x5e46d8 + power * 4, x, y1, 255);
    sprite(0x5e46d8 + power * 4, x, y2, 255);
    x += 1u << power;
  }
  for (uint32_t y = y1 + 4; (int32_t)y < (int32_t)(y2 - 4);) {
    unsigned power = 5;
    while (power && (int32_t)(y2 - 4 - y) < (int32_t)(1u << power))
      --power;
    sprite(0x5db020 + power * 4, x1, y, 255);
    sprite(0x5db020 + power * 4, x2, y, 255);
    y += 1u << power;
  }
  rectangle(x1 - 10, y1 - 10, x2 + 10, y1 + 10);
  rectangle(x1 - 10, y1 - 10, x1 + 10, y2 + 6);
  rectangle(x2 - 10, y1 - 10, x2 + 10, y2 + 6);
  rectangle(x1 - 10, y2 - 14, x2 + 10, y2 + 6);
}

static uintptr_t selected_actor(void) {
  uintptr_t f = BB_U32(0x4844a8);
  return BB_U32(0x553f90) + BB_U32(f + 0x452) * 314u;
}

void BB_THISCALL bb_toolbar_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  sprite(0x5db044, 0, 0, 255);
  for (unsigned i = 1; i <= 5; ++i)
    if (BB_U32(p + i * 4u))
      sprite(0x63f030, 148 + (i - 1) * 34, 4, 9 + i);
  uintptr_t letter = BB_U32(0x5db054);
  uint32_t frame = letter && !BB_U32(letter + 0x400) ? 4 : 6;
  sprite(BB_U32(p + 24) ? 0x63f030 : 0x63f0a0, 318, 4, frame);
  if (BYTE(BB_U32(0x4844a8) + 0x451)) {
    uintptr_t actor = selected_actor();
    if (BYTE(actor + 0xf7) == BYTE(0x4843f0)) {
      unsigned prof = BYTE(actor + 0xf4);
      for (int32_t i = 0; i < BB_I32(0x5e5eee + prof * 292u); ++i) {
        unsigned kind = BYTE(0x5e5ee8 + prof * 292u + (uint32_t)i);
        sprite(BYTE(actor + 0xfc) == kind ? 0x63f030 : 0x63f0a0,
               601u - (uint32_t)i * 34u, 4, BYTE(0x5f6f58 + kind * 33u));
      }
    }
  }
  BB_U32(p + 8) = BB_U32(0x5eac80);
  BB_U32(p + 12) = BB_U32(0x559128);
}
void bb_toolbar_action_entry(uint32_t index) {
  if (!BYTE(BB_U32(0x4844a8) + 0x451))
    return;
  uintptr_t actor = selected_actor();
  if (BYTE(actor + 0xf7) != BYTE(0x4843f0))
    return;
  unsigned kind = BYTE(0x5e5ee8 + BYTE(actor + 0xf4) * 292u + (uint8_t)index);
  bb_command_stat_entry(PTR(0x554fbc + BYTE(0x4843f0) * 0xd00u), 2,
                        BYTE(actor + 0xfc) == kind ? 0 : kind);
}
uint32_t BB_THISCALL
bb_toolbar_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (y > 40)
    return 0;
  if (y >= 4 && y <= 35) {
    if (x >= 147 && x <= 183) {
      BB_U32(p + 24) = 0;
      BB_U32(p + 4) = !BB_U32(p + 4);
      C1(0x42b260, BB_U32(p + 4) ? 2 : 0);
      C0(0x403190);
    } else if (x >= 183 && x <= 214) {
      M1(0x429570, 0x5eac80, !BB_U32(0x5eac80));
    } else if (x > 217 && x < 248) {
      BB_U32(0x559128) = !BB_U32(0x559128);
    } else if (x > 251 && x < 282) {
      BB_U32(p + 16) = 1;
      BB_U32(p + 24) = 0;
      BB_U32(p + 4) = 0;
      uintptr_t e = BB_U32(0x48440c);
      e = e ? BB_U32(e + 4) : BB_U32(0x554f98);
      BB_U32(0x48440c) = 0;
      for (unsigned pass = 0; pass < 2 && !BB_U32(0x48440c); ++pass) {
        if (pass)
          e = BB_U32(0x554f98);
        while (e && !BB_U32(0x48440c)) {
          if (BYTE(e + 0xf7) == BYTE(0x4843f0) && BYTE(e + 0xd4) == 0x62)
            BB_U32(0x48440c) = e;
          e = BB_U32(e + 4);
        }
      }
      if (BB_U32(0x48440c))
        bb_camera_focus_entity_entry(PTR(BB_U32(0x48440c)));
      if (BB_U32(0x48443c))
        C1(0x42b260, 0);
    } else if (x >= 285 && x < 316) {
      BB_U32(p + 24) = 0;
      BB_U32(p + 4) = 0;
      bb_mission_popup_visibility_entry(PTR(0x6a9950), 1);
    } else if (x >= 318 && x < 347) {
      BB_U32(p + 4) = 0;
      if (BB_U32(p + 24)) {
        BB_U32(p + 24) = 0;
        C1(0x42b260, 0);
      } else if (BB_U32(0x5db054)) {
        BB_U32(p + 24) = 1;
        C1(0x42b260, 3);
        C0(0x42ea50);
      }
    } else if (x >= 351 && x < 380) {
      BB_U32(p + 24) = 0;
      BB_U32(p + 4) = 0;
      C1(0x42b260, 0);
      bb_sample_play_entry(BB_U32(0x4842e4));
      bb_dialog_visibility_entry(PTR(0x5ca308), 1);
      bb_dialog_build_entry(PTR(0x5ca308), 5);
    } else if (x >= 433 && x <= 464)
      bb_toolbar_action_entry(5);
    else if (x >= 467 && x <= 498)
      bb_toolbar_action_entry(4);
    else if (x >= 501 && x <= 535)
      bb_toolbar_action_entry(3);
    else if (x >= 535 && x <= 569)
      bb_toolbar_action_entry(2);
    else if (x >= 569 && x <= 603)
      bb_toolbar_action_entry(1);
    else if (x >= 603)
      bb_toolbar_action_entry(0);
  }
  bb_sample_play_entry(BB_U32(0x4842d8));
  BYTE(0x5c5b64) = 0;
  return 1;
}

void bb_toolbar_tick_entry(void) {}

/* Inline original copies scan first, then copy forward in DWORD/byte groups. */
static void copy_text(char *out, uintptr_t source) {
  uint32_t n = 1;
  while (BYTE(source + n - 1))
    ++n;
  uintptr_t dst = (uintptr_t)out;
  while (n >= 4) {
    BB_U32(dst) = BB_U32(source);
    dst += 4;
    source += 4;
    n -= 4;
  }
  while (n--)
    BYTE(dst++) = BYTE(source++);
}
void bb_toolbar_help_c(int32_t x, int32_t y, char *out, int32_t selector) {
  (void)y;
  copy_text(out, 0x48481c);
  uintptr_t slot = 0;
  if (x < 147)
    slot = 0x603754;
  else if (x <= 183)
    slot = 0x603944;
  else if (x <= 214)
    slot = 0x6038f0;
  else if (x > 217 && x < 248)
    slot = 0x60393c;
  else if (x > 251 && x < 282)
    slot = 0x6038f4;
  else if (x >= 285 && x < 316)
    slot = 0x6037e8;
  else if (x >= 318 && x < 347)
    slot = 0x6038fc;
  else if (x >= 351 && x < 380)
    slot = 0x603940;
  else if (x >= 433 && x <= 464)
    selector = 5;
  else if (x >= 467 && x <= 498)
    selector = 4;
  else if (x >= 501 && x <= 535)
    selector = 3;
  else if (x >= 535 && x <= 569)
    selector = 2;
  else if (x >= 569 && x <= 603)
    selector = 1;
  else if (x >= 603)
    selector = 0;
  if (slot)
    copy_text(out, BB_U32(slot) ? BB_U32(slot) : 0x48481c);
  if (x >= 433 && BYTE(BB_U32(0x4844a8) + 0x451)) {
    unsigned prof = BYTE(selected_actor() + 0xf4);
    if (selector < BB_I32(0x5e5eee + prof * 292u)) {
      unsigned kind = BYTE(0x5e5ee8 + prof * 292u + (uint32_t)selector);
      copy_text(out, 0x5f6f38 + kind * 33u);
    }
  }
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((stdcall, naked))
bb_toolbar_help_entry(int32_t x __attribute__((unused)),
                      int32_t y __attribute__((unused)),
                      char *out __attribute__((unused))) {
  /* The two X gaps consume the unwritten E-4 word. Preserve it explicitly. */
  __asm__ volatile("movl -4(%esp),%eax;pushl %eax;pushl 16(%esp);"
                   "pushl 16(%esp);pushl 16(%esp);call " BB_ASM(
                       bb_toolbar_help_c) ";addl $16,%esp;ret $12");
}
#endif

void BB_THISCALL bb_build_menu_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p)) {
    if (BYTE(0x5e5cc0)) {
      sprite(0x6033c8, 400, BB_U32(0x484448) - BB_U32(BB_U32(0x6033c8) + 0x48),
             255);
      rectangle(400, BB_U32(0x484448) - BB_U32(BB_U32(0x6033c8) + 0x48), 500,
                BB_U32(0x484448) - 1);
    }
    return;
  }
  int32_t step = (int32_t)(BB_U32(BB_U32(0x5e4794) + 0x44) - 1);
  int32_t index = BB_I32(p + 8) / step;
  uint32_t x = 0u - (uint32_t)(BB_I32(p + 8) % step), y = BB_U32(p + 16);
  int restricted = BB_U32(0x48474c) != 0;
  while ((int32_t)x < BB_I32(0x484444)) {
    if (index >= BYTE(BB_U32(0x4844a8) + 0x2be))
      break;
    sprite(0x5e4794, x, y, 2);
    unsigned k = BYTE(BB_U32(0x4844a8) + 0x27e + (uint32_t)index);
    unsigned icon = BYTE(0x6638c4 + k * 914u);
    int blocked = restricted && BYTE(0x6638c5 + k * 914u) >= 2;
    sprite(icon >= 128 ? 0x6afbe4
           : blocked   ? 0x5fe28c
                       : 0x5ca300,
           x + 5, y + 7, icon & 127u);
    char text[32];
    if (blocked)
      copy_text(text, 0x486f5c);
    else {
      k = BYTE(BB_U32(0x4844a8) + 0x27e + (uint32_t)index);
      bb_crt_errors_itoa_entry(
          WORD(0x6638b8 + k * 914u), text, 10);
    }
    bb_font_draw(PTR(0x5f74b8), text, (int32_t)(x + 35), (int32_t)(y + 85), 1);
    ++index;
    x += BB_U32(BB_U32(0x5e4794) + 0x44) - 1;
  }
  if ((int32_t)x < BB_I32(0x484444))
    sprite(0x4eebc4, x, y, 0);
  rectangle(0, BB_U32(0x484448) - BB_U32(BB_U32(0x5e4794) + 0x48) - 1,
            BB_U32(0x484444) - 1, BB_U32(0x484448) - 1);
}
uint32_t BB_THISCALL
bb_build_menu_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (y <= BB_I32(p + 16))
    return 0;
  int32_t step = (int32_t)(BB_U32(BB_U32(0x5e4794) + 0x44) - 1);
  int32_t index = (int32_t)(BB_U32(p + 8) + (uint32_t)x) / step;
  if (index >= BYTE(BB_U32(0x4844a8) + 0x2be))
    index = (int32_t)BYTE(BB_U32(0x4844a8) + 0x2be) - 1;
  if (index < 0)
    index = 0;
  unsigned k = BYTE(BB_U32(0x4844a8) + 0x27e + (uint32_t)index);
  bb_sample_play_entry(BB_U32(0x4842d8));
  BYTE(0x554fa4) = 0;
  int sufficient = WORD(0x6638b8 + k * 914u) <= BB_I32(BB_U32(0x4844a8) + 0x4a);
  int blocked = sufficient && BB_U32(0x48474c) &&
                BYTE(0x6638c5 + k * 914u) >= 2 && BB_U32(0x48474c);
  if (sufficient && !blocked) {
    BYTE(0x5c5b64) = BYTE(BB_U32(0x4844a8) + 0x27e + (uint32_t)index);
    return 1;
  }
  bb_sample_play_entry(BB_U32(0x4842e0));
  uintptr_t slot = blocked ? 0x603930 : 0x603934;
  const char *format = PTR(BB_U32(slot) ? BB_U32(slot) : 0x48481c);
#ifdef BB_MODEL_RUNTIME
  bb_crt_output_words(PTR(0x6033d8),256,format,NULL,0);
#else
  ((int (*)(char *, const char *, ...))BB_CODE((uintptr_t)0x46bae0))(PTR(0x6033d8),
                                                            format);
#endif
  bb_message_append_entry(PTR(0x5ecf38), 0x6033d8, 1);
  return 1;
}
void BB_THISCALL bb_build_menu_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  uint32_t target = BB_U32(p + 4), y = BB_U32(p + 16);
  uint32_t diff = target - y;
  uint32_t quarter =
      (uint32_t)((int32_t)(diff + ((int32_t)diff < 0 ? 3u : 0u)) >> 2);
  if ((int32_t)y < (int32_t)(target - 3))
    BB_U32(p + 16) = y + quarter + 3;
  else if ((int32_t)(target + 3) < (int32_t)y)
    BB_U32(p + 16) = y + quarter - 3;
  else {
    BB_U32(p + 16) = target;
    if (target == BB_U32(0x484448))
      BB_U32(p) = 0;
  }
}
void BB_THISCALL bb_build_menu_open_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  uint32_t y = BB_U32(0x484448) - BB_U32(BB_U32(0x5e4794) + 0x48);
  BB_U32(p) = 1;
  BB_U32(p + 4) = y;
  bb_sample_play_entry(BB_U32(0x4842ec));
}
void BB_THISCALL bb_build_menu_close_entry(void *object) {
  BB_U32((uintptr_t)object + 4) = BB_U32(0x484448);
  bb_sample_play_entry(BB_U32(0x4842f0));
}
void bb_interface_resources_refresh_entry(void) {
  BB_U32(0x484288) = 1;
  C0(0x403190);
  M0(0x401e60, 0x5590e8);
  M0(0x401ea0, 0x5590e8);
  BB_U32(0x484290) = 1;
  static const uintptr_t slots[] = {
      0x6aff40, 0x600034, 0x604698, 0x6aff48, 0x6024bc, 0x6024c0, 0x6024a0,
      0x6024a4, 0x6024a8, 0x6024ac, 0x6024b0, 0x5c950c, 0x55911c};
  for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i)
    if (BB_U32(slots[i]))
      bb_sprite_remap(PTR(BB_U32(slots[i])));
  C1(0x403270, 0);
  BB_U32(0x484290) = 0;
  M0(0x401e30, 0x5590e8);
  M0(0x401e80, 0x5590e8);
}
void bb_observer_display_refresh_entry(void) {
  M0(0x401e60, 0x5590e8);
  M0(0x401ea0, 0x5590e8);
  BB_U32(0x484290) = 1;
  M0(0x4324c0, 0x4eebc8);
  BB_U32(0x484288) = 0;
  C0(0x403190);
  BB_U32(0x484290) = 0;
  M0(0x401e30, 0x5590e8);
  M0(0x401e80, 0x5590e8);
}
void bb_camera_focus_entity_entry(void *object) {
  uintptr_t e = (uintptr_t)object;
  if (!e)
    return;
  uint32_t x = BB_U32(e + 0x62), y = BB_U32(e + 0x66), z = BB_U32(e + 0x6a);
  BB_U32(0x6a8f28) =
      (uint32_t)((int32_t)bb_fixed_multiply((int32_t)(x - y), 0x270000) >> 16);
  uint32_t py =
      (uint32_t)((int32_t)bb_fixed_multiply((int32_t)(x + y), 0x140000) >> 16);
  py -= (uint32_t)((int32_t)bb_fixed_multiply((int32_t)z, 0xb0000) >> 16);
  --py;
  BB_U32(0x6a8f24) = py;
  uintptr_t image = BB_U32(e + 0x58);
  BB_U32(0x6a8f28) +=
      (uint32_t)(BB_I32(image + 0x44) / 2) - BB_U32(image + 0x54);
  image = BB_U32(e + 0x58);
  uint32_t half = (uint32_t)(BB_I32(image + 0x48) / 2);
  py -= BB_U32(image + 0x58);
  BB_U32(0x4846ec) = 1;
  BB_U32(0x6a8f24) = py + half;
}
void bb_camera_focus_tile_entry(uint32_t tx, uint32_t ty) {
  tx = (uint8_t)tx;
  ty = (uint8_t)ty;
  uint32_t x = (tx << 16) + 0x8000, y = (ty << 16) + 0x8000;
  uint32_t z = (uint32_t)BYTE(0x559b65 + (ty * 192u + tx) * 12u) << 16;
  BB_U32(0x6a8f28) =
      (uint32_t)((int32_t)bb_fixed_multiply((int32_t)(x - y), 0x270000) >> 16);
  uint32_t py =
      (uint32_t)((int32_t)bb_fixed_multiply((int32_t)(x + y), 0x140000) >> 16);
  uint32_t pz =
      (uint32_t)((int32_t)bb_fixed_multiply((int32_t)z, 0xb0000) >> 16);
  BB_U32(0x4846ec) = 1;
  BB_U32(0x6a8f24) = py - pz - 1;
}
