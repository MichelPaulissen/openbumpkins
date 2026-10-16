/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "controls.h"
#include "interface_controls.h"
#include "selection.h"
#include "dialog.h"
#include <string.h>
#include <math.h>
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#define C1(id, arg) BB_GAME_CALL(id, 0, arg)
#else
#define C1(id, arg) ((uint32_t (*)(uint32_t))BB_CODE(id))(arg)
#endif
#define BYTE(a) BB_U8(a)
#define SHORT(a) BB_I16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
static uintptr_t selected_actor(void) {
  uintptr_t faction = BB_U32(0x4844a8);
  return BB_U32(0x553f90) + BB_U32(faction + 0x452) * 314u;
}
static void cursor(uintptr_t slot, int32_t x, int32_t y, uint32_t flags) {
  bb_sprite_cursor_draw(PTR(BB_U32(slot)), x, y, flags);
}

/* 004193f0: ECX is unused; three stack arguments are removed by the entry. */
void BB_STDCALL bb_cursor_select_entry(int32_t x, int32_t y,
                                                     uint32_t flags) {
  if (BB_U32(0x48426c) == 1) {
    cursor(0x50fa90, x, y, flags);
    return;
  }
  if (BB_U32(0x48426c) != 2)
    return;
  if (BB_U32(0x484700) == 2) {
    x = BB_I32(0x4846c8);
    y = BB_I32(0x4846cc);
  }
  BYTE(0x5f746c) = 0;
  if (BB_U32(0x5ca308)) {
    cursor(0x50fa90, x, y, flags);
    return;
  }
  if (BB_U32(0x48443c) == 4)
    return;
  if (BB_U32(0x484290)) {
    cursor(0x641024, x, y, flags);
    return;
  }
  if (y < 30 || BB_U32(0x484704) == 2) {
    cursor(0x559110, x, y, flags);
    BYTE(0x5f746c) = 0;
    BB_U32(0x484488) = 0;
    return;
  }
  uintptr_t target = BB_U32(0x484488);
  int handled = 0;
  if (target) {
    unsigned kind = BYTE(target + 0xd4);
    if (!BYTE(BB_U32(0x4844a8) + 0x451)) {
      if (kind == 0xa9) {
        BYTE(0x5f746c) = 0x26;
        handled = 1;
      } else if (BB_U32(0x66359a + kind * 914u) ||
                 BB_U32(0x663578 + kind * 914u) || BYTE(0x4843e8)) {
        BYTE(0x5f746c) = 0x20;
        handled = 1;
      }
    } else {
      uintptr_t actor = selected_actor();
      unsigned actor_kind = BYTE(actor + 0xd4);
      if (BYTE(actor + 0xf7) != BYTE(0x4843f0) ||
          (BB_U32(0x663578 + actor_kind * 914u) &&
           !BB_U32(0x6635c0 + actor_kind * 914u)) ||
          (BYTE(0x4843e8) && BB_U32(0x48473c))) {
        BYTE(0x5f746c) = 0x20;
        handled = 1;
      } else if (BYTE(target + 0xf7) == BYTE(0x4843f0) && !BB_U32(0x484738)) {
        if (BYTE(actor + 0xfc) == 26) {
          BYTE(0x5f746c) = 0x30;
          handled = 1;
        }
      } else if ((BB_U32(0x484738) || (kind != 0 && kind != 5 && kind != 6)) &&
                 (!(BB_U32(target + 0x9c) & 0x800000) ||
                  BYTE(actor + 0xf4) == 26) &&
                 ((kind != 0x6e && kind != 0x53) || BB_U32(0x484738))) {
        BYTE(0x5f746c) = 0x23;
        handled = 1;
      }
      if (BYTE(actor + 0xf7) == BYTE(0x4843f0) && BYTE(actor + 0xf4) == 4 &&
          BYTE(target + 0xf7) == BYTE(0x4843f0) &&
          BB_U32(0x663578 + kind * 914u)) {
        if (BB_U32(0x484738)) {
          BYTE(0x5f746c) = 0x2f;
          handled = 1;
        } else if (SHORT(target + 0xa0) < SHORT(target + 0xa2)) {
          BYTE(0x5f746c) = 0x2e;
          handled = 1;
        }
      }
      if (!handled) {
        switch (kind) {
        case 1:
        case 0x42:
        case 0x45:
        case 0x56:
        case 0x91:
        case 0x96:
        case 0x98:
        case 0x9a:
        case 0x9d:
        case 0xcf:
          if (BYTE(target + 0xf7) == BYTE(0x4843f0)) {
            if (bb_entity_can_work(PTR(actor), kind))
              BYTE(0x5f746c) = 0x24;
            else if (bb_entity_can_train(PTR(actor),
                                         BYTE(BB_U32(0x484488) + 0xd4)))
              BYTE(0x5f746c) = 0x2c;
            else
              BYTE(0x5f746c) = 0x2d;
            handled = 1;
          }
          break;
        case 0x53:
        case 0xa0:
          BYTE(0x5f746c) = 0x28;
          handled = 1;
          break;
        case 0x62:
          if (BYTE(target + 0xf7) != BYTE(0x4844a4)) {
            BYTE(0x5f746c) = 0x23;
            handled = 1;
          }
          break;
        case 0x6e: {
          unsigned p = BYTE(actor + 0xf4);
          if (!BB_U32(0x484738) && (p == 1 || p == 2 || (p >= 11 && p <= 14))) {
            BYTE(0x5f746c) = BYTE(target + 0xf2) == 90 &&
                                     BYTE(target + 0xf7) == BYTE(actor + 0xf7)
                                 ? 0x2b
                                 : 0x25;
            handled = 1;
          }
          break;
        }
        case 0x72:
        case 0x73:
          if (BYTE(target + 0xf7) == BYTE(0x4843f0) && !BYTE(target + 0xf2) &&
              (BYTE(actor + 0xf4) == 1 || BYTE(actor + 0xf4) == 4)) {
            BYTE(0x5f746c) = 0x27;
            handled = 1;
          }
          break;
        case 0xa9:
          if (BYTE(target + 0xf7) == BYTE(0x4843f0)) {
            BYTE(0x5f746c) = 0x26;
            handled = 1;
          }
          break;
        case 0xad:
          if (BYTE(actor + 0x11a) == 0xad)
            break;
          /* Fall through to the ordinary resource cursor. */
          __attribute__((fallthrough));
        case 0x9f:
        case 0xa2:
        case 0xac:
        case 0xae:
        case 0xb0:
        case 0xb1:
        case 0xb2:
        case 0xc0:
        case 0xc1:
        case 0xc6:
        case 0xc8:
        case 0xd2:
        case 0xd3:
        case 0xd4:
        case 0xd6:
          BYTE(0x5f746c) = 0x29;
          handled = 1;
          break;
        default:
          break;
        }
        if (!handled) {
          unsigned k = BYTE(BB_U32(0x484488) + 0xd4);
          if (BB_U32(0x66359a + k * 914u) && !BB_U32(0x663578 + k * 914u)) {
            BYTE(0x5f746c) = 0x20;
            handled = 1;
          }
        }
      }
    }
  }
  if (!handled && BYTE(BB_U32(0x4844a8) + 0x451))
    BYTE(0x5f746c) = 0x21;
  cursor(0x559110, x, y, flags);
}

/* Shared body from interface_controls.c. */
void BB_THISCALL bb_toolbar_reset_entry(void *p) {
  BYTE(0x484724) = 0;
  if (BB_U32((uintptr_t)p + 16))
    BB_U32((uintptr_t)p + 16) = 0;
}

/* 00454f40/00454f80: only kind 0x67 qualifies; building and profession
 * arguments wrap to bytes. */
uint32_t BB_THISCALL bb_entity_can_work(void *object,
                                                      uint32_t building) {
  uintptr_t node = (uintptr_t)object;
  return BYTE(node + 0xd4) == 0x67 &&
         BYTE(0x006635bd + (uint8_t)building * 914) == BYTE(node + 0xf4);
}

/* Shared body from entity_activity.c. */
uint32_t BB_THISCALL bb_entity_can_train(void *object,
                                                       uint32_t building) {
  uintptr_t node = (uintptr_t)object;
  return BYTE(node + 0xd4) == 0x67 &&
         BYTE(0x005e5efb + BYTE(node + 0xf4) * 292) == (uint8_t)building;
}

/* Shared body from selection.c. */
void BB_THISCALL bb_tooltip_visibility_entry(void *object,
                                                           uint32_t visible) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 4) = visible != 0;
  BYTE(p + 9) = visible ? 0x10 : 0;
}

/* Shared body from selection.c. */
void BB_THISCALL bb_tooltip_reset_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p + 4))
    BYTE(p + 9) = 0;
}

/* Shared body from controls.c. */
void BB_THISCALL bb_numeric_panel_set_entry(void *object,
                                                          uint32_t entity) {
  uintptr_t p = (uintptr_t)object;
  if (!entity) {
    if (BB_U32(p)) {
      BB_U32(p + 0x200) = 0;
      C1(0x415860, BB_U32(0x4842f0));
      BB_U32(p) = 0;
      C1(0x41ad70, 0);
    }
  } else {
    BB_U32(p) = entity;
    BB_U32(p + 0x200) = 200;
    BB_U32(p + 0x1fc) = 0;
    if (BB_U32(entity + 0x38))
      for (unsigned i = 0; i < 4; ++i)
        BB_U32(p + 0x220 + i * 4u) = UINT32_MAX;
  }
}

/* Feedback shares the input layer's formatted, packed message strings. */
static int32_t signed_add(uint32_t a, uint32_t b) { return (int32_t)(a + b); }
static void string_copy(uintptr_t out, uintptr_t in) {
  size_t n = strlen(BB_DATA(in)) + 1;
  while (n >= 4) {
    BB_U32(out) = BB_U32(in);out += 4;in += 4;n -= 4;
  }
  while (n--)BB_U8(out++) = BB_U8(in++);
}
#ifdef BB_MODEL_RUNTIME
#define M4(id,self,a,b,c,d) BB_GAME_CALL(id,self,a,b,c,d)
#define M5(id,self,a,b,c,d,e) BB_GAME_CALL(id,self,a,b,c,d,e)
#else
typedef uint32_t(BB_THISCALL *method4)(void *,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint32_t(BB_THISCALL *method5)(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
#define M4(id,self,a,b,c,d) ((method4)BB_CODE(id))(PTR(self),a,b,c,d)
#define M5(id,self,a,b,c,d,e) ((method5)BB_CODE(id))(PTR(self),a,b,c,d,e)
#endif
void BB_THISCALL bb_feedback_append_entry(void *object,
                                                        const char *text) {
  uintptr_t p = (uintptr_t)object;
  string_copy(p + BB_U32(p + 0x824) * 0x104, (uintptr_t)text);
  BB_U32(p + 0x100 + BB_U32(p + 0x824) * 0x104) = BB_U32(0x00484740);
  int32_t old = BB_I32(p + 0x820), next = signed_add(BB_U32(p + 0x824), 1) % 8;
  BB_I32(p + 0x824) = next;
  if (old == next)
    BB_I32(p + 0x820) = signed_add((uint32_t)old, 1) % 8;
}

void BB_THISCALL bb_feedback_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  int32_t head = BB_I32(p + 0x820), tail = BB_I32(p + 0x824);
  uint32_t y = BB_U32(0x00484448) - 10;
  if (head < tail) {
    for (int32_t i = tail - 1; i >= BB_I32(p + 0x820); --i) {
      uint32_t at = y;
      y -= 10;
      M4(0x00430560, 0x005cf3f0, p + (uint32_t)i * 0x104, 10, at, 0);
    }
  } else if (tail < head) {
    for (int32_t i = tail - 1; i >= 0; --i) {
      uint32_t at = y;
      y -= 10;
      M4(0x00430560, 0x005cf3f0, p + (uint32_t)i * 0x104, 10, at, 0);
    }
    for (int32_t i = 7; i >= BB_I32(p + 0x820); --i) {
      uint32_t at = y;
      y -= 10;
      M4(0x00430560, 0x005cf3f0, p + (uint32_t)i * 0x104, 10, at, 0);
    }
  } else {
    if (!BB_U32(p + 0x828)) {
      M5(0x00418900, 0x005d19e0, 10, BB_U32(0x00484448) - 80, 320,
         BB_U32(0x00484448) - 6, 1);
      BB_U32(p + 0x828) = 1;
    }
    return;
  }
  M5(0x00418900, 0x005d19e0, 10, y - 20, 320, BB_U32(0x00484448) - 6, 1);
  BB_U32(p + 0x828) = 0;
}

void BB_THISCALL bb_feedback_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  uint32_t head = BB_U32(p + 0x820);
  if (head != BB_U32(p + 0x824) &&
      BB_U32(0x00484740) - BB_U32(p + 0x100 + head * 0x104) > 6000) {
    BYTE(p + head * 0x104) = 0;
    BB_I32(p + 0x820) = signed_add(BB_U32(p + 0x820), 1) % 8;
  }
}

/* 00415280: the sole consumer stores AL. Retain the two DOUBLE stores and
 * the actual angle constants rather than substituting ideal multiples of pi. */
uint8_t bb_direction_entry(int32_t x, int32_t y) {
  if (!x)
    return y < 0 ? 128 : 0;
  if (!y)
    return x < 0 ? 64 : 192;
  volatile double ratio = (double)((long double)y / (long double)x);
  volatile double angle =
      (double)(atanl((long double)ratio) +
               (x > 0 ? BB_DOUBLE(0x4821b8) : BB_DOUBLE(0x4821c0)));
  return (uint8_t)(int64_t)((long double)angle * BB_DOUBLE(0x4821c8));
}
uint32_t bb_direction_difference_entry(uint32_t a, uint32_t b) {
  a = (uint8_t)a;
  b = (uint8_t)b;
  uint32_t d = a > b ? a - b : b - a;
  return d > 128 ? 128 : d;
}
