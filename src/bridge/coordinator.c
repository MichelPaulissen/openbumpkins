/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "coordinator.h"
#include "interop.h"
#include "session_controls.h"
#include "worker_runtime.h"
#include "letters.h"
#include "mission_io.h"
#include "script.h"
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "game_file.h"
#include "crt_output.h"
#define bb_display_invalidate_entry() BB_GAME_CALL(0x403190, 0)
#define bb_stock_panel_tick_entry(p) BB_GAME_CALL(0x42a1b0,bb_address_encode(p))
#define bb_message_tick_entry(p) BB_GAME_CALL(0x41e5a0,bb_address_encode(p))
#define bb_message_draw_entry(p) BB_GAME_CALL(0x41e440,bb_address_encode(p))
#define bb_notification_tick_entry(p) BB_GAME_CALL(0x41e150,bb_address_encode(p))
#define bb_notification_draw_entry(p) BB_GAME_CALL(0x41e0b0,bb_address_encode(p))
#define bb_entity_pool_tick(p) BB_GAME_CALL(0x4347b0,bb_address_encode(p))
#define bb_random_event_entry() BB_GAME_CALL(0x4202c0,0)
#define bb_faction_tick(p) BB_GAME_CALL(0x41afe0,bb_address_encode(p))
#define bb_script_tick_entry(p) BB_GAME_CALL(0x426a60,bb_address_encode(p))
#define bb_script_draw_entry(p) BB_GAME_CALL(0x423600,bb_address_encode(p))
#define bb_faction_buildable(p) BB_GAME_CALL(0x41ced0,bb_address_encode(p))
#define bb_entity_nearest(p,a,b,c,d) ((void*)(uintptr_t)BB_GAME_CALL(0x4341a0,bb_address_encode(p),a,b,c,d))
#define bb_session_outro_entry() BB_GAME_CALL(0x42e620,0)
#endif
#define BYTE(a) BB_U8(a)
#define SHORT(a) BB_I16(a)
#define TEXT(a) ((const char *)(uintptr_t)(a))
typedef void(BB_THISCALL * self_fn)(void *);
typedef void(BB_THISCALL * self_word_fn)(void *, uint32_t);
typedef void(BB_THISCALL * self_text_fn)(void *, const char *);
typedef int (*format_fn)(char *, const char *, ...);
#ifdef BB_MODEL_RUNTIME
#define FORMAT(out,fmt,...) bb_crt_output_words(out,256,fmt,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4)
#define FORMAT_WORD(p) ((uint32_t)(p))
#else
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#define FORMAT_WORD(p) TEXT(p)
#endif
static void self_call(uintptr_t entry, uintptr_t object) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(entry, object);
#else
  ((self_fn)BB_CODE(entry))((void *)object);
#endif
}
static void self_word(uintptr_t entry, uintptr_t object, uint32_t value) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(entry, object, value);
#else
  ((self_word_fn)BB_CODE(entry))((void *)object, value);
#endif
}
static void video(const char *name) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, name, strlen(BB_DATA(name)) + 1);
  BB_GAME_CALL(0x432df0, 0x4eebc8, buffer.reference);
  bb_model_buffer_end(&buffer, 0);
#else
  ((self_text_fn)BB_CODE((uintptr_t)0x00432df0))((void *)0x004eebc8, name);
#endif
}
static void font(uintptr_t object, uintptr_t text, int32_t x, int32_t y,
                 uint32_t align) {
  bb_font_draw((void *)object, TEXT(text), x, y, align);
}
static void sprite(uintptr_t object, int32_t x, int32_t y, int32_t frame) {
  bb_sprite_draw((void *)object, x, y, frame, 1);
}
static void clip(int32_t x, int32_t y, int32_t width, int32_t height) {
  bb_display_viewport((void *)0x004eebc8, x, y, width, height);
}
static void rectangle(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  bb_render_queue_insert((void *)0x005d19e0, x1, y1, x2, y2, 1);
}
static void invalidate(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  bb_render_queue_both((void *)0x005d19e0, x1, y1, x2, y2);
}
static void pending(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  bb_render_queue_pending_add((void *)0x005d19e0, x1, y1, x2, y2, 1);
}

/* 00429250: this clock uses its own original format string. */
void bb_clock_text_entry(uint32_t time, char *out) {
  FORMAT(out, TEXT(0x0048856c), time / 129600u, time / 2160u % 60u,
         time / 36u % 60u, time % 36u);
}

/* 0040ccc0: interface callbacks retain their fixed order and objects. */
void bb_interface_tick_entry(void) {
  self_call(0x0041a0a0, 0x005d19a0);
  bb_stock_panel_tick_entry((void *)(uintptr_t)0x006a98e0);
  self_call(0x0041a930, 0x005f90d8);
  self_call(0x0041c400, 0x00640ce8);
  self_call(0x004295a0, 0x005eac80);
  self_call(0x0041b450, 0x006b0858);
  bb_message_tick_entry((void *)0x005ecf38);
  bb_letters_tick_entry((void *)0x005db050);
  self_call(0x0041e040, 0x00662c98);
  self_call(0x0041e9c0, 0x005ca308);
  self_call(0x00420950, 0x006af100);
  self_call(0x00427280, 0x006b08c0);
  self_call(0x0042a7d0, 0x006a8ef8);
  self_call(0x0042ab40, 0x00559128);
  self_call(0x0042b430, 0x006a9950);
}

/* 0040cd60: paused simulation still advances cursor delay, the displayed
 * treasury and scripts. Clock checks reread state after each child callback. */
void bb_simulation_tick_entry(void) {
  if (!BYTE(0x0055540c)) {
    self_call(0x004296b0, 0x005e470c);
    if (BB_U32(0x00484760))
      self_call(0x00420c20, 0x00642c48);
    bb_entity_pool_tick((void *)0x00553f90);
    bb_notification_tick_entry((void *)0x005eaca8);
    self_call(0x0042c450, 0x006025d0);
    if (BB_U32(0x004842c4) % 36u == 0 && BB_U32(0x00484434))
      self_call(0x0042dc30, 0x00554fbc + BYTE(0x004843f0) * 0xd00u);
    if (!(BB_U32(0x004842c4) & 0x7ffu))
      bb_random_event_entry();
    bb_faction_tick((void *)0x00554fb8);
    int32_t dx = bb_random() % 21 - 10;
    BB_U32(0x004846a8) += (uint32_t)dx;
    int32_t dy = bb_random() % 21 - 10;
    BB_U32(0x004846ac) += (uint32_t)dy;
    if (BB_I32(0x004846a8) > 512)
      BB_I32(0x004846a8) = 512;
    if (BB_I32(0x004846ac) > 512)
      BB_I32(0x004846ac) = 512;
    if (BB_I32(0x004846a8) < -512)
      BB_I32(0x004846a8) = -512;
    if (BB_I32(0x004846ac) < -512)
      BB_I32(0x004846ac) = -512;
  }
  uint32_t delay = BB_U32(0x0048427c);
  if (delay) {
    BB_U32(0x0048427c) = --delay;
    if (!delay) {
      BB_U32(0x00484280) = 1;
      BB_U32(0x00484284) = 1;
    }
  }
  int32_t target = BB_I32(BB_U32(0x004844a8) + 0x4a);
  uint32_t value = BB_U32(0x00484720);
  if ((int32_t)value != target) {
    if ((int32_t)(value + 10000u) < target)
      value += 4700u;
    else if ((int32_t)(BB_U32(0x00484720) + 1000u) < target)
      value = BB_U32(0x00484720) + 470u;
    else if ((int32_t)(value + 100u) < target)
      value += 47u;
    else if ((int32_t)(value + 10u) < target)
      value += 7u;
    else if ((int32_t)value < target)
      ++value;
    else if ((int32_t)(value - 10000u) > target)
      value -= 4700u;
    else if ((int32_t)(value - 1000u) > target)
      value -= 470u;
    else if ((int32_t)(value - 100u) > target)
      value -= 47u;
    else if ((int32_t)(value - 10u) > target)
      value -= 7u;
    else if ((int32_t)value > target)
      --value;
    /* Equal-after-wrapped-threshold comparisons can leave the value alone. */
    if (value != BB_U32(0x00484720))
      BB_U32(0x00484720) = value;
#ifndef BB_MODEL_RUNTIME
    typedef void (*sample_fn)(uint32_t);
#endif
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x415860, 0, BB_U32(0x4842e8));
#else
    ((sample_fn)BB_CODE((uintptr_t)0x00415860))(BB_U32(0x004842e8));
#endif
  }
  BB_I32(0x0048449c) = (int32_t)(BB_U32(0x0048449c) + 1u) % 16;
  bb_script_tick_entry((void *)0x005e47b0);
}

static uint32_t absolute(uint32_t value) {
  return (int32_t)value < 0 ? 0u - value : value;
}
static void pick_hover(void) {
  int32_t best = (int32_t)(BB_U32(0x00484444) + BB_U32(0x00484448));
  BB_U32(0x00484488) = 0;
  if (BB_U32(0x00484704) == 2 || BB_U32(0x00484700) == 2)
    return;
  for (uintptr_t p = BB_U32(0x00554f98); p; p = BB_U32(p + 4)) {
    if (!BYTE(p + 0x128) || SHORT(p + 0xbe) > BB_I32(0x004846b8) ||
        SHORT(p + 0xc0) > BB_I32(0x004846bc) ||
        SHORT(p + 0xc2) < BB_I32(0x004846b8) ||
        SHORT(p + 0xc4) < BB_I32(0x004846bc))
      continue;
    uint32_t type = BYTE(p + 0xd4);
    uint32_t first = BB_U32(0x0066359a + type * 914u),
             second = BB_U32(0x0066359e + type * 914u);
    if ((type == 0x67 || type == 0x68) && !BYTE(p + 0xf4))
      first = second = 0;
    if (!first && !BYTE(0x004843e8) &&
        !(BYTE(BB_U32(0x004844a8) + 0x451) && second))
      continue;
    uint32_t dx = (uint32_t)(SHORT(p + 0xbe) / 2 + SHORT(p + 0xc2) / 2) -
                  BB_U32(0x004846b8);
    uint32_t dy = (uint32_t)(SHORT(p + 0xc0) / 2 + SHORT(p + 0xc4) / 2) -
                  BB_U32(0x004846bc);
    int32_t distance = (int32_t)(absolute(dx) + absolute(dy));
    if (distance < best) {
      best = distance;
      BB_U32(0x00484488) = p;
    }
  }
}

/* 0040bba0: complete frame orchestration, hit testing, overlays and cursor. */
void bb_frame_update_entry(void) {
  if (BYTE(0x004847bc)) {
    invalidate(0, 0, BB_U32(0x00484444) - 1u, BB_U32(0x00484448) - 1u);
    --BYTE(0x004847bc);
  }
  bb_sprite_cursor_erase((void *)(uintptr_t)BB_U32(0x00559110));
  if (!BYTE(0x005c5b64)) {
    if (BYTE(0x005e4714))
      bb_placement_clear(BYTE(0x004843f0), BYTE(0x005e4714), BB_U32(0x004846dc),
                         BB_U32(0x004846e0));
  } else if (BB_U32(0x004846dc) != BB_U32(0x004846d0) ||
             BB_U32(0x004846e0) != BB_U32(0x004846d4) ||
             BYTE(0x005e4714) != BYTE(0x005c5b64) ||
             BB_U32(0x004842c4) % 36u == 0) {
    bb_placement_clear(BYTE(0x004843f0), BYTE(0x005e4714), BB_U32(0x004846dc),
                       BB_U32(0x004846e0));
    BB_U32(0x00484728) =
        bb_placement_probe(BYTE(0x004843f0), BYTE(0x005c5b64),
                           BB_U32(0x004846d0), BB_U32(0x004846d4));
  }
  BYTE(0x005e4714) = BYTE(0x005c5b64);
  BB_U32(0x004846dc) = BB_U32(0x004846d0);
  BB_U32(0x004846e0) = BB_U32(0x004846d4);
  if (BB_I32(0x005fe288) < -7680)
    BB_I32(0x005fe288) = -7680;
  if (BB_I32(0x005fe288) > 7680)
    BB_I32(0x005fe288) = 7680;
  if (BB_I32(0x005fe290) < 0)
    BB_I32(0x005fe290) = 0;
  if (BB_I32(0x005fe290) > 7680)
    BB_I32(0x005fe290) = 7680;
  BB_U32(0x006024b8) = BB_U32(0x005fe288);
  BB_U32(0x00602484) = BB_U32(0x005fe290);
  bb_screen_to_map_entry(BB_U32(0x005db014), BB_U32(0x006a9078),
                         (int32_t *)0x004846e4, (int32_t *)0x004846e8, 0);
  BB_U32(0x00513444) = BB_U32(0x006024b8);
  BB_U32(0x00513430) = BB_U32(0x00602484);
  bb_screen_to_map_entry(0, 0, (int32_t *)0x005fe3b0, (int32_t *)0x005fe3c0, 0);
  bb_screen_to_map_entry(BB_U32(0x00484444), 0, (int32_t *)0x005fe3b4,
                         (int32_t *)0x005fe3c4, 0);
  bb_screen_to_map_entry(0, BB_U32(0x00484448), (int32_t *)0x005fe3b8,
                         (int32_t *)0x005fe3c8, 0);
  bb_screen_to_map_entry(BB_U32(0x00484444), BB_U32(0x00484448),
                         (int32_t *)0x005fe3bc, (int32_t *)0x005fe3cc, 0);
  switch (BB_U32(0x0048443c)) {
  case 0:
    BB_U32(0x00484494) = 0;
    BB_U32(0x00484498) = 41;
    BB_U32(0x00604638) = BB_U32(0x00484444);
    BB_I32(0x005db014) = BB_I32(0x00484444) / 2;
    BB_I32(0x006a9078) = (int32_t)(BB_U32(0x00484448) + 41u) / 2;
    BB_U32(0x00513440) = BB_U32(0x005f90e8) - 41u;
    clip(0, 41, BB_U32(0x00604638), BB_U32(0x00513440));
    bb_render_frame();
    clip(0, 0, BB_U32(0x00484444), BB_U32(0x00484448));
    break;
  case 1: {
    clip(0, 0, BB_U32(0x00484444), BB_U32(0x00484448));
    sprite(BB_U32(0x006aff50), 0, 0, 255);
#ifndef BB_MODEL_RUNTIME
    typedef void (*map_fn)(uint32_t, int32_t, int32_t, uint32_t, uint32_t,
                           uint32_t);
#endif
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x41d670, 0, BB_U32(0x4eec00), BB_I32(0x484444)/2,
                 BB_I32(0x484448)/2, BB_U32(0x484470), BB_U32(0x484474), 1);
#else
    ((map_fn)BB_CODE((uintptr_t)0x0041d670))(BB_U32(0x004eec00), BB_I32(0x00484444) / 2,
                                    BB_I32(0x00484448) / 2, BB_U32(0x00484470),
                                    BB_U32(0x00484474), 1);
#endif
    break;
  }
  case 2:
    self_call(0x0042c420, 0x006025d0);
    break;
  case 3:
    bb_letters_draw_entry((void *)0x005db050);
    break;
  }
  if (BB_U32(0x0048443c) != 4) {
    self_call(0x00419aa0, 0x005d19a0);
    if (BB_U32(0x0048474c))
      font(0x005cd7d0, 0x00486e54, 424, 26, 1);
    if (!BB_U32(0x0048443c)) {
      self_call(0x00429d60, 0x006a98e0);
      bb_message_draw_entry((void *)0x005ecf38);
      self_call(0x0041a430, 0x005f90d8);
      self_call(0x0041b1e0, 0x006b0858);
      self_call(0x0041c360, 0x00640ce8);
      self_call(0x00429480, 0x005eac80);
      uintptr_t weather = BB_U32(0x005e477c);
      sprite(weather, 639u - BB_U32(weather + 0x44), 46, BB_U32(0x00484768));
      weather = BB_U32(0x005e477c);
      pending(639u - BB_U32(weather + 0x44), 46, 640,
              BB_U32(weather + 0x48) + 46u);
    }
    self_call(0x004271c0, 0x006b08c0);
    self_call(0x0042a5f0, 0x006a8ef8);
    font(0x005f74b8, 0x006ad200, 250, 14, 0);
    int32_t gold = BB_I32(0x00484720);
    if (gold >= 0)
      for (int32_t x = 116; x > -4; x -= 20)
        if (gold > 0 || x == 116) {
          sprite(BB_U32(0x005f9174), x, 14, gold % 10);
          gold /= 10;
        }
    self_call(0x0042aac0, 0x00559128);
    uint8_t indicator = BYTE(BB_U32(0x004844a8) + 0x450);
    uintptr_t slot = indicator == 1   ? 0x0069c738
                     : indicator == 2 ? 0x005ecf9c
                                      : 0;
    if (slot) {
      uintptr_t p = BB_U32(slot);
      sprite(p, BB_U32(0x0048444c) - (uint32_t)(BB_I32(p + 0x44) / 2),
             BB_U32(0x00484450) - (uint32_t)(BB_I32(p + 0x48) / 2), 255);
      p = BB_U32(slot);
      int32_t height = BB_I32(p + 0x48) / 2, width = BB_I32(p + 0x44) / 2;
      pending(BB_U32(0x0048444c) - (uint32_t)width,
              BB_U32(0x00484450) - (uint32_t)height,
              BB_U32(0x0048444c) + (uint32_t)width,
              BB_U32(0x00484450) + (uint32_t)height);
    }
  }
  if (BYTE(0x004843e8) == 2)
    bb_script_draw_entry((void *)0x005e47b0);
  pick_hover();
  if (!BB_U32(0x0048443c)) {
    if (BB_U32(0x004843e4)) {
      uintptr_t text = BB_U32(0x006034f8);
      FORMAT((char *)0x006033d8, TEXT(text ? text : 0x0048481c),
             BYTE(0x004843f0));
      font(0x0063f0d0, 0x006033d8, BB_I32(0x00484444) / 2,
           BB_U32(0x00484448) - 10u, 1);
      rectangle((uint32_t)(BB_I32(0x00484444) / 2) - 80u,
                BB_U32(0x00484448) - 40u, BB_U32(0x00484444) + 80u,
                BB_U32(0x00484448));
      BYTE(0x004843e0) = BYTE(0x00484454);
    } else if (BYTE(0x004843e0)) {
      invalidate((uint32_t)(BB_I32(0x00484444) / 2) - 80u,
                 BB_U32(0x00484448) - 40u, BB_U32(0x00484444) + 80u,
                 BB_U32(0x00484448));
      --BYTE(0x004843e0);
    }
    if (BYTE(0x004843e8) == 1) {
      if (BYTE(0x004843f0) == 4)
        FORMAT((char *)0x006033d8, TEXT(0x00486e40));
      else
        FORMAT((char *)0x006033d8, TEXT(0x00486e2c), BYTE(0x004843f0));
      font(0x0063f0d0, 0x006033d8, 200, 70, 0);
      rectangle(199, 54, 400, 75);
    } else if (BYTE(0x004843e8) == 2) {
      FORMAT((char *)0x006033d8, TEXT(0x00486e20));
      font(0x0063f0d0, 0x006033d8, 200, 70, 0);
      rectangle(199, 54, 340, 75);
      bb_clock_text_entry(BB_U32(0x004842c4), (char *)0x006033d8);
      font(0x005f74b8, 0x006033d8, 320, 20, 0);
    } else {
      FORMAT((char *)0x006033d8, TEXT(0x00486e1c),
             FORMAT_WORD(0x00484778 + BB_U32(0x00484768) * 7u));
      BYTE(0x004843f0) = BYTE(0x004844a4);
    }
    if (BB_U32(0x004842a0))
      font(0x005c5b70, 0x00486e14, 630, 6, 2);
    if (BB_U32(0x00484704) == 2) {
#ifndef BB_MODEL_RUNTIME
      typedef void (*marquee_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
#endif
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(0x415360, 0, BB_U32(0x48470c), BB_U32(0x484710),
                   BB_U32(0x484714), BB_U32(0x484718));
#else
      ((marquee_fn)BB_CODE((uintptr_t)0x00415360))(
          BB_U32(0x0048470c), BB_U32(0x00484710), BB_U32(0x00484714),
          BB_U32(0x00484718));
#endif
      BB_U32(0x004846f0) = 0;
      BB_U32(0x004846f8) = 0;
      BB_U32(0x004846f4) = 0;
      BB_U32(0x004846fc) = 0;
      BB_U32(0x006a8f28) = BB_U32(0x006024b8);
      BB_U32(0x006a8f24) = BB_U32(0x00602484);
    }
  }
  self_call(0x0041ded0, 0x00662c98);
  if (!BB_U32(0x0048443c))
    bb_notification_draw_entry((void *)0x005eaca8);
  if (BB_U32(0x00484280))
    BB_U32(0x00484284) = BYTE(0x005c5b64);
  int32_t x, y;
  if (BB_U32(0x00484700) == 2) {
    x = BB_U32(0x004846c8);
    y = BB_U32(0x004846cc);
  } else {
    x = BB_U32(0x004846b8);
    y = BB_U32(0x004846bc);
  }
#ifndef BB_MODEL_RUNTIME
  typedef void(BB_THISCALL * cursor_fn)(void *, int32_t, int32_t,
                                                      uint32_t);
#endif
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x4193f0, 0x6b0830, x, y, 1);
#else
  ((cursor_fn)BB_CODE((uintptr_t)0x004193f0))((void *)0x006b0830, x, y, 1);
#endif
  bb_display_flip((void *)0x004eebc8);
}

/* 00414990/00414a20/00414b10: mission introductions, archive progression,
 * player-script selection and numbered mission exports. */
void bb_mission_intro_entry(void) {
  char name[256];
  self_word(0x0042b480, 0x006a9950, BYTE(0x005e5cc3));
  FORMAT(name, TEXT(0x00486ea4), BYTE(0x005e5cc3));
  video(name);
  FORMAT(name, TEXT(0x00486e9c), BYTE(0x005e5cc3));
  video(name);
  if (BB_U32(0x00484274) == 2) {
    BB_U32(0x00484274) = 1;
    BB_U32(0x00484278) = 1;
  }
}
void bb_mission_begin_entry(int32_t mission) {
  char name[256];
  if (mission <= 30) {
    FORMAT(name, TEXT(0x00486eb8), mission);
    const char *tail = BB_DATA(0x00486eb0);
    size_t n = strlen(tail) + 1;
    char *out = name + strlen(name);
    while (n >= 4) {
      BB_U32(out) = BB_U32(tail);
      out += 4;
      tail += 4;
      n -= 4;
    }
    while (n--)
      *out++ = *tail++;
    if (bb_mission_load_entry(name, 1)) {
      BYTE(0x005e5cc3) = (uint8_t)mission;
      bb_mission_intro_entry();
      for (unsigned i = 0; i < 5; ++i)
        bb_script_block_select_entry((void *)0x005e47b0, i, 0);
      return;
    }
  }
  BYTE(0x005e5cc3) = (uint8_t)mission;
  bb_mission_intro_entry();
  BB_U32(0x00484400) = 1;
  if (BB_U32(0x0048474c)) {
    bb_session_credits_entry();
    return;
  }
  bb_session_outro_entry();
  video(TEXT(0x00484b50));
}
void bb_mission_export_entry(int32_t mission) {
  char name[256], description[256];
  FORMAT(name, TEXT(0x00486ec0), mission);
  FORMAT(description, TEXT(0x00486ebc), mission);
  BYTE(0x005e5cc3) = (uint8_t)mission;
  bb_mission_save_entry(name, description);
}
void bb_world_refresh_entry(void) {
  bb_random_seed_entry(1);
  for (unsigned i = 0; i < 5; ++i) {
    void *node = bb_entity_nearest((void *)0x00553f90, i, 0x62, 96, 96);
    if (node)
      BB_U32(0x00554fbc + i * 0xd00u) = BB_U32((uintptr_t)node + 0x8e);
  }
  bb_display_invalidate_entry();
  for (uintptr_t p = 0x00554fbc; p < 0x005590bc; p += 0xd00) {
    self_call(0x00429120, p);
    bb_faction_buildable((void *)p);
  }
}
