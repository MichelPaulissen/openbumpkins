/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "interface_platform.h"
#include "command_builders.h"
#include "controls.h"
#include "dialog.h"
#include "interface_controls.h"
#include "interop.h"
#include "statistics.h"
#include "world.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
typedef uint32_t (*c0)(void);
typedef int (*format_fn)(char *, const char *, ...);
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#else
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x46bae0))
#endif

/* Typed interfaces to the existing C archive bodies, beyond their ABI shims. */
uint32_t bb_archive_open(void *);
void *bb_archive_close(void *);
void *bb_archive_member(void *, const char *);
uint32_t bb_archive_release(void *);
uint32_t bb_archive_seek(void *, uint32_t, uint32_t, uint32_t);
uint32_t bb_archive_read(void *, void *, uint32_t);

static uint32_t string_size(uintptr_t p) {
  uint32_t n = 1;
  while (BYTE(p + n - 1))
    ++n;
  return n;
}
static void copy_bytes(uintptr_t out, uintptr_t in, uint32_t n) {
  while (n >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    n -= 4;
  }
  while (n--)
    BYTE(out++) = BYTE(in++);
}
static void copy_text(uintptr_t out, uintptr_t in) {
  copy_bytes(out, in, string_size(in));
}
static void append_text(uintptr_t out, uintptr_t in) {
  uint32_t n = string_size(in);
  copy_bytes(out + string_size(out) - 1, in, n);
}
static uintptr_t faction(void) { return 0x554fbcu + BYTE(0x4843f0) * 0xd00u; }
#ifdef BB_MODEL_RUNTIME
static void invalidate(void) { BB_GAME_CALL(0x403190,0); }
#else
static void invalidate(void) { ((c0)BB_CODE((uintptr_t)0x403190))(); }
#endif

/* 00403270 consumes one filtered mouse message, despite its caller's name. */
void bb_interface_mouse_drain_entry(uint32_t viewport) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_scratch scratch;
  bb_model_scratch_begin(&scratch, 28);
  uint32_t message = scratch.address;
  BB_U32(0x4844a0) = 0;
  if (BB_GAME_CALL(BB_U32(0x6ca52c),0,message,0,0x200,0x200,0) &&
      BB_GAME_CALL(BB_U32(0x6ca528),0,message,0,0x200,0x200)) {
    if (viewport)
      bb_display_viewport(PTR(0x4eebc8),0,0,BB_I32(0x484444),BB_I32(0x484448));
    BB_GAME_CALL(BB_U32(0x6ca524),0,message);
    BB_GAME_CALL(BB_U32(0x6ca520),0,message);
    if (viewport)
      bb_display_viewport(PTR(0x4eebc8),BB_I32(0x484494),BB_I32(0x484498),BB_I32(0x604638),BB_I32(0x513440));
  }
  bb_model_scratch_end(&scratch);
#else
  uint32_t message[7];
  typedef int(BB_STDCALL * peek_fn)(void *, void *, uint32_t,
                                                  uint32_t, uint32_t);
  typedef int(BB_STDCALL * get_fn)(void *, void *, uint32_t,
                                                 uint32_t);
  typedef int(BB_STDCALL * message_fn)(void *);
  BB_U32(0x4844a0) = 0;
  if (!((peek_fn)BB_CODE(BB_POINTER(0x6ca52c)))(message, NULL, 0x200, 0x200, 0))
    return;
  if (!((get_fn)BB_CODE(BB_POINTER(0x6ca528)))(message, NULL, 0x200, 0x200))
    return;
  if (viewport)
    bb_display_viewport(PTR(0x4eebc8), 0, 0, BB_I32(0x484444),
                        BB_I32(0x484448));
  ((message_fn)BB_CODE(BB_POINTER(0x6ca524)))(message);
  ((message_fn)BB_CODE(BB_POINTER(0x6ca520)))(message);
  if (viewport)
    bb_display_viewport(PTR(0x4eebc8), BB_I32(0x484494), BB_I32(0x484498),
                        BB_I32(0x604638), BB_I32(0x513440));
#endif
}

void BB_THISCALL bb_minimap_panel_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p) && !BYTE(p + 13)) {
    uint32_t night = BB_U32(0x484288);
    BB_U32(0x484288) = 0;
    BYTE(p + 12) = BYTE(0x484454);
    bb_display_viewport(PTR(0x4eebc8), 10, 48, 160, 98);
    bb_minimap_draw_entry(PTR(BB_U32(0x4eec00)), BB_U32(p + 4), BB_U32(p + 8),
                          BB_U32(0x4846e4), BB_U32(0x4846e8), 0);
    bb_display_viewport(PTR(0x4eebc8), 0, 0, BB_I32(0x484444),
                        BB_I32(0x484448));
    bb_sprite_draw(PTR(BB_U32(0x5f6f2c)), 0, 37, 255, 1);
    bb_render_queue_pending_add(PTR(0x5d19e0), 0, 37, 210, 160, 1);
    BB_U32(0x484288) = night;
  } else if (BYTE(p + 12)) {
    bb_render_queue_both(PTR(0x5d19e0), 0, 37, 210, 170);
    --BYTE(p + 12);
  }
}
void BB_THISCALL
bb_minimap_panel_visibility_entry(void *object, uint32_t visible) {
  BB_U32((uintptr_t)object) = visible;
  bb_render_queue_both(PTR(0x5d19e0), 0, 37, 210, 170);
}
void bb_minimap_panel_noop_entry(void) {}
uint32_t BB_THISCALL
bb_minimap_panel_click_entry(void *object, int32_t x, int32_t y) {
  uintptr_t p = (uintptr_t)object;
  if (!BB_U32(p) || BYTE(p + 13) || x >= 172 || y <= 48 || y >= 146)
    return 0;
  uint8_t tx, ty;
  bb_mouse_map_position_entry(BB_I32(p + 4), BB_I32(p + 8), BB_I32(0x4846e4),
                              BB_I32(0x4846e8), &tx, &ty);
  if (BYTE(BB_U32(0x4844a8) + 0x451)) {
    bb_command_walk_entry(PTR(faction()), tx, ty);
    bb_sample_play_entry(BB_U32(0x48430c));
  } else {
    bb_sample_play_entry(BB_U32(0x4842d8));
    bb_camera_focus_tile_entry(tx, ty);
  }
  return 1;
}

void bb_season_tick_entry(void) {
  BB_U32(0x48476c) = BB_U32(0x484768);
  uint32_t night = BB_U32(0x484288), time = BB_U32(0x4842c4) >> 1;
  BB_U32(0x48428c) = night;
  BB_U32(0x484764) = time;
  uint32_t seasons = time / 1440;
  BB_U32(0x484768) = seasons & 3;
  BB_U32(0x484770) = (seasons >> 2) + 1100;
  if (BB_U32(0x484768) != BB_U32(0x48476c)) {
    static const uintptr_t palettes[] = {0x4885cc, 0x4885c4, 0x4885bc,
                                         0x4885b4};
    uint32_t season = BB_U32(0x484768);
    bb_palette_install(PTR(0x4eebc8), PTR(palettes[season]));
    bb_sound_event_entry(season + 1, 1);
    int winter = BB_U32(0x484768) == 3;
    if (winter || BB_U32(0x48476c) == 3) {
      static const uintptr_t slots[] = {0x6aff40, 0x5c950c, 0x6b0844, 0x6af0f4,
                                        0x6a9070};
      static const uintptr_t names[2][5] = {
          {0x486dcc, 0x486d54, 0x4861f0, 0x484f4c, 0x484f40},
          {0x4885a8, 0x48859c, 0x484d84, 0x488590, 0x488580}};
      uintptr_t first = BB_U32(slots[0]);
      copy_text(first, names[winter][0]);
      for (unsigned i = 1; i < 5; ++i)
        copy_text(BB_U32(slots[i]), names[winter][i]);
      bb_sprite_restore(PTR(first));
      for (unsigned i = 1; i < 5; ++i)
        bb_sprite_restore(PTR(BB_U32(slots[i])));
    }
  }
  uint32_t now = BB_U32(0x484764) % 1440 / 1120;
  BB_U32(0x484288) = now;
  if (now && !BB_U32(0x48428c)) {
    bb_interface_resources_refresh_entry();
    bb_sound_event_entry(5, 0);
  } else if (!now && BB_U32(0x48428c)) {
    bb_observer_display_refresh_entry();
    bb_sprite_restore(PTR(BB_U32(0x6aff40)));
    bb_sprite_restore(PTR(BB_U32(0x5c950c)));
    bb_sound_event_entry(5, 1);
  }
}

uint32_t BB_STDCALL
bb_nearest_free_tile_entry(uint32_t x, uint32_t y, uint8_t *out_x,
                           uint8_t *out_y, uint32_t distance) {
  x = (uint8_t)x;
  y = (uint8_t)y;
  int32_t best = (int32_t)(((uint32_t)(uint8_t)distance << 16) + 1);
  uint32_t found = 0;
  bb_entity_blockers_c(PTR(0x553f90), 1, (uint32_t)best);
  for (int dy = -4; dy <= 4; ++dy) {
    uint32_t ty = (uint8_t)(y + (uint32_t)dy);
    for (int dx = -4; dx <= 4; ++dx) {
      uint32_t tx = (uint8_t)(x + (uint32_t)dx);
      uintptr_t t = 0x559b60u + (ty * 192u + tx) * 12u;
      if (!(BB_U32(t + 8) & 0x200001) && !BYTE(t + 4) && !BYTE(t + 6)) {
        int32_t d = (int32_t)bb_entity_distance(x, y, tx, ty);
        if (d < best) {
          best = d;
          BYTE(out_x) = tx;
          found = 1;
          BYTE(out_y) = ty;
        }
      }
    }
  }
  bb_entity_blockers_c(PTR(0x553f90), UINT32_MAX, 0);
  return found;
}

static void stock_text(char *text, int32_t x) {
  int32_t x1, y1, x2, y2;
  bb_font_bounds(PTR(0x5cf3f0), text, x, 470, 2, &x1, &y1, &x2, &y2);
  bb_display_shade(PTR(0x4eebc8), (int32_t)((uint32_t)x1 - 3),
                   (int32_t)((uint32_t)y1 - 3), (int32_t)((uint32_t)x2 + 3),
                   (int32_t)((uint32_t)y2 + 3));
  bb_font_draw(PTR(0x5cf3f0), text, x, 470, 2);
}
/* Explicit incoming frame avoids uninitialized C in the original default arm.
 */
uint32_t bb_stock_panel_draw_c(void *object, uint32_t frame) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p + 32)) {
    uintptr_t e = BB_U32(p + 40), storage = BB_U32(e + 0x38);
    if (storage) {
      uint32_t count = WORD(storage);
      unsigned category = BYTE(0x6638c7 + BYTE(e + 0xd4) * 914u);
      uintptr_t slot = 0;
      switch (category) {
      case 1:
        slot = 0x6037f0;
        if (count)
          frame = 0;
        break;
      case 7:
        slot = 0x6037f4;
        if (count)
          frame = 1;
        break;
      case 9:
        slot = 0x6037fc;
        if (count)
          frame = 3;
        break;
      case 18:
        slot = 0x6037f8;
        if (count)
          frame = 2;
        break;
      default:
        break;
      }
      if (count) {
        uint32_t x = BB_U32(0x484444) - 30, y = BB_U32(0x484448) - 30;
        for (unsigned i = 0; i < count; ++i) {
          bb_sprite_draw(PTR(BB_U32(0x640ce0)), (int32_t)x, (int32_t)y, frame,
                         1);
          x -= 10;
          if (i == 7 || i == 15) {
            y -= 20;
            x += 80;
          }
        }
      } else {
        char text[256];
        *(uint32_t *)text = 0;
        if (slot)
          copy_text((uintptr_t)text, BB_U32(slot) ? BB_U32(slot) : 0x48481c);
        stock_text(text, 636);
      }
    } else {
      unsigned kind = BYTE(e + 0xd4);
      uintptr_t bank = kind * 914u;
      if (BYTE(0x6635bd + bank)) {
        uint32_t owner = BYTE(e + 0xf7) * 0x680u;
        uint32_t current =
            WORD(0x5559bc + (owner + BYTE(0x6635be + bank)) * 2u);
        uint32_t capacity =
            WORD(0x55503a + (owner + kind) * 2u) * BYTE(0x6635bf + bank);
        char text[256];
        FORMAT(text, PTR(0x4885d4), current, capacity);
        stock_text(text, 630);
      } else if (kind == 0x67) {
        BYTE(0x4842bc) = BYTE(e + 0xf8);
        unsigned prof = BYTE(BB_U32(p + 40) + 0xf4);
        bb_sprite_draw(PTR(BB_U32(0x663500)), BB_I32(p), BB_I32(p + 4),
                       BB_U32(0x5e5ee0 + prof * 292u), 1);
        if (BYTE(BB_U32(p + 40) + 0x11a)) {
          uintptr_t image = BB_U32(0x5c7784);
          bb_sprite_draw(PTR(image),
                         (int32_t)(BB_U32(0x484444) - BB_U32(image + 0x44) - 1),
                         (int32_t)(BB_U32(0x484448) - BB_U32(image + 0x48) - 1),
                         255, 1);
        }
        uint32_t f = BB_U32(p + 8);
        if (f) {
          int32_t tick = BB_I32(p + 12);
          if (tick < 4 || tick >= 116)
            f = 0;
          else if (tick < 8 || tick >= 112)
            f = 1;
          bb_sprite_draw(PTR(BB_U32(0x5fe3a8)), BB_I32(p + 16), BB_I32(p + 20),
                         f, 1);
        }
      }
    }
  }
  if (BB_U32(p + 36) || BB_U32(p + 32)) {
    bb_render_queue_insert(
        PTR(0x5d19e0), BB_I32(p), (int32_t)(BB_U32(p + 4) - 80),
        (int32_t)(BB_U32(0x484444) - 1), (int32_t)(BB_U32(0x484448) - 1), 1);
    if (!BB_U32(p + 32))
      --BB_U32(p + 36);
    else
      BB_U32(p + 36) = BB_U32(0x484454);
  }
  return frame;
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((thiscall, naked))
bb_stock_panel_draw_entry(void *object __attribute__((unused))) {
  __asm__ volatile("movl -0x114(%esp),%eax;pushl %eax;pushl %ecx;call " BB_ASM(
      bb_stock_panel_draw_c) ";addl $8,%esp;movl %eax,-0x114(%esp);ret");
}
#endif

void bb_panel_mode_entry(uint32_t mode) {
  uint32_t prior = BB_U32(0x48443c);
  if (prior == 4) {
    bb_display_reset(PTR(0x4eebc8));
    bb_palette_install(PTR(0x4eebc8), PTR(0x486dd8));
  }
  if (prior == 2 || prior == 3 || prior == 4)
    bb_command_resume_entry(PTR(faction()));
  if (!mode)
    invalidate();
  else if (mode == 2 || mode == 3 || mode == 4) {
    bb_command_pause_entry(PTR(faction()));
    if (mode == 4) {
      bb_display_reset(PTR(0x4eebc8));
      bb_palette_install(PTR(0x4eebc8), PTR(0x4885ec));
    }
  }
  BB_U32(0x48443c) = mode;
}

void BB_THISCALL bb_mission_description_draw_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p)) {
    if (BB_U32(0x4842d0))
      bb_sprite_draw(PTR(BB_U32(0x4842d0)), 0, 0, 255, 1);
    bb_sprite_draw(PTR(BB_U32(0x6afb20)), 0, BB_I32(p + 8), 255, 1);
  }
}
void BB_THISCALL
bb_mission_description_click_entry(void *object) {
  if (BB_I32((uintptr_t)object + 8) < 460) {
    bb_sample_play_entry(BB_U32(0x4842e4));
    bb_mission_popup_visibility_entry(object, 0);
  }
}
void BB_THISCALL bb_mission_description_tick_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p)) {
    if (BB_I32(p + 8) > 20)
      --BB_U32(p + 8);
    if (BB_U32(0x48474c) && !(bb_random() % 72) &&
        BB_I32(p + 8) < (int32_t)(BB_U32(p + 12) - 72))
      BB_U32(p + 12) = BB_U32(p + 8);
  }
}
void BB_THISCALL
bb_mission_description_load_entry(void *object, uint32_t number) {
  uintptr_t p = (uintptr_t)object;
  char text[256];
  bb_archive_open(PTR(0x5d1028));
  FORMAT(text, PTR(0x488610), number);
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer filename;
  bb_model_buffer_begin(&filename,text,string_size((uintptr_t)text));
  void *member = bb_archive_member(PTR(0x5d1028),(const char *)(uintptr_t)filename.reference);
  bb_model_buffer_end(&filename,0);
#else
  void *member = bb_archive_member(PTR(0x5d1028), text);
#endif
  bb_mission_description_clear_entry(object);
  if (member) {
    uint32_t n = bb_archive_seek(member, 0, 2, 0);
    bb_archive_seek(member, 0, 0, 0);
#ifdef BB_MODEL_RUNTIME
    void *data = bb_game_allocate(n + 1);
#else
    void *data = ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x46c060))(n + 1);
#endif
    BB_U32(p + 4) = bb_address_encode(data);
    if (data) {
      bb_archive_read(member, data, n);
      BYTE(BB_U32(p + 4) + n) = 0;
    }
    bb_archive_release(member);
  } else {
    append_text((uintptr_t)text, 0x4885fc);
#ifdef BB_MODEL_RUNTIME
    void *data = bb_game_allocate(string_size((uintptr_t)text));
#else
    void *data = ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x46c060))(
        string_size((uintptr_t)text));
#endif
    BB_U32(p + 4) = bb_address_encode(data);
    if (data)
      copy_text((uintptr_t)data, (uintptr_t)text);
  }
  bb_archive_close(PTR(0x5d1028));
}
void bb_game_resource_guard_entry(void) {
  char path[256];
#ifndef BB_MODEL_RUNTIME
  typedef void *(*open_fn)(const char *, const char *);
  typedef int (*close_fn)(void *);
#endif
  copy_text((uintptr_t)path, 0x6afd08);
  append_text((uintptr_t)path, 0x488770);
#ifdef BB_MODEL_RUNTIME
  void *file = bb_game_open(path,0x48416c);
#else
  void *file = ((open_fn)BB_CODE((uintptr_t)0x46bdd0))(path, PTR(0x48416c));
#endif
  if (!file)
    return;
#ifdef BB_MODEL_RUNTIME
  bb_game_close(file);
#else
  ((close_fn)BB_CODE((uintptr_t)0x46bbd0))(file);
#endif
  copy_text((uintptr_t)path, 0x6afd08);
  append_text((uintptr_t)path, 0x484b50);
#ifdef BB_MODEL_RUNTIME
  file = bb_game_open(path,0x48416c);
#else
  file = ((open_fn)BB_CODE((uintptr_t)0x46bdd0))(path, PTR(0x48416c));
#endif
  if (file) {
  #ifdef BB_MODEL_RUNTIME
  bb_game_close(file);
#else
  ((close_fn)BB_CODE((uintptr_t)0x46bbd0))(file);
#endif
    return;
  }
  for (unsigned i = 0; i < 5000; ++i)
    BB_U32(BB_U32(0x553f90) + i * 314u) = 0;
}
