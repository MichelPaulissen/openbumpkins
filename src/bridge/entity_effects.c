/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#endif

#define BYTE(address) BB_U8(address)
#define WORD(address) BB_U16(address)
#define SHORT(address) BB_I16(address)

/* 0x00437ee0..0x00437f09: refresh the dirty byte only for a visible,
 * unattached entity. Compare the existing byte with the full signed buffer
 * count, then store its low byte. Retain the leaf's partial-EAX and EDX.
 */
uint64_t bb_entity_dirty_c(void *object, uint32_t eax, uint32_t edx) {
  uint32_t node = bb_address_encode(object);
  uint32_t visible = BYTE(node + 0x128);
  eax = (eax & 0xffffff00) | visible;
  if (visible) {
    eax = BB_U32(node + 0x28);
    if (!eax) {
      eax = BYTE(node + 0x129);
      edx = BB_U32(0x00484454);
      if ((int32_t)eax < (int32_t)edx)
        BYTE(node + 0x129) = edx;
    }
  }
  return ((uint64_t)edx << 32) | eax;
}
void bb_entity_dirty(void *object) { bb_entity_dirty_c(object, 0, 0); }

/* 0x004384a0..0x004384ae: install a sprite before refreshing dirty state. */
uint64_t bb_entity_set_sprite_c(void *object, uint32_t sprite, uint32_t edx) {
  BB_U32(bb_address_encode(object) + 0x58) = sprite;
  return bb_entity_dirty_c(object, sprite, edx);
}
void bb_entity_set_sprite(void *object, void *sprite) {
  bb_entity_set_sprite_c(object, bb_address_encode(sprite), 0);
}

/* Match i386 signed division, including its quotient-overflow fault. This
 * boundary prevents C's INT_MIN/-1 undefined behavior from changing it.
 */
static int32_t divide(int32_t numerator, int32_t denominator) {
#ifdef BB_MODEL_RUNTIME
  if (!denominator || (numerator == INT32_MIN && denominator == -1)) {
    raise(SIGFPE);
    abort();
  }
  return numerator / denominator;
#else
  int32_t remainder;
  __asm__ volatile("cltd\n\tidivl %2"
                   : "+a"(numerator), "=&d"(remainder)
                   : "rm"(denominator)
                   : "cc");
  return numerator;
#endif
}
static uint32_t draw(uint32_t slot, uint32_t x, uint32_t y, uint32_t frame) {
  return bb_sprite_draw(BB_POINTER(slot), x, y, frame, 1);
}
static uint32_t bar_segments(uint16_t *x, uint32_t y, int32_t count,
                             uint32_t frame, uint32_t eax) {
  while (count > 0) {
    uint32_t power = 5;
    while (count < (int32_t)(1u << power))
      --power;
    uint32_t length = 1u << power;
    uint32_t screen_x = (int16_t)*x;
    *x += length;
    count -= length;
    eax = draw(0x00604678 + power * 4, screen_x, y, frame);
  }
  return eax;
}

/* 0x0043ef00..0x0043f16e: two end caps and power-of-two bar segments.
 * Coordinates and the filled length truncate to signed words; the color
 * thresholds compare the low byte of the wrapping percentage quotient.
 * The bottom argument is unused. Zero maximum draws nothing.
 */
uint32_t bb_entity_bar_c(uint32_t eax, uint32_t left, uint32_t top,
                         uint32_t right, uint32_t bottom, uint32_t current,
                         uint32_t maximum) {
  (void)bottom;
  if (!maximum)
    return eax;
  uint32_t width =
      (uint32_t)(int32_t)(int16_t)right - (uint32_t)(int32_t)(int16_t)left;
  int32_t filled = (int16_t)divide((width - 2) * current, maximum);
  if (filled < 0)
    filled = 0;
  int32_t empty = width - filled - 2;
  uint8_t percentage = divide(current * 100, maximum);
  uint32_t color = percentage < 20 ? 2 : percentage < 60 ? 1 : 0;
  uint16_t x = left;
  uint32_t y = (int32_t)(int16_t)top;
  eax = draw(0x005db184, (int16_t)x, y, 255);
  ++x;
  eax = bar_segments(&x, y, filled, color, eax);
  bar_segments(&x, y, empty, 3, eax);
  return draw(0x005db184, (int16_t)x, y, 255);
}

/* 0x0043f170..0x0043f304: visible selections use four corners and an
 * optional health bar; kinds 0x67/0x68 instead use a clamped gauge icon.
 * Bounds/resources/kind are re-read after draws, as in the original.
 */
uint32_t BB_THISCALL bb_entity_selection(void *object) {
  uint32_t node = bb_address_encode(object);
  if (!BYTE(node + 0x128))
    return 0;
  uint32_t kind = BYTE(node + 0xd4);
  if (kind == 0x67 || kind == 0x68) {
    uint32_t x = BB_U32(node + 0x120), y = BB_U32(node + 0x124) - 20;
    if (BYTE(node + 0xf2) == 16)
      y += 6;
    uint32_t frame = 0;
    int32_t maximum = SHORT(node + 0xa2);
    if (maximum)
      frame = divide(SHORT(node + 0xa0) * 9, maximum);
    uint32_t count = BB_U32(BB_U32(0x006afb24) + 0x4c);
    if ((frame & 0xffff) >= count)
      frame = count - 1;
    return draw(0x006afb24, x, y, frame & 0xffff);
  }
  draw(0x005133f8, SHORT(node + 0xbe), SHORT(node + 0xc0) + 10, 0);
  uint32_t sprite = BB_U32(0x005133f8);
  draw(0x005133f8, (uint32_t)SHORT(node + 0xc2) - BB_U32(sprite + 0x44),
       SHORT(node + 0xc0) + 10, 1);
  sprite = BB_U32(0x005133f8);
  draw(0x005133f8, SHORT(node + 0xbe),
       (uint32_t)SHORT(node + 0xc4) - BB_U32(sprite + 0x48), 3);
  sprite = BB_U32(0x005133f8);
  uint32_t y = (uint32_t)SHORT(node + 0xc4) - BB_U32(sprite + 0x48);
  uint32_t x = (uint32_t)SHORT(node + 0xc2) - BB_U32(sprite + 0x44);
  draw(0x005133f8, x, y, 2);
  kind = BYTE(node + 0xd4);
  uint32_t eax = kind * 457;
  if (!BB_U32(0x0066359a + kind * 914))
    return eax;
  uint32_t maximum = (int32_t)SHORT(node + 0xa2);
  eax = (maximum & 0xffff0000) | WORD(node + 0xc2);
  return bb_entity_bar_c(eax, WORD(node + 0xbe), WORD(node + 0xc0),
                         WORD(node + 0xc2), WORD(node + 0xc4),
                         (int32_t)SHORT(node + 0xa0), maximum);
}

/* 0x0043f310..0x0043f433: four terrain definitions gate reflection.
 * A newly reflective entity becomes dirty and extends its lower bound.
 * Reflection uses blend mode 1, then resets it to zero after the draw.
 */
uint32_t BB_THISCALL bb_entity_reflection(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t fallback = BB_U32(0x006ad1ec);
  if (BB_U32(node + 0x58) == fallback)
    return fallback;
  uint32_t tile =
      0x00559b60 + (BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8)) * 12;
  uint32_t terrain = BB_U32(0x006a9092 + BYTE(tile) * 8);
  terrain |= BB_U32(0x006a9092 + BYTE(tile + 12) * 8);
  terrain |= BB_U32(0x006a9092 + BYTE(tile + 192 * 12 + 12) * 8);
  terrain |= BB_U32(0x006a9092 + BYTE(tile + 192 * 12) * 8);
  int reflected = (terrain & 8) && !BB_U32(0x00484288);
  uint32_t kind = BYTE(node + 0xd4);
  if (kind == 0x67 || kind == 0x68)
    reflected = 0;
  uint32_t flags = BB_U32(node + 0x9c);
  if (reflected) {
    if (!(flags & 0x800)) {
      BB_U32(node + 0x9c) = flags | 0x800;
      bb_entity_dirty(object);
    }
    WORD(node + 0xc4) = BB_U32(BB_U32(node + 0x58) + 0x48) + WORD(node + 0xcc) +
                        WORD(node + 0x124);
  } else {
    BB_U32(node + 0x9c) = flags & 0xfffff7ff;
  }
  flags = BB_U32(node + 0x9c);
  if (!(flags & 0x800))
    return flags;
  BYTE(0x004842c0) = 1;
  uint32_t sprite = BB_U32(node + 0x58);
  uint32_t y =
      BB_U32(sprite + 0x58) * 2 - BB_U32(sprite + 0x48) + BB_U32(node + 0x124);
  uint32_t result = bb_sprite_draw((void *)(uintptr_t)sprite,
                                   BB_U32(node + 0x120), y, 255, 1);
  BYTE(0x004842c0) = 0;
  return result;
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_DIRTY_ASM "_bb_entity_dirty_c"
#define BB_SET_ASM "_bb_entity_set_sprite_c"
#define BB_BAR_ASM "_bb_entity_bar_c"
#define BB_SELECT_ASM "_bb_entity_selection"
#define BB_REFLECT_ASM "_bb_entity_reflection"
#else
#define BB_DIRTY_ASM "bb_entity_dirty_c"
#define BB_SET_ASM "bb_entity_set_sprite_c"
#define BB_BAR_ASM "bb_entity_bar_c"
#define BB_SELECT_ASM "bb_entity_selection"
#define BB_REFLECT_ASM "bb_entity_reflection"
#endif
/* Stores do not change flags in the original dirty helper. Reconstruct its
 * final byte test, pointer test or signed comparison after C restores ECX.
 */
#define BB_DIRTY_FLAGS                                                         \
  "cmpb $0, 0x128(%ecx)\n\tje 1f\n\tcmpl $0, 0x28(%ecx)\n\tjne 2f\n\t"         \
  "cmpl %edx, %eax\n\tjmp 1f\n"                                                \
  "2: testl %eax, %eax\n"                                                      \
  "1: "
void __attribute__((naked)) bb_entity_dirty_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl %ecx\n\t"
                   "call " BB_DIRTY_ASM
                   "\n\taddl $12, %esp\n\tpopl %ecx\n\t" BB_DIRTY_FLAGS "ret");
}
void __attribute__((naked)) bb_entity_set_sprite_entry(void) {
  __asm__ volatile(
      "pushl %ecx\n\tpushl %edx\n\tpushl 12(%esp)\n\tpushl %ecx\n\t"
      "call " BB_SET_ASM "\n\taddl $12, %esp\n\tpopl %ecx\n\t" BB_DIRTY_FLAGS
      "ret $4");
}
void __attribute__((naked)) bb_entity_bar_entry(void) {
  __asm__ volatile(
      "cmpl $0, 24(%esp)\n\tje 1f\n\tpushl %ebp\n\tmovl %esp, %ebp\n\t"
      "pushl 28(%ebp)\n\tpushl 24(%ebp)\n\tpushl 20(%ebp)\n\t"
      "pushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tpushl %eax\n\t"
      "call " BB_BAR_ASM "\n\taddl $28, %esp\n\tpopl %ebp\n"
      "1: leal -4(%esp), %esp\n\taddl $4, %esp\n\tret");
}
void __attribute__((naked)) bb_entity_selection_entry(void) {
  __asm__ volatile("cmpb $0, 0x128(%ecx)\n\tjne " BB_SELECT_ASM
                   "\n\txorw %ax, %ax\n\tret");
}
void __attribute__((naked)) bb_entity_reflection_entry(void) {
  __asm__ volatile("movl 0x006ad1ec, %eax\n\tmovl 0x58(%ecx), %edx\n\t"
                   "cmpl %eax, %edx\n\tjne " BB_REFLECT_ASM
                   "\n\txorl %ecx, %ecx\n\tret");
}

#endif
