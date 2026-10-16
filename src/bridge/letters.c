/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "letters.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_file.h"
#include "crt_output.h"
#define bb_sample_play_entry(n) BB_GAME_CALL(0x415860, 0, n)
static void portable_message(void *object, uintptr_t text, uint32_t value) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, (const void *)text, strlen(BB_DATA(text)) + 1);
  BB_GAME_CALL(0x41e310, bb_address_encode(object), buffer.reference, value);
  bb_model_buffer_end(&buffer, 0);
}
#define bb_message_append_entry portable_message
#endif
#include "mission_io.h"
#include <signal.h>
#include <string.h>

#define TEXT(a) ((const char *)(uintptr_t)(a))
typedef int (*format_fn)(char *, const char *, ...);
#ifdef BB_MODEL_RUNTIME
#define FORMAT(out,fmt,...) bb_crt_output_words(out,256,fmt,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4)
#else
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#endif
static void copy_text(char *out, const char *text) {
  out = BB_DATA(out);
  text = BB_DATA(text);
  size_t n = strlen(text) + 1;
  while (n >= 4) {
    BB_U32(out) = BB_U32(text);
    out += 4;
    text += 4;
    n -= 4;
  }
  while (n--)
    *out++ = *text++;
}
static void *allocate(uint32_t size) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_allocate(size);
#else
  return ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x0046c060))(size);
#endif
}
static void release(uintptr_t p) {
#ifdef BB_MODEL_RUNTIME
  bb_game_release(p);
#else
  ((void (*)(void *))BB_CODE((uintptr_t)0x0046c110))((void *)p);
#endif
}
static const char *localized(uintptr_t slot) {
  uintptr_t p = BB_U32(slot);
  return TEXT(p ? p : 0x0048481c);
}

/* 00421ea0/00421eb0/00421ec0: clearing preserves the old display pointer
 * and animation word. The next head is captured before each release call. */

/* 00421ef0: archive-backed text, terminated after the captured member size.
 * Existing archive entry bridges retain their opaque member/handle ABI. */
uint32_t BB_THISCALL
bb_letter_archive_text_entry(void *object, const char *name, char *out) {
  (void)object;
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer filename;
  bb_model_buffer_begin(&filename, name, strlen(BB_DATA(name)) + 1);
  BB_GAME_CALL(0x466b99, 0x5d1028);
  uint32_t member = BB_GAME_CALL(0x466c66, 0x5d1028, filename.reference);
  bb_model_buffer_end(&filename, 0);
  if (!member) {
    BB_GAME_CALL(0x466c1e, 0x5d1028);
    return 0;
  }
  uint32_t size = BB_GAME_CALL(0x466ec2, member, 0, 2);
  BB_GAME_CALL(0x466ec2, member, 0, 0);
  struct bb_model_buffer text;
  bb_model_buffer_begin(&text, out, size + 1u);
  BB_GAME_CALL(0x466e10, member, text.reference, size);
  BB_U8(text.reference + size) = 0;
  bb_model_buffer_end(&text, 1);
  BB_GAME_CALL(0x466d59, 0x5d1028, member);
  BB_GAME_CALL(0x466c1e, 0x5d1028);
#else
  typedef void(BB_THISCALL * plain_fn)(void *);
  typedef void *(BB_THISCALL * member_fn)(void *, const char *);
  typedef uint32_t(BB_THISCALL * seek_fn)(void *, uint32_t,
                                                        uint32_t);
  typedef void(BB_THISCALL * read_fn)(void *, void *, uint32_t);
  typedef void(BB_THISCALL * release_fn)(void *, void *);
  void *archive = (void *)0x005d1028;
  ((plain_fn)BB_CODE((uintptr_t)0x00466b99))(archive);
  void *member = ((member_fn)BB_CODE((uintptr_t)0x00466c66))(archive, name);
  if (!member) {
    ((plain_fn)BB_CODE((uintptr_t)0x00466c1e))(archive);
    return 0;
  }
  uint32_t size = ((seek_fn)BB_CODE((uintptr_t)0x00466ec2))(member, 0, 2);
  ((seek_fn)BB_CODE((uintptr_t)0x00466ec2))(member, 0, 0);
  ((read_fn)BB_CODE((uintptr_t)0x00466e10))(member, out, size);
  out[size] = 0;
  ((release_fn)BB_CODE((uintptr_t)0x00466d59))(archive, member);
  ((plain_fn)BB_CODE((uintptr_t)0x00466c1e))(archive);
#endif
  return 1;
}

/* 00421f70: bracketed resource references, otherwise literal text. Keep
 * the original forward copies and its unchecked closing-bracket assumption. */
char *BB_THISCALL
bb_letter_resolve_entry(void *object, const char *text, char *out) {
  text = BB_DATA(text);
  if (strchr(text, '[') && strchr(text, ']')) {
    char name[256];
    copy_text(name, strchr(text, '[') + 1);
    *strchr(name, ']') = 0;
    if (bb_letter_archive_text_entry(object, name, out))
      return out;
  }
  copy_text(out, text);
  return out;
}

/* 00422040: '*' posts a transient message; other text appends a 1032-byte
 * letter. The owner argument is unused by this original presentation queue. */
void BB_THISCALL
bb_letter_post_entry(void *object, uint32_t owner, const char *text) {
  (void)owner;
  uintptr_t self = (uintptr_t)object;
  char resolved[1024];
  bb_letter_resolve_entry(object, text, resolved);
  if (resolved[0] == '*') {
    bb_message_append_entry((void *)0x005ecf38, (uintptr_t)(resolved + 1), 0);
    return;
  }
  char *letter = allocate(0x408);
  if (!letter)
    return;
  copy_text(letter, resolved);
  BB_U32(letter + 0x400) = BB_U32(letter + 0x404) = 0;
  uintptr_t tail = BB_U32(self);
  if (tail) {
    while (BB_U32(tail + 0x404))
      tail = BB_U32(tail + 0x404);
    BB_U32(tail + 0x404) = bb_address_encode(letter);
  } else
    BB_U32(self) = bb_address_encode(letter);
  BB_U32(self + 4) = bb_address_encode(letter);
  FORMAT((char *)0x006033d8, localized(0x006037e0));
  bb_message_append_entry((void *)0x005ecf38, 0x006033d8, 0);
  bb_sample_play_entry(BB_U32(0x00484314));
}

/* 00422290/004222b0: count all links, or find a target's predecessor. */
uint32_t BB_THISCALL bb_letters_count_entry(void *object) {
  uint32_t count = 0;
  for (uintptr_t p = BB_U32(object); p; p = BB_U32(p + 0x404))
    ++count;
  return count;
}
void *BB_THISCALL bb_letter_predecessor_entry(void *object,
                                                            void *letter) {
  for (uintptr_t p = BB_U32(object); p && BB_U32(p + 0x404);
       p = BB_U32(p + 0x404))
    if (BB_U32(p + 0x404) == bb_address_encode(letter))
      return (void *)p;
  return 0;
}
void BB_THISCALL bb_letter_select_entry(void *object,
                                                      void *letter) {
  BB_U32((uintptr_t)letter + 0x400) = 1; /* 004222f0 */
  BB_U32((uintptr_t)object + 4) = bb_address_encode(letter);
  bb_sample_play_entry(BB_U32(0x004842fc));
}
void BB_THISCALL bb_letter_select_index_entry(void *object,
                                                            uint32_t index) {
  uintptr_t p = BB_U32(object); /* 00422320 */
  for (uint32_t i = 0; p; ++i, p = BB_U32(p + 0x404))
    if (i == index) {
      bb_letter_select_entry(object, (void *)p);
      return;
    }
}
void BB_THISCALL bb_letter_delete_entry(void *object) {
  uintptr_t self = (uintptr_t)object, current = BB_U32(self + 4);
  if (!current)
    return; /* 004224e0 */
  uintptr_t next = BB_U32(current + 0x404);
  uintptr_t previous =
      (uintptr_t)bb_letter_predecessor_entry(object, (void *)current);
  BB_U32(previous ? previous + 0x404 : self) = next;
  release(BB_U32(self + 4));
  bb_sample_play_entry(BB_U32(0x00484318));
  BB_U32(self + 4) = next ? next : previous;
}

/* 00422350: advance the close button only while the letter panel is active. */
void BB_THISCALL bb_letters_tick_entry(void *object) {
  uintptr_t self = (uintptr_t)object;
  if (BB_U32(0x0048443c) != 3)
    return;
  uint32_t divisor = BB_U32(BB_U32(0x00663510) + 0x4c) * 4u;
  if (!divisor) {
    raise(SIGFPE);
    __builtin_unreachable();
  }
  BB_U32(self + 12) = (BB_U32(self + 12) + 1u) % divisor;
  if (!BB_U32(self + 4)) {
    BB_U32(0x005d19b8) = 0;
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x42b260, 0, 0);
#else
    ((void (*)(uint32_t))BB_CODE((uintptr_t)0x0042b260))(0);
#endif
  }
}
static int32_t row_index(int32_t y) {
  return (int32_t)(440u - (uint32_t)y) / 22;
}
uint32_t BB_THISCALL
bb_letters_click_entry(void *object, int32_t x, int32_t y) {
  if (BB_U32(0x0048443c) != 3)
    return 0; /* 00422390 */
  if (x >= 34 && x <= 94 && y >= 140 && y <= 480) {
    int32_t row = row_index(y);
    if (row >= 0)
      bb_letter_select_index_entry(object, row);
  }
  if (x >= 520 && y >= 360)
    bb_letter_delete_entry(object);
  return 1;
}
uint32_t BB_THISCALL
bb_letters_noop_entry(void *object, uint32_t a, uint32_t b) {
  (void)object;
  (void)a;
  (void)b;
  return 0; /* 00422410 */
}
uint32_t BB_THISCALL
bb_letters_hover_entry(void *object, int32_t x, int32_t y, char *out) {
  (void)object;
  if (x >= 520 && y >= 360) {
    copy_text(out, localized(0x006037e4)); /* 00422420 */
    return 1;
  }
  if (x >= 34 && x <= 94 && y >= 140 && y <= 480) {
    int32_t row = row_index(y);
    if (row >= 0) {
      FORMAT(out, localized(0x006037ec), (uint32_t)row + 1u);
      return 1;
    }
  }
  return 0;
}

/* 00422140: letter text or the blinking composition buffer, then icons and
 * the close animation. The displayed flag uses the live current pointer. */
void BB_THISCALL bb_letters_draw_entry(void *object) {
  uintptr_t self = (uintptr_t)object;
  bb_sprite_draw((void *)(uintptr_t)BB_U32(0x00602450), 0, 0, 255, 1);
  if (BB_U32(0x004847b4) == 1) {
    char *out = (char *)0x006b0910;
    copy_text(out, TEXT(0x005e5cd0));
    copy_text(out + strlen(BB_DATA(out)),
              TEXT(BB_U32(0x004842c4) % 36u < 18u ? 0x0048700c : 0x00487008));
    bb_font_draw_wrapped((void *)0x0063f0d0, out, 190, 97, 274, 0, 0);
  } else if (BB_U32(self + 4)) {
    bb_font_draw_wrapped((void *)0x0063f0d0, TEXT(BB_U32(self + 4)), 190, 97,
                         274, 0, 0);
    BB_U32(BB_U32(self + 4) + 0x400) = 1;
  }
  int32_t y = 440;
  for (uintptr_t p = BB_U32(self); p; p = BB_U32(p + 0x404)) {
    bb_sprite_draw((void *)(uintptr_t)BB_U32(0x006b083c), 64, y, 255, 1);
    y = (uint32_t)y - 22u;
  }
  bb_sprite_draw((void *)(uintptr_t)BB_U32(0x00663510), 560, 400,
                 (int32_t)BB_U32(self + 12) / 4, 1);
}

/* 00422530/004225b0: exact raw record stream. The original loader overwrites
 * its head on every read, retaining only the last allocation; preserve that.
 */
void BB_THISCALL bb_letters_read_entry(void *object, void *file) {
  bb_letters_clear_entry(object);
  uint32_t count;
  bb_mission_read_entry(&count, 4, 1, file);
  for (uint32_t i = 0; i < count; ++i) {
    void *letter = allocate(0x408);
    bb_mission_read_entry(letter, 0x408, 1, file);
    BB_U32((uintptr_t)letter + 0x404) = 0;
    BB_U32(object) = bb_address_encode(letter);
  }
  BB_U32((uintptr_t)object + 4) = BB_U32(object);
}
void BB_THISCALL bb_letters_write_entry(void *object,
                                                      void *file) {
#ifdef BB_MODEL_RUNTIME
#define write(data,size,count,file) bb_game_write(data,size,count,file)
#else
  typedef void (*write_fn)(const void *, uint32_t, uint32_t, void *);
  write_fn write = (write_fn)BB_CODE((uintptr_t)0x0046cd20);
#endif
  uintptr_t letter = BB_U32(object);
  uint32_t count = bb_letters_count_entry(object);
  write(&count, 4, 1, file);
  while (letter) {
    write((void *)letter, 0x408, 1, file);
    letter = BB_U32(letter + 0x404);
  }
}
