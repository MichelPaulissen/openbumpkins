/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "bootstrap_resources.h"
#include "mission_io.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_file.h"
#define bb_entity_data_construct(p) BB_GAME_CALL(0x41c290, bb_address_encode(p))
#define bb_display_invalidate_entry() BB_GAME_CALL(0x403190, 0)
#define bb_message_append_entry(p,a,b) BB_GAME_CALL(0x41e310, bb_address_encode(p), a, b)
#include "crt_output.h"
#endif
#include "letters.h"
#include "script.h"
#include <string.h>

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define TEXT(a) ((const char *)(uintptr_t)(a))
typedef uint32_t (*io_fn)(void *, uint32_t, uint32_t, void *);
typedef uint32_t(BB_THISCALL * member_read_fn)(void *, void *,
                                                             uint32_t);
typedef void(BB_THISCALL * self_fn)(void *);
typedef void(BB_THISCALL * self_file_fn)(void *, void *);
typedef int (*format_fn)(char *, const char *, ...);
#ifdef BB_MODEL_RUNTIME
#define FORMAT(out,fmt,...) bb_crt_output_words(out,256,fmt,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4)
#else
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#endif
static int32_t version(void) { return (int32_t)BB_U32(0x004843d4); }
static void self_call(uintptr_t entry, uintptr_t object) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(entry, object);
#else
  ((self_fn)BB_CODE(entry))((void *)object);
#endif
}
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
static void *allocate(uint32_t n) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_allocate(n);
#else
  return ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x0046c060))(n);
#endif
}
static void fatal(void) { bb_fatal_teardown_entry(1); }
static void seed_random(void) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x46be00, 0, 1);
#else
  ((void (*)(uint32_t))BB_CODE((uintptr_t)0x0046be00))(1);
#endif
}
static void *open_file(const char *name, uintptr_t mode) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_open(name, mode);
#else
  return ((void *(*)(const char *, const char *))BB_CODE((uintptr_t)0x0046bdd0))(
      name, TEXT(mode));
#endif
}
static void close_file(void *file) {
#ifdef BB_MODEL_RUNTIME
  bb_game_close(file);
#else
  ((void (*)(void *))BB_CODE((uintptr_t)0x0046bbd0))(file);
#endif
}
static uint32_t write_record(void *file, const void *data, uint32_t size) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_write(data, size, 1, file);
#else
  return ((io_fn)BB_CODE((uintptr_t)0x0046cd20))((void *)data, size, 1, file);
#endif
}
static void read_record(void *file, void *data, uint32_t size) {
  bb_mission_read_entry(data, size, 1, file);
}
static void clear_selection(uintptr_t faction) {
  for (unsigned i = 0; i < 64; ++i) {
    BYTE(faction + 0x2bf + i) = 0;
    BB_U32(faction + 0x300 + i * 4) = 0;
  }
}
static const uintptr_t saved_fields[] = {
    0x004842c4, 0x006024b8, 0x00602484, 0x005fe288, 0x005fe290,
    0x006a8f28, 0x006a8f24, 0x004846ec, 0x004846f0, 0x004846f8};

/* 00415d30: serialized entity links use an unsigned offset and record index. */
static void relocate(uint32_t *pointer, uint32_t old_base) {
  if (BB_U32(pointer)) {
    uint32_t remainder;
    bb_entity_relocate(pointer, old_base, &remainder);
  }
}

/* 00415d70: recount/buildable state precedes clearing each player's selection.
 */
void bb_mission_refresh_entry(void) {
  BB_U32(0x00555000) = 1;
  static const unsigned offsets[] = {0x2c0, 0x2c1, 0x2c6, 0x2c8, 0x2c7, 0x2c5,
                                     0x2c4, 0x2c2, 0x2c3, 0x2d3, 0x2d2};
  for (uintptr_t p = 0x00554fbc; p < 0x005590bc; p += 0xd00) {
    self_call(0x00429120, p);
    self_call(0x0041ced0, p);
    clear_selection(p);
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
      BYTE(p + offsets[i]) = 8;
  }
}

/* 00415e30/00415e80: retain the archive's byte result versus CRT element
 * result, wrapped 32-bit multiplication and the live archived/file routing
 * flag. */
uint32_t bb_mission_read_entry(void *data, uint32_t size, uint32_t count,
                               void *file) {
#ifdef BB_MODEL_RUNTIME
  return bb_game_read(data, size, count, file, !!BB_U32(0x4842a4));
#else
  if (BB_U32(0x004842a4))
    return ((member_read_fn)BB_CODE((uintptr_t)0x00466e10))(
        (void *)(uintptr_t)BB_U32(0x004842cc), data, size * count);
  return ((io_fn)BB_CODE((uintptr_t)0x0046cf40))(data, size, count, file);
#endif
}
uint32_t bb_mission_decode_entry(void *data, uint32_t size, uint32_t count,
                                 void *file) {
  return bb_save_decode(data, size, count, file, !!BB_U32(0x004842a4));
}

/* 0042d950/0042d9a0: two-byte panel header, compressed body and raw tail. */
void BB_THISCALL bb_panel_write_entry(void *object, void *file) {
  uintptr_t p = (uintptr_t)object;
  write_record(file, object, 2);
  bb_save_encode((void *)(p + 2), 0xb54, 1, file);
  write_record(file, (void *)(p + 0xb56), 12);
}
void BB_THISCALL bb_panel_read_entry(void *object, void *file) {
  uintptr_t p = (uintptr_t)object;
  bb_panel_initialize_entry(object);
  read_record(file, object, 2);
  if (version() < 119)
    read_record(file, (void *)(p + 2), 0xb54);
  else
    bb_mission_decode_entry((void *)(p + 2), 0xb54, 1, file);
  read_record(file, (void *)(p + 0xb56), 12);
  bb_panel_localize_entry(object);
}

/* 004159c0: complete MAPV1.25 save. The title's unused tail remains unwritten;
 * records retain their original pointers/padding for the reader to repair. */
uint32_t bb_mission_save_entry(const char *name, const char *description) {
  uint8_t width = 192, height = 192, present = 1, absent = 0;
  uint32_t head = BB_U32(0x00554f98), old_base = BB_U32(0x00553f90),
           total = 5000;
  char title[64];
  void *file = open_file(name, 0x00486ed4);
  if (!file)
    return 0;
  write_record(file, (void *)0x00486ec8, 8);
  copy_text(title, description);
  write_record(file, title, 64);
  write_record(file, &width, 1);
  write_record(file, &height, 1);
  for (uintptr_t p = 0x00559b60; p < 0x005c5b60; p += 0x900)
    bb_save_encode((void *)p, 192, 12, file);
  write_record(file, &head, 4);
  write_record(file, &old_base, 4);
  write_record(file, &total, 4);
  for (uint32_t offset = 0; offset < 5000u * 314u; offset += 314) {
    uintptr_t p = BB_U32(0x00553f90) + offset;
    if (BB_U32(p)) {
      write_record(file, &present, 1);
      bb_save_encode((void *)p, 314, 1, file);
      if (BB_U32(p + 0xec))
        write_record(file, (void *)(uintptr_t)BB_U32(p + 0xec), BYTE(p + 0xf0));
      if (BYTE(p + 0xd4) == 0x62)
        write_record(file, (void *)(uintptr_t)BB_U32(p + 0x3c), 32);
      if (BB_U32(p + 0x38))
        write_record(file, (void *)(uintptr_t)BB_U32(p + 0x38), 2);
    } else
      write_record(file, &absent, 1);
  }
  write_record(file, (void *)0x00554f94, 4);
  for (int32_t i = 0; i < (int32_t)BB_U32(0x00554f94); ++i)
    write_record(file, (void *)(0x00553f94 + (uintptr_t)i * 4), 4);
  for (uintptr_t p = 0x00554fbc; p < 0x005590bc; p += 0xd00)
    write_record(file, (void *)p, 0xd00);
  bb_letters_write_entry((void *)0x005db050, file);
  for (unsigned i = 0; i < sizeof(saved_fields) / sizeof(saved_fields[0]); ++i)
    write_record(file, (void *)saved_fields[i], i < 8 ? 4 : 8);
  bb_script_write_entry((void *)0x005e47b0, file);
  bb_panel_write_entry((void *)0x006025d0, file);
  absent = 64;
  uint32_t result = write_record(file, &absent, 1);
  close_file(file);
  uintptr_t text = BB_U32(result ? 0x0060374c : 0x0060391c);
  FORMAT((char *)0x006033d8, TEXT(text ? text : 0x0048481c));
  bb_message_append_entry((void *)0x005ecf38, 0x006033d8, 0);
  seed_random();
  return 1;
}

static void close_source(void *file) {
  if (BB_U32(0x004842a4)) {
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x466d59, 0x5c77c8, BB_U32(0x4842cc));
#else
    ((self_file_fn)BB_CODE((uintptr_t)0x00466d59))(
        (void *)0x005c77c8, (void *)(uintptr_t)BB_U32(0x004842cc));
#endif
    self_call(0x00466c1e, 0x005c77c8);
  } else
    close_file(file);
}
static void diagnostic(uintptr_t format, int32_t number) {
  FORMAT((char *)0x006033d8, TEXT(format), number);
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x41de40, 0x662c98, 0x6033d8);
#else
  ((self_file_fn)BB_CODE((uintptr_t)0x0041de40))((void *)0x00662c98, (void *)0x006033d8);
#endif
}

/* 00415ed0: file/archive loading, legacy conversions, entity link relocation,
 * five factions, letters/scripts/panel, closure and final world refresh. */
uint32_t bb_mission_load_entry(const char *name, uint32_t archived) {
  uint8_t width = 192, height = 192, present = 1;
  uint32_t head = 0, old_base = BB_U32(0x00553f90);
  int32_t total = 5000;
  char header[9], title[64];
  BB_U32(0x00484720) = 0;
  BB_U32(0x004842a4) = archived;
  BB_U32(0x004842c8) = 0;
  self_call(0x00433910, 0x00553f90);
  bb_display_invalidate_entry();
  self_call(0x00433b50, 0x00553f90);
  if (strcmp(BB_DATA(name), BB_DATA(0x005116d0)))
    copy_text((char *)0x005116d0, name);
  void *file = 0; /* Opaque FILE input is unused on the archive route. */
  if (BB_U32(0x004842a4)) {
    self_call(0x00466b99, 0x005c77c8);
#ifndef BB_MODEL_RUNTIME
    typedef void *(BB_THISCALL * find_fn)(void *, const char *);
#endif
#ifdef BB_MODEL_RUNTIME
    struct bb_model_buffer filename;
    bb_model_buffer_begin(&filename, name, strlen(BB_DATA(name)) + 1);
    BB_U32(0x4842cc) = BB_GAME_CALL(0x466c66, 0x5c77c8, filename.reference);
    bb_model_buffer_end(&filename, 0);
#else
    BB_U32(0x004842cc) =
        (uintptr_t)((find_fn)BB_CODE((uintptr_t)0x00466c66))((void *)0x005c77c8, name);
#endif
    if (!BB_U32(0x004842cc))
      return 0;
  } else {
    file = open_file(name, 0x0048416c);
    if (!file)
      return 0;
  }
  read_record(file, header, 8);
  header[8] = header[5] = 0;
#ifndef BB_MODEL_RUNTIME
  typedef int32_t (*atoi_fn)(const char *);
#endif
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer text;
  bb_model_buffer_begin(&text, header, sizeof header);
  uint32_t major = BB_GAME_CALL(0x46d230, 0, text.reference + 4);
  BB_U32(0x4843d4) = major * 100u + BB_GAME_CALL(0x46d230, 0, text.reference + 6);
  bb_model_buffer_end(&text, 0);
#else
  atoi_fn number = (atoi_fn)BB_CODE((uintptr_t)0x0046d230);
  uint32_t major = number(header + 4);
  BB_U32(0x004843d4) = major * 100u + (uint32_t)number(header + 6);
#endif
  if (version() < 103) {
    diagnostic(0x00486f18, version());
    diagnostic(0x00486ef4, 0);
    diagnostic(0x00486ed8, 0);
    close_source(file);
    return 0;
  }
  read_record(file, title, 64);
  read_record(file, &width, 1);
  read_record(file, &height, 1);
  for (uint32_t row = 0; row < height; ++row) {
    uintptr_t p = 0x00559b60 + row * 0x900u;
    if (version() < 119) {
      for (uint32_t col = 0; col < width; ++col) {
        read_record(file, (void *)(p + col * 12), 12);
        BYTE(p + col * 12 + 3) = 0;
        BB_U32(p + col * 12 + 8) &= ~0x100u;
      }
    } else {
      bb_mission_decode_entry((void *)p, width, 12, file);
      for (uint32_t col = 0; col < width; ++col) {
        BYTE(p + col * 12 + 3) = 0;
        BB_U32(p + col * 12 + 8) &= ~0x100u;
      }
    }
  }
  read_record(file, &head, 4);
  read_record(file, &old_base, 4);
  read_record(file, &total, 4);
  for (int32_t i = 0; i < total; ++i) {
    uintptr_t p = BB_U32(0x00553f90) + (uint32_t)i * 314u;
    read_record(file, &present, 1);
    if (!present)
      continue;
    if (version() < 119)
      read_record(file, (void *)p, 314);
    else
      bb_mission_decode_entry((void *)p, 314, 1, file);
    if (BB_U32(p + 0xec)) {
      BB_U32(p + 0xec) = (uintptr_t)allocate(BYTE(p + 0xf0));
      if (!BB_U32(p + 0xec))
        fatal();
      read_record(file, (void *)(uintptr_t)BB_U32(p + 0xec), BYTE(p + 0xf0));
    }
    if (BYTE(p + 0xd4) == 0x62) {
      BB_U32(p + 0x3c) = (uintptr_t)allocate(32);
      if (!BB_U32(p + 0x3c))
        fatal();
      read_record(file, (void *)(uintptr_t)BB_U32(p + 0x3c), 32);
    }
    if (BB_U32(p + 0x38)) {
      void *data = allocate(2);
      BB_U32(p + 0x38) = data ? bb_entity_data_construct(data) : 0;
      if (!BB_U32(p + 0x38))
        fatal();
      read_record(file, (void *)(uintptr_t)BB_U32(p + 0x38), 2);
    }
    BB_U32(p + 0x8e) = (uint32_t)i;
    if (version() < 105) {
      uint8_t v = BYTE(p + 0x12a);
      if (v) {
        v += 2;
        BYTE(p + 0x12c) = v < 5 ? 5 : v;
      } else
        BYTE(p + 0x12c) = 0;
    }
    if (version() < 109 && (BYTE(p + 0xd4) == 0x53 || BYTE(p + 0xd4) == 0x3a)) {
      BYTE(p + 0xea) = BYTE(p + 0xf4);
      BYTE(p + 0xf4) = 0;
    }
    if (version() < 110)
      BYTE(p + 0x12d) = 0;
    if (version() < 111)
      BYTE(p + 0x114) = 0;
    BYTE(p + 0x104) = 0;
    if (version() < 116 && BYTE(p + 0xd4) == 0xab &&
        (BYTE(p + 0xf4) == 0x18 || BYTE(p + 0xf4) == 0x1c))
      BYTE(p + 0xd4) = 0xe2;
    if (version() < 125) {
      uint8_t kind = BYTE(p + 0xd4);
      if (kind == 0x62 || kind == 0xe2 || kind == 0x6f || kind == 0x4e) {
        uintptr_t tile =
            0x00559b60 +
            ((uint32_t)BYTE(p + 0xd9) * 192u + BYTE(p + 0xd8)) * 12u;
        BYTE(tile + 6) = kind == 0x62 || kind == 0xe2;
        if (kind == 0x6f || kind == 0x4e)
          BB_U32(tile + 8) |= 0x200000u;
      } else if (kind == 0x53) {
        BB_U32(p + 0x62) = (uint32_t)BYTE(p + 0xd8) * 65536u + 0x8000u;
        BB_U32(p + 0x66) = (uint32_t)BYTE(p + 0xd9) * 65536u + 0x8000u;
      }
    }
  }
  for (int32_t i = 0; i < total; ++i) {
    uintptr_t p = BB_U32(0x00553f90) + (uint32_t)i * 314u;
    for (unsigned offset = 4; offset <= 0x34; offset += 4)
      relocate((void *)(p + offset), old_base);
    self_call(0x00454370, p);
    BB_U32(p + 0x5c) = 0;
  }
  relocate(&head, old_base);
  BB_U32(0x00554f98) = head;
  read_record(file, (void *)0x00554f94, 4);
  for (int32_t i = 0; i < (int32_t)BB_U32(0x00554f94); ++i)
    read_record(file, (void *)(0x00553f94 + (uintptr_t)i * 4), 4);
  for (uintptr_t p = 0x00554fbc; p < 0x005590bc; p += 0xd00) {
    if (version() == 103) {
      read_record(file, (void *)p, 0x940);
      memset(BB_SPAN(p + 0xa00, 0x200), 0, 0x200);
    } else if (version() < 106) {
      clear_selection(p);
      read_record(file, (void *)p, 0x360);
      read_record(file, (void *)(p + 0x400), 0x900);
    } else {
      read_record(file, (void *)p, 0xd00);
      if (version() < 108) {
        BB_U32(p + 0xc38) = BB_U32(p + 0xc3c) = 0;
        BB_U32(p + 0xc40) = BB_U32(p + 0xc44) = 191;
      }
    }
    if (version() < 112) {
#ifndef BB_MODEL_RUNTIME
      typedef void(BB_THISCALL * enable_fn)(void *, uint32_t,
                                                          uint32_t);
#endif
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(0x429230, p, 16, 1);
      BB_GAME_CALL(0x429230, p, 17, 1);
      BB_GAME_CALL(0x429230, p, 21, 1);
#else
      enable_fn enable = (enable_fn)BB_CODE((uintptr_t)0x00429230);
      enable((void *)p, 16, 1);
      enable((void *)p, 17, 1);
      enable((void *)p, 21, 1);
#endif
    }
    if (version() < 114) {
      BYTE(p + 0x451) = 0;
      BB_U32(p + 0x4d2) = BB_U32(p + 0x4d6) = 0;
      WORD(p + 0x4da) = 0;
    }
    if (version() < 123)
      WORD(p + 0x66) = WORD(0x005e45a8);
    BB_U32(p) = 0;
    self_call(0x00429120, p);
  }
  bb_letters_read_entry((void *)0x005db050, file);
  for (unsigned i = 0; i < sizeof(saved_fields) / sizeof(saved_fields[0]); ++i)
    read_record(file, (void *)saved_fields[i], i < 8 ? 4 : 8);
  bb_script_read_entry((void *)0x005e47b0, file);
  if (version() <= 112)
    bb_panel_initialize_entry((void *)0x006025d0);
  else
    bb_panel_read_entry((void *)0x006025d0, file);
  close_source(file);
  if (version() < 115)
    for (uintptr_t p = 0x00559b65; p < 0x005c5b60; p += 12)
      BYTE(p) = 0;
  BB_U32(0x00554f9c) = 0;
  BB_U32(0x00554f94) = 0;
  BB_U32(0x00554fa0) = (uint32_t)total;
  seed_random();
  bb_display_invalidate_entry();
  bb_mission_refresh_entry();
  BB_U32(0x004842a4) = 0;
  return 1;
}
