/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#define BYTE(a) BB_U8(a)

/* 0x00421590..0x004215c6: byte-truncate both coordinates and mark only
 * currently visible tiles. EAX is the byte offset, EDX the original flags;
 * ECX is the column byte or the buffer-count byte when marked.
 */
uint64_t bb_terrain_dirty(uint32_t column, uint32_t row) {
  uint32_t offset = ((row & 255) * 192 + (column & 255)) * 12;
  uint32_t flags = BB_U32(0x00559b68 + offset);
  if (flags & 0x400000)
    BYTE(0x00559b67 + offset) = BYTE(0x00484454);
  return ((uint64_t)flags << 32) | offset;
}
#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_DIRTY_ASM "_bb_terrain_dirty"
#else
#define BB_DIRTY_ASM "bb_terrain_dirty"
#endif
void __attribute__((naked)) bb_terrain_dirty_entry(void) {
  __asm__ volatile("pushl 8(%esp)\n\tpushl 8(%esp)\n\tcall " BB_DIRTY_ASM
                   "\n\taddl $8, %esp\n\tmovzbl 4(%esp), %ecx\n\t"
                   "testl $0x400000, %edx\n\tje 1f\n\t"
                   "movb 0x00484454, %cl\n"
                   "1: ret $8");
}

#endif
