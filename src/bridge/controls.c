/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "controls.h"
#include "interop.h"
#include <limits.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#else
typedef uint32_t (*c1)(uint32_t);
typedef uint32_t (*c2)(uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m1)(void *, uint32_t);
typedef uint32_t(BB_THISCALL * m2)(void *, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m3)(void *, uint32_t, uint32_t,
                                                 uint32_t);
typedef uint32_t(BB_THISCALL * m4)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL * m5)(void *, uint32_t, uint32_t,
                                                 uint32_t, uint32_t, uint32_t);
#define M5(a, p, x, y, z, w, v) ((m5)BB_CODE((uintptr_t)(a)))(PTR(p), x, y, z, w, v)
#define C1(a, x) ((c1)BB_CODE((uintptr_t)(a)))(x)
#define C2(a, x, y) ((c2)BB_CODE((uintptr_t)(a)))(x, y)
#define M1(a, p, x) ((m1)BB_CODE((uintptr_t)(a)))(PTR(p), x)
#define M2(a, p, x, y) ((m2)BB_CODE((uintptr_t)(a)))(PTR(p), x, y)
#define M3(a, p, x, y, z) ((m3)BB_CODE((uintptr_t)(a)))(PTR(p), x, y, z)
#define M4(a, p, x, y, z, w) ((m4)BB_CODE((uintptr_t)(a)))(PTR(p), x, y, z, w)
#endif



void bb_ambience_noop_entry(void) {}
void bb_ambience_tick_entry(void) {
  for (uintptr_t r = 0x6021a8; r < 0x602270; r += 20) {
    BB_U32(r) = 0;
    BB_U32(r + 16) = 2000;
  }
  for (uintptr_t e = BB_U32(0x554f98); e; e = BB_U32(e + 4)) {
    unsigned category = BYTE(0x6638b6 + BYTE(e + 0xd4) * 914u);
    uintptr_t row = 0;
    if (category)
      row = 0x6021a8 + category * 20u;
    else if (BB_U32(e + 0x9c) & 0x80000)
      row = 0x6021bc;
    else if (BYTE(e + 0xd4) == 0x68 &&
             (BYTE(e + 0xf4) == 24 || BYTE(e + 0xf4) == 28))
      row = 0x602234;
    if (!row)
      continue;
    int32_t d = (int32_t)bb_entity_distance(BYTE(e + 0xd8), BYTE(e + 0xd9),
                                            BB_U32(0x4846e4), BB_U32(0x4846e8));
    if (d < 0xf0000) {
      /* Category metadata is live across the distance callback. */
      if (category)
        row = 0x6021a8 + BYTE(0x6638b6 + BYTE(e + 0xd4) * 914u) * 20u;
      ++BB_U32(row);
      if (category)
        row = 0x6021a8 + BYTE(0x6638b6 + BYTE(e + 0xd4) * 914u) * 20u;
      if (d < BB_I32(row + 16))
        BB_U32(row + 16) = (uint32_t)d;
    }
  }
  uint32_t cy = BB_U32(0x4846e8), cx = BB_U32(0x4846e4);
  if ((int32_t)(cy - 5) <= (int32_t)(cy + 5)) {
    uintptr_t tile = 0x559b60 + (cy - 5) * 192u * 12u + (cx - 5) * 12u;
    for (unsigned y = 0; y < 11; ++y, tile += 0x900) {
      if ((int32_t)(cx - 5) <= (int32_t)(cx + 5)) {
        for (unsigned x = 0; x < 11; ++x) {
          uintptr_t t = tile + x * 12u;
          unsigned category = BYTE(0x6a9096 + BYTE(t) * 8u);
          if (category) {
            BB_U32(0x6021b8 + category * 20u) = 0;
            ++BB_U32(0x6021a8 + category * 20u);
          } else if (BB_U32(t + 8) & 0x200000)
            ++BB_U32(0x6021d0);
        }
      }
    }
  }
  if (BB_U32(0x602248) < BB_U32(0x60225c))
    BB_U32(0x602248) = 0;
  else
    BB_U32(0x60225c) = 0;
  if (BB_U32(0x602248) < BB_U32(0x6021d0))
    BB_U32(0x602248) = 0;
  else
    BB_U32(0x6021d0) = 0;
  if (BB_U32(0x60225c) < BB_U32(0x6021d0))
    BB_U32(0x60225c) = 0;
  else
    BB_U32(0x6021d0) = 0;
  if (!BB_U32(0x602248) && !BB_U32(0x60225c) && !BB_U32(0x6021d0))
    C2(0x459b80, 0, 1);
  static const uint8_t start[] = {0, 7, 12, 10, 11, 6, 0, 9, 13, 14};
  static const uint8_t gain[] = {0, 4, 0, 10, 8, 2, 0, 6, 0, 0};
  static const uint8_t stop[] = {0, 7, 0, 10, 11, 6, 0, 9, 0, 0};
  for (unsigned i = 1; i < 10; ++i) {
    uintptr_t r = 0x6021a8 + i * 20u;
    int active = 0;
    if (BB_U32(r)) {
      int32_t ratio = (int32_t)bb_fixed_divide(
          (int32_t)bb_fixed_multiply(0x10000, (int32_t)(BB_U32(r) << 16)),
          (int32_t)(BB_U32(r + 4) << 16));
      if (ratio > 0x10000)
        ratio = 0x10000;
      int32_t d = BB_I32(r + 16);
      uint32_t attenuation =
          d < 0x50000 ? 0x10000
          : d < 0xf0000
              ? bb_fixed_divide((int32_t)(0xf0000u - (uint32_t)d), 0xa0000)
              : 0;
      int32_t level =
          (int32_t)bb_fixed_multiply(
              (int32_t)bb_fixed_multiply(ratio, (int32_t)attenuation),
              0x7f0000) >>
          16;
      if (level) {
        if (!BB_U32(r + 12)) {
          BB_U32(r + 12) = 1;
          if (start[i])
            C2(0x459b80, start[i], 1);
        }
        active = 1;
      }
      if (gain[i])
        C2(0x459ca0, gain[i], (uint32_t)level);
    }
    if (!active && BB_U32(r + 12)) {
      BB_U32(r + 12) = 0;
      if (stop[i])
        C2(0x459b80, stop[i], 0);
    }
  }
}


void BB_THISCALL bb_digit_set_entry(void *object,
                                                  uint32_t value) {
  BYTE((uintptr_t)object + 4) = (uint8_t)value;
}
uint16_t BB_STDCALL bb_digit_direction_entry(uint32_t x,
                                                           int32_t y) {
  (void)x;
  if (y < 11) {
    C1(0x415860, BB_U32(0x4842d8));
    return 1;
  }
  if (y > 34) {
    C1(0x415860, BB_U32(0x4842d8));
    return 0xffff;
  }
  return 0;
}


static int32_t decimal_power(unsigned n) {
  uint32_t value = 1, base = 10;
  do {
    if (n & 1)
      value *= base;
    n >>= 1;
    if (n)
      base *= base;
  } while (n);
  return (int32_t)value;
}
/* The original conversion returns the integer-indefinite value for overflow.
 * Preserve its consumed low byte, including out-of-range editor input. */
static int64_t truncate_integer(long double value) {
  if (!(value >= -0x1p63L && value < 0x1p63L))
    return INT64_MIN;
  return (int64_t)value;
}
void BB_THISCALL bb_number_set_entry(void *object,
                                                   uint32_t value) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0x72) = value;
  int32_t remaining = (int32_t)value;
  unsigned count = BYTE(p + 0x70);
  for (unsigned i = 0; i < count; ++i) {
    unsigned n = BYTE(p + 0x70) - i - 1;
    double power = decimal_power(n);
    uint8_t digit = (uint8_t)truncate_integer((long double)remaining / power);
    bb_digit_set_entry(PTR(p + i * 14u), digit);
    count = BYTE(p + 0x70);
    power = decimal_power(count - i - 1);
    remaining =
        (int32_t)((uint32_t)remaining -
                  (uint32_t)truncate_integer((long double)digit * power));
  }
}
void BB_THISCALL bb_number_adjust_entry(void *object,
                                                      uint32_t delta) {
  uintptr_t p = (uintptr_t)object;
  int32_t value = (int32_t)(BB_U32(p + 0x72) + delta);
  if (value >= 0 && value < decimal_power(BYTE(p + 0x70)))
    BB_U32(p + 0x72) = (uint32_t)value;
  bb_number_set_entry(object, BB_U32(p + 0x72));
}
uint32_t BB_THISCALL bb_number_click_entry(void *object,
                                                         int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  int32_t left = BB_I32(p + 0x76), top = BB_I32(p + 0x7a);
  if (x < left || y < top ||
      x > (int32_t)((uint32_t)left + BYTE(p + 0x70) * 28u) ||
      y > (int32_t)((uint32_t)top + 45))
    return 0;
  int32_t index = (int32_t)((uint32_t)x - (uint32_t)left) / 28;
  if (index < 0)
    index = 0;
  if (index > BYTE(p + 0x70) - 1)
    index = BYTE(p + 0x70) - 1;
  int16_t direction = (int16_t)bb_digit_direction_entry(
      (uint32_t)x - (uint32_t)left - (uint32_t)index * 28u,
      (int32_t)((uint32_t)y - (uint32_t)top));
  if (index >= 0 && index < 4) {
    static const unsigned steps[] = {1000, 100, 10, 1};
    bb_number_adjust_entry(object,
                           (uint32_t)(direction * (int32_t)steps[index]));
  }
  return 1;
}


void bb_numeric_panel_draw_entry(void) {}
uint32_t BB_THISCALL
bb_numeric_panel_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p) && BB_U32(BB_U32(p) + 0x38))
    for (unsigned i = 0; i < 4; ++i)
      if (bb_number_click_entry(PTR(p + 4 + i * 126u), x, y))
        return 1;
  return 0;
}
void BB_THISCALL bb_numeric_panel_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  uint32_t target = BB_U32(p + 0x200), current = BB_U32(p + 0x208);
  if ((int32_t)current < (int32_t)(target - 3)) {
    if (!BB_U32(0x4846ec)) {
      if (!BB_U32(p + 0x1fc))
        C1(0x415860, BB_U32(0x4842ec));
      BB_U32(p + 0x1fc) = 1;
      uint32_t difference = BB_U32(p + 0x200) - BB_U32(p + 0x208);
      BB_U32(p + 0x208) = BB_U32(p + 0x208) + (int32_t)difference / 4 + 3u;
    }
  } else if ((int32_t)(target + 3) < (int32_t)current)
    BB_U32(p + 0x208) = current + (int32_t)(target - current) / 4 - 3u;
  else
    BB_U32(p + 0x208) = target;
}

void bb_editor_remove_entry(uint32_t faction, uint32_t x, uint32_t y) {
  unsigned tx = (uint8_t)x, ty = (uint8_t)y;
  int changed = 0;
  for (uintptr_t e = M2(0x433e20, 0x553f90, tx, ty); e;
       e = M3(0x433f10, 0x553f90, e, tx, ty)) {
    unsigned owner = BYTE(e + 0xf7), kind = BYTE(e + 0xd4);
    if ((owner == (uint8_t)faction && kind != 0x67) ||
        (owner == 4 && (kind == 0 || kind == 5 || kind == 6))) {
      changed = 1;
      M1(0x4342c0, 0x553f90, e);
    }
  }
  uintptr_t tile = 0x559b60 + (ty * 192u + tx) * 12u;
  if (BYTE(tile + 1) ||
      ((BB_U32(0x6a9092 + BYTE(tile) * 8u) & 0x100) && !BYTE(tile + 6))) {
    changed = 1;
    BYTE(tile + 1) = 0;
    C2(0x418360, x, y);
  }
  if (changed)
    C1(0x415860, BB_U32(0x4842f8));
}
void bb_editor_heights_copy_entry(void) {
  for (unsigned i = 0; i < 0x9000; ++i)
    BYTE(0x5db190 + i) = BYTE(0x559b65 + i * 12u);
}
uint32_t bb_editor_height_relax_entry(uint32_t raise, uint32_t x, uint32_t y) {
  int32_t left = (uint8_t)x - 5, top = (uint8_t)y - 5;
  if (left < 2)
    left = 2;
  if (top < 2)
    top = 2;
  int32_t width = left + 10 >= 190 ? 190 - left : 10,
          height = top + 10 >= 190 ? 190 - top : 10;
  int changed = 0;
  for (unsigned ty = (uint8_t)top; (int32_t)ty < top + height;
       ty = (uint8_t)(ty + 1))
    for (unsigned tx = (uint8_t)left; (int32_t)tx < left + width;
         tx = (uint8_t)(tx + 1)) {
      uintptr_t h = 0x5db190 + ty * 192u + tx;
      uint8_t value = BYTE(h);
      if (BYTE(h + 384) == value && BYTE(h + 192) < value) {
        if (raise)
          ++BYTE(h + 192);
        else {
          BYTE(h) = (uint8_t)(value - 1);
          BYTE(h + 384) = (uint8_t)(value - 2);
        }
        changed = 1;
      }
      if (BYTE(h - 2) == value && BYTE(h - 1) < value) {
        if (raise)
          ++BYTE(h - 1);
        else {
          --BYTE(h);
          --BYTE(h - 2);
        }
        changed = 1;
      }
    }
  return changed;
}
uint32_t bb_editor_height_change_entry(uint32_t raise, uint32_t x, uint32_t y) {
  uintptr_t h = 0x5db190 + (uint8_t)y * 192u + (uint8_t)x;
  if (!raise) {
    if (!BYTE(h))
      return 2;
    --BYTE(h);
    return 0;
  }
  ++BYTE(h);
  uint32_t result;
  do {
    result = bb_editor_height_relax_entry(raise, x, y);
  } while (result == 1);
  return result == 2 ? 2 : 0;
}
uint32_t bb_editor_height_probe_entry(uint32_t raise, uint32_t x, uint32_t y) {
  bb_editor_heights_copy_entry();
  if (bb_editor_height_change_entry(raise, x, y))
    return 0;
  for (unsigned ty = 0; ty < 192; ++ty)
    for (unsigned tx = 0; tx < 192; ++tx) {
      unsigned i = ty * 192u + tx;
      if (BYTE(0x5db190 + i) != BYTE(0x559b65 + i * 12u) &&
          M2(0x433e20, 0x553f90, tx, ty))
        return 0;
    }
  return 1;
}
uint32_t bb_editor_height_apply_entry(uint32_t raise, uint32_t x, uint32_t y) {
  if (!bb_editor_height_probe_entry(raise, x, y))
    return 0;
  for (unsigned ty = 1; ty < 191; ++ty)
    for (unsigned tx = 1; tx < 191; ++tx) {
      unsigned i = ty * 192u + tx;
      uintptr_t tile = 0x559b60 + i * 12u;
      if (BYTE(0x5db190 + i) == BYTE(tile + 5))
        continue;
      if (!BYTE(0x5db190 + i))
        BYTE(tile) = (uint8_t)(bb_random() % 2 + 6);
      else if (!BYTE(tile + 5))
        BYTE(tile) = (uint8_t)(bb_random() % 6);
      BYTE(tile + 5) = BYTE(0x5db190 + i);
      C2(0x4186e0, tx, ty);
      C2(0x4186e0, tx - 1, ty);
      C2(0x4186e0, tx + 1, ty);
      C2(0x4186e0, (uint8_t)tx, (uint8_t)(ty - 1));
      C2(0x4186e0, (uint8_t)tx, (uint8_t)(ty + 1));
      C2(0x4186e0, tx - 1, (uint8_t)(ty - 1));
      C2(0x4186e0, tx + 1, (uint8_t)(ty - 1));
      C2(0x4186e0, tx - 1, (uint8_t)(ty + 1));
      C2(0x4186e0, tx + 1, (uint8_t)(ty + 1));
    }
  return 1;
}

static uintptr_t minimap_sprite(unsigned kind) {
  switch (kind) {
  case 0:
  case 5:
  case 6:
  case 0x4f:
  case 0x83:
    return 0x6afb1c;
  case 1:
    return 0x6ad240;
  case 2:
    return 0x604618;
  case 0x2f:
    return 0x6afcf4;
  case 0x33:
    return 0x6a8f2c;
  case 0x35:
    return 0x5db180;
  case 0x36:
    return 0x5c77c0;
  case 0x42:
    return 0x6a8ea8;
  case 0x45:
    return 0x602458;
  case 0x4b:
  case 0xdd:
    return 0x513438;
  case 0x56:
    return 0x6a8f1c;
  case 0x67:
  case 0x68:
    return 0x6afcfc;
  case 0x91:
    return 0x5e4710;
  case 0x96:
    return 0x6a98a4;
  case 0x98:
    return 0x602278;
  case 0x9a:
    return 0x6a8ef0;
  case 0x9d:
    return 0x5f6f34;
  case 0xcf:
    return 0x5d101c;
  default:
    return 0;
  }
}
void bb_minimap_draw_entry(void *surface, uint32_t screen_x, uint32_t screen_y,
                           uint32_t map_x, uint32_t map_y, uint32_t markers) {
#ifndef BB_MODEL_RUNTIME
  typedef uint32_t(BB_STDCALL * lock_fn)(void *, uint32_t, void *,
                                                       uint32_t, uint32_t);
  typedef uint32_t(BB_STDCALL * unlock_fn)(void *, uint32_t);
  uintptr_t vtable = BB_U32((uintptr_t)surface);
  lock_fn lock = (lock_fn)BB_CODE((uintptr_t)BB_U32(vtable + 100));
  uint32_t description[27];
  description[0] = 108;
#else
  uint32_t vtable = BB_U32(surface);
  uint32_t lock = BB_U32(vtable + 100);
  struct bb_model_scratch scratch;
  bb_model_scratch_begin(&scratch,108);
  uint32_t *description = BB_SPAN(scratch.address,108);
  description[0] = 108;
#endif
  uint32_t result;
  do {
#ifdef BB_MODEL_RUNTIME
    result = BB_GAME_CALL(lock,0,bb_address_encode(surface),0,scratch.address,0,0);
#else
    result = lock(surface, 0, description, 0, 0);
#endif
  } while (result == 0x8876021c);
#ifdef BB_MODEL_RUNTIME
  if (result) { bb_model_scratch_end(&scratch); return; }
#else
  if (result)
    return;
#endif
  uint32_t pitch = description[4], pixels = description[9];
  int32_t left = BB_I32(0x4eebc8), top = BB_I32(0x4eebcc);
  int32_t right = (int32_t)(BB_U32(0x4eebd0) + (uint32_t)left - 1),
          bottom = (int32_t)(BB_U32(0x4eebd4) + (uint32_t)top - 1);
  uint32_t row_x = screen_x, row_y = screen_y - 130;
  int32_t first_x = (int32_t)(map_x - 64), end_x = (int32_t)(map_x + 64);
  int32_t first_y = (int32_t)(map_y - 64), end_y = (int32_t)(map_y + 64);
  if (first_y < end_y)
    for (unsigned row = 0; row < 128; ++row, row_x -= 2, ++row_y) {
      uint32_t tile_y = (uint32_t)first_y * 192u + row * 192u, px = row_x,
               py = row_y;
      for (int32_t tx = first_x; tx < end_x; ++tx, px += 2, ++py) {
        if ((int32_t)px < left || (int32_t)py < top || (int32_t)px > right ||
            (int32_t)py > bottom)
          continue;
        uintptr_t tile = 0x559b60 + (tile_y + (uint32_t)tx) * 12u,
                  p = pixels + py * pitch + px;
        if ((int32_t)tile_y > 0 && tx > 0 && (int32_t)tile_y < 0x8f40 &&
            tx < 191 && (BB_U32(tile + 8) & 0x1800)) {
          unsigned visible = !!(BB_U32(tile + 8) & 0x1000), kind = BYTE(tile);
          if (BB_U32(0x484768) == 3)
            kind += 256;
          if (!BYTE(tile + 1)) {
            BYTE(p - pitch) = BYTE(0x559250 + kind * 4u) + visible;
            BYTE(p - 1) = BYTE(0x559251 + kind * 4u) + visible;
            BYTE(p) = BYTE(0x559252 + kind * 4u) + visible;
            BYTE(p + 1) = BYTE(0x559253 + kind * 4u) + visible;
          } else {
            BYTE(p - pitch) = visible - 60;
            BYTE(p - 1) = visible - 61;
            BYTE(p) = visible - 59;
            BYTE(p + 1) = visible - 60;
          }
        } else {
          BYTE(p - pitch) = 0;
          BYTE(p - 1) = 0;
          BYTE(p) = 0;
          BYTE(p + 1) = 0;
        }
      }
    }
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(vtable+128),0,bb_address_encode(surface),0);
  bb_model_scratch_end(&scratch);
#else
  ((unlock_fn)BB_CODE((uintptr_t)BB_U32(vtable + 128)))(surface, 0);
#endif
  for (uintptr_t e = BB_U32(0x554f98); e; e = BB_U32(e + 4)) {
    unsigned x = BYTE(e + 0xd8), y = BYTE(e + 0xd9);
    if (BB_U32(0x559b68 + (y * 192u + x) * 12u) & 0x4000)
      continue;
    int32_t dx = (int32_t)(x - map_x), dy = (int32_t)(y - map_y);
    if (dx < -64 || dy < -64 || dx > 64 || dy > 64)
      continue;
    BYTE(0x4842bc) = BYTE(e + 0xf8);
    unsigned kind = BYTE(e + 0xd4);
    uintptr_t slot = minimap_sprite(kind);
    if (slot)
      M4(0x42fd90, BB_U32(slot), screen_x + (uint32_t)(dx - dy) * 2u,
         screen_y + (uint32_t)dx + (uint32_t)dy,
         kind == 0x67   ? BYTE(e + 0xf7)
         : kind == 0x68 ? 5
                        : 0,
         1);
  }
  for (uintptr_t e = BB_U32(0x554f98); e; e = BB_U32(e + 4)) {
    if (BYTE(e + 0xd4) != 0x62)
      continue;
    unsigned x = BYTE(e + 0xd8), y = BYTE(e + 0xd9);
    int32_t dx = (int32_t)(x - map_x), dy = (int32_t)(y - map_y);
    if ((BB_U32(0x559b68 + (y * 192u + x) * 12u) & 0x800) && dx >= -64 &&
        dy >= -64 && dx <= 64 && dy <= 64)
      M4(0x430560, 0x5c5b70, BB_U32(e + 0x3c),
         screen_x + (uint32_t)(dx - dy) * 2u,
         screen_y + (uint32_t)dx + (uint32_t)dy, 1);
  }
  if (markers)
    for (unsigned i = 0; i < 4; ++i) {
      int32_t dx = (int32_t)(BB_U32(0x5fe3b0 + i * 4u) - map_x),
              dy = (int32_t)(BB_U32(0x5fe3c0 + i * 4u) - map_y);
      if (dx >= -64 && dy >= -64 && dx <= 64 && dy < 64)
        M4(0x42fd90, BB_U32(0x6b08a8 + i * 4u),
           screen_x + (uint32_t)(dx - dy) * 2u,
           screen_y + (uint32_t)dx + (uint32_t)dy, 255, 1);
    }
}

void BB_THISCALL bb_building_stock_set_entry(void *object,
                                                           uint32_t entity) {
  C1(0x415860, BB_U32(0x4842f0));
  BB_U32((uintptr_t)object) = entity;
}
void bb_building_stock_tick_entry(void) {}
void BB_THISCALL bb_building_stock_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  uint32_t left = (uint32_t)(BB_I32(0x484444) / 2) - 250,
           top = (uint32_t)(BB_I32(0x484448) / 2) - 154;
  BB_U32(p + 6) = left;
  BB_U32(p + 14) = left + 500;
  BB_U32(p + 10) = top;
  BB_U32(p + 18) = top + 308;
  if (BB_U32(p)) {
    M4(0x42fd90, BB_U32(0x662c90), left, top, 255, 1);
    M4(0x42fd90, BB_U32(0x6ab584), BB_U32(p + 6), BB_U32(p + 10), 255, 1);
    M4(0x42fd90, BB_U32(0x5ca300), BB_U32(p + 6) + 18, BB_U32(p + 10) + 20,
       BYTE(0x6638c4 + BYTE(BB_U32(p) + 0xd4) * 914u), 1);
    uintptr_t entity = BB_U32(p);
    unsigned count = WORD(BB_U32(entity + 0x38)),
             category = BYTE(0x6638c7 + BYTE(entity + 0xd4) * 914u);
    unsigned frame = 0;
    switch (category) {
    case 1:
      frame = 0;
      break;
    case 7:
      frame = 1;
      break;
    case 9:
      frame = 3;
      break;
    case 18:
      frame = 2;
      break;
    }
    uint32_t x = BB_U32(p + 6) + 72, y = BB_U32(p + 10) + 173;
    for (unsigned i = 0; i < count; ++i) {
      M4(0x42fd90, BB_U32(0x640ce0), x, y, frame, 1);
      x += 26;
      if (i == 11) {
        y += 35;
        x -= 312;
      }
    }
    M5(0x418900, 0x5d19e0, BB_U32(p + 6), BB_U32(p + 10), BB_U32(p + 14),
       BB_U32(p + 18), 1);
    BYTE(p + 4) = (uint8_t)(BB_U32(0x484454) * 2u);
  } else if (BYTE(p + 4)) {
    M5(0x418900, 0x5d19e0, left, top, left + 500, top + 308, 1);
    --BYTE(p + 4);
  }
}
uint32_t BB_THISCALL
bb_building_stock_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p) || x < BB_I32(p + 6) || y < BB_I32(p + 10) ||
      x > BB_I32(p + 14) || y > BB_I32(p + 18))
    return 0;
  uint32_t dx = (uint32_t)x - BB_U32(p + 6), dy = (uint32_t)y - BB_U32(p + 10);
  if ((int32_t)dx > 416 && (int32_t)dx < 486 && (int32_t)dy > 224 &&
      (int32_t)dy < 294) {
    C1(0x415860, BB_U32(0x4842d8));
    bb_building_stock_set_entry(object, 0);
  }
  return 1;
}
