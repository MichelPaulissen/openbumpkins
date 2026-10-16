/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include <limits.h>
#include <signal.h>
#endif

/* 0x00401d90..0x00401da5: signed Q16 division. IDIV retains the
 * original divide-by-zero/quotient-overflow faults and signed remainder. */
uint64_t bb_fixed_divide_registers(int32_t a, int32_t b) {
#ifdef BB_MODEL_RUNTIME
  int64_t numerator = (int64_t)a * 65536;
  if (!b || numerator / b < INT32_MIN || numerator / b > INT32_MAX) {
    raise(SIGFPE);
    abort();
  }
  uint32_t quotient = (uint32_t)(numerator / b);
  int32_t remainder = (int32_t)(numerator % b);
#else
  uint32_t quotient = (uint32_t)a << 16;
  int32_t remainder = a >> 16;
  __asm__ volatile("idivl %2"
                   : "+a"(quotient), "+d"(remainder)
                   : "rm"(b)
                   : "cc");
#endif
  return ((uint64_t)(uint32_t)remainder << 32) | quotient;
}
uint32_t bb_fixed_divide(int32_t a, int32_t b) {
  return bb_fixed_divide_registers(a, b);
}
#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_DIVIDE_ASM "_bb_fixed_divide_registers"
#else
#define BB_DIVIDE_ASM "bb_fixed_divide_registers"
#endif
void __attribute__((naked)) bb_fixed_divide_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\t"
                   "call " BB_DIVIDE_ASM
                   "\n\taddl $8, %esp\n\tpopl %ecx\n\tret");
}

#endif

static uint64_t rounded_product(int32_t a, int32_t b) {
  return (uint64_t)((int64_t)a * b) + 0x8000;
}

/* 0x00401d70..0x00401d86: signed Q16 product rounded by adding 0x8000
 * before truncation. The boundary also retains the rounded product's high
 * DWORD in EDX and the incoming ECX.
 */
uint32_t bb_fixed_multiply(int32_t a, int32_t b) {
  return rounded_product(a, b) >> 16;
}
uint64_t bb_fixed_multiply_registers(int32_t a, int32_t b) {
  uint64_t rounded = rounded_product(a, b);
  return (rounded & UINT64_C(0xffffffff00000000)) | (uint32_t)(rounded >> 16);
}
#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_FIXED_ASM "_bb_fixed_multiply_registers"
#else
#define BB_FIXED_ASM "bb_fixed_multiply_registers"
#endif
void __attribute__((naked)) bb_fixed_multiply_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\t"
                   "call " BB_FIXED_ASM
                   "\n\taddl $8, %esp\n\tpopl %ecx\n\tret");
}

#endif

#define BYTE(address) BB_U8(address)

/* 0x0040c5a0..0x0040c85a: clear the last visible rectangle's tile bit,
 * scan staggered isometric rows, queue dirty elevated tiles and retain the
 * new byte-sized extents. Signed division/remainders truncate toward zero.
 */
uint32_t bb_terrain_visible(void) {
  int32_t map_x = BB_U32(0x006024b8) - BB_U32(0x005db014) - 78;
  int32_t map_y = BB_U32(0x00602484) - BB_U32(0x006a9078) - 20;
  int32_t x_remainder = map_x % 78;
  uint32_t screen_y = 0u - 80 - (uint32_t)(map_y % 40);
  uint32_t column = (uint32_t)(map_x / 78) + (uint32_t)(map_y / 40) - 1;
  uint32_t row = (uint32_t)(map_y / 40) - (uint32_t)(map_x / 78) - 1;
  uint32_t old_row = BYTE(0x00602280), last_row = BYTE(0x0060219c);
  if (old_row <= last_row) {
    uint32_t old_column = BYTE(0x00602288);
    uint32_t tile = 0x00559b68 + (old_row * 192 + old_column) * 12;
    for (uint32_t count = last_row - old_row + 1; count; --count) {
      uint32_t end = BYTE(0x006021a0);
      if (old_column <= end)
        for (uint32_t i = 0; i < end - old_column + 1; ++i)
          BB_U32(tile + i * 12) &= 0xffbfffff;
      tile += 192 * 12;
    }
  }
  BYTE(0x00602288) = BYTE(0x00602280) = 192;
  BYTE(0x006021a0) = BYTE(0x0060219c) = 0;
  uint32_t scan = 0;
  while ((int32_t)screen_y < (int32_t)(BB_U32(0x00484448) + 80)) {
    int32_t odd = (int32_t)scan % 2;
    uint32_t screen_x = (odd ? 0u - 117 : 0u - 78) - (uint32_t)x_remainder;
    uint32_t tile_row = row - (odd ? 0 : 1), tile_column = column;
    uint32_t row_base = tile_row * 192;
    while ((int32_t)screen_x < (int32_t)(BB_U32(0x00484444) + 39)) {
      if ((int32_t)row_base > 192 && (int32_t)row_base < 0x8f40 &&
          (int32_t)tile_column > 1 && (int32_t)tile_column < 191) {
        uint32_t tile = 0x00559b60 + (row_base + tile_column) * 12;
        uint32_t bottom = screen_y + 39 - BYTE(tile + 5) * 11;
        if (BYTE(tile + 7)) {
          bb_render_queue_insert((void *)(uintptr_t)0x005d19e0, screen_x,
                                 bottom - 40, screen_x + 78, bottom, 1);
          --BYTE(tile + 7);
        }
        if ((int32_t)tile_row < BYTE(0x00602280))
          BYTE(0x00602280) = tile_row;
        if ((int32_t)tile_row > BYTE(0x0060219c))
          BYTE(0x0060219c) = tile_row;
        if ((int32_t)tile_column < BYTE(0x00602288))
          BYTE(0x00602288) = tile_column;
        if ((int32_t)tile_column > BYTE(0x006021a0))
          BYTE(0x006021a0) = tile_column;
        BB_U32(tile + 8) |= 0x400000;
      }
      screen_x += 78;
      ++tile_column;
      row_base -= 192;
      --tile_row;
    }
    if (odd == 1) {
      ++column;
      ++row;
    }
    screen_y += 20;
    ++scan;
  }
  return BB_U32(0x00484448) + 80;
}

/* 0x0040b9e0..0x0040bb97: project/compact/scroll, mark terrain and changed
 * entities, merge and draw queued regions, then reset pending rectangles.
 * The captured entity head survives child callbacks. Selection halves are
 * signed truncation toward zero, calculated separately before addition.
 */
uint32_t bb_render_frame(void) {
  uint32_t first = BB_U32(0x00554f98);
  uint32_t start = (BB_U32(0x005fe3b0) + BB_U32(0x005fe3c0) - 5) << 16;
  void *manager = (void *)(uintptr_t)0x00553f90;
  void *queue = (void *)(uintptr_t)0x005d19e0;
  bb_entity_begin(manager);
  bb_entity_project(manager, start, 0x2fffff);
  bb_render_queue_compact(queue);
  bb_render_queue_scroll(queue);
  while (first && BB_I32(first + 0x86) < (int32_t)start)
    first = BB_U32(first + 4);
  bb_terrain_visible();
  uint32_t selected = BB_U32(0x00484488);
  if (selected) {
    int32_t x = BB_I16(selected + 0xbe) / 2 +
                BB_I16(selected + 0xc2) / 2;
    int32_t y = BB_I16(selected + 0xc0) / 2 +
                BB_I16(selected + 0xc4) / 2;
    bb_render_queue_both(queue, x - 40, y - 54, x + 40, y + 40);
  }
  uint32_t node = first;
  while (node && BB_I32(node + 0x86) <= (int32_t)(start + 0x2fffff)) {
    if (BYTE(node + 0x128) && BYTE(node + 0x129)) {
      bb_render_queue_insert(queue, BB_I16(node + 0xa6),
                             BB_I16(node + 0xac),
                             BB_I16(node + 0xb2),
                             BB_I16(node + 0xb8), 1);
      bb_render_queue_insert(queue, BB_I16(node + 0xbe),
                             BB_I16(node + 0xc0),
                             BB_I16(node + 0xc2),
                             BB_I16(node + 0xc4), 1);
      --BYTE(node + 0x129);
    }
    node = BB_U32(node + 4);
  }
  bb_render_queue_merge(queue);
  uint32_t result =
      bb_render_queue_process(queue, (void *)(uintptr_t)first, start, 0x2fffff);
  BB_U32((uintptr_t)queue + 0x960c) = 0;
  return result;
}
