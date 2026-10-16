/* SPDX-License-Identifier: GPL-3.0-only */
#include "selection_panels.h"
#include "abi.h"
#include "interop.h"
#include "object_initializers.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#else
typedef uint32_t (*c1)(uint32_t);
typedef uint32_t (*c4)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*c6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                       uint32_t);
typedef uint32_t(BB_THISCALL *m2)(void *, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *m3)(void *, uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *m4)(void *, uint32_t, uint32_t, uint32_t,
                                  uint32_t);
typedef uint32_t(BB_THISCALL *m5)(void *, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t);
#define C1(a, x) ((c1)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x))
#define C4(a, x, y, z, w) ((c4)BB_CODE((uintptr_t)(a)))(x, y, z, w)
#define C6(a, x, y, z, w, v, u) ((c6)BB_CODE((uintptr_t)(a)))(x, y, z, w, v, u)
#define M2(a, p, x, y)                                                         \
  ((m2)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),              \
                                (uint32_t)(uintptr_t)(y))
#define M3(a, p, x, y, z)                                                      \
  ((m3)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x), y, z)
#define M4(a, p, x, y, z, w)                                                   \
  ((m4)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x), y, z, w)
#define M5(a, p, x, y, z, w, v)                                                \
  ((m5)BB_CODE((uintptr_t)(a)))(PTR(p), x, y, z, w, v)
typedef int (*format_fn)(char *, const char *, ...);
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#endif

static void panel_rectangle(uintptr_t p, int both) {
  uint32_t x = BB_U32(p + 8), y = BB_U32(p + 12);
  uint32_t bottom = BB_U32(BB_U32(0x005590d8) + 0x48) + y + 20;
  uint32_t right = BB_U32(BB_U32(0x005590d8) + 0x44) + x;
  if (both)
    M4(0x00418a10, 0x005d19e0, x, y, right, bottom);
  else
    M5(0x00418a70, 0x005d19e0, x, y, right, bottom, 1);
}
static void icon(uintptr_t p, uintptr_t slot, unsigned index, unsigned frame) {
  uint32_t y = BB_U32(0x00486f88 + index * 4u) + BB_U32(p + 12);
  uint32_t x = BB_U32(0x00486f68 + index * 4u) + BB_U32(p + 8);
  M4(0x0042fd90, BB_U32(slot), x, y, frame, 1);
}

void BB_THISCALL bb_action_panel_draw_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (!BB_U32(p) || !BB_U32(p + 4)) {
    if (BYTE(p + 32)) {
      panel_rectangle(p, 1);
      --BYTE(p + 32);
    }
    return;
  }
  M4(0x0042fd90, BB_U32(0x005590d8), BB_U32(p + 8), BB_U32(p + 12), 255, 1);
  int32_t step = BB_I32(p + 16);
  if (step > 3)
    M4(0x0042fd90, BB_U32(0x0060245c), BB_U32(p + 8), BB_U32(p + 12),
       step / 4 - 1, 1);
  for (unsigned i = 0; i < 7; ++i) {
    uint32_t selected = BB_U32(p + 28);
    if (selected && selected - i == 1) {
      unsigned frame = i + (BYTE(BB_U32(p + 4) + 0xf4) == 9 ? 0 : 7);
      icon(p, 0x005e476c, i, frame);
    } else {
      if (BYTE(BB_U32(p + 4) + 0xf4) == 9 &&
          BYTE(BB_U32(0x004844a8) + 0x9e0 + i))
        icon(p, 0x006a907c, i, i);
      if (BYTE(BB_U32(p + 4) + 0xf4) == 3 &&
          BYTE(BB_U32(0x004844a8) + 0x9f0 + i))
        icon(p, 0x006a907c, i, i + 7);
    }
  }
  uintptr_t bitmap = BB_U32(0x005590d8), entity = BB_U32(p + 4);
  uint32_t bottom = BB_U32(bitmap + 0x48) + WORD(p + 12) + 4;
  uint32_t x = (bitmap & 0xffff0000u) | WORD(p + 8);
  uint32_t maximum = BYTE(entity + 0x10a), value = BYTE(entity + 0x109);
  uint32_t width = BB_U32(BB_U32(0x005590d8) + 0x44);
  uint32_t right = (width & 0xffff0000u) | (uint16_t)(width + x);
  C6(0x0043ef00, x, bottom, right, bottom, value, maximum);
  if (BYTE(0x005e4740)) {
    uint32_t y = BB_U32(BB_U32(0x005590d8) + 0x48) + BB_U32(p + 12) + 8;
    uint32_t rate = BYTE(0x00640f20 + BYTE(0x005e4740));
    int32_t product = (int32_t)(rate * BB_U32(BB_U32(0x005590d8) + 0x44));
    uint32_t at = BB_U32(p + 8) + (uint32_t)(product / 255);
    M4(0x0042fd90, BB_U32(0x006afcfc), at, y, 0, 1);
  }
  panel_rectangle(p, 0);
  BYTE(p + 32) = (uint8_t)(BYTE(0x00484454) * 2);
}
void BB_THISCALL bb_action_panel_visibility_entry(void *object,
                                                  uint32_t visible) {
  uint32_t p = bb_address_encode(object);
  if (BB_U32(p) == visible)
    return;
  panel_rectangle(p, 1);
  BB_U32(p) = visible;
  if (!visible)
    BYTE(0x005e4740) = 0;
}
void BB_THISCALL bb_action_panel_tick_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  int32_t step = BB_I32(p + 16);
  if (BB_U32(p + 20)) {
    if (step < 35)
      BB_U32(p + 16) = (uint32_t)step + 1;
  } else if (step > 0)
    BB_U32(p + 16) = (uint32_t)step - 1;
  if (BB_U32(p + 24))
    --BB_U32(p + 24);
  uintptr_t entity = 0, faction = BB_U32(0x004844a8);
  if (BYTE(faction + 0x451) == 1) {
    entity = BB_U32(0x00553f90) + BB_U32(faction + 0x452) * 314u;
    uint8_t profession = BYTE(entity + 0xf4);
    if ((profession != 9 && profession != 3) ||
        BYTE(entity + 0xf7) != BYTE(0x004843f0))
      entity = 0;
  }
  if (entity) {
    if (!BB_U32(p))
      bb_action_panel_visibility_entry(object, 1);
  } else if (BB_U32(p))
    bb_action_panel_visibility_entry(object, 0);
  BB_U32(p + 4) = entity;
}
void BB_THISCALL bb_action_panel_expand_entry(void *object, uint32_t expand) {
  uint32_t p = bb_address_encode(object);
  BB_U32(p + 20) = expand;
  if (expand)
    BB_U32(p + 24) = 72;
}
void BB_THISCALL bb_action_panel_button_entry(void *object, uint32_t button) {
  BB_U32((uintptr_t)object + 28) = button;
  C1(0x00415860, BB_U32(0x004842d8));
}
void BB_THISCALL bb_action_panel_spell_begin_entry(void *object,
                                                   uint32_t spell) {
  if (!BB_U32((uintptr_t)object + 4))
    return;
  bb_action_panel_expand_entry(PTR(0x006b0858), 1);
  C1(0x00415730, BB_U32(0x005f9140));
  BYTE(0x005c5b64) = 0;
  BYTE(0x005e4740) = spell;
}
void BB_THISCALL bb_action_panel_spell_cast_entry(void *object, uint32_t spell,
                                                  uint32_t x, uint32_t y) {
  uint32_t p = bb_address_encode(object), entity = BB_U32(p + 4);
  if (!entity) {
    bb_action_panel_button_entry(object, 0);
    BYTE(0x005e4740) = 0;
    return;
  }
  if ((uint8_t)spell == 0x65) {
    uintptr_t tile = 0x00559b60 + ((uint8_t)y * 192u + (uint8_t)x) * 12u;
    if (BYTE(tile + 6) || !(BB_U32(0x006a9092 + BYTE(tile) * 8u) & 4) ||
        (BYTE(tile + 8) & 2)) {
      C1(0x00415860, BB_U32(0x004842e0));
      return;
    }
  }
  int32_t distance = (int32_t)C4(0x00434de0, (uint8_t)x, (uint8_t)y,
                                 BYTE(entity + 0xd8), BYTE(entity + 0xd9));
  if (distance > 0xe0000) {
    uintptr_t format = BB_U32(0x0060394c);
    if (!format)
      format = 0x0048481c;
    FORMAT((char *)0x006033d8, (const char *)format);
    M2(0x0041e310, 0x005ecf38, 0x006033d8, 1);
    C1(0x00415860, BB_U32(0x004842e0));
    return;
  }
  bb_action_panel_button_entry(object, 0);
  uintptr_t faction = 0x00554fbc + BYTE(0x004843f0) * 0xd00u;
  M3(0x0042dd40, faction, (uint8_t)spell, x, y);
  BYTE(0x005e4740) = 0;
  bb_action_panel_expand_entry(PTR(0x006b0858), 0);
}
uint32_t BB_THISCALL bb_action_panel_click_entry(void *object, int32_t x,
                                                 int32_t y) {
  uint32_t p = bb_address_encode(object);
  if (!BYTE(p + 32))
    return 0;
  for (unsigned i = 0; i < 7; ++i) {
    int32_t left = BB_I32(0x004820e0 + i * 4u),
            top = BB_I32(0x00482100 + i * 4u);
    if (left > x || top > y || (int32_t)((uint32_t)left + 32) < x ||
        (int32_t)((uint32_t)top + 32) < y || !BB_U32(p + 4))
      continue;
    uint8_t profession = BYTE(BB_U32(p + 4) + 0xf4);
    if (BYTE(BB_U32(0x004844a8) + 0x9e0 + i + (profession == 3 ? 16 : 0))) {
      bb_action_panel_button_entry(object, i + 1);
      static const uint8_t special[] = {0x48, 0x4c, 0x51, 0x65,
                                        0x4b, 0x50, 0x47};
      static const uint8_t ordinary[] = {0x66, 0x4d, 0x4a, 0x4f,
                                         0x49, 0x4e, 0x67};
      /* The button feedback callback can change the selected profession. */
      profession = BYTE(BB_U32(p + 4) + 0xf4);
      bb_action_panel_spell_begin_entry(object, profession == 9 ? special[i]
                                                                : ordinary[i]);
    } else
      C1(0x00415860, BB_U32(0x004842e0));
    return 1;
  }
  return 0;
}

void BB_THISCALL bb_building_panel_set_entry(void *object, void *entity) {
  C1(0x00415860, BB_U32(0x004842f0));
  BB_U32((uintptr_t)object) = bb_address_encode(entity);
}
void BB_THISCALL bb_building_panel_draw_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t left = (uint32_t)(BB_I32(0x00484444) / 2) -
                  (uint32_t)(BB_I32(BB_U32(0x00602468) + 0x44) / 2);
  BB_U32(p + 6) = left;
  uint32_t top = (uint32_t)(BB_I32(0x00484448) / 2) -
                 (uint32_t)(BB_I32(BB_U32(0x00602468) + 0x48) / 2);
  BB_U32(p + 10) = top;
  uint32_t right = BB_U32(BB_U32(0x00602468) + 0x44) + left;
  uint32_t visible = BB_U32(p);
  BB_U32(p + 14) = right;
  uint32_t bottom = BB_U32(BB_U32(0x00602468) + 0x48) + top;
  BB_U32(p + 18) = bottom;
  if (visible) {
    M4(0x0042fd90, BB_U32(0x00602468), left, top, 255, 1);
    M5(0x00418a70, 0x005d19e0, BB_U32(p + 6), BB_U32(p + 10), BB_U32(p + 14),
       BB_U32(p + 18), 0);
    BYTE(p + 4) = (uint8_t)(BYTE(0x00484454) * 2);
  } else if (BYTE(p + 4)) {
    M5(0x00418900, 0x005d19e0, left, top, right, bottom, 1);
    --BYTE(p + 4);
  }
}
void BB_THISCALL bb_building_panel_noop_entry(void *object) { (void)object; }
void BB_THISCALL bb_building_panel_left_entry(void *object, uint32_t row,
                                              uint32_t amount) {
  (void)object;
  (void)row;
  (void)amount;
  C1(0x00415860, BB_U32(0x004842d8));
}
void BB_THISCALL bb_building_panel_right_entry(void *object, uint32_t row,
                                               uint32_t amount) {
  (void)object;
  (void)row;
  (void)amount;
  C1(0x00415860, BB_U32(0x004842d8));
}
uint32_t BB_THISCALL bb_building_panel_click_entry(void *object, int32_t x,
                                                   int32_t y) {
  uint32_t p = bb_address_encode(object);
  if (!BB_U32(p) || BB_I32(p + 6) > x || BB_I32(p + 10) > y ||
      BB_I32(p + 14) < x || BB_I32(p + 18) < y)
    return 0;
  x = (int32_t)((uint32_t)x - BB_U32(p + 6));
  y = (int32_t)((uint32_t)y - BB_U32(p + 10));
  if ((x > 151 && x < 175) || (x > 343 && x < 367)) {
    static const int32_t stops[] = {31,  49,  65,  83,  99,
                                    117, 133, 151, 167, 182};
    for (unsigned i = 0; i < 10; ++i)
      if (y < stops[i]) {
        if (x < 175)
          bb_building_panel_left_entry(object, i / 2 + 1, i & 1 ? -1u : 1);
        else
          bb_building_panel_right_entry(object, i / 2 + 1, i & 1 ? -1u : 1);
        break;
      }
  }
  if (x > 127 && x < 253 && y > 189 && y < 220) {
    C1(0x00415860, BB_U32(0x004842d8));
    bb_building_panel_set_entry(object, NULL);
  }
  return 1;
}

void BB_THISCALL bb_tooltip_draw_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  if (BB_U32(p + 4)) {
    M4(0x00430560, 0x0063f0d0, p + 34, BB_U32(p + 10), BB_U32(p + 14), 1);
    M4(0x00418a10, 0x005d19e0, BB_U32(p + 18), BB_U32(p + 22), BB_U32(p + 26),
       BB_U32(p + 30));
    BYTE(p + 8) = BYTE(0x00484454);
  } else if (BYTE(p + 8)) {
    if (BB_I32(p + 26) > BB_I32(p + 18) && BB_I32(p + 30) > BB_I32(p + 22))
      M4(0x00418a10, 0x005d19e0, BB_U32(p + 18), BB_U32(p + 22), BB_U32(p + 26),
         BB_U32(p + 30));
    --BYTE(p + 8);
  }
}
