/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "session_controls.h"
#include "bootstrap_resources.h"
#include "capture_video.h"
#include "command_builders.h"
#include "dialog.h"
#include "interface_platform.h"
#include "interop.h"
#include "game_calls.h"
#include "session_models.h"
#include "statistics.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#define REF(p) bb_address_encode((const void *)(uintptr_t)(p))
#define API(slot, ...) BB_GAME_CALL(BB_U32(slot), 0, __VA_ARGS__)
#ifdef BB_MODEL_RUNTIME
/* Children outside this conversion retain explicit logical code identities. */
#define bb_video_entry(p,n) ((void)BB_GAME_CALL(0x432df0u, REF(p), REF(n)))
#define bb_dialog_visibility_entry(p,n) ((void)BB_GAME_CALL(0x41ffc0u, REF(p), n))
#define bb_dialog_select_entry(p,n) ((void)BB_GAME_CALL(0x41f7a0u, REF(p), n))
#define bb_command_pause_entry(p) BB_GAME_CALL(0x42dbd0u, REF(p))
#define bb_command_resume_entry(p) BB_GAME_CALL(0x42dc00u, REF(p))
#define bb_mission_theme_entry(p,n) ((void)BB_GAME_CALL(0x42b5a0u, REF(p), n))
#define bb_sound_play_entry(n) BB_GAME_CALL(0x415730u, 0, n)
#define bb_sample_play_entry(n) BB_GAME_CALL(0x415860u, 0, n)
#define bb_sound_update_entry() BB_GAME_CALL(0x459d80u, 0)
#define bb_display_invalidate_entry() ((void)BB_GAME_CALL(0x403190u, 0))
#endif
uint32_t bb_archive_open(void *);
void *bb_archive_close(void *);
void *bb_archive_member(void *, const char *);
uint32_t bb_archive_release(void *);
uint32_t bb_archive_seek(void *, uint32_t, uint32_t, uint32_t);
uint32_t bb_archive_read(void *, void *, uint32_t);
void bb_cd_play(void *, int32_t);
uint32_t bb_cd_stop(void *);

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
static int text_equal(uintptr_t a, uintptr_t b) {
  while (BYTE(a) == BYTE(b)) {
    if (!BYTE(a))
      return 1;
    ++a;
    ++b;
  }
  return 0;
}
static void reset(void) { bb_display_reset(PTR(0x4eebc8)); }
static void present(void) { bb_display_flip(PTR(0x4eebc8)); }
static void flush(void) { bb_display_flush_entry(PTR(0x4eebc8)); }
static void quit(void) { bb_fatal_teardown_entry(0); }

void bb_session_credits_entry(void) {
  BB_U32(0x484400) = 0;
  if (!BB_U32(0x48474c))
    bb_video_entry(PTR(0x4eebc8), PTR(0x486fc4));
  reset();
  bb_shared_stop_entry(PTR(0x513458));
  bb_shared_reset_entry(PTR(0x513458));
  BB_U32(0x48426c) = 1;
  BB_U32(0x484274) = 0;
  bb_dialog_visibility_entry(PTR(0x5ca308), 1);
  bb_dialog_select_entry(PTR(0x5ca308), 1);
}
void bb_session_music_start_entry(uint32_t mode) {
  if (BB_U32(0x48474c))
    mode = 2;
  bb_interface_audio_stop_entry(PTR(0x5590e8));
  bb_interface_sound_enable_entry(PTR(0x5590e8));
  bb_cd_play(PTR(0x69e370), (int32_t)mode);
  uint32_t now = API(0x6ca378u);
  BB_U32(0x4842b0) = now;
  BB_U32(0x4842b4) = mode;
  bb_interface_audio_start_entry(PTR(0x5590e8));
  bb_interface_sound_disable_entry(PTR(0x5590e8));
}
void bb_session_music_stop_entry(void) {
  bb_interface_audio_stop_entry(PTR(0x5590e8));
  bb_interface_sound_enable_entry(PTR(0x5590e8));
  bb_cd_stop(PTR(0x69e370));
  bb_interface_audio_start_entry(PTR(0x5590e8));
  bb_interface_sound_disable_entry(PTR(0x5590e8));
  BB_U32(0x4842b4) = 0;
}
void bb_session_music_tick_entry(void) {
  uint32_t mode = BB_U32(0x4842b4);
  if (mode) {
    uint32_t now = API(0x6ca378u);
    if (now - BB_U32(0x4842b0) >= BB_U32(0x5f91c0u + mode * 4u))
      bb_session_music_stop_entry();
  }
}
void bb_session_audio_tick_entry(void) {
  bb_sound_update_entry();
  bb_session_music_tick_entry();
}
uint16_t bb_session_wait_c(uint32_t duration, uint32_t message) {
  uint32_t tick = BB_U32(0x6ca378u);
  uint32_t start = BB_GAME_CALL(tick, 0);
  BB_U32(0x4847c0) = 1;
  WORD(0x4847c4) = 0;
  BB_U32(0x484740) = BB_GAME_CALL(tick, 0);
  uint16_t key = WORD(0x4847c4);
  if (BB_U32(0x484740) - start < duration) {
    uint32_t translate = BB_U32(0x6ca524u);
    while (!key) {
      BB_U32(0x484740) = BB_GAME_CALL(tick, 0);
      if (API(0x6ca52cu, message, 0, 0, 0, 0) &&
          API(0x6ca528u, message, 0, 0, 0)) {
        BB_GAME_CALL(translate, 0, message);
        API(0x6ca520u, message);
      }
      bb_session_audio_tick_entry();
      uint32_t elapsed = BB_U32(0x484740) - start;
      key = WORD(0x4847c4);
      if (elapsed >= duration)
        break;
    }
  }
  BB_U32(0x4847c0) = 0;
  return key;
}
uint16_t bb_session_wait_entry(uint32_t duration) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(0x42e3a0u, 0, duration);
#else
  uint32_t message[7];
  return bb_session_wait_c(duration, REF(message));
#endif
}
void bb_session_pause_tick_entry(void) {
  if (!BYTE(0x4843dc)) {
    if (BB_U32(0x4842c4) - BB_U32(0x4842ac) > 72) {
      bb_command_pause_entry(PTR(0x554fbcu + BYTE(0x4843f0) * 0xd00u));
      BB_U32(0x4843e4) = 1;
      BB_U32(0x4842ac) = BB_U32(0x4842c4);
    }
  } else {
    bb_command_resume_entry(PTR(0x554fbcu + BYTE(0x4843f0) * 0xd00u));
    BB_U32(0x4843e4) = 0;
  }
}
uint32_t bb_session_escape_c(uint32_t message) {
  if (API(0x6ca52cu, message, 0, 0, 0, 0)) {
    if (!API(0x6ca528u, message, 0, 0, 0))
      return 0;
    if (BB_U32(message + 4) == 0x100 && BB_U32(message + 8) == 0x1b)
      return 1;
    API(0x6ca524u, message);
    API(0x6ca520u, message);
  }
  return 0;
}
uint32_t bb_session_escape_entry(void) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(0x42eb80u, 0);
#else
  uint32_t message[7];
  return bb_session_escape_c(REF(message));
#endif
}

/* Keep incoming archive-read tails and failed API outputs in the original
 * caller frame. The helpers own all behavior; entry shims reserve that frame
 * before their own calls can overwrite it. */
struct text_frame {
  char name[260], first[4096], second[4096];
};
static void load_text(struct text_frame *frame, uintptr_t name, char *out) {
  text_copy((uintptr_t)frame->name, name);
  void *member = bb_archive_member(PTR(0x5d1028), frame->name);
  if (member) {
    uint32_t n = bb_archive_seek(member, 0, 2, 0);
    if (n > 4095)
      n = 4095;
    bb_archive_seek(member, 0, 0, 0);
    bb_archive_read(member, out, n);
    out[n] = 0;
    bb_archive_release(member);
  } else
    BB_GAME_CALL(0x46bae0u, 0, REF(out), 0x488714u, REF(frame->name));
}
void bb_session_info_c(struct text_frame *frame) {
  bb_archive_open(PTR(0x5d1028));
  load_text(frame, 0x48874c, frame->first);
  load_text(frame, 0x488704, frame->second);
  bb_archive_close(PTR(0x5d1028));
  bb_sprite_draw(PTR(BB_U32(0x5c77b8)), 0, 0, 255, 1);
  bb_font_draw_wrapped(PTR(0x641028), frame->first, 20, 36, 600, 1, NULL);
  bb_font_draw_wrapped(PTR(0x641028), frame->second, 20, 358, 600, 1, NULL);
  present();
  if (!bb_session_wait_entry(500))
    bb_sound_play_entry(BB_U32(0x5c9518u + (uint32_t)(bb_random() % 13) * 4u));
  if (!bb_session_wait_entry(8000)) {
    int32_t choice = bb_random() % 2;
    if (choice == 0 || choice == 1)
      bb_sound_play_entry(BB_U32(choice ? 0x6a9918 : 0x5e4774));
    bb_session_wait_entry(100000);
  }
  bb_sound_play_entry(BB_U32(0x5e46f0u + (uint32_t)(bb_random() % 6) * 4u));
  API(0x6ca388u, 4000);
  flush();
  reset();
  quit();
}
void bb_session_outro_c(struct text_frame *frame) {
  reset();
  bb_mission_theme_entry(PTR(0x6a9950), 5);
  bb_archive_open(PTR(0x5d1028));
  load_text(frame, 0x488758, frame->first);
  bb_archive_close(PTR(0x5d1028));
  bb_sprite_draw(PTR(BB_U32(0x4842d0)), 0, 0, 255, 1);
  bb_font_draw_wrapped(PTR(0x6ab590), frame->first, 20, 30, 600, 0, NULL);
  bb_session_music_start_entry(2);
  present();
  bb_session_wait_entry(500);
  bb_sound_play_entry(BB_U32(0x5c9520));
  bb_session_wait_entry(500);
  bb_session_wait_entry(180000);
  API(0x6ca388u, 500);
  flush();
  reset();
}
#ifdef BB_MODEL_RUNTIME
void bb_session_info_entry(void) { BB_GAME_CALL(0x42e110u, 0); }
void bb_session_outro_entry(void) { BB_GAME_CALL(0x42e620u, 0); }
#else
void __attribute__((naked)) bb_session_info_entry(void) {
  __asm__ volatile("subl $0x2104,%esp;pushl %esp;call " BB_ASM(
      bb_session_info_c) ";addl $4,%esp;addl $0x2104,%esp;ret");
}
void __attribute__((naked)) bb_session_outro_entry(void) {
  __asm__ volatile("subl $0x1104,%esp;pushl %esp;call " BB_ASM(
      bb_session_outro_c) ";addl $4,%esp;addl $0x1104,%esp;ret");
}
#endif
struct __attribute__((packed)) disc_frame {
  uint32_t sectors, bytes, free, total;
  char path[1024], volume[1024], drives[1024];
};
uint32_t bb_session_disc_present_c(struct disc_frame *frame) {
  uint32_t found = 0;
  API(0x6ca398u, 1024, REF(frame->drives));
  for (char *root = frame->drives; *root; root += text_size((uintptr_t)root)) {
    if (API(0x6ca394u, REF(root)) != 5 ||
        !API(0x6ca390u, REF(root), REF(&frame->sectors), REF(&frame->bytes),
           REF(&frame->free), REF(&frame->total)) ||
        (frame->sectors * frame->bytes * frame->free & 0xfff00000u))
      continue;
    API(0x6ca38cu, REF(root), REF(frame->volume), 1024, 0, 0, 0, 0, 0);
    for (uint32_t i = 0;
         (int32_t)i < (int32_t)(text_size((uintptr_t)frame->volume) - 1); ++i)
      if (frame->volume[i] >= 'A' && frame->volume[i] <= 'Z')
        frame->volume[i] += 32;
    text_copy((uintptr_t)frame->path, (uintptr_t)root);
    if (BB_GAME_CALL(0x46c260u, 0, REF(frame->path), '\\'))
      BYTE(BB_GAME_CALL(0x46c260u, 0, REF(frame->path), '\\')) = 0;
    if (frame->path[0] >= 'a' && frame->path[0] <= 'z')
      frame->path[0] -= 32;
    if (text_equal((uintptr_t)frame->volume, 0x488764))
      found = 1;
  }
  return found;
}
#ifdef BB_MODEL_RUNTIME
uint32_t bb_session_disc_present_entry(void) { return BB_GAME_CALL(0x42e890u, 0); }
void bb_session_disc_check_entry(void) { (void)BB_GAME_CALL(0x42e890u, 0); }
#else
uint32_t __attribute__((naked)) bb_session_disc_present_entry(void) {
  __asm__ volatile("subl $0xc10,%esp;pushl %esp;call " BB_ASM(
      bb_session_disc_present_c) ";addl $4,%esp;addl $0xc10,%esp;ret");
}
void __attribute__((naked)) bb_session_disc_check_entry(void) {
  /* bb-patch NoCD removes only the jump to the prompt, retaining this scan. */
  /* Its failed-output frame starts exactly one CALL below this entry. */
  __asm__ volatile("call " BB_ASM(bb_session_disc_present_entry) ";ret");
}
#endif
void bb_session_disc_prompt_entry(void) {
  reset();
  bb_palette_install(PTR(0x4eebc8), PTR(0x486dd8));
  uint32_t text = BB_U32(0x603920);
  bb_font_draw(PTR(0x63f0d0), PTR(text ? text : 0x48481c), BB_I32(0x484444) / 2,
               200, 1);
  text = BB_U32(0x603924);
  bb_font_draw(PTR(0x5cd7d0), PTR(text ? text : 0x48481c), BB_I32(0x484444) / 2,
               260, 1);
  present();
  uint16_t key = 0;
  uint32_t found;
  do {
    found = bb_session_disc_present_entry();
    if (!found)
      key = bb_session_wait_entry(5000);
    if (key == 27)
      quit();
  } while (!found);
  bb_sample_play_entry(BB_U32(0x4842dc));
  flush();
  reset();
  bb_palette_install(PTR(0x4eebc8), PTR(0x486dd8));
  bb_display_invalidate_entry();
}

void bb_session_settings_c(uint32_t scratch) {
  static const uintptr_t cleared[] = {0x5db060, 0x6a8f40, 0x553e88, 0x6afe20,
                                      0x559a58};
  static const struct {
    uintptr_t name, destination;
  } paths[] = {{0x484b0c, 0x5db060}, {0x484b04, 0x6a8f40}, {0x484afc, 0x553e88},
               {0x484af4, 0x6afe20}, {0x484ae8, 0x602338}, {0x484adc, 0x600040},
               {0x484ad4, 0x559a58}, {0x484ad0, 0x6afd08}};
  for (unsigned i = 0; i < sizeof(cleared) / sizeof(*cleared); ++i)
    text_copy(cleared[i], 0x48481c);
  text_copy(0x6afd08, 0x484b2c);
  uint32_t key = scratch, name_size = scratch + 4, data_size = scratch + 8;
  uint32_t type = scratch + 12, name = scratch + 16, data = scratch + 272;
  BB_U32(name_size) = BB_U32(data_size) = 256;
  if (API(0x6ca330u, 0x80000001u, 0x484134u, 0, 0xf003f, key))
    return;
  uint32_t enumerate = BB_U32(0x6ca334u);
  for (uint32_t index = 0;
       !BB_GAME_CALL(enumerate, 0, BB_U32(key), index, name, name_size, 0, type, data, data_size);
       ++index) {
    if (text_equal((uintptr_t)name, 0x484b20))
      BB_U32(0x4843d8) = BB_U32((uintptr_t)data) == 1;
    else if (text_equal((uintptr_t)name, 0x484b14))
      BB_U32(0x484264) = BB_U32((uintptr_t)data);
    else {
      unsigned i = 0;
      for (; i < sizeof(paths) / sizeof(*paths); ++i)
        if (text_equal((uintptr_t)name, paths[i].name)) {
          BB_GAME_CALL(0x46c470u, 0, paths[i].destination, data, BB_U32(data_size));
          BYTE(paths[i].destination + BB_U32(data_size)) = 0;
          break;
        }
      if (i == sizeof(paths) / sizeof(*paths)) {
        if (text_equal((uintptr_t)name, 0x484ac0))
          BB_U32(0x484444) = BB_U32((uintptr_t)data);
        else if (text_equal((uintptr_t)name, 0x484ab0))
          BB_U32(0x484448) = BB_U32((uintptr_t)data);
      }
    }
    BB_U32(name_size) = BB_U32(data_size) = 256;
  }
  API(0x6ca32cu, BB_U32(key));
}

void bb_session_settings_entry(void) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x4064b0u, 0);
#else
  uint32_t scratch[BB_SESSION_SETTINGS_BYTES / 4];
  bb_session_settings_c(REF(scratch));
#endif
}
