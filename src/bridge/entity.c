/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"

/* Resource globals selected by each entity kind, 0x00..0xe7. A zero slot
 * leaves the default sprite; type 0xa0 instead dispatches on byte 0xf2.
 * Resources remain owned by the original sprite loader during bridging. */
static const uint32_t sprite_slots[0xe8] = {
    /* 0x00 */ 0x005f74b4,
    0x00602318,
    0x005f51c0,
    0x00000000,
    0x00000000,
    0x005f90d0,
    0x006ad264,
    0x006ad248,
    /* 0x08 */ 0x00000000,
    0x006ad1ec,
    0x0060232c,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    /* 0x10 */ 0x006ad1ec,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    /* 0x18 */ 0x005f9194,
    0x006ad1ec,
    0x006022f8,
    0x00000000,
    0x006ad1ec,
    0x0055911c,
    0x00000000,
    0x00000000,
    /* 0x20 */ 0x00000000,
    0x00553e84,
    0x005d1024,
    0x00000000,
    0x006034e0,
    0x005f6e18,
    0x006a904c,
    0x005e4798,
    /* 0x28 */ 0x005f6e18,
    0x006634d8,
    0x006634d0,
    0x006634d0,
    0x006afb30,
    0x00000000,
    0x006045f0,
    0x006afbdc,
    /* 0x30 */ 0x005f74b0,
    0x00604610,
    0x006ad1e4,
    0x006a9044,
    0x00000000,
    0x006afcf8,
    0x005c77b4,
    0x00663524,
    /* 0x38 */ 0x006ad248,
    0x005c9510,
    0x005e4730,
    0x00000000,
    0x006afb2c,
    0x005eac9c,
    0x00513434,
    0x006ad1ec,
    /* 0x40 */ 0x006ad1ec,
    0x00000000,
    0x005d19d8,
    0x00554fb0,
    0x00600164,
    0x00640ce4,
    0x00604648,
    0x00663504,
    /* 0x48 */ 0x006a98d0,
    0x005db04c,
    0x005d1554,
    0x005f7358,
    0x005fe3f8,
    0x00602310,
    0x005c78dc,
    0x006ad1fc,
    /* 0x50 */ 0x0063f05c,
    0x005e4744,
    0x005cf3e8,
    0x0063f034,
    0x005c9508,
    0x005f918c,
    0x0063f098,
    0x006a8eb0,
    /* 0x58 */ 0x006a8ed8,
    0x006a8eb4,
    0x006a8ee8,
    0x00600150,
    0x0063f0c0,
    0x005f9164,
    0x00600170,
    0x005f9250,
    /* 0x60 */ 0x00662c94,
    0x005fe3e8,
    0x005ecf2c,
    0x006a8edc,
    0x005e42c8,
    0x00000000,
    0x005c77a0,
    0x00602300,
    /* 0x68 */ 0x00602300,
    0x00000000,
    0x006a9088,
    0x00000000,
    0x005f6f30,
    0x00000000,
    0x006a98ac,
    0x0066351c,
    /* 0x70 */ 0x00000000,
    0x005f51c0,
    0x00602188,
    0x005c77bc,
    0x005c77bc,
    0x006afcf8,
    0x006afbdc,
    0x006a9044,
    /* 0x78 */ 0x00000000,
    0x00000000,
    0x0055911c,
    0x00640ce4,
    0x005c77b4,
    0x00000000,
    0x005d19d8,
    0x00602318,
    /* 0x80 */ 0x00604648,
    0x006b0844,
    0x006b0844,
    0x006ad1fc,
    0x006aff20,
    0x00000000,
    0x006022f4,
    0x00604674,
    /* 0x88 */ 0x006a9944,
    0x006a9944,
    0x005c78d4,
    0x00000000,
    0x006ad1ec,
    0x006ad1ec,
    0x00663524,
    0x00663510,
    /* 0x90 */ 0x006ad1ec,
    0x005f51bc,
    0x005f51bc,
    0x0063f03c,
    0x005d19c0,
    0x005f9118,
    0x00604694,
    0x00604694,
    /* 0x98 */ 0x006a994c,
    0x006a994c,
    0x006034e4,
    0x006034e4,
    0x006afb34,
    0x005f51cc,
    0x005f51cc,
    0x00602490,
    /* 0xa0 */ 0x00000000,
    0x006a98b8,
    0x006ad1f8,
    0x006afb28,
    0x00600140,
    0x00000000,
    0x005c77b0,
    0x00000000,
    /* 0xa8 */ 0x00000000,
    0x00663530,
    0x00663530,
    0x006ad1ec,
    0x006a9920,
    0x006ad1e4,
    0x00662c70,
    0x0063f098,
    /* 0xb0 */ 0x006afe18,
    0x005f91bc,
    0x005f9264,
    0x005f9260,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    /* 0xb8 */ 0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x005e475c,
    0x005f9110,
    /* 0xc0 */ 0x006a9960,
    0x0063f020,
    0x006ad1d4,
    0x0063f0b0,
    0x0063f0ac,
    0x005d195c,
    0x005e472c,
    0x006a989c,
    /* 0xc8 */ 0x006021a4,
    0x00663524,
    0x006ad1dc,
    0x005e473c,
    0x005133f0,
    0x00663524,
    0x006ad1ec,
    0x005e46d0,
    /* 0xd0 */ 0x005e46d0,
    0x006022a4,
    0x00641020,
    0x0063f064,
    0x006ad1d0,
    0x005d19d4,
    0x005ecf28,
    0x006a8ee0,
    /* 0xd8 */ 0x005590c0,
    0x005e4190,
    0x006024b4,
    0x006af0f4,
    0x006a9070,
    0x0051342c,
    0x006ad248,
    0x006afe14,
    /* 0xe0 */ 0x0060248c,
    0x0051342c,
    0x005590d0,
    0x005e4728,
    0x005db164,
    0x006a8ef4,
    0x006ad1cc,
    0x005ecf30,
};

/* 0x00454370..0x00454a3c: load-time sprite reassignment for the packed
 * 314-byte entity records. Return the original EAX value even on default
 * paths, where it contains the kind/subtype instead of the resource pointer. */
uint32_t bb_entity_sprite(void *self) {
  uint32_t entity = bb_address_encode(self);
  BB_SET_POINTER(entity + 0x58, BB_POINTER(0x006ad1ec));
  uint32_t kind = BB_U8(entity + 0xd4);
  uint32_t slot = kind < 0xe8 ? sprite_slots[kind] : 0;
  if (kind == 0xa0) {
    kind = BB_U8(entity + 0xf2);
    if (kind == 'T')
      slot = 0x005d1018;
    else if (kind == 'U')
      slot = 0x005fe39c;
    else if (kind == 'V')
      slot = 0x006a98d4;
  }
  if (slot) {
    void *resource = BB_POINTER(slot);
    BB_SET_POINTER(entity + 0x58, resource);
    return (uint32_t)(uintptr_t)resource;
  }
  return kind;
}

/* 0x00434aa0..0x00434aed: shift twelve packed 16-bit screen coordinates
 * for each visible entity in list order. Read links after the updates.
 * The flag load changes only AL; a shifted entity leaves EAX zero.
 */
uint32_t bb_entity_translate_c(void *object, uint32_t eax, uint32_t dx,
                               uint32_t dy) {
  uint32_t node = BB_U32(bb_address_encode(object) + 0x1008);
  while (node) {
    uint32_t flag = BB_U8(node + 0x128);
    eax = (eax & 0xffffff00) | flag;
    if (flag) {
      for (uint32_t i = 0; i < 3; ++i) {
        uint32_t row = node + 0xa6 + i * 2;
        BB_U16(row) += (uint16_t)dx;
        BB_U16(row + 6) += (uint16_t)dy;
        BB_U16(row + 12) += (uint16_t)dx;
        BB_U16(row + 18) += (uint16_t)dy;
      }
      eax = 0;
    }
    node = BB_U32(node + 4);
  }
  return eax;
}

/* 0x00433b90..0x00433b9c: snapshot the current rendering-list head. */
uint32_t BB_THISCALL bb_entity_begin(void *object) {
  uint32_t head = BB_U32(bb_address_encode(object) + 0x1008);
  BB_U32(bb_address_encode(object) + 0x100c) = head;
  return head;
}

/* 0x00434830..0x00434a97: project entities in the inclusive depth range,
 * retain two previous rectangles, and refresh/decay visibility. Arithmetic
 * wraps before fixed-point truncation; packed bounds wrap again at 16 bits.
 */
uint32_t BB_THISCALL
bb_entity_project(void *object, uint32_t start, uint32_t extent) {
  uint32_t node = BB_U32(bb_address_encode(object) + 0x1008);
  while (node && BB_I32(node + 0x86) < (int32_t)start) {
    volatile bb_memory_u8 *visible = BB_SPAN(node + 0x128, 1);
    if (*visible)
      --*visible;
    node = BB_U32(node + 4);
  }
  uint32_t eax = 0;
  while (node) {
    volatile bb_memory_u8 *visible = BB_SPAN(node + 0x128, 1);
    eax = BB_U32(node + 0x86);
    if ((int32_t)eax <= (int32_t)(start + extent)) {
      uint32_t x = BB_U32(node + 0x62), y = BB_U32(node + 0x66);
      uint32_t projected_x = (int32_t)bb_fixed_multiply(x - y, 0x270000) >> 16;
      BB_U32(node + 0x120) =
          projected_x - BB_U32(0x006024b8) + BB_U32(0x005db014);
      uint32_t projected_y =
          (int32_t)bb_fixed_multiply(BB_U32(node + 0x62) + BB_U32(node + 0x66),
                                     0x140000) >>
          16;
      uint32_t height =
          (int32_t)bb_fixed_multiply(BB_I32(node + 0x6a), 0xb0000) >> 16;
      BB_U32(node + 0x124) =
          projected_y - height - BB_U32(0x00602484) + BB_U32(0x006a9078) - 1;
      for (uint32_t side = 0; side < 4; ++side)
        BB_U16(node + 0xa6 + side * 6) =
            BB_U16(node + 0xa8 + side * 6);
      for (uint32_t side = 0; side < 4; ++side)
        BB_U16(node + 0xa8 + side * 6) =
            BB_U16(node + 0xbe + side * 2);
      uint32_t sprite = BB_U32(node + 0x58);
      BB_U16(node + 0xbe) =
          BB_U16(node + 0xc6) -
          BB_U16(sprite + 0x54) + BB_U32(node + 0x120);
      sprite = BB_U32(node + 0x58);
      BB_U16(node + 0xc0) =
          BB_U16(node + 0xc8) -
          BB_U16(sprite + 0x58) + BB_U32(node + 0x124);
      sprite = BB_U32(node + 0x58);
      BB_U16(node + 0xc2) =
          BB_U16(node + 0xca) -
          BB_U16(sprite + 0x54) +
          BB_U16(sprite + 0x44) + BB_U32(node + 0x120);
      sprite = BB_U32(node + 0x58);
      BB_U16(node + 0xc4) =
          BB_U32(sprite + 0x48) - BB_U16(sprite + 0x58) +
          BB_U16(node + 0xcc) + BB_U32(node + 0x124);
      eax = BB_U32(node + 0x9c);
      if (eax & 0x800) {
        eax = BB_U32(BB_U32(node + 0x58) + 0x58);
        eax = (eax & 0xffff0000) |
              (uint16_t)(eax + BB_U16(node + 0xc4));
        BB_U16(node + 0xc4) = eax;
      }
      uint32_t flag = BB_U8(node + 0x104);
      eax = (eax & 0xffffff00) | flag;
      if (flag) {
        BB_U16(node + 0xc2) += 10;
        BB_U16(node + 0xc0) -= 10;
      }
      uint32_t kind = BB_U8(node + 0xf2);
      eax = (eax & 0xffffff00) | kind;
      if (kind == 6 || kind == 9 || kind == 10)
        BB_U16(node + 0xc0) -= 20;
      int shown = 0;
      if (BB_I16(node + 0xc2) > 0) {
        eax = (int32_t)BB_I16(node + 0xbe);
        if ((int32_t)eax < BB_I32(0x00484444) &&
            BB_I16(node + 0xc4) > 0) {
          eax = (int32_t)BB_I16(node + 0xc0);
          shown = (int32_t)eax < BB_I32(0x00484448);
        }
      }
      if (shown) {
        eax = (eax & 0xffffff00) |
              BB_U8(0x00484454);
        *visible = eax;
        node = BB_U32(node + 4);
        continue;
      }
    }
    eax = (eax & 0xffffff00) | *visible;
    if (*visible) {
      eax = (eax & 0xffffff00) | (uint8_t)(eax - 1);
      *visible = eax;
    }
    node = BB_U32(node + 4);
  }
  return eax;
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_SPRITE_ASM "_bb_entity_sprite"
#define BB_TRANSLATE_ASM "_bb_entity_translate_c"
#define BB_BEGIN_ASM "_bb_entity_begin"
#define BB_PROJECT_ASM "_bb_entity_project"
#else
#define BB_SPRITE_ASM "bb_entity_sprite"
#define BB_TRANSLATE_ASM "bb_entity_translate_c"
#define BB_BEGIN_ASM "bb_entity_begin"
#define BB_PROJECT_ASM "bb_entity_project"
#endif
void __attribute__((naked)) bb_entity_sprite_entry(void) {
  __asm__ volatile("pushl %edx\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_SPRITE_ASM "\n\t"
                   "popl %ecx\n\t"
                   "popl %edx\n\t"
                   "ret");
}
void __attribute__((naked)) bb_entity_translate_entry(void) {
  __asm__ volatile("cmpl $0, 0x1008(%ecx)\n\t"
                   "je 1f\n\t"
                   "pushl %ecx\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl %eax\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_TRANSLATE_ASM "\n\t"
                   "addl $16, %esp\n\t"
                   "movl 8(%esp), %edx\n\t"
                   "movl 12(%esp), %ecx\n\t"
                   "addl $4, %esp\n"
                   "1: ret $8");
}
void __attribute__((naked)) bb_entity_begin_entry(void) {
  __asm__ volatile("pushl %edx\n\tpushl %ecx\n\t"
                   "call " BB_BEGIN_ASM "\n\t"
                   "popl %ecx\n\tpopl %edx\n\tret");
}
void __attribute__((naked)) bb_entity_project_entry(void) {
  __asm__ volatile("cmpl $0, 0x1008(%ecx)\n\tjne 1f\n\t"
                   "xorl %eax, %eax\n\tmovl 4(%esp), %ecx\n\tret $8\n"
                   "1: jmp " BB_PROJECT_ASM);
}

#endif
