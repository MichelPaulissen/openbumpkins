/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "coordinator.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#endif
#define BYTE(a) BB_U8(a)

/* The game uses the low DWORD of signed 64-bit truncation. Its mapped
 * projection coefficients are finite, and all integer coordinate products
 * remain within that conversion range. */
static uint32_t integer(long double value) { return (uint32_t)(int64_t)value; }

/* 00414be0: wrapped screen/camera differences, two stored doubles, then
 * truncation and signed halves. Outputs may alias either scratch value. */
void bb_screen_to_map_entry(int32_t x, int32_t y, int32_t *outx, int32_t *outy,
                            uint32_t unused) {
  (void)unused;
  uint32_t vertical = BB_U32(0x00602484) - BB_U32(0x006a9078) + (uint32_t)y;
  uint32_t center = BB_U32(0x005db014), camera = BB_U32(0x006024b8);
  uint32_t right = (uint32_t)x - center + camera,
           left = center - camera - (uint32_t)x;
  long double a = (long double)(int32_t)vertical * BB_DOUBLE(0x00482188);
  long double b = (long double)(int32_t)right * BB_DOUBLE(0x00482190);
  long double first = b + a;
  long double c = (long double)(int32_t)left * BB_DOUBLE(0x00482190);
  BB_DOUBLE(0x006b08e0) = (double)first;
  BB_DOUBLE(0x006b08e8) = (double)(a + c);
  BB_I32(outx) = (int32_t)integer(BB_DOUBLE(0x006b08e0)) / 2;
  BB_I32(outy) = (int32_t)integer(BB_DOUBLE(0x006b08e8)) / 2;
}
static void panel_move(int32_t amount) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x41a9e0, 0x5f90d8, amount);
#else
  typedef void(BB_THISCALL * move_fn)(void *, int32_t);
  ((move_fn)BB_CODE((uintptr_t)0x0041a9e0))((void *)0x005f90d8, amount);
#endif
}
static void panel_call(uintptr_t address) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(address, 0x5f90d8);
#else
  typedef void(BB_THISCALL * self_fn)(void *);
  ((self_fn)BB_CODE(address))((void *)0x005f90d8);
#endif
}

/* 00414c90: edge/key scrolling, map-panel movement, inertial camera decay,
 * drag recentering, panel visibility, camera destinations and shaking. */
void bb_camera_update_entry(void) {
  int up = 0, down = 0, left = 0, right = 0;
  if (!BB_U32(0x00484700) && !BB_U32(0x00484704) && !BB_U32(0x0048443c)) {
    up = BB_I32(0x004846bc) < 2 || (BB_U32(0x00484520) && !BYTE(0x004843e8));
    left = BB_I32(0x004846b8) < 2 || (BB_U32(0x00484528) && !BYTE(0x004843e8));
    right = (int32_t)(BB_U32(0x00484444) - 3u) < BB_I32(0x004846b8) ||
            (BB_U32(0x0048452c) && !BYTE(0x004843e8));
    down = ((int32_t)(BB_U32(0x00484448) - 3u) < BB_I32(0x004846bc) &&
            (BB_I32(0x004846b8) < 400 || BB_I32(0x004846b8) > 500)) ||
           (BB_U32(0x00484524) && !BYTE(0x004843e8));
  }
  if (up && BB_I32(0x004846bc) <= BB_I32(0x005f90dc)) {
    if (!left && !right)
      BB_U32(0x006a8f28) = BB_U32(0x006024b8);
    BB_U32(0x004846ec) = 1;
    BB_U32(0x006a8f24) =
        BB_U32(0x00602484) - (uint32_t)(BB_I32(0x00484448) / 4);
  }
  if (down && BB_I32(0x004846bc) <= BB_I32(0x005f90dc)) {
    if (!left && !right)
      BB_U32(0x006a8f28) = BB_U32(0x006024b8);
    BB_U32(0x004846ec) = 1;
    BB_U32(0x006a8f24) =
        BB_U32(0x00602484) + (uint32_t)(BB_I32(0x00484448) / 4);
  }
  if (left) {
    if (BB_I32(0x005f90dc) < BB_I32(0x004846bc))
      panel_move(-12);
    else {
      BB_U32(0x006a8f28) =
          BB_U32(0x006024b8) - (uint32_t)(BB_I32(0x00484444) / 4);
      if (!up && !down)
        BB_U32(0x006a8f24) = BB_U32(0x00602484);
      BB_U32(0x004846ec) = 1;
    }
  }
  if (right) {
    if (BB_I32(0x005f90dc) < BB_I32(0x004846bc))
      panel_move(12);
    else {
      BB_U32(0x006a8f28) =
          BB_U32(0x006024b8) + (uint32_t)(BB_I32(0x00484444) / 4);
      if (!up && !down)
        BB_U32(0x006a8f24) = BB_U32(0x00602484);
      BB_U32(0x004846ec) = 1;
    }
  }
  if (BB_U32(0x0048443c) == 1) {
    if (BB_U32(0x00484520) && BB_I32(0x00484474) > 0) {
      BB_U32(0x00484478) = 1;
      --BB_U32(0x00484474);
    }
    if (BB_U32(0x00484524) && (int32_t)(BB_U32(0x00484474) + 128u) < 191) {
      BB_U32(0x0048447c) = 1;
      ++BB_U32(0x00484474);
    }
    if (BB_U32(0x00484528) && BB_I32(0x00484470) > 0) {
      BB_U32(0x00484484) = 1;
      --BB_U32(0x00484470);
    }
    if (BB_U32(0x0048452c) && (int32_t)(BB_U32(0x00484470) + 128u) < 191) {
      BB_U32(0x00484480) = 1;
      ++BB_U32(0x00484470);
    }
  }
  int drag = BB_U32(0x00484700) == 2;
  uint32_t dx = integer(BB_DOUBLE(0x004846f0));
  BB_U32(0x005fe288) += dx;
  uint32_t dy = integer(BB_DOUBLE(0x004846f8));
  long double vx = (long double)BB_DOUBLE(0x004846f0) * BB_DOUBLE(0x00482198);
  long double vy = (long double)BB_DOUBLE(0x004846f8) * BB_DOUBLE(0x00482198);
  BB_DOUBLE(0x004846f0) = (double)vx;
  BB_DOUBLE(0x004846f8) = (double)vy;
  BB_U32(0x005fe290) += dy;
  if (drag) {
    if (BB_U32(0x004846b8) != BB_U32(0x004846c8) ||
        BB_U32(0x004846bc) != BB_U32(0x004846cc)) {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(BB_U32(0x6ca550), 0, BB_U32(0x4846c8), BB_U32(0x4846cc));
#else
      typedef int32_t(BB_STDCALL * cursor_fn)(int32_t, int32_t);
      ((cursor_fn)BB_CODE((uintptr_t)BB_U32(0x006ca550)))(BB_U32(0x004846c8),
                                                 BB_U32(0x004846cc));
#endif
    }
  } else {
    BB_U32(BB_U32(0x00559110) + 0x50) = 0;
    if (BB_U32(0x004846b0) && BB_I32(0x004846bc) < 30)
      BB_U32(BB_U32(0x00559110) + 0x50) = 0;
    uintptr_t faction = BB_U32(0x004844a8);
    if (BB_U32(0x004847a0) && BYTE(faction + 0x451)) {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(0x41ad70, 0, BB_U32(faction + 0x452) * 314u + BB_U32(0x553f90));
#else
      typedef void (*focus_fn)(void *);
      ((focus_fn)BB_CODE((uintptr_t)0x0041ad70))(
          (void *)(uintptr_t)(BB_U32(faction + 0x452) * 314u +
                              BB_U32(0x00553f90)));
#endif
    }
  }
  if (BYTE(0x005e5cc0) && !BB_U32(0x0048443c) && !BB_U32(0x00484704) &&
      !BB_U32(0x00484700)) {
    if (BB_I32(0x004846bc) < (int32_t)(BB_U32(0x005f90e8) - 5u)) {
      if (BB_I32(0x005f90dc) < BB_I32(0x00484448))
        panel_call(0x0041a9c0);
    } else if (BB_I32(0x004846b8) >= 400 && BB_I32(0x004846b8) <= 500 &&
               BB_U32(0x005f90dc) == BB_U32(0x00484448))
      panel_call(0x0041a990);
  }
  if (BB_U32(0x004846ec)) {
    int32_t x = (int32_t)(BB_U32(0x006a8f28) - BB_U32(0x005fe288));
    int32_t y = (int32_t)(BB_U32(0x006a8f24) - BB_U32(0x005fe290));
    long double ax = (long double)x * BB_DOUBLE(0x004821a0),
                ay = (long double)y * BB_DOUBLE(0x004821a0);
    BB_DOUBLE(0x004846f0) = (double)ax;
    BB_DOUBLE(0x004846f8) = (double)ay;
    if (BB_DOUBLE(0x004846f0) < 1.0 &&
        BB_DOUBLE(0x004846f0) > BB_DOUBLE(0x004821b0) &&
        BB_DOUBLE(0x004846f8) < 1.0 &&
        BB_DOUBLE(0x004846f8) > BB_DOUBLE(0x004821b0))
      BB_U32(0x004846ec) = 0;
  }
  uint32_t shake = BB_U32(0x004846b4);
  if ((int32_t)shake > 0) {
    BB_U32(0x005fe290) += BB_U32(0x005fe298 + shake * 4u);
    BB_U32(0x004846b4) = shake - 1u;
  }
  bb_screen_to_map_entry(BB_U32(0x004846b8), BB_U32(0x004846bc),
                         (int32_t *)0x004846d0, (int32_t *)0x004846d4, 1);
  if (BB_I32(0x004846d0) < 0)
    BB_U32(0x004846d0) = 0;
  if (BB_I32(0x004846d4) < 0)
    BB_U32(0x004846d4) = 0;
  if (BB_I32(0x004846d0) > 191)
    BB_U32(0x004846d0) = 191;
  if (BB_I32(0x004846d4) > 191)
    BB_U32(0x004846d4) = 191;
  BB_U32(0x004846d8) =
      BYTE(0x00559b65 +
           (BB_U32(0x004846d4) * 192u + BB_U32(0x004846d0)) * 12u) *
      2u;
}
