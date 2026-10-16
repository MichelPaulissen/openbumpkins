/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "render_calls.h"
#endif

#define BYTE(address) BB_U8(address)

struct tile_scan {
  uint32_t y, column, row, index;
  int32_t x_remainder;
};
static struct tile_scan start_scan(void) {
  int32_t x = BB_U32(0x006024b8) - BB_U32(0x005db014) - 78;
  int32_t y = BB_U32(0x00602484) - BB_U32(0x006a9078) - 20;
  struct tile_scan scan = {
      0u - 80 - (uint32_t)(y % 40), (uint32_t)(x / 78) + (uint32_t)(y / 40) - 1,
      (uint32_t)(y / 40) - (uint32_t)(x / 78) - 1, 0, x % 78};
  return scan;
}
static uint32_t viewport_bottom(void) {
  return BB_U32(0x004eebcc) + BB_U32(0x004eebd4) + 80;
}
static uint32_t viewport_right(void) {
  return BB_U32(0x004eebc8) + BB_U32(0x004eebd0) + 39;
}
static void next_row(struct tile_scan *scan) {
  if ((int32_t)scan->index % 2 == 1) {
    ++scan->column;
    ++scan->row;
  }
  scan->y += 20;
  ++scan->index;
}
static int inner_tile(uint32_t row_base, uint32_t column) {
  return (int32_t)row_base > 384 && (int32_t)row_base < 0x8e80 &&
         (int32_t)column > 2 && (int32_t)column < 190;
}
static void draw(uint32_t slot, uint32_t x, uint32_t y, uint32_t frame) {
  bb_sprite_draw(BB_POINTER(slot), x, y, frame, 1);
}

/* 0x0040c860..0x0040ccbe: staggered terrain layers, faction overlays,
 * classification counters, hover marking and the raised overlay. Keep flags
 * and adjusted terrain kind cached across child draws; re-read sprite slots,
 * decals, faction state and viewport bounds when the original does.
 */
uint32_t bb_render_background(void) {
  struct tile_scan scan = start_scan();
  while ((int32_t)scan.y < (int32_t)viewport_bottom()) {
    int odd = (int32_t)scan.index % 2 != 0;
    uint32_t x = (odd ? 0u - 117 : 0u - 78) - (uint32_t)scan.x_remainder;
    uint32_t row = scan.row - (odd ? 0 : 1), column = scan.column;
    uint32_t row_base = row * 192, plain_y = scan.y + 39;
    while ((int32_t)x < (int32_t)viewport_right()) {
      uint32_t screen_x = x + 39;
      if (!inner_tile(row_base, column)) {
        draw(0x006aff40, screen_x, plain_y, 37);
      } else {
        uint32_t tile = 0x00559b60 + (row_base + column) * 12;
        uint32_t kind = BYTE(tile);
        if (kind >= 234 && kind <= 250)
          kind -= 119;
        uint32_t flags = BB_U32(tile + 8);
        uint32_t y = plain_y - BYTE(tile + 5) * 11;
        if (!(flags & 0x4000)) {
          uint32_t slot = 0x006aff40;
          uint32_t frame = kind;
          if (kind == 225 || kind == 226 || kind == 200 || kind == 201) {
            slot = kind == 225   ? 0x00604698
                   : kind == 226 ? 0x006aff48
                   : kind == 200 ? 0x006024bc
                                 : 0x006024c0;
            frame = BB_U32(0x00484768) == 3;
          } else if (kind >= 202 && kind < 218) {
            frame = 0;
          } else if (kind >= 227 && kind < 232) {
            slot = 0x00602114 + kind * 4;
            frame = 0;
          } else if (kind >= 98 && kind < 114) {
            slot = 0x005c950c;
            frame = kind - 98;
          }
          draw(slot, screen_x, y, frame);
          if (BYTE(tile + 1))
            draw(0x00600034, screen_x, y, (uint32_t)BYTE(tile + 1) - 1);
        }
        if ((BYTE(tile + 3) & (1u << (BYTE(0x004843f0) & 31))) &&
            !(flags & 0x200000))
          draw(0x00600178, screen_x, y, flags & 0x100 ? 25 : 12);
        uint32_t hovered_column = BB_U32(0x004846d0);
        uint32_t classification = BYTE(0x006a9096 + kind * 8);
        ++BB_U32(0x006021a8 + classification * 20);
        BB_U32(0x006021b8 + classification * 20) = 0;
        if (hovered_column == column && BB_U32(0x004846d4) == row &&
            BYTE(0x005f746c) == 1) {
          draw(0x00600178, screen_x, y, 12);
          bb_terrain_dirty(column, row);
        }
        if (flags & 0x8000)
          draw(0x006a98d0, screen_x, y - 20, 255);
      }
      ++column;
      x += 78;
      row_base -= 192;
      --row;
    }
    next_row(&scan);
  }
  return viewport_bottom();
}

/* 0x00429ac0..0x00429d48: cached center flags/elevation, a four-neighbor
 * fog mask in west/north/south/east order, then optional numeric debug text.
 * Debug reads remain unchecked even on outside-map tiles, as in the original.
 */
uint32_t bb_render_overlays(void) {
#ifndef BB_MODEL_RUNTIME
  typedef int (*format_fn)(char *, const char *, ...);
#endif
  struct tile_scan scan = start_scan();
  while ((int32_t)scan.y < (int32_t)viewport_bottom()) {
    int odd = (int32_t)scan.index % 2 != 0;
    uint32_t x = (odd ? 0u - 117 : 0u - 78) - (uint32_t)scan.x_remainder;
    uint32_t row_base = (scan.row - (odd ? 0 : 1)) * 192;
    uint32_t column = scan.column, plain_y = scan.y + 39;
    while ((int32_t)x < (int32_t)viewport_right()) {
      uint32_t tile = 0x00559b60 + (row_base + column) * 12;
      uint32_t screen_x = x + 39, y = plain_y;
      if (!inner_tile(row_base, column)) {
        draw(0x006aff40, screen_x, plain_y, 37);
      } else {
        uint32_t flags = BB_U32(tile + 8);
        y -= BYTE(tile + 5) * 11;
        if (!(flags & 0x800)) {
          uint32_t mask = !(BB_U32(tile - 4) & 0x800);
          mask = mask * 2 + !(BYTE(tile - 192 * 12 + 9) & 8);
          mask = mask * 2 + !(BYTE(tile + 192 * 12 + 9) & 8);
          mask = mask * 2 + !(BYTE(tile + 12 + 9) & 8);
          draw(0x005f51b8, screen_x, y, mask);
        }
      }
      if (BB_U32(0x004842a8)) {
#ifdef BB_MODEL_RUNTIME
        BB_RENDER_CALL(0x0046bae0, 0, 0x006033d8, 0x00486ebc, BYTE(tile + 6));
#else
        ((format_fn)BB_CODE((uint32_t)0x0046bae0))(
            (char *)(uint32_t)0x006033d8, (const char *)(uint32_t)0x00486ebc,
            (uint32_t)BYTE(tile + 6));
#endif
        bb_font_draw((void *)(uint32_t)0x005c5b70,
                     (const char *)(uint32_t)0x006033d8, screen_x, y - 20, 1);
      }
      ++column;
      x += 78;
      row_base -= 192;
    }
    next_row(&scan);
  }
  return viewport_bottom();
}
