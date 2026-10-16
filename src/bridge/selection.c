/* SPDX-License-Identifier: GPL-3.0-only */
#include "selection.h"
#include "abi.h"
#include "interop.h"
#include "selection_panels.h"
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#else
typedef uint32_t (*c0)(void);
typedef uint32_t (*c1)(uint32_t);
typedef uint32_t(BB_THISCALL *m0)(void *);
typedef uint32_t(BB_THISCALL *m1)(void *, uint32_t);
typedef uint32_t(BB_THISCALL *m2)(void *, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *m3)(void *, uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *m8)(void *, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t);
#define C0(a) ((c0)BB_CODE((uintptr_t)(a)))()
#define C1(a, x) ((c1)BB_CODE((uintptr_t)(a)))((uint32_t)(uintptr_t)(x))
#define M0(a, p) ((m0)BB_CODE((uintptr_t)(a)))(PTR(p))
#define M1(a, p, x)                                                            \
  ((m1)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x))
#define M2(a, p, x, y)                                                         \
  ((m2)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),              \
                                (uint32_t)(uintptr_t)(y))
#define M3(a, p, x, y, z)                                                      \
  ((m3)BB_CODE((uintptr_t)(a)))(PTR(p), (uint32_t)(uintptr_t)(x),              \
                                (uint32_t)(uintptr_t)(y),                      \
                                (uint32_t)(uintptr_t)(z))
#define M8(a, p, x, y, z, w, v, u, t, s)                                       \
  ((m8)BB_CODE((uintptr_t)(a)))(                                               \
      PTR(p), (uint32_t)(uintptr_t)(x), y, z, w, (uint32_t)(uintptr_t)(v),     \
      (uint32_t)(uintptr_t)(u), (uint32_t)(uintptr_t)(t),                      \
      (uint32_t)(uintptr_t)(s))
#endif

/* These copies measure first, then copy forward in DWORDs and trailing bytes,
 * preserving the original behavior when a label overlaps its output. */
static void text_copy(uintptr_t out, uintptr_t text) {
  uint32_t size = 1;
  while (BYTE(text + size - 1))
    ++size;
  while (size >= 4) {
    BB_U32(out) = BB_U32(text);
    out += 4;
    text += 4;
    size -= 4;
  }
  while (size--)
    BYTE(out++) = BYTE(text++);
}
static uintptr_t label(uintptr_t slot) {
  uintptr_t p = BB_U32(slot);
  return p ? p : 0x0048481c;
}
static uintptr_t local_faction(void) {
  return 0x00554fbc + BYTE(0x004843f0) * 0xd00u;
}
static uint32_t absolute(uint32_t n) { return (int32_t)n < 0 ? 0u - n : n; }

/* Complete click/action dispatch. Feedback uses the selected entity sampled
 * before dispatch, while targets, factions and sound slots remain live. */
void bb_selection_action_entry(void) {
  uintptr_t faction = BB_U32(0x004844a8);
  uintptr_t speaker = BB_U32(0x00553f90) + BB_U32(faction + 0x452) * 314u;
  uint8_t count = BYTE(faction + 0x451);
  if (count) {
    int32_t index = (int32_t)C0(0x0046be20) % count;
    faction = BB_U32(0x004844a8);
    speaker = BB_U32(0x00553f90) +
              BB_U32(faction + 0x452 + (uint32_t)index * 4u) * 314u;
  }
  uint8_t mode = BYTE(0x005f746c);
  uintptr_t target;
  if (mode == 1) {
    M2(0x0042db90, local_faction(), BB_U32(0x004846d0), BB_U32(0x004846d4));
    return;
  }
  if (mode == 0x20) {
    target = BB_U32(0x0048448c);
    if (!target)
      return;
    uint8_t kind = BYTE(target + 0xd4);
    if (!BB_U32(0x0066359a + kind * 914u) && !BYTE(0x004843e8)) {
      if (kind != 0x46)
        return;
      if (BB_U32(0x00484738)) {
        C1(0x00415730, BB_U32(0x006b0838));
        BYTE(BB_U32(0x0048448c) + 0xf2) = 0x15;
      } else {
        C1(0x00415860, BB_U32(0x004842d8));
        target = BB_U32(0x0048448c);
        BYTE(target + 0xe5) += 0x40;
        target = BB_U32(0x0048448c);
        uintptr_t child = BB_U32(target + 0x10);
        if (child)
          BYTE(child + 0xe5) = BYTE(target + 0xe5);
        M0(0x00437ee0, BB_U32(0x0048448c));
      }
      return;
    }
    if (!BB_U32(0x0048473c))
      M0(0x00427810, BB_U32(0x004844a8));
    else if ((1u << (BYTE(0x004843f0) & 31)) &
             BYTE(BB_U32(0x0048448c) + 0x12d)) {
      M1(0x00427760, BB_U32(0x004844a8), BB_U32(0x0048448c));
      C1(0x00415860, BB_U32(0x00484310));
      return;
    }
    M2(0x00427560, BB_U32(0x004844a8), BB_U32(0x0048448c), 1);
    C1(0x00415860, BB_U32(0x00484310));
    return;
  }
  if (mode == 0x26) {
    target = BB_U32(0x0048448c);
    if (!target)
      return;
    uint8_t flag = BYTE(target + 0xf2) != 0x5f;
    M1(0x00454ed0, target, flag ? 0x5f : 0);
    M0(0x00437ee0, BB_U32(0x0048448c));
    if (flag)
      BYTE(BB_U32(0x0048448c) + 0xf5) = 0x78;
    target = BB_U32(0x0048448c);
    uint32_t tile = BYTE(target + 0xd9) * 192u + BYTE(target + 0xd8);
    uint32_t sound = BB_U32(flag ? 0x00484324 : 0x00484320);
    target = BB_U32(0x0048448c);
    BYTE(0x00559b66 + tile * 12u) = flag;
    M1(0x00453ab0, target, sound);
    return;
  }
  if (mode == 0x2a) {
    target = BB_U32(0x0048448c);
    if (!target)
      return;
    if (BYTE(target + 0xd4) == 0x42) {
      M0(0x00427810, BB_U32(0x004844a8));
      bb_building_panel_set_entry(PTR(0x006b08c0), PTR(BB_U32(0x0048448c)));
    } else {
      if (!BB_U32(target + 0x38))
        return;
      M0(0x00427810, BB_U32(0x004844a8));
      BB_U32(0x004847a4) = BB_U32(0x0048448c);
    }
    C1(0x00415860, BB_U32(0x00484310));
    return;
  }
  uint32_t activity;
  switch (mode) {
  case 0x21:
    activity = 0;
    break;
  case 0x22:
    activity = 5;
    break;
  case 0x23:
    activity = 0;
    break;
  case 0x24:
  case 0x2c:
    activity = 2;
    break;
  case 0x25:
    activity = 6;
    break;
  case 0x27:
    activity = 7;
    break;
  case 0x28:
    activity = 8;
    break;
  case 0x29:
    activity = 9;
    break;
  case 0x2b:
    activity = 10;
    break;
  case 0x2e:
    activity = 14;
    break;
  case 0x2f:
    activity = 15;
    break;
  case 0x30:
    activity = 16;
    break;
  default:
    return;
  }
  if (!BYTE(BB_U32(0x004844a8) + 0x451))
    return;
  if (mode == 0x21) {
    M2(0x0042da00, local_faction(), BB_U32(0x004846d0), BB_U32(0x004846d4));
  } else {
    target = BB_U32(0x0048448c);
    if (!target)
      return;
    uint32_t id = BB_U32(target + 0x8e);
    if (mode == 0x23)
      M1(0x0042da50, local_faction(), id);
    else
      M2(0x0042dd00, local_faction(), id, activity);
  }
  C1(0x00415860, BB_U32(0x0048430c));
  if (mode == 0x22)
    return;
  if (mode == 0x21)
    C0(0x0046be20);
  M2(0x00453580, speaker, 2, 1);
}

/* Disabled abilities still claim the hit without replacing existing text. */
uint32_t BB_THISCALL bb_selection_help_entry(void *object, int32_t x, int32_t y,
                                             char *output) {
  uint32_t p = bb_address_encode(object);
  if (!BYTE(p + 0x20))
    return 0;
  for (unsigned i = 0; i < 7; ++i) {
    int32_t left = BB_I32(0x004820e0 + i * 4u);
    int32_t top = BB_I32(0x00482100 + i * 4u);
    if (left > x || top > y || (int32_t)((uint32_t)left + 32) < x ||
        (int32_t)((uint32_t)top + 32) < y || !BB_U32(p + 4))
      continue;
    uint8_t profession = BYTE(BB_U32(p + 4) + 0xf4);
    unsigned ability = i + (profession == 3 ? 16 : 0);
    if (BYTE(BB_U32(0x004844a8) + 0x9e0 + ability)) {
      static const uintptr_t ordinary[] = {0x00603778, 0x0060377c, 0x00603528,
                                           0x00603784, 0x00603788, 0x0060378c,
                                           0x00603790};
      text_copy((uintptr_t)output,
                label(profession == 9 ? 0x0060375c + i * 4u : ordinary[i]));
    }
    return 1;
  }
  return 0;
}

/* Complete tooltip selection, countdown, text source and viewport correction.
 */
void BB_THISCALL bb_tooltip_tick_entry(void *object) {
  uint32_t p = bb_address_encode(object), nearest = 0;
  int32_t best = (int32_t)(BB_U32(0x00484444) + BB_U32(0x00484448));
  BYTE(p + 0x22) = 0;
  if (!BB_U32(p)) {
    if (BB_U32(p + 4))
      bb_tooltip_visibility_entry(object, 0);
    return;
  }
  if (!BB_U32(p + 4)) {
    ++BYTE(p + 9);
    if (BYTE(p + 9) < 36)
      return;
    bb_tooltip_visibility_entry(object, 1);
  }
  uintptr_t mouse = BB_U32(0x00484700) ? 0x004846c8 : 0x004846b8;
  int32_t x = BB_I32(mouse), y = BB_I32(mouse + 4);
  uint32_t handled = 0;
  if (y < 41) {
    handled = 1;
    M3(0x0041a0b0, 0x005d19a0, x, y, p + 0x22);
  } else if (BB_U32(0x0048443c) == 2) {
    handled = 1;
    M3(0x0042d2f0, 0x006025d0, x, y, p + 0x22);
  } else if (BB_I32(0x005f90e8) < y) {
    handled = 1;
    text_copy(p + 0x22, 0x0048481c);
    int32_t icon = (int32_t)(BB_U32(0x005f90e0) + (uint32_t)x) /
                   (int32_t)(BB_U32(BB_U32(0x005e4794) + 0x44) - 1u);
    if (icon < 0)
      icon = 0;
    uintptr_t faction = BB_U32(0x004844a8);
    if (icon < BYTE(faction + 0x2be))
      text_copy(p + 0x22,
                0x00663538 + BYTE(faction + 0x27e + (uint32_t)icon) * 914u);
  } else if (!BB_U32(0x0048443c)) {
    for (uintptr_t node = BB_U32(0x00554f98); node; node = BB_U32(node + 4)) {
      if (!BYTE(node + 0x128) || SHORT(node + 0xbe) > x ||
          SHORT(node + 0xc0) > y || SHORT(node + 0xc2) < x ||
          SHORT(node + 0xc4) < y)
        continue;
      uint32_t tile = BYTE(node + 0xd9) * 192u + BYTE(node + 0xd8);
      if (WORD(0x00559b68 + tile * 12u) & 0x4000)
        continue;
      uint8_t kind = BYTE(node + 0xd4);
      switch (kind) {
      case 0:
      case 5:
      case 6:
      case 0x1d:
      case 0x24:
      case 0x25:
      case 0x5d:
      case 0x81:
      case 0xab:
      case 0xdb:
        continue;
      }
      if ((kind == 0x67 || kind == 0x68) && !BYTE(node + 0xf4))
        continue;
      uint32_t mid_y =
          (uint32_t)(SHORT(node + 0xc4) / 2 + SHORT(node + 0xc0) / 2);
      uint32_t mid_x =
          (uint32_t)(SHORT(node + 0xbe) / 2 + SHORT(node + 0xc2) / 2);
      int32_t distance = (int32_t)(absolute(mid_x - (uint32_t)x) +
                                   absolute(mid_y - (uint32_t)y));
      if (distance < best) {
        nearest = node;
        best = distance;
      }
    }
  } else if (BB_U32(0x0048443c) == 3) {
    handled = M3(0x00422420, 0x005db050, x, y, p + 0x22);
  }
  if (bb_selection_help_entry(PTR(0x006b0858), x, y,
                              (char *)(uintptr_t)(p + 0x22)))
    handled = 1;
  if (x >= 600 && x <= 640 && y >= 40 && y <= 80) {
    uint32_t season = BB_U32(0x00484768);
    if (season <= 3)
      text_copy(p + 0x22, label(0x00603900 + season * 4u));
    handled = 1;
  }
  if (handled) {
    BB_U32(p + 0xe) = y;
    BB_U32(p + 0xa) = x;
  } else if (nearest) {
    uint8_t kind = BYTE(nearest + 0xd4);
    uintptr_t text = kind == 0x62 ? BB_U32(nearest + 0x3c)
                     : kind == 0x67 || kind == 0x68
                         ? 0x005e5de0 + BYTE(nearest + 0xf4) * 292u
                         : 0x00663538 + kind * 914u;
    text_copy(p + 0x22, text);
    BB_U32(p + 0xa) = SHORT(nearest + 0xbe) / 2 + SHORT(nearest + 0xc2) / 2;
    BB_U32(p + 0xe) = SHORT(nearest + 0xc0) / 2 + SHORT(nearest + 0xc4) / 2;
  } else {
    --BYTE(p + 9);
    if (!BYTE(p + 9))
      bb_tooltip_visibility_entry(object, 0);
  }
  if (!BB_U32(p + 4))
    return;
  if (!BYTE(p + 0x22)) {
    BB_U32(p + 0x12) = 0;
    BB_U32(p + 0x16) = 0;
    BB_U32(p + 0x1a) = 0;
    BB_U32(p + 0x1e) = 0;
    return;
  }
  BB_U32(p + 0x16) = 0;
  BB_U32(p + 0x12) = 0;
  BB_U32(p + 0x1a) = 0;
  BB_U32(p + 0x1e) = 0;
  M8(0x004309b0, 0x0063f0d0, p + 0x22, BB_U32(p + 0xa), BB_U32(p + 0xe), 1,
     p + 0x12, p + 0x16, p + 0x1a, p + 0x1e);
  if (BB_I32(p + 0x12) < 0) {
    uint32_t delta = 0u - BB_U32(p + 0x12);
    BB_U32(p + 0xa) += delta;
    BB_U32(p + 0x1a) += delta;
    BB_U32(p + 0x12) = 0;
  }
  if (BB_I32(p + 0x16) < 0) {
    uint32_t delta = 0u - BB_U32(p + 0x16);
    BB_U32(p + 0xe) += delta;
    BB_U32(p + 0x1e) += delta;
    BB_U32(p + 0x16) = 0;
  }
  if (BB_I32(p + 0x1a) > BB_I32(0x00484444)) {
    uint32_t delta = BB_U32(p + 0x1a) - BB_U32(0x00484444);
    BB_U32(p + 0xa) -= delta;
    BB_U32(p + 0x1a) -= delta;
    BB_U32(p + 0x12) -= delta;
  }
  if (BB_I32(p + 0x1e) > BB_I32(0x00484448)) {
    uint32_t delta = BB_U32(p + 0x1e) - BB_U32(0x00484448);
    BB_U32(p + 0xe) -= delta;
    BB_U32(p + 0x16) -= delta;
    BB_U32(p + 0x1e) -= delta;
  }
}
