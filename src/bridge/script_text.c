/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "coordinator.h"
#include "interop.h"
#include "script.h"
#include "worker_runtime.h"
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#else
#define FORMAT_WORD(p) (p)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
typedef int (*format_fn)(char *, const char *, ...);
#ifndef BB_MODEL_RUNTIME
#define FORMAT ((format_fn)BB_CODE((uintptr_t)0x0046bae0))
#endif
#define TEXT(a) ((const char *)(uintptr_t)(a))
#define OUTPUT ((char *)(uintptr_t)0x006033d8)

/* Preserve the original NUL-inclusive forward DWORD/byte copy, including
 * overlap. All script descriptions intentionally retain their old bounds. */
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
static void append(char *out, const char *text) {
  /* Measure the source before searching the destination, as the old code. */
  out = BB_DATA(out);
  text = BB_DATA(text);
  size_t n = strlen(text) + 1;
  out += strlen(out);
  while (n >= 4) {
    BB_U32(out) = BB_U32(text);
    out += 4;
    text += 4;
    n -= 4;
  }
  while (n--)
    *out++ = *text++;
}

/* 00422600: script timestamps use 36 ticks/second and four display fields. */
void bb_script_time_text_entry(uint32_t time, char *out) {
  FORMAT(out, TEXT(0x00487010), time / 129600u, time / 2160u % 60u,
         time / 36u % 60u, time % 36u);
}

/* 00422680: format the timestamp and the message's packed command payload. */
void BB_THISCALL bb_script_event_text_entry(void *object, char *out) {
  uint32_t self = bb_address_encode(object);
  char description[256];
  bb_script_time_text_entry(BB_U32(self + 2), out);
  append(out, TEXT(0x00487168));
  uintptr_t p = BB_U32(self + 6);
  const char *text = description;
  switch (WORD(p)) {
  case 1:
    FORMAT(description, TEXT(0x00487118), BB_U32(p + 8), BYTE(p + 5),
           BYTE(p + 6));
    break;
  case 2:
    text = TEXT(0x00487110);
    break;
  case 3:
    text = TEXT(0x00487100);
    break;
  case 4:
    FORMAT(description, TEXT(0x004870e0), BYTE(p + 5), BYTE(p + 6),
           BYTE(p + 7));
    break;
  case 6:
    text = TEXT(0x0048715c);
    break;
  case 17:
    text = TEXT(0x00487068);
    break;
  case 18:
    text = TEXT(0x0048705c);
    break;
  case 19:
    FORMAT(description, TEXT(0x00487038), BYTE(p + 10), BB_U32(p + 6),
           BB_U32(p + 12));
    break;
  case 20:
    text = TEXT(BYTE(p + 5) == 1 ? 0x00487148 : 0x00487134);
    break;
  case 21:
    FORMAT(description, TEXT(0x004870cc), BYTE(p + 5), !!BB_U32(p + 6));
    break;
  case 22:
    FORMAT(description, TEXT(0x004870b0), BYTE(p + 5), BB_U32(p + 6));
    break;
  case 23:
  case 24:
    FORMAT(description, TEXT(WORD(p) == 23 ? 0x00487024 : 0x004870a0),
           BB_U32(p + 6));
    break;
  case 25:
    if (BB_U32(p + 6))
      FORMAT(description, TEXT(0x00487070), FORMAT_WORD(TEXT(p + 14)));
    else
      FORMAT(description, TEXT(0x00487088), BB_U32(p + 10));
    break;
  default:
    return;
  }
  append(out, text);
}

/* 00422d80: condition descriptions, including incomplete and unknown kinds. */
void BB_THISCALL bb_script_condition_text_entry(void *object, char *out) {
  uint32_t self = bb_address_encode(object);
  char description[256];
  if (!BB_U32(self + 16)) {
    copy_text(out, TEXT(0x00487368));
    return;
  }
  FORMAT(out, TEXT(0x00487350), BYTE(self + 14));
  const char *text = description;
  switch (BYTE(self)) {
  case 1:
    append(out, TEXT(0x00487340));
    bb_script_time_text_entry(BB_U32(self + 2), description);
    break;
  case 2:
  case 3:
    FORMAT(description, TEXT(BYTE(self) == 2 ? 0x00487320 : 0x004872fc),
           BB_U32(self + 2), BB_U32(self + 6));
    break;
  case 4:
  case 5:
    FORMAT(description, TEXT(BYTE(self) == 4 ? 0x004872d8 : 0x004872b4),
           FORMAT_WORD(TEXT(0x00663538 + BB_U32(self + 6) * 914u)),
           BB_U32(self + 6), BB_U32(self + 2), BB_U32(self + 10));
    break;
  case 6:
  case 8:
  case 14:
    FORMAT(description,
           TEXT(BYTE(self) == 6   ? 0x00487274
                : BYTE(self) == 8 ? 0x004871c0
                                  : 0x004871a0),
           BB_U32(self + 2));
    break;
  case 7:
    FORMAT(description, TEXT(0x004871d8), BB_U32(self + 2), BB_U32(self + 6),
           BB_U32(self + 10));
    break;
  case 9:
  case 10:
  case 12:
  case 13:
    FORMAT(description,
           TEXT(BYTE(self) == 9    ? 0x00487254
                : BYTE(self) == 10 ? 0x00487238
                : BYTE(self) == 12 ? 0x00487218
                                   : 0x004871f8),
           BB_U32(self + 2), BB_U32(self + 6));
    break;
  case 11:
    FORMAT(description, TEXT(0x0048728c), BB_U32(self + 2),
           FORMAT_WORD(TEXT(BB_U32(self + 6) ? 0x004872ac : 0x004872a4)));
    break;
  default:
    text = TEXT(0x00487194);
    break;
  }
  append(out, text);
}

static void draw(const char *text, int32_t x, int32_t y) {
  bb_font_draw((void *)(uintptr_t)0x005c5b70, text, x, y, 0);
}

/* 00422950: two columns, with a marker at the live message cursor. */
void BB_THISCALL bb_script_block_draw_entry(void *object, int32_t x,
                                            int32_t y) {
  uint32_t self = bb_address_encode(object);
  uintptr_t event = BB_U32(self), condition = BB_U32(self + 12);
  char line[256];
  uint32_t index = 0;
  int32_t row = y;
  while (event) {
    FORMAT(line, TEXT(BB_U32(self + 4) == event ? 0x0048718c : 0x00487184),
           index++);
    bb_script_event_text_entry((void *)(uintptr_t)event, OUTPUT);
    append(line, OUTPUT);
    draw(line, x, row);
    row = (uint32_t)row + 12u;
    event = BB_U32(event + 10);
  }
  if (!BB_U32(self))
    draw(TEXT(0x00487178), x, row);
  x = (uint32_t)x + 200u;
  index = 0;
  while (condition) {
    FORMAT(line, TEXT(0x00487184), index++);
    bb_script_condition_text_entry((void *)(uintptr_t)condition, OUTPUT);
    append(line, OUTPUT);
    draw(line, x, y);
    condition = BB_U32(condition + 20);
    y = (uint32_t)y + 12u;
  }
  if (!BB_U32(self + 12))
    draw(TEXT(0x0048716c), x, y);
}

/* 00423600: script inspector header, selected block, prompts and mouse info. */
void BB_THISCALL bb_script_draw_entry(void *object) {
  uint32_t self = bb_address_encode(object);
  char time[256];
  bb_clock_text_entry(
      BB_U32(0x004842c4) - BB_U32(bb_script_selected_block(object) + 20), time);
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer time_arg;
  bb_model_buffer_begin(&time_arg, time, strlen(time) + 1);
  FORMAT(OUTPUT, TEXT(0x4873bc), BYTE(self + 0x82),
         BYTE(self + 0x83 + BYTE(self + 0x82)), time_arg.reference);
  bb_model_buffer_end(&time_arg, 0);
#else
  FORMAT(OUTPUT, TEXT(0x4873bc), BYTE(self + 0x82),
         BYTE(self + 0x83 + BYTE(self + 0x82)), time);
#endif
  draw(OUTPUT, 40, 85);
  bb_script_block_draw_entry(
      (void *)(uintptr_t)bb_script_selected_block(object), 40, 100);
  FORMAT(OUTPUT, TEXT(0x004873b4), FORMAT_WORD(TEXT(self + 0x120c)),
         FORMAT_WORD(TEXT(self + 0x140c)));
  draw(OUTPUT, 40, 235);
  FORMAT(OUTPUT, TEXT(0x00486e1c), FORMAT_WORD(TEXT(self + 0x130c)));
  bb_font_draw_wrapped((void *)(uintptr_t)0x005c5b70, OUTPUT, 40, 245, 600, 0,
                       0);
  uintptr_t hover = BB_U32(0x00484488);
  if (!hover)
    FORMAT(OUTPUT, TEXT(0x004873a0), BB_U32(0x004846d0), BB_U32(0x004846d4));
  else {
    unsigned kind = BYTE(hover + 0xd4);
    FORMAT(OUTPUT, TEXT(0x00487374), BB_U32(hover + 0x8e), kind,
           FORMAT_WORD(TEXT(0x00663538 + kind * 914u)), BYTE(hover + 0xf4),
           BB_U32(0x004846d0), BB_U32(0x004846d4));
  }
  draw(OUTPUT, 440, 26);
#ifdef BB_MODEL_RUNTIME
  M5(0x418900, (void *)(uintptr_t)0x5d19e0, 0, 80, 640, 260, 1);
#else
  typedef void(BB_THISCALL * outline_fn)(void *, uint32_t, uint32_t, uint32_t,
                                         uint32_t, uint32_t);
  ((outline_fn)BB_CODE((uintptr_t)0x00418900))((void *)(uintptr_t)0x005d19e0, 0,
                                               80, 640, 260, 1);
#endif
}
