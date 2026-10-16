/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#define BYTE(a) BB_U8(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* 0x00447a80: reveal around a point or every active building footprint tile.
 * Radius is a signed full DWORD. Footprints and nonbuilding bounds remain
 * live across the five ordered dirty callbacks for each newly revealed tile.
 */
static void reveal_tile(int32_t x, int32_t y, int32_t home_x, int32_t home_y,
                        uint32_t radius) {
  uint32_t row = (uint32_t)y * 192u;
  if ((int32_t)row <= 0x240 || (int32_t)row >= 0x8d00 || x <= 3 || x >= 188)
    return;
  uint32_t tile = 0x00559b68 + ((uint32_t)x + row) * 12u;
  uint32_t flags = BB_U32(tile);
  if (flags & 0x800)
    return;
  uint32_t index =
      ((uint32_t)y - (uint32_t)home_y) * 64u - (uint32_t)home_x + (uint32_t)x;
  if (BB_I32(0x005fb2f0 + index * 4u) > (int32_t)(radius << 16))
    return;
  BB_U32(tile) = flags | 0x800;
  BB_U32(tile) = (flags | 0x800) & 0xffffbfffu;
  const int32_t neighbors[] = {-0x900, 0x900, -12, 12};
  for (unsigned i = 0; i < 4; i++) {
    uint32_t f = BB_U32(tile + (uint32_t)neighbors[i]);
    if (f & 0x4000)
      BB_U32(tile + (uint32_t)neighbors[i]) = f & 0xffffbfffu;
  }
  bb_terrain_dirty(x, (uint8_t)(y - 1));
  bb_terrain_dirty(x, (uint8_t)(y + 1));
  bb_terrain_dirty((uint8_t)(x - 1), y);
  bb_terrain_dirty((uint8_t)(x + 1), y);
  bb_terrain_dirty(x, y);
}
uint32_t bb_entity_reveal_c(void *object, uint32_t radius, uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  eax = (eax & 0xffffff00u) | BYTE(0x004843f0);
  if (BYTE(node + 0xf7) != (uint8_t)eax)
    return eax;
  eax = radius;
  if (!radius)
    return eax;
  if (BB_U32(0x00663578 + BYTE(node + 0xd4) * 914u)) {
    for (unsigned y = 0; y < 7; y++)
      for (unsigned x = 0; x < 7; x++)
        if (BB_U32(0x006635c4 + BYTE(node + 0xd4) * 914u + (y * 7 + x) * 4u)) {
          int32_t hx = (int32_t)BYTE(node + 0xd8) - 3 + x,
                  hy = (int32_t)BYTE(node + 0xd9) - 3 + y;
          uint32_t last_x = (uint32_t)hx + radius,
                   last_y = (uint32_t)hy + radius;
          for (uint32_t row = (uint32_t)hy - radius;
               (int32_t)row <= (int32_t)last_y; row++)
            for (uint32_t col = (uint32_t)hx - radius;
                 (int32_t)col <= (int32_t)last_x; col++)
              reveal_tile(col, row, hx, hy, radius);
        }
    return 49;
  }
  uint32_t first_y = (uint32_t)BYTE(node + 0xd9) - radius;
  if ((int32_t)first_y > (int32_t)((uint32_t)BYTE(node + 0xd9) + radius))
    return first_y;
  for (uint32_t row = first_y;
       (int32_t)row <= (int32_t)((uint32_t)BYTE(node + 0xd9) + radius); row++)
    for (uint32_t col = (uint32_t)BYTE(node + 0xd8) - radius;
         (int32_t)col <= (int32_t)((uint32_t)BYTE(node + 0xd8) + radius); col++)
      reveal_tile(col, row, BYTE(node + 0xd8), BYTE(node + 0xd9), radius);
  return (uint32_t)BYTE(node + 0xd9) + radius;
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_reveal_entry(void) {
  __asm__ volatile("pushl %eax;pushl 8(%esp);pushl %ecx;call " BB_ASM(
      bb_entity_reveal_c) ";addl $12,%esp;ret $4");
}

#endif
