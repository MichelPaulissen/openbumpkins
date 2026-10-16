/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "object_initializers.h"
#include <stddef.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "ui_calls.h"
#endif

static const char *empty_text(void) {
  return (const char *)(uintptr_t)0x0048481c;
}

static const char *localized_text(uintptr_t slot) {
  const char *text = BB_POINTER(slot);
  return text ? text : empty_text();
}

/* The original inline copies measure first, then copy forward in DWORDs and
 * trailing bytes. Preserve that order even when a source aliases the object.
 * These are the original unbounded string fields, including their terminator.
 */
static void copy_text(void *destination, const char *text) {
  unsigned char *out = BB_DATA(destination);
  text = BB_DATA(text);
  size_t length = strlen(text) + 1;
  while (length >= 4) {
    BB_U32(out) = BB_U32(text);
    out += 4;
    text += 4;
    length -= 4;
  }
  while (length--)
    *out++ = (unsigned char)*text++;
}

/* 0x0042b9f0..0x0042bfe1: twenty-five regular labels and five compact fields.
 * Slot eight is always empty; the second compact field reuses slot five. */

/* 0x0042b8c0..0x0042b9e4: clear record headers/payloads, retain unrelated
 * padding, assign control codes and color bytes, then load the live labels. */

/* 0x0042b8b0..0x0042b8bb: the allocating caller expects self in EAX. */

/* 0x0042d2f0..0x0042d71b: inclusive rectangle and the original signed row
 * divisions, including their gap and truncation toward zero. Subtractions
 * explicitly wrap at 32 bits before division, as in the executable. */
void BB_THISCALL bb_panel_hover_text_entry(void *object, int32_t x, int32_t y,
                                           char *out) {
  uintptr_t self = (uintptr_t)object;
  copy_text(out, empty_text());
  if (x < BB_I32(self + 0xcd6) || y < BB_I32(self + 0xcda) ||
      x > BB_I32(self + 0xcde) || y > BB_I32(self + 0xce2))
    return;
  uint32_t relative = (uint32_t)y - BB_U32(self + 0xcda);
  int32_t row = (int32_t)(relative - 0x29u) / 0x1f;
  if (row < 0)
    row = 0;
  else if (row > 6) {
    row = (int32_t)(relative - 0x133u) / 0x1f + 7;
    if (row > 10)
      row = 10;
  }
  if (!BB_U32(self + 0xcea)) {
    static const uintptr_t fixed[] = {0x005e448c, 0x005e43fc, 0x005e44d4,
                                      0x005e4660, 0x005e451c, 0x005e4588,
                                      0x005e4564};
    copy_text(out, row < 7 ? (const char *)fixed[row]
                           : localized_text(0x006038d0 + (row - 7) * 4));
  } else if (row > 6) {
    static const unsigned offsets[] = {0x79, 0x7b, 0x7c, 0x7d};
    const unsigned char *settings = BB_DATA(BB_U32(0x004844a8));
    unsigned value = settings[offsets[row - 7]];
    if (value < 4)
      copy_text(out, localized_text(0x006038e0 + value * 4));
  }
}

/* 0x0042d770..0x0042d77a. */
void BB_THISCALL bb_panel_hover_reset_entry(void *object) {
  BB_U32((uintptr_t)object + 0xcea) = 0;
}

struct help_text {
  unsigned char command, stage; /* stage 255 accepts every stage */
  uintptr_t prompt, description;
};
static const struct help_text help_texts[] = {
    {0, 255, 0x0048481c, 0x0048835c},
    {1, 255, 0x0048834c, 0x00488310},   /* PLAYER */
    {2, 255, 0x004882c4, 0x00488294},   /* BLOCK */
    {3, 255, 0x004881e8, 0x004881bc},   /* FIND OBJECT ID */
    {4, 255, 0x0048802c, 0x00487fac},   /* MESSAGE TYPE */
    {5, 255, 0x00487fa0, 0x00487ef8},   /* EVENT TYPE */
    {6, 0, 0x004881ac, 0x0048816c},     /* SET OBJECT ID */
    {6, 1, 0x0048815c, 0x004880cc},     /* SET STAT NUMBER */
    {6, 2, 0x004880c0, 0x00488094},     /* NEW VALUE */
    {7, 255, 0x00487c78, 0x00487c0c},   /* SET ATTRIBUTE */
    {8, 255, 0x00488304, 0x004882d0},   /* SET GOLD */
    {9, 255, 0x00488280, 0x00488244},   /* LOAD MISSION */
    {10, 255, 0x00488230, 0x004881f8},  /* SAVE MISSION */
    {11, 255, 0x00488078, 0x0048803c},  /* DELETE OBJECT TYPE */
    {50, 255, 0x00487c00, 0x00487bd8},  /* TECH LEVEL */
    {51, 255, 0x00487bd0, 0x00487ba4},  /* DISEASE */
    {52, 0, 0x00487b98, 0x00487b6c},    /* SPELL ID */
    {52, 1, 0x00487b60, 0x00487b54},    /* AVAILABLE */
    {53, 255, 0x00487ac8, 0x00487b54},  /* BAN BUILDING */
    {54, 255, 0x00487a9c, 0x00487b54},  /* BAN ATTACKING HUMAN */
    {55, 255, 0x00487a6c, 0x00487b54},  /* BAN ATTACKING COMPUTER */
    {56, 0, 0x00487b48, 0x00487afc},    /* WEIRD FLAG */
    {56, 1, 0x00487af4, 0x00487ae8},    /* STATE */
    {57, 255, 0x00487a58, 0x00487a20},  /* BACKGROUND INDEX */
    {100, 255, 0x00487848, 0x0048781c}, /* DELETE EVENT INDEX */
    {101, 0, 0x0048780c, 0x004877d4},   /* BRANCH TO BLOCK */
    {101, 1, 0x004877c4, 0x0048779c},   /* IF TIME REACHES */
    {102, 0, 0x0048780c, 0x004877d4},
    {102, 1, 0x00487720, 0x004876ec}, /* IF GOLD OF PLAYER */
    {102, 2, 0x004876e4, 0x004876bc}, /* EXCEEDS */
    {103, 0, 0x0048780c, 0x004877d4},
    {103, 1, 0x00487720, 0x004876ec},
    {103, 2, 0x004876b0, 0x00487688}, /* DROPS BELOW */
    {104, 0, 0x0048780c, 0x004877d4},
    {104, 1, 0x0048767c, 0x00487648}, /* OBJECT TYPE */
    {104, 2, 0x0048763c, 0x004876ec}, /* OF PLAYER */
    {104, 3, 0x004876b0, 0x004875d8},
    {105, 0, 0x0048780c, 0x004877d4},
    {105, 1, 0x0048767c, 0x00487648},
    {105, 2, 0x0048763c, 0x004876ec},
    {105, 3, 0x004876e4, 0x00487608},
    {106, 0, 0x0048780c, 0x004877d4},
    {106, 1, 0x004875c0, 0x00487598}, /* IF DESTROYED OBJECT ID */
    {107, 0, 0x0048780c, 0x004877d4},
    {107, 1, 0x00487548, 0x00487598}, /* IF OBJECT ID */
    {107, 2, 0x00487460, 0x00487434}, /* ENTERS TILE */
    {108, 0, 0x0048780c, 0x004877d4},
    {108, 1, 0x0048741c, 0x00487598}, /* IF UNHAPPY OBJECT ID */
    {109, 0, 0x0048780c, 0x004877d4},
    {109, 1, 0x0048758c, 0x0048756c}, /* IF PLAYER */
    {109, 2, 0x00487558, 0x00487598}, /* CAPTURES OBJECT ID */
    {110, 0, 0x0048780c, 0x004877d4},
    {110, 1, 0x00487548, 0x00487518},
    {110, 2, 0x00487504, 0x004874e0}, /* REACHES OBJECT ID */
    {111, 0, 0x0048780c, 0x004877d4},
    {111, 1, 0x00487790, 0x0048776c}, /* GLOBAL FLAG */
    {111, 2, 0x00487758, 0x00487734}, /* TRUE OR FALSE */
    {112, 0, 0x0048780c, 0x004877d4},
    {112, 1, 0x004874d0, 0x004874a8}, /* IF OBJECT TYPE */
    {112, 2, 0x00487504, 0x004874e0},
    {113, 0, 0x0048780c, 0x004877d4},
    {113, 1, 0x00487498, 0x00487474}, /* IF OBJECT SEX */
    {113, 2, 0x00487504, 0x004874e0},
    {114, 0, 0x0048780c, 0x004877d4},
    {114, 1, 0x0048740c, 0x004873d8},   /* ALTAR OBJECT ID */
    {200, 255, 0x00487ef0, 0x00487ed0}, /* LETTER */
    {201, 255, 0x00487eb8, 0x00487e88}, /* DELETE MESSAGE INDEX */
    {202, 0, 0x00487a10, 0x004879e4},   /* ENTER OBJECT ID */
    {202, 1, 0x004879d0, 0x004879b8},   /* WALK TO POSITION */
    {203, 0, 0x004879a4, 0x00487970},   /* GLOBAL FLAG NUMBER */
    {203, 1, 0x00487af4, 0x00487950},
    {204, 0, 0x004878d4, 0x0048789c}, /* REWARD PLAYER */
    {204, 1, 0x00487894, 0x0048785c}, /* AMOUNT */
    {205, 0, 0x00487a10, 0x00487918},
    {205, 1, 0x00487910, 0x004880cc},   /* STAT */
    {205, 2, 0x00487908, 0x004878e4},   /* VALUE */
    {206, 255, 0x00487e74, 0x00487e50}, /* DELETE OBJECT ID */
    {207, 0, 0x00487e38, 0x00487e0c},   /* CREATE OBJECT OF PLAYER */
    {207, 1, 0x00487df8, 0x00487dd4},   /* CREATE OBJECT TYPE */
    {207, 2, 0x00487dc8, 0x00487d8c},   /* AT X,Y */
    {208, 255, 0x00487d80, 0x00487d34}, /* PLAY TRACK */
    {209, 0, 0x00487d28, 0x00487ce0},   /* OBJECT ID */
    {209, 1, 0x00487cd4, 0x00487c88},   /* PLAY SAMPLE */
};

/* 0x004238a0..0x00424ac0: store the byte-sized command/stage, clear both
 * strings, select help, and play feedback. Invalid event stages recursively
 * reset to command zero, playing feedback once inside and again outside. */
void BB_THISCALL bb_script_help_entry(void *object, uint32_t command_value,
                                      uint32_t stage_value) {
  unsigned char *self = BB_DATA(object);
  unsigned char command = command_value, stage = stage_value;
  self[0] = command;
  self[1] = stage;
  copy_text(self + 0x120c, empty_text());
  copy_text(self + 0x130c, empty_text());
  const struct help_text *selected = NULL;
  for (size_t i = 0; i < sizeof(help_texts) / sizeof(help_texts[0]); ++i)
    if (help_texts[i].command == command &&
        (help_texts[i].stage == 255 || help_texts[i].stage == stage)) {
      selected = &help_texts[i];
      break;
    }
  if (selected) {
    copy_text(self + 0x120c, (const char *)selected->prompt);
    copy_text(self + 0x130c, (const char *)selected->description);
  } else if (command >= 101 && command <= 114)
    bb_script_help_entry(object, 0, 0);
  bb_sound_play_entry(BB_U32(0x006b0838));
}

/* 0x00424c70..0x00424c7e: advance the byte-sized stage with wrapping. */
void BB_THISCALL bb_script_help_next_entry(void *object) {
  const unsigned char *self = BB_DATA(object);
  bb_script_help_entry(object, self[0], (unsigned char)(self[1] + 1u));
}
