/* SPDX-License-Identifier: GPL-3.0-only */
#include "object_initializers.h"
#include "controls.h"
#include "interface_controls.h"
#include "interface_platform.h"
#include "selection_panels.h"
#include "dialog.h"
#include "letters.h"
#include "script.h"
#include "world.h"
#include "game_calls.h"
#include <string.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
/* Shared constructor bodies; native ABI adapters remain in their owner files. */
static const char *empty_panel_text(void) {
  return (const char *)(uintptr_t)0x0048481c;
}

static const char *localized_panel_text(uintptr_t slot) {
  const char *text = BB_POINTER(slot);
  return text ? text : empty_panel_text();
}

/* The original inline copies measure first, then copy forward in DWORDs and
 * trailing bytes. Preserve that order even when a source aliases the object.
 * These are the original unbounded string fields, including their terminator.
 */
static void copy_panel_text(void *destination, const char *text) {
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

static uint32_t mask_for(unsigned bits) {
  return bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
}
static uint32_t common_flags(uint32_t value, unsigned bits) {
  value &= mask_for(bits);
  uint32_t low = value & 255u;
  low ^= low >> 4;
  uint32_t parity = (0x9669u >> (low & 15u)) & 1u;
  return (!value ? 0x40u : 0u) | (value & (1u << (bits - 1)) ? 0x80u : 0u) |
         (parity << 2);
}
static uint32_t logic_flags(uint32_t flags, uint32_t value, unsigned bits) {
  return (flags & ~0x8d5u) | common_flags(value, bits);
}
static uint32_t add_flags(uint32_t flags, uint32_t a, uint32_t b,
                          unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1);
  a &= mask;
  b &= mask;
  uint32_t result = (a + b) & mask;
  return logic_flags(flags, result, bits) | ((uint64_t)a + b > mask ? 1u : 0u) |
         (((a ^ b ^ result) & 16u) ? 16u : 0u) |
         ((~(a ^ b) & (a ^ result) & sign) ? 0x800u : 0u);
}
static uint32_t sub_flags(uint32_t flags, uint32_t a, uint32_t b,
                          unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1);
  a &= mask;
  b &= mask;
  uint32_t result = (a - b) & mask;
  return logic_flags(flags, result, bits) | (a < b ? 1u : 0u) |
         (((a ^ b ^ result) & 16u) ? 16u : 0u) |
         (((a ^ b) & (a ^ result) & sign) ? 0x800u : 0u);
}

/* Extracted from world.c. */
void BB_THISCALL bb_cursor_initialize_entry(void *self) {
  BB_U32((uintptr_t)self + 4) = 0;
  BB_U32(self) = 0;
}

/* Extracted from world.c. */
void BB_THISCALL bb_build_menu_initialize_entry(void *self) {
  uintptr_t p = (uintptr_t)self;
  BB_U32(p + 0x10) = BB_U32(0x00484448);
  BB_U32(p + 4) = BB_U32(0x00484448);
  BB_U32(p) = 0;
  BB_U32(p + 8) = 0;
}

/* Extracted from world.c. */
void BB_THISCALL bb_worker_menu_initialize_entry(void *self) {
  uintptr_t p = (uintptr_t)self;
  BB_U32(p) = BB_U32(0x00484444) - 100;
  BB_U32(p + 0x20) = 0;
  BB_U32(p + 0x24) = 0;
  BB_U32(p + 8) = 0;
  BB_U32(p + 0x28) = 0;
  BB_U32(p + 0x2c) = 0;
  BB_U32(p + 0x18) = 0;
  BB_U32(p + 4) = BB_U32(0x00484448) - 100;
}

/* Extracted from interface_controls.c. */
void *BB_THISCALL bb_cursor_construct_entry(void *p) {
  bb_cursor_initialize_entry(p);
  return p;
}

/* Extracted from interface_controls.c. */
void BB_THISCALL bb_toolbar_initialize_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  for (unsigned i = 0; i < 7; ++i)
    BB_U32(p + i * 4u) = 0;
  BB_U32(p + 8) = 1;
  BB_U32(p + 12) = 1;
}

/* Extracted from interface_controls.c. */
void *BB_THISCALL bb_toolbar_construct_entry(void *p) {
  bb_toolbar_initialize_entry(p);
  return p;
}

/* Extracted from interface_controls.c. */
void *BB_THISCALL bb_build_menu_construct_entry(void *p) {
  bb_build_menu_initialize_entry(p);
  return p;
}

/* Extracted from controls.c. */
void *BB_THISCALL bb_ambience_identity_entry(void *p) {
  return p;
}

/* Extracted from controls.c. */
void *BB_THISCALL bb_digit_construct_entry(void *object) {
  BB_U32((uintptr_t)object) = 0;
  return object;
}

/* Extracted from controls.c. */
void *BB_THISCALL bb_number_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  for (unsigned i = 0; i < 8; ++i)
    bb_digit_construct_entry(PTR(p + i * 14u));
  return object;
}

/* Extracted from controls.c. */
void bb_number_draw_entry(void) {}

/* Extracted from controls.c. */
void *BB_THISCALL bb_numeric_panel_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  for (unsigned i = 0; i < 4; ++i)
    bb_number_construct_entry(PTR(p + 4 + i * 126u));
  return object;
}

/* Extracted from controls.c. */
void *BB_THISCALL
bb_building_stock_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p) = 0;
  BYTE(p + 4) = 0;
  return object;
}

/* Extracted from interface_platform.c. */
void *BB_THISCALL
bb_minimap_panel_initialize_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p) = 1;
  BYTE(p + 12) = BYTE(p + 13) = 0;
  BB_U32(p + 4) = 89;
  BB_U32(p + 8) = 96;
  return object;
}

/* Extracted from interface_platform.c. */
void *BB_THISCALL bb_stock_panel_construct_entry(void *object) {
  bb_worker_menu_initialize_entry(object);
  return object;
}

/* Extracted from interface_platform.c. */
void *BB_THISCALL
bb_mission_description_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p) = BB_U32(p + 4) = 0;
  bb_mission_description_clear_entry(object);
  return object;
}

/* Extracted from interface_platform.c. */
void BB_THISCALL
bb_mission_description_clear_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  if (BB_U32(p + 4)) {
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x46c110, 0, BB_U32(p + 4));
#else
    ((void (*)(void *))BB_CODE(0x46c110))(PTR(BB_U32(p + 4)));
#endif
  }
  BB_U32(p + 4) = 0;
  BB_U32(p + 8) = 500;
  BB_U32(p + 12) = 600;
}

/* Extracted from selection_panels.c. */
void *BB_THISCALL bb_action_panel_identity_entry(void *object) {
  return object;
}

/* Extracted from selection_panels.c. */
void *BB_THISCALL
bb_building_panel_initialize_entry(void *object) {
  BB_U32((uintptr_t)object) = 0;
  BYTE((uintptr_t)object + 4) = 0;
  return object;
}

/* Extracted from selection_panels.c. */
void *BB_THISCALL bb_tooltip_construct_entry(void *object) {
  bb_tooltip_initialize_entry(object);
  return object;
}

/* Extracted from selection_panels.c. */
void BB_THISCALL bb_tooltip_initialize_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 10) = 320;
  BB_U32(p) = 1;
  BYTE(p + 34) = 0;
  BYTE(p + 9) = 0;
  BB_U32(p + 14) = 475;
  BB_U32(p + 4) = 1;
  BYTE(p + 8) = 0;
}

/* Extracted from dialog.c. */
void *BB_THISCALL bb_feedback_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0x820) = BB_U32(p + 0x824) = 0;
  for (unsigned i = 0; i < 8; ++i)
    BYTE(p + i * 0x104) = 0;
  BB_U32(p + 0x828) = 1;
  return object;
}

/* Extracted from dialog.c. */
void *BB_THISCALL bb_highlight_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0xa18) = 0;
  BB_U32(p + 0xa14) = 128;
  for (uint32_t i = 0; (int32_t)i < BB_I32(p + 0xa14); ++i)
    BB_U32(p + 20 + i * 20) = 0;
  BB_U32(p) = 0;
  return object;
}

/* Extracted from dialog.c. */
void *BB_THISCALL bb_demo_construct_entry(void *object) {
  bb_demo_clear_entry(object);
  return object;
}

/* Extracted from dialog.c. */
void BB_THISCALL bb_demo_clear_entry(void *object) {
  BB_U32((uintptr_t)object + 0x20000) = 0;
  BB_U32((uintptr_t)object + 0x20004) = 0;
}

/* Extracted from letters.c. */
void *BB_THISCALL bb_letters_construct_entry(void *object) {
  memset(BB_DATA(object), 0, 16);
  return object;
}

/* Extracted from letters.c. */
void BB_THISCALL bb_letters_clear_entry(void *object) {
  uintptr_t self = (uintptr_t)object;
  while (BB_U32(self)) {
    uint32_t next = BB_U32(BB_U32(self) + 0x404);
    #ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x46c110, 0, BB_U32(self));
#else
    ((void (*)(void *))BB_CODE(0x46c110))((void *)(uintptr_t)BB_U32(self));
#endif
    BB_U32(self) = next;
  }
  BB_U32(self + 8) = 0;
}

/* Extracted from letters.c. */
void BB_THISCALL bb_letters_destroy_entry(void *object) {
  bb_letters_clear_entry(object);
}

/* Extracted from worker_runtime.c. */
void *BB_THISCALL bb_short_object_reset_entry(void *object) {
  WORD((uintptr_t)object) = 0;
  return object;
}

/* Extracted from script_records.c. */
void *BB_THISCALL bb_script_block_construct_entry(void *object) {
  memset(BB_DATA(object), 0, 20);
  return object;
}

/* Extracted from script_records.c. */
uint32_t BB_THISCALL
bb_script_block_select_entry(void *object, uint32_t player_value,
                             uint32_t block_value) {
  unsigned char *self = BB_DATA(object); /* 00426b20 */
  unsigned player = (unsigned char)player_value;
  unsigned block = (unsigned char)block_value;
  if (block > 31)
    block = 31;
  self[0x83 + player] = block;
  uint32_t tick = BB_U32(0x004842c4);
  uintptr_t record = (uintptr_t)self + 0x88 + (player * 32 + block) * 28;
  BB_U32(record + 20) = tick;
  BB_U32(record + 4) = BB_U32(record);
  return tick; /* original EAX, used by the still-bridged condition evaluator */
}

/* Extracted from script_records.c. */
void BB_THISCALL bb_script_player_select_entry(void *object,
                                                             uint32_t value) {
  unsigned player = (unsigned char)value; /* 00426b70 */
  if (player > 4)
    player = 4;
  BB_U8((uintptr_t)object + 0x82) = player;
}

/* Extracted from script_records.c. */
void *BB_THISCALL bb_script_construct_entry(void *object) {
  unsigned char *self = BB_DATA(object); /* 00423560 */
  for (unsigned i = 0; i < 160; ++i)
    bb_script_block_construct_entry(self + 0x88 + i * 28);
  for (unsigned i = 0; i < 5; ++i) {
    self[0x83 + i] = 0;
    bb_script_block_select_entry(object, i, 0);
  }
  memset(self + 2, 0, 128);
  self[0x82] = self[0x140c] = self[0x120c] = self[0] = self[1] = 0;
  BB_U32(self + 0x150c) = 0;
  bb_script_player_select_entry(object, 0);
  self[0x1511] = self[0x1512] = self[0x1513] = 0;
  BB_U32(self + 0x1518) = BB_U32(self + 0x1514) = 0;
  self[0x1510] = 5;
  return object;
}

/* Extracted from panel_text.c. */
void BB_THISCALL bb_panel_localize_entry(void *object) {
  unsigned char *self = BB_DATA(object);
  for (unsigned i = 0; i < 25; ++i)
    copy_panel_text(self + 6 + i * 0x74,
              i == 8 ? empty_panel_text() : localized_panel_text(0x00603800 + i * 4));
  static const unsigned slots[] = {25, 5, 27, 28, 29};
  for (unsigned i = 0; i < 5; ++i)
    copy_panel_text(self + 0xb7c + i * 0x40,
              localized_panel_text(0x00603800 + slots[i] * 4));
}

/* Extracted from panel_text.c. */
void BB_THISCALL bb_panel_initialize_entry(void *object) {
  unsigned char *self = BB_DATA(object);
  self[0] = self[1] = 0;
  for (unsigned i = 0; i < 25; ++i) {
    BB_U32(self + 2 + i * 0x74) = 0;
    memset(self + 0x46 + i * 0x74, 0, 0x30);
  }
  memset(self + 0xcc2, 0, 20);
  self[0xcbc] = 0x79;
  self[0xcbd] = 0x31;
  self[0xcbe] = 1;
  self[0xcbf] = 0xe6;
  self[0xcc0] = 0x74;
  static const unsigned char codes[] = {0,  5,  6,  7,  9,  8,  0,  1,  2,
                                        3,  4,  15, 16, 18, 19, 17, 10, 11,
                                        12, 13, 14, 20, 21, 22, 23, 24};
  memcpy(self + 0xb62, codes, sizeof(codes));
  bb_panel_localize_entry(object);
}

/* Extracted from panel_text.c. */
void *BB_THISCALL bb_panel_construct_entry(void *object) {
  bb_panel_initialize_entry(object);
  return object;
}

/* Extracted from entity_lifecycle.c. */
void *BB_THISCALL bb_entity_manager_construct(void *object) {
  uintptr_t self = (uintptr_t)object;
  BB_U32(self) = 0;
  BB_U32(self + 0x1008) = 0;
  BB_U32(self + 0x1004) = 0;
  BB_U32(self + 0x100c) = 0;
  BB_U32(self + 0x1010) = 0;
  return object;
}

/* Extracted from faction.c. */
void *BB_THISCALL bb_faction_manager_construct(void *self) {
  BB_U32((uintptr_t)self) = 5;
  return self;
}

/* Extracted from messages.c. */
void bb_message_construct_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  BB_U32(ecx + 0xcu) = 0x0u;
  BB_U32(ecx) = 0x0u;
  eax = BB_U32(0x484444u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax - edx);
  eax = ((uint32_t)((int32_t)(eax) >> 1));
  BB_U32(ecx + 0x14u) = eax;
  eax = BB_U32(0x484448u);
  a = eax;
  b = 0x14u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  BB_U32(ecx + 0x18u) = eax;
  eax = ecx;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}

/* Extracted from messages.c. */
void bb_message_self_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  eax = ecx;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}

/* Extracted from notifications.c. */
void bb_notification_clear_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  edx = (ecx + 0x8u);
  eax = 0x20u;
L_41e098:;
  BB_U32(edx) = 0x0u;
  a = edx;
  b = 0x114u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edx = result;
  a = eax;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  eax = result;
  if (!(flags & 0x40u))
    goto L_41e098;
  eax = ecx;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}

#ifdef BB_MODEL_RUNTIME
#define MESSAGE_DISPATCH(state, id, args) \
  bb_model_call_at(state, id, args, 10, esp, edi, esi, ebp, ebx)
#else
#define MESSAGE_DISPATCH(state, id, args) bb_call_words10(state, BB_CODE(id), args)
#endif
#define MESSAGE_CALL(target, consumed) \
  do { \
    struct bb_call_state state = {eax, ecx, edx, flags}; \
    MESSAGE_DISPATCH(&state, target, args + sp); \
    eax = state.eax; ecx = state.ecx; edx = state.edx; flags = state.flags; \
    sp += (consumed) / 4u; esp += (consumed); \
  } while (0)
void bb_message_destructor_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  MESSAGE_CALL(0x41e2e0u, 0); /* tail call 0041e2d0 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}

void bb_message_destroy_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx);
  edi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e302;
L_41e2ea:;
  eax = BB_U32(edi);
  args[--sp] = eax;
  esp -= 4u;
  esi = BB_U32(eax + 0x206u);
  MESSAGE_CALL(0x46c110u, 0); /* 0041e2f4 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi) = esi;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41e2ea;
L_41e302:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}

/* Shared from controls.c. */
void BB_THISCALL bb_digit_position_entry(void *object, uint32_t x,
                                                       uint32_t y) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 6) = x;
  BB_U32(p + 10) = y;
}

/* Shared from controls.c. */
void BB_THISCALL
bb_number_initialize_entry(void *object, uint32_t count, uint32_t value) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0x72) = value;
  BYTE(p + 0x70) = (uint8_t)count;
  for (unsigned i = 0; i < BYTE(p + 0x70); ++i)
    bb_digit_position_entry((void *)(p + i * 14u), i * 28u, 0);
}

/* Shared from controls.c. */
void BB_THISCALL bb_numeric_panel_initialize_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p) = 0;
  BB_U32(p + 0x20c) = 100;
  BB_U32(p + 0x204) = 100;
  BB_U32(p + 0x208) = 0;
  BB_U32(p + 0x200) = 0;
  for (unsigned i = 0; i < 4; ++i)
    bb_number_initialize_entry((void *)(p + 4 + i * 126u), 4, 0);
  for (unsigned i = 0; i < 4; ++i)
    BB_U32(p + 0x220 + i * 4u) = UINT32_MAX;
}

/* Shared from controls.c. */
void BB_THISCALL bb_ambience_initialize_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p) = 570;
  BB_U32(p + 8) = 0;
  BB_U32(p + 12) = 0;
  BB_U32(p + 4) = 40;
}

/* Shared from selection_panels.c. */
void BB_THISCALL bb_action_panel_initialize_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 8) = 500;
  BB_U32(p + 12) = 60;
  BB_U32(p) = 0;
  BB_U32(p + 16) = 0;
  BB_U32(p + 20) = 0;
  BB_U32(p + 24) = 0;
  BB_U32(p + 28) = 0;
  BYTE(p + 32) = 0;
  BB_U32(p + 4) = 0;
}

/* Shared from entity_state.c. */
uint32_t bb_entity_data_construct(void *object) {
  WORD((uintptr_t)object) = 0;
  return bb_address_encode(object);
}
