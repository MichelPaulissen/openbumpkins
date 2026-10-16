/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "script.h"
#include "worker_runtime.h"
#include "world_controls.h"
#include <string.h>

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)

#ifdef BB_MODEL_RUNTIME
#include "crt_primitives.h"
#include "ui_calls.h"
static uint32_t integer(const char *text, int wide) {
  return wide ? bb_crt_string_long_entry(text) : bb_crt_string_int_entry(text);
}
static int equal_folded(const char *text, uintptr_t word) {
  return !bb_crt_string_case_compare_entry(text, (const char *)word);
}
static unsigned char *allocate(uint32_t size) {
  void *p = bb_game_allocate(size);
  return p ? BB_DATA(p) : NULL;
}
static void interface_mode(uint32_t mode) { C1(0x42b260, mode); }
static void mission(uintptr_t function, uint32_t number) {
  C1(function, number);
}
#else
/* These CRT/parser and mission/UI boundaries still belong to the temporary
 * binary bridge. Keep their current parsing, locale and allocator semantics. */
static uint32_t integer(const char *text, int wide) {
  return ((uint32_t (*)(const char *))BB_CODE(
      (uintptr_t)(wide ? 0x0046d110 : 0x0046d230)))(text);
}
static int equal_folded(const char *text, uintptr_t word) {
  typedef int (*compare_fn)(const char *, const char *);
  return !((compare_fn)BB_CODE((uintptr_t)0x0046c1c0))(text,
                                                       (const char *)word);
}
static unsigned char *allocate(uint32_t size) {
  return ((void *(*)(uint32_t))BB_CODE((uintptr_t)0x0046c060))(size);
}
static void interface_mode(uint32_t mode) {
  ((void (*)(uint32_t))BB_CODE((uintptr_t)0x0042b260))(mode);
}
static void mission(uintptr_t function, uint32_t number) {
  ((void (*)(uint32_t))BB_CODE(function))(number);
}
#endif
static uintptr_t current_block(uintptr_t self) { return BB_U32(self + 0x1208); }
static uintptr_t current_condition(uintptr_t self) {
  return BB_U32(current_block(self) + 16);
}
static void reset(void *self) { bb_script_help_entry(self, 0, 0); }
static void next(void *self) { bb_script_help_next_entry(self); }

static void text_copy(unsigned char *out, const char *text) {
  size_t n = strlen(text) + 1;
  while (n >= 4) {
    BB_U32(out) = BB_U32(text);
    out += 4;
    text += 4;
    n -= 4;
  }
  while (n--)
    *out++ = (unsigned char)*text++;
}
static unsigned char *payload(unsigned type, unsigned size) {
  unsigned char *p = allocate(size);
  WORD(p) = type;
  WORD(p + 2) = size;
  p[4] = 0;
  return p;
}

struct command_word {
  uintptr_t text;
  unsigned char command, kind;
};
static const struct command_word messages[] = {
    {0x004884e4, 201, 0}, {0x004884dc, 202, 0}, {0x004884d0, 203, 0},
    {0x004884c8, 204, 0}, {0x004884c0, 205, 0}, {0x004884b0, 206, 0},
    {0x004884a0, 207, 0}, {0x00488494, 208, 0}, {0x00488488, 209, 0}};
static const struct command_word conditions[] = {
    {0x00488480, 101, 1},  {0x00488474, 102, 2},  {0x00488468, 111, 11},
    {0x0048845c, 103, 3},  {0x00488450, 105, 4},  {0x00488444, 104, 5},
    {0x00488438, 106, 6},  {0x0048842c, 109, 9},  {0x00488420, 110, 10},
    {0x00488410, 112, 12}, {0x00488400, 113, 13}, {0x004883f8, 107, 7},
    {0x004883f0, 108, 8},  {0x004883e4, 114, 14}, {0x004884e4, 100, 0}};

/* 00424c80..0042671f: the complete script-editor input dispatcher. Record
 * fields retain their packed offsets, and reads after callbacks remain live. */
void BB_THISCALL bb_script_input_entry(void *object, const char *text) {
  unsigned char *self = BB_DATA(object);
  text = BB_DATA(text);
  uint32_t address = bb_address_encode(object);
  BB_U32(self + 0x1208) = bb_script_selected_block(object);
  bb_sample_play_entry(BB_U32(0x004842d8));
  unsigned command = self[0], stage = self[1];
  uint32_t value;
  unsigned char *p;
  const char *comma;
  switch (command) {
  case 0: {
    static const struct command_word words[] = {{0x00488544, 255, 0},
                                                {0x00488538, 9, 0},
                                                {0x0048852c, 10, 0},
                                                {0x00488524, 11, 0}};
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
      if (!strcmp(text, BB_DATA(words[i].text))) {
        if (words[i].command == 255)
          bb_script_clear_entry(object);
        else
          bb_script_help_entry(object, words[i].command, 0);
        return;
      }
    unsigned chosen;
    switch ((unsigned char)*text) {
    case 'P':
      chosen = 1;
      break;
    case 'B':
      chosen = 2;
      break;
    case 'E':
      chosen = 5;
      break;
    case 'M':
      chosen = 4;
      break;
    case 'F':
      chosen = 3;
      break;
    case 'R':
      bb_script_rewind_entry(object);
      return;
    case 'S':
      chosen = 6;
      break;
    case 'A':
      chosen = 7;
      break;
    case 'G':
      chosen = 8;
      break;
    default:
      return;
    }
    bb_script_help_entry(object, chosen, 0);
    return;
  }
  case 1:
    value = integer(text, 0);
    bb_script_player_select_entry(object, value);
    reset(object);
    return;
  case 2:
    value = integer(text, 0);
    bb_script_block_select_entry(object, self[0x82], value);
    reset(object);
    return;
  case 3:
    value = integer(text, 1);
#ifdef BB_MODEL_RUNTIME
    C1(0x41ad70, BB_U32(0x553f90) + value * 314u);
#else
    ((void (*)(void *))BB_CODE(0x41ad70))(
        (void *)(uintptr_t)(BB_U32(0x553f90) + value * 314u));
#endif
    reset(object);
    return;
  case 4:
    if (equal_folded(text, 0x00487ef0)) {
      bb_script_help_entry(object, 200, 0);
      BB_U32(0x004847b4) = 1;
      interface_mode(3);
      BB_U32(0x004847b8) = 1;
    } else {
      int win = equal_folded(text, 0x004884f4);
      if (win || equal_folded(text, 0x004884ec)) {
        p = payload(20, 6);
        p[5] = win ? 1 : 2;
        bb_script_event_new_entry(object, p);
        reset(object);
      } else
        for (unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i)
          if (equal_folded(text, messages[i].text)) {
            bb_script_help_entry(object, messages[i].command, 0);
            break;
          }
    }
    if (!*text)
      reset(object);
    return;
  case 5:
    for (unsigned i = 0; i < sizeof(conditions) / sizeof(conditions[0]); ++i)
      if (equal_folded(text, conditions[i].text)) {
        if (conditions[i].kind)
          bb_script_condition_new_entry(object, conditions[i].kind);
        bb_script_help_entry(object, conditions[i].command, 0);
        break;
      }
    if (!*text)
      reset(object);
    return;
  case 6:
    if (stage < 2) {
      value = integer(text, 1);
      BB_U32(0x006b1910 + stage * 4) = value;
      next(object);
    } else if (stage == 2) {
      if (BB_U32(0x006b1910)) {
        value = integer(text, 1);
        bb_entity_stat(
            (void *)(uintptr_t)(BB_U32(0x00553f90) + BB_U32(0x006b1910) * 314u),
            BB_U32(0x006b1914), value);
      } else {
        uintptr_t base = BB_U32(0x00553f90);
        for (unsigned i = 0; i < BYTE(0x0055540d + BYTE(0x004843f0) * 0xd00);
             ++i) {
          value = integer(text, 1);
          uint32_t index =
              BB_U32(0x0055540e + BYTE(0x004843f0) * 0xd00 + i * 4);
          bb_entity_stat((void *)(uintptr_t)(base + index * 314u),
                         BB_U32(0x006b1914), value);
        }
      }
      reset(object);
    }
    return;
  case 7: {
    static const struct command_word words[] = {
        {0x0048851c, 53, 0}, {0x0048850c, 54, 0}, {0x004884f8, 55, 0}};
    unsigned chosen = 0;
    for (unsigned i = 0; i < 3; ++i)
      if (!strcmp(text, BB_DATA(words[i].text))) {
        chosen = words[i].command;
        break;
      }
    if (!chosen)
      switch ((unsigned char)*text) {
      case 'T':
        chosen = 50;
        break;
      case 'D':
        chosen = 51;
        break;
      case 'S':
        chosen = 52;
        break;
      case 'W':
        chosen = 56;
        break;
      case 'B':
        chosen = 57;
        break;
      }
    if (chosen)
      bb_script_help_entry(object, chosen, 0);
    if (!*text)
      reset(object);
    return;
  }
  case 8:
    value = integer(text, 0);
    BB_U32(0x00555006 + self[0x82] * 0xd00) = value;
    reset(object);
    return;
  case 9:
    mission(0x00414a20, integer(text, 0));
    reset(object);
    return;
  case 10:
    if (*text)
      mission(0x00414b10, integer(text, 0));
    reset(object);
    return;
  case 11: {
    value = integer(text, 1);
    bb_entity_remove_kind_entry((void *)(uintptr_t)0x00553f90, value);
    reset(object);
    return;
  }
  case 50:
  case 51:
  case 57:
    value = integer(text, 0);
    self[command == 50 ? 0x1510 : command == 51 ? 0x1511 : 0x151c] = value;
    reset(object);
    return;
  case 52:
  case 56:
    if (!stage) {
      BB_U32(0x006b1910) = integer(text, 1);
      next(object);
    } else if (stage == 1) {
      if (BB_U32(0x006b1910) < 32) {
        value = integer(text, 1);
        if (command == 52)
          BYTE(0x0055599c + BB_U32(0x006b1910) + BYTE(0x004843f0) * 0xd00) =
              value;
        else {
          uintptr_t flags = 0x00555c4c + BYTE(0x004843f0) * 0xd00;
          uint32_t mask = 1u << (BB_U32(0x006b1910) & 31);
          BB_U32(flags) = value ? BB_U32(flags) | mask : BB_U32(flags) & ~mask;
        }
        bb_sample_play_entry(BB_U32(0x004842dc));
      }
      reset(object);
    }
    return;
  case 53:
  case 54:
  case 55: {
    value = integer(text, 0);
    uintptr_t flags = 0x00555bbc + self[0x82] * 0xd00;
    uint32_t mask = 1u << (command - 53);
    BB_U32(flags) = value ? BB_U32(flags) | mask : BB_U32(flags) & ~mask;
    reset(object);
    return;
  }
  case 100:
    if (!stage) {
      value = integer(text, 0);
      bb_script_condition_remove_index_entry(
          (void *)(uintptr_t)current_block(address), value);
      reset(object);
    }
    return;
  case 101:
  case 102:
  case 103:
  case 104:
  case 105:
  case 106:
  case 107:
  case 108:
  case 109:
  case 110:
  case 111:
  case 112:
  case 113:
  case 114: {
    static const unsigned char fields[14][4] = {
        {14, 2, 0, 0},  {14, 2, 6, 0}, {14, 2, 6, 0}, {14, 6, 2, 10},
        {14, 6, 2, 10}, {14, 2, 0, 0}, {14, 2, 6, 0}, {14, 2, 0, 0},
        {14, 2, 6, 0},  {14, 2, 6, 0}, {14, 2, 6, 0}, {14, 2, 6, 0},
        {14, 2, 6, 0},  {14, 2, 0, 0}};
    if (stage >= 4 || !fields[command - 101][stage])
      return;
    unsigned offset = fields[command - 101][stage];
    if (!stage) {
      value = integer(text, 0);
      BYTE(current_condition(address) + 14) = value;
      next(object);
      return;
    }
    if (command == 101) {
      BB_U32(current_condition(address) + 16) = 1;
      value = bb_clock_text_parse_entry(text);
      BB_U32(current_condition(address) + 2) = value;
    } else if (command == 107 && stage == 2) {
      if (!strchr(text, ','))
        return;
      value = integer(text, 1);
      BB_U32(current_condition(address) + 6) = value;
      comma = strchr(text, ',');
      value = integer(comma + 1, 1);
      BB_U32(current_condition(address) + 10) = value;
      BB_U32(current_condition(address) + 16) = 1;
    } else {
      value = integer(text, 1);
      BB_U32(current_condition(address) + offset) = value;
      if (stage + 1 < 4 && fields[command - 101][stage + 1]) {
        next(object);
        return;
      }
      BB_U32(current_condition(address) + 16) = 1;
    }
    reset(object);
    return;
  }
  case 200:
    p = allocate(246);
    if (p) {
      WORD(p) = 6;
      WORD(p + 2) = 246;
      p[4] = 0;
      text_copy(p + 5, text);
      bb_script_event_new_entry(object, p);
    }
    reset(object);
    interface_mode(0);
    return;
  case 201:
    value = integer(text, 0);
    bb_script_event_remove_index_entry(
        (void *)(uintptr_t)current_block(address), value);
    reset(object);
    return;
  case 202:
  case 203:
  case 204:
  case 205:
  case 207:
  case 209:
    if (!stage || (stage == 1 && (command == 205 || command == 207))) {
      value = integer(text, command == 205 || command == 209);
      BB_U32(0x006b1910 + stage * 4) = value;
      next(object);
      return;
    }
    if (stage != (command == 205 || command == 207 ? 2 : 1))
      return;
    if (command == 202 || command == 207) {
      if (!strchr(text, ','))
        return;
      p = payload(command == 202 ? 1 : 4, command == 202 ? 12 : 10);
      if (command == 202) {
        value = integer(text, 0);
        p[5] = value;
        comma = strchr(text, ',');
        value = integer(comma + 1, 0);
        p[6] = value;
        BB_U32(p + 8) = BB_U32(0x006b1910);
      } else {
        p[4] = BB_U32(0x006b1910);
        p[5] = BB_U32(0x006b1914);
        value = integer(text, 0);
        p[6] = value;
        comma = strchr(text, ',');
        value = integer(comma + 1, 0);
        p[7] = value;
        p[8] = BYTE(0x00559b65 + (p[7] * 192 + p[6]) * 12);
      }
    } else if (command == 203 || command == 204) {
      p = payload(command == 203 ? 21 : 22, 10);
      p[5] = BB_U32(0x006b1910);
      BB_U32(p + 6) = integer(text, 0);
    } else if (command == 205) {
      p = payload(19, 16);
      p[4] = BYTE(0x004843f0);
      BB_U32(p + 6) = BB_U32(0x006b1910);
      p[10] = BB_U32(0x006b1914);
      BB_U32(p + 12) = integer(text, 1);
    } else {
      p = payload(25, 34);
      BB_U32(p + 30) = BB_U32(0x006b1910);
      value = integer(text, 1);
      if (!value) {
        BB_U32(p + 6) = 1;
        text_copy(p + 14, text);
      } else {
        BB_U32(p + 6) = 0;
        BB_U32(p + 10) = integer(text, 1);
      }
    }
    bb_script_event_new_entry(object, p);
    reset(object);
    return;
  case 206:
  case 208:
    p = payload(command == 206 ? 23 : 24, 10);
    BB_U32(p + 6) = integer(text, 1);
    bb_script_event_new_entry(object, p);
    reset(object);
    return;
  }
}
