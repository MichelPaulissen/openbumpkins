/* SPDX-License-Identifier: GPL-3.0-only */
#include "capture_video.h"
#include "abi.h"
#include "bootstrap_resources.h"
#include "dialog.h"
#include "interface_platform.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#include "movie_runtime.h"
#include "session_controls.h"
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
typedef void *(*open_fn)(const char *, const char *);
typedef uint32_t (*close_fn)(void *);
typedef uint32_t (*write_fn)(const void *, uint32_t, uint32_t, void *);
typedef int (*format_fn)(char *, const char *, ...);
typedef char *(*find_fn)(const char *, const char *);
typedef int32_t(BB_STDCALL *palette_fn)(void *, uint32_t, uint32_t, uint32_t,
                                        void *);
typedef int32_t(BB_STDCALL *release_fn)(void *);
typedef int32_t(BB_STDCALL *mode_fn)(void *, uint32_t, uint32_t, uint32_t);
typedef int32_t(BB_STDCALL *surface_fn)(void *, void *, void *, uint32_t);
typedef int32_t(BB_STDCALL *install_fn)(void *, void *);
typedef void(BB_STDCALL *debug_fn)(const char *);
uint32_t bb_cd_stop(void *);
uint32_t bb_cd_resume(void *);

static void *method(void *object, uint32_t offset) {
  return BB_POINTER((uintptr_t)BB_POINTER((uintptr_t)object) + offset);
}
static uint32_t text_size(uintptr_t p) {
  uint32_t n = 1;
  while (BYTE(p + n - 1))
    ++n;
  return n;
}
/* Original REP copies are forward DWORDs followed by the remaining bytes. */
static void copy_text(uintptr_t out, uintptr_t in) {
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
static void append_text(uintptr_t out, uintptr_t in) {
  copy_text(out + text_size(out) - 1, in);
}
static void *open_file(const char *name, uintptr_t mode) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_open(name, mode);
#else
  return ((open_fn)BB_CODE((uintptr_t)0x46bdd0))(name, PTR(mode));
#endif
}
#ifdef BB_MODEL_RUNTIME
static void close_file(void *file) { bb_game_close(file); }
#else
static void close_file(void *file) {
  ((close_fn)BB_CODE((uintptr_t)0x46bbd0))(file);
}
#endif
static void write_file(const void *bytes, uint32_t size, void *file) {
#ifdef BB_MODEL_RUNTIME
  bb_game_write(bytes, size, 1, file);
#else
  ((write_fn)BB_CODE((uintptr_t)0x46cd20))(bytes, size, 1, file);
#endif
}

/* 0x431d00: return the first missing prefixNN.extension (01..99). Failed
 * creation still copies the candidate name; full collisions leave it alone. */
void *bb_numbered_file_entry(const char *prefix, const char *extension,
                             char *output) {
#ifdef BB_MODEL_RUNTIME
  MOVIE_BUFFER(char, name, 256);
  char digits[3];
#else
  char name[256], digits[3];
#endif
  uint32_t number = 1;
  do {
    copy_text((uintptr_t)name, (uintptr_t)prefix);
    digits[2] = 0;
    digits[0] = number / 10 + '0';
    digits[1] = number % 10 + '0';
    append_text((uintptr_t)name, (uintptr_t)digits);
    append_text((uintptr_t)name, 0x48891c);
    append_text((uintptr_t)name, (uintptr_t)extension);
    void *existing = open_file(name, 0x488918);
    if (!existing) {
      void *file = open_file(name, 0x486ed4);
      if (output)
        copy_text((uintptr_t)output, (uintptr_t)name);
      return file;
    }
    ++number;
    close_file(existing);
  } while (number <= 99);
  return 0;
}

/* 0x431af0: entry reserves the exact original local frame. Header/filename/
 * palette bytes which the API or failed filename search leave untouched keep
 * their incoming values. The callback sees the same borrowed buffers. */
void bb_screenshot_c(unsigned char *frame, void *object, const char *prefix) {
  uint32_t f = bb_address_encode(frame), p = bb_address_encode(object);
  WORD(f + 0xa) = 0;
  uint32_t width = BB_U32(0x484444);
  WORD(f + 0xc) = 0;
  BYTE(f + 3) = 0;
  BB_U32(f + 0xe) = 0x436;
  BB_U32(f + 0x14) = 40;
  BB_U32(f + 0x18) = BB_U32(0x484444);
  BB_U32(f + 0x1c) = BB_U32(0x484448);
  uint32_t pixels = width * BB_U32(0x484448);
  WORD(f + 4) = 0x4d42;
  BB_U32(f + 0x24) = 0;
  BB_U32(f + 6) = pixels + 0x436;
  BB_U32(f + 0x34) = 0;
  BB_U32(f + 0x28) = pixels;
  BB_U32(f + 0x2c) = 0x800;
  BB_U32(f + 0x30) = 0x800;
  BB_U32(f + 0x38) = 0;
  void *palette = BB_POINTER(p + 0xb8);
  WORD(f + 0x20) = 1;
  WORD(f + 0x22) = 8;
#ifdef BB_MODEL_RUNTIME
  MV5(MV_WORD(method(palette, 0x10)), palette, 0, 0, 256, f + 0x13c);
#else
  ((palette_fn)BB_CODE(method(palette, 0x10)))(palette, 0, 0, 256,
                                               PTR(f + 0x13c));
#endif
  for (uint32_t i = 0; i < 256; ++i) {
    uintptr_t entry = f + 0x13c + i * 4;
    uint8_t red = BYTE(entry), green = BYTE(entry + 1);
    BYTE(p + 0xabc + i) = red;
    BYTE(p + 0x9bc + i) = green;
    BYTE(p + 0x8bc + i) = BYTE(entry + 2);
  }
  uint32_t data = bb_display_lock(object);
  if (!data)
    return;
  char *name = PTR(f + 0x3c);
  void *file = bb_numbered_file_entry(prefix, PTR(0x488914), name);
  if (file) {
    write_file(PTR(f + 4), 14, file);
    write_file(PTR(f + 0x14), 40, file);
    for (uint32_t i = 0; i < 256; ++i) {
      write_file(PTR(p + 0x8bc + i), 1, file);
      write_file(PTR(p + 0x9bc + i), 1, file);
      write_file(PTR(p + 0xabc + i), 1, file);
      write_file(PTR(f + 3), 1, file);
    }
    uint32_t row = data + (BB_U32(0x484448) - 1u) * BB_U32(p + 0x5c);
    uint32_t lines = 0;
    if (BB_I32(0x484448) > 0) {
      do {
        ++lines;
        write_file(PTR(row), BB_U32(0x484444), file);
        row -= BB_U32(p + 0x5c);
      } while ((int32_t)lines < BB_I32(0x484448));
    }
    close_file(file);
  }
  bb_display_unlock(object);
#ifdef BB_MODEL_RUNTIME
  MV_FORMAT(PTR(0x6033d8), PTR(0x486e1c), MV_WORD(name));
#else
  ((format_fn)BB_CODE((uintptr_t)0x46bae0))(PTR(0x6033d8), PTR(0x486e1c), name);
#endif
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x41de40, 0x662c98, 0x6033d8);
#else
  bb_feedback_append_entry(PTR(0x662c98), PTR(0x6033d8));
#endif
}

/* 0x432df0: retain the original frame's output slot, panel state, partially
 * initialized video context, surface descriptor and asset pathname. */
void bb_video_c(unsigned char *frame, void *object, const char *filename) {
  uint32_t f = bb_address_encode(frame), p = bb_address_encode(object);
  BB_U32(f + 4) = BB_U32(0x48443c);
  uint32_t saved_mode = BB_U32(0x48426c);
  bb_session_disc_check_entry();
  int32_t number = bb_random() % 30 + 1;
#ifdef BB_MODEL_RUNTIME
  MV_FORMAT(PTR(0x6033d8), PTR(0x484170), 0x6afd08, number);
#else
  ((format_fn)BB_CODE((uintptr_t)0x46bae0))(PTR(0x6033d8), PTR(0x484170),
                                            PTR(0x6afd08), number);
#endif
  void *probe = open_file(PTR(0x6033d8), 0x48416c);
  BB_U32(0x6a9894) = bb_address_encode(probe);
  if (!probe)
    for (;;) {
    }
  close_file(probe);
  char *path = PTR(f + 0xa0);
  copy_text((uintptr_t)path, 0x6afd08);
  append_text((uintptr_t)path, (uintptr_t)filename);
  void *file = open_file(path, 0x48416c);
  if (!file)
    return;
  close_file(file);
  bb_cd_stop(PTR(0x69e370));
  bb_interface_audio_stop_entry(PTR(0x5590e8));
  bb_interface_sound_enable_entry(PTR(0x5590e8));
  bb_panel_mode_entry(5);
  BB_U32(0x48426c) = 3;
  bb_display_reset(object);
  void *primary = BB_POINTER(p + 0x34);
#ifdef BB_MODEL_RUNTIME
  MV1(MV_WORD(method(primary, 8)), primary);
#else
  ((release_fn)BB_CODE(method(primary, 8)))(primary);
#endif
  void *draw = BB_POINTER(p + 0x30);
#ifdef BB_MODEL_RUNTIME
  int32_t result = MV4(MV_WORD(method(draw, 0x54)), draw, BB_U32(0x484444),
                       BB_U32(0x484448), 16);
#else
  int32_t result = ((mode_fn)BB_CODE(method(draw, 0x54)))(
      draw, BB_U32(0x484444), BB_U32(0x484448), 16);
#endif
  int failed_mode = result != 0;
  if (!failed_mode) {
    for (uint32_t i = 0; i < 27; ++i)
      BB_U32(f + 0x34 + i * 4) = 0;
    draw = BB_POINTER(p + 0x30);
    BB_U32(f + 0x34) = 0x6c;
    BB_U32(f + 0x38) = 1;
    BB_U32(f + 0x9c) = 0x200;
#ifdef BB_MODEL_RUNTIME
    result = MV4(MV_WORD(method(draw, 0x18)), draw, f + 0x34, f, 0);
#else
    result = ((surface_fn)BB_CODE(method(draw, 0x18)))(draw, PTR(f + 0x34),
                                                       frame, 0);
#endif
    if (result) {
#ifdef BB_MODEL_RUNTIME
      MV_FORMAT(PTR(0x6033d8), PTR(0x488950), result);
#else
      ((format_fn)BB_CODE((uintptr_t)0x46bae0))(PTR(0x6033d8), PTR(0x488950),
                                                result);
#endif
#ifdef BB_MODEL_RUNTIME
      MV1(BB_U32(0x6ca374), 0x6033d8);
#else
      ((debug_fn)BB_CODE(BB_POINTER(0x6ca374)))(PTR(0x6033d8));
#endif
      bb_fatal_teardown_entry(0);
    }
    BB_U32(f + 8) = 0x372004c;
#ifdef BB_MODEL_RUNTIME
    if (MV2(0x46d280, filename, 0x48894c) ||
        MV2(0x46d280, filename, 0x488944) || MV2(0x46d280, filename, 0x48893c))
#else
    find_fn find = (find_fn)BB_CODE((uintptr_t)0x46d280);
    if (find(filename, PTR(0x48894c)) || find(filename, PTR(0x488944)) ||
        find(filename, PTR(0x48893c)))
#endif
      BB_U32(f + 8) |= 0x10;
    BB_U32(f + 0x14) = BB_U32(0x4eebf8);
    uint32_t surface = BB_U32(f);
    uint32_t sound = BB_U32(0x5590e8);
    BB_U32(f + 0x18) = surface;
    uint32_t secondary = BB_U32(0x5590ec);
    BB_U32(f + 0x20) = sound;
    BB_U32(f + 0x24) = secondary;
    BB_U32(f + 0x30) = 0x42eb80;
    bb_movie_play_entry(path, BB_U32(0x5cd7c4), PTR(f + 8));
    void *movie_surface = BB_POINTER(f);
#ifdef BB_MODEL_RUNTIME
    MV1(MV_WORD(method(movie_surface, 8)), movie_surface);
#else
    ((release_fn)BB_CODE(method(movie_surface, 8)))(movie_surface);
#endif
    draw = BB_POINTER(p + 0x30);
#ifdef BB_MODEL_RUNTIME
    MV4(MV_WORD(method(draw, 0x54)), draw, BB_U32(0x484444), BB_U32(0x484448),
        8);
#else
    ((mode_fn)BB_CODE(method(draw, 0x54)))(draw, BB_U32(0x484444),
                                           BB_U32(0x484448), 8);
#endif
  }
  bb_display_primary(object);
  bb_panel_mode_entry(BB_U32(f + 4));
  BB_U32(0x48426c) = saved_mode;
  bb_display_reset(object);
  primary = BB_POINTER(p + 0x34);
#ifdef BB_MODEL_RUNTIME
  MV2(MV_WORD(method(primary, 0x7c)), primary, BB_U32(p + 0xb8));
#else
  ((install_fn)BB_CODE(method(primary, 0x7c)))(primary, BB_POINTER(p + 0xb8));
#endif
  bb_interface_sound_disable_entry(PTR(0x5590e8));
  bb_interface_audio_start_entry(PTR(0x5590e8));
  if (failed_mode)
    bb_cd_resume(PTR(0x69e370));
}

#ifdef BB_MODEL_RUNTIME
void BB_THISCALL bb_screenshot_entry(void *object, const char *prefix) {
  MOVIE_BUFFER(unsigned char, frame, 0x53c);
  bb_screenshot_c(frame, object, prefix);
}
void BB_THISCALL bb_video_entry(void *object, const char *filename) {
  MOVIE_BUFFER(unsigned char, frame, 0x1a0);
  bb_video_c(frame, object, filename);
}
#else
#define FRAME_ENTRY(name, body, size)                                          \
  void __attribute__((thiscall, naked)) name(                                  \
      void *object __attribute__((unused)),                                    \
      const char *text __attribute__((unused))) {                              \
    __asm__ volatile("movl 4(%esp),%eax;"                                      \
                     "subl $" #size ",%esp;movl %esp,%edx;"                    \
                     "pushl %eax;pushl %ecx;pushl %edx;call " BB_ASM(          \
                         body) ";addl $12,%esp;addl $" #size ",%esp;ret $4");  \
  }
FRAME_ENTRY(bb_screenshot_entry, bb_screenshot_c, 0x53c)
FRAME_ENTRY(bb_video_entry, bb_video_c, 0x1a0)

#endif
