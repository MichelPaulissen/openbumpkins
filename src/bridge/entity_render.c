/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include <signal.h>
#include <stdlib.h>
#endif

#define BYTE(address) BB_U8(address)
#define SHORT(address) BB_I16(address)

static uint32_t draw(uint32_t slot, uint32_t node, uint32_t frame,
                     uint32_t y_offset) {
  return bb_sprite_draw(BB_POINTER(slot), BB_U32(node + 0x120),
                        BB_U32(node + 0x124) + y_offset, frame, 1);
}
static uint32_t centered(uint32_t slot, uint32_t node, uint32_t frame) {
  /* Each signed bound is halved separately, truncating toward zero. */
  int32_t y = SHORT(node + 0xc4) / 2 + SHORT(node + 0xc0) / 2;
  int32_t x = SHORT(node + 0xbe) / 2 + SHORT(node + 0xc2) / 2;
  return bb_sprite_draw(BB_POINTER(slot), x, y, frame, 1);
}
static void set_frame(uint32_t slot, uint32_t frame) {
  BB_U32(BB_U32(slot) + 0x50) = frame;
}
static void layer(uint32_t slot, uint32_t node, uint32_t frame,
                  uint32_t x_offset, uint32_t y_offset) {
  bb_sprite_draw(BB_POINTER(slot), BB_U32(node + 0x120) + x_offset,
                 BB_U32(node + 0x124) + y_offset, frame, 1);
}
static void use_sprite(uint32_t node, uint32_t slot) {
  void *sprite = BB_POINTER(slot);
  if (BB_POINTER(node + 0x58) != sprite)
    bb_entity_set_sprite((void *)(uintptr_t)node, sprite);
}
/* Building damage uses one shared animation, followed by either one flame
 * or three independently positioned flames. Re-read positions/resources
 * after each draw, without resetting the current frame between layers. */
static void fire_frame(uint32_t node) {
  set_frame(0x005c5b6c, (BYTE(node + 0xfd) % 21) / 3);
}
static void fire_three(uint32_t node, uint32_t x_offset) {
  if (BB_U32(node + 0x9c) & 0x80000) {
    fire_frame(node);
    layer(0x005c5b6c, node, 255, x_offset, 0);
    layer(0x005c5b6c, node, 255, x_offset - 25, 0u - 10);
    layer(0x005c5b6c, node, 255, x_offset + 10, 0u - 40);
  }
}
static void fire_one(uint32_t node, uint32_t y_offset) {
  if (BB_U32(node + 0x9c) & 0x80000) {
    fire_frame(node);
    layer(0x005c5b6c, node, 255, 0, y_offset);
  }
}
static void alternate_layer(uint32_t node, uint32_t environment_slot,
                            uint32_t flag_slot, uint32_t flag_frame) {
  if (BB_U32(0x00484288))
    layer(environment_slot, node, 255, 0, 0);
  else if (BB_U32(node + 0x9c) & 0x20000)
    layer(flag_slot, node, flag_frame, 0, 0);
}
/* Preserve the original DIV fault for a zero frame count. */
static uint32_t frame_remainder(uint32_t value, uint32_t divisor) {
#ifdef BB_MODEL_RUNTIME
  if (!divisor) {
    raise(SIGFPE);
    abort();
  }
  return value % divisor;
#else
  uint32_t result;
  __asm__ volatile("xorl %%edx, %%edx\n\tdivl %2"
                   : "+a"(value), "=&d"(result)
                   : "rm"(divisor)
                   : "cc");
  return result;
#endif
}

/* Direction rounding deliberately does not wrap the byte before shifting:
 * values 240..255 select direction eight in most unit animations. */
static uint32_t direction(uint32_t node) {
  return (BYTE(node + 0xe5) + 16) >> 5;
}
static uint32_t walk_frame(uint32_t node) {
  return direction(node) * 6 + (BYTE(node + 0xea) >> 1);
}

/* 0x0043c20f..0x0043cdc9: the second unit family uses its part byte for
 * poses and moving variants. All primary draws re-read the entity sprite;
 * part 0x12 also has an environment-gated, explicit-direction overlay. */
static void unit_68(uint32_t node) {
  uint32_t subtype = BYTE(node + 0xf2), part = BYTE(node + 0xf4);
  uint32_t slot = 0;
  uint32_t frame = 0;
  int write_frame = 1;
  if (subtype == 0) {
    switch (part) {
    case 0x0f:
    case 0x16:
      slot = part == 0x0f ? 0x005c77b0 : 0x006a8f14;
      frame = direction(node) * 4 + ((BYTE(node + 0xea) & 12) >> 2);
      break;
    case 0x12:
      use_sprite(node, 0x005f6f30);
      set_frame(node + 0x58,
                direction(node) * 6 + ((BYTE(node + 0xea) >> 2) % 6));
      layer(node + 0x58, node, 255, 0, 0);
      if (BB_U32(0x00484288))
        layer(0x005e47a4, node, direction(node), 0, 0);
      goto finish;
    case 0x17:
    case 0x1d:
      if (!BYTE(node + 0x11d)) {
        slot = part == 0x17 ? 0x006ab580 : 0x006a98c0;
        frame = direction(node);
      } else {
        slot = part == 0x17 ? 0x0050fa98 : 0x005fe3ec;
        frame = direction(node) * 4 + ((BYTE(node + 0xea) & 12) >> 2);
      }
      break;
    case 0x18:
    case 0x19:
    case 0x1c:
      slot = part == 0x18 ? 0x0069c73c : part == 0x19 ? 0x00559a50 : 0x006ad1b4;
      frame = direction(node) * 6 + BYTE(node + 0xea) % 6;
      break;
    case 0x1a:
      slot = 0x0060218c;
      if (!BB_U32(node + 0x6e) && !BB_U32(node + 0x72))
        frame = BB_U32(node + 0x9c) & 0x10000
                    ? 54
                    : 48 + (BYTE(node + 0xfd) >> 1) % 6;
      else
        frame = direction(node) * 6 + (BYTE(node + 0xfd) >> 1) % 6;
      break;
    case 0x1b:
      if (!BYTE(node + 0x11d)) {
        slot = 0x005ecf88;
        write_frame = 0;
      } else {
        slot = 0x005f6e08;
        frame = direction(node) * 6 + (BYTE(node + 0xea) >> 1) % 6;
      }
      break;
    case 0x1e:
      if (!BYTE(node + 0x11d)) {
        slot = 0x0050fa9c;
        write_frame = 0;
      } else {
        slot = 0x006a8ed4;
        frame = direction(node) * 6 + BYTE(node + 0xea) % 6;
      }
      break;
    case 0x1f:
      slot = 0x005db040;
      frame = BYTE(node + 0xea);
      break;
    default:
      goto finish;
    }
  } else if (subtype == 7) {
    switch (part) {
    case 0x0f:
    case 0x16:
      slot = part == 0x0f ? 0x004eebc0 : 0x006a8f14;
      frame = direction(node) * 2 + BYTE(node + 0xf9);
      break;
    case 0x12:
      slot = 0x005f9198;
      frame = direction(node) * 4 + BYTE(node + 0xf9);
      break;
    case 0x17:
    case 0x1d:
      if (!BYTE(node + 0xf9)) {
        slot = part == 0x17 ? 0x0050fa98 : 0x005fe3ec;
        frame = direction(node) * 4;
      } else {
        slot = part == 0x17 ? 0x0063f06c : 0x006a9040;
        frame = direction(node);
      }
      break;
    case 0x18:
    case 0x1c:
      if (BYTE(node + 0xea) > 11) {
        slot = part == 0x18 ? 0x0069c73c : 0x006ad1b4;
        frame = direction(node) * 6 + BYTE(node + 0xea) % 6;
      } else {
        slot = part == 0x18 ? 0x005e4764 : 0x006a9928;
        frame = direction(node) * 6 + BYTE(node + 0xf9);
      }
      break;
    case 0x19:
      if (BYTE(node + 0xf9) == 1) {
        slot = 0x005590d4;
        frame = direction(node);
      } else {
        slot = 0x00559a50;
        frame = direction(node) * 6 + BYTE(node + 0xea) % 6;
      }
      break;
    case 0x1a:
      slot = 0x0060218c;
      frame = direction(node) * 6 + (BYTE(node + 0xfd) >> 1) % 6;
      break;
    case 0x1b:
      if (BYTE(node + 0xf9) > 3) {
        slot = 0x005f6e08;
        frame = direction(node) * 6 + BYTE(node + 0xea) % 6;
      } else {
        slot = 0x00602320;
        frame = direction(node) * 4 + BYTE(node + 0xf9);
      }
      break;
    case 0x1e:
      if (BYTE(node + 0xf9) < 7) {
        slot = 0x0069c744;
        frame = direction(node) * 7 + BYTE(node + 0xf9);
      } else {
        slot = 0x006a8ed4;
        frame = direction(node) * 6;
      }
      break;
    case 0x1f:
      if (BYTE(node + 0xf9) == 0 || BYTE(node + 0xf9) > 7) {
        slot = 0x005db040;
        frame = 0;
      } else {
        slot = 0x005cd7b8;
        frame = BYTE(node + 0xf9) - 1;
      }
      break;
    default:
      goto finish;
    }
  } else
    goto finish;
  use_sprite(node, slot);
  if (write_frame)
    set_frame(node + 0x58, frame);
  layer(node + 0x58, node, 255, 0, 0);
finish:
  fire_one(node, 0);
}

/* Stationary poses of kind 0x67. Preceding faction/item draws can change
 * the live carried-item and part bytes before pose selection. */
static void unit_67_idle(uint32_t node) {
  if (BB_U32(0x00484288) && BYTE(node + 0xf7) == BYTE(0x004843f0))
    layer(0x006b08b8, node, 255, 0, 0);
  if (BYTE(node + 0xfb) == 15)
    layer(0x006ad1e4, node, BYTE(node + 0xfd) >> 1, 0, 0);
  uint32_t carried = BYTE(node + 0xfb), part = BYTE(node + 0xf4);
  uint32_t slot = 0;
  int mode = 1, entity_frame = 0, replace = 1;
  /* mode 0: explicit activity frame; 1: six frames per direction;
   * 2: retain current frame; 3: constant 42; 4: fd >> 2;
   * 5: four-direction animation with activity divided by three. */
  if (carried == 6 || carried == 2 || carried == 4 || carried == 10)
    slot = part == 1 ? 0x005590dc : 0x005f91a4;
  else if (carried == 8)
    slot = part == 1 ? 0x005d19c4 : 0x006ad1a0;
  else if (carried == 19) {
    slot = 0x0060246c;
    replace = 0;
  } else {
    static const uint32_t stationary[22] = {
        0,          0x005f90cc, 0x006a9940, 0x005e4790, 0x005db048, 0x0060462c,
        0x005f6e0c, 0x0063f040, 0x005db17c, 0x006a9898, 0x005e4718, 0x005cf3e4,
        0x0063f0a8, 0x00602474, 0x00662c54, 0,          0x006af0f0, 0x00600150,
        0,          0x006ad250, 0x006aff60, 0x006ab588};
    static const uint32_t moving[22] = {
        0,          0x00602300, 0x005e42b8, 0x005f9160, 0x00559120, 0x005cd7bc,
        0x005f9188, 0x005fe400, 0x0069c740, 0x005fe3d8, 0x0063f08c, 0x005cf3e4,
        0x0063f0a8, 0x00663528, 0x005c9500, 0,          0x006af0f0, 0x00600150,
        0,          0x00663520, 0x005eaca0, 0x005c7780};
    if (part == 32 || part == 33) {
      slot = part == 32 ? 0x006afe08 : 0x005f51e4;
      entity_frame = 1;
      if (!BYTE(node + 0x11d))
        mode = 3;
    } else if (part < 22 && stationary[part]) {
      int moving_pose = BYTE(node + 0x11d) != 0;
      slot = moving_pose ? moving[part] : stationary[part];
      if (part >= 1 && part <= 3 && BYTE(node + 0x106)) {
        static const uint32_t special[] = {0, 0x00600148, 0x005cd7c8,
                                           0x00602478};
        slot = special[part];
        moving_pose = 1;
      }
      if (part == 16) {
        mode = 5;
        entity_frame = 1;
      } else if (part == 11 || part == 12 || part == 17)
        mode = 1;
      else if (!moving_pose) {
        if (part == 19)
          mode = 2;
        else if (part == 20 || part == 21) {
          mode = 4;
          entity_frame = 1;
        } else
          mode = 0;
      } else if (part == 21)
        entity_frame = 1;
    } else
      return;
  }
  if (replace)
    use_sprite(node, slot);
  uint32_t frame = 255;
  uint32_t frame_slot = entity_frame ? node + 0x58 : slot;
  switch (mode) {
  case 0:
    frame = BYTE(node + 0xea);
    break;
  case 1:
    set_frame(frame_slot, walk_frame(node));
    break;
  case 2:
    break;
  case 3:
    set_frame(frame_slot, 42);
    break;
  case 4:
    set_frame(frame_slot, BYTE(node + 0xfd) >> 2);
    break;
  case 5:
    set_frame(frame_slot, direction(node) * 4 + ((BYTE(node + 0xea) / 3) & 3));
    break;
  }
  layer(entity_frame ? node + 0x58 : slot, node, frame, 0, 0);
}

/* 0x0043a56a..0x0043c20e: first unit family, including stationary,
 * moving, work, death and special poses plus shared extra/damage layers. */
static void unit_67(uint32_t node) {
  uint32_t subtype = BYTE(node + 0xf2), part = BYTE(node + 0xf4);
  uint32_t slot = 0;
  int entity_frame = 0;
  uint32_t frame = 0, y_offset = 0;
  switch (subtype) {
  case 0:
    unit_67_idle(node);
    goto finish;
  case 3:
  case 4:
  case 5:
  case 6: {
    static const uint32_t choices[4][2] = {{0x005590e0, 0x00604628},
                                           {0x005f51e0, 0x005133f4},
                                           {0x0063f09c, 0x005e4780},
                                           {0x00559a54, 0x006ad1a4}};
    slot = choices[subtype - 3][part == 1];
    frame = BYTE(node + 0xf9) >> 2;
    break;
  }
  case 7:
    switch (part) {
    case 1:
    case 2:
      slot = part == 1 ? 0x005db174 : 0x0063f04c;
      entity_frame = 1;
      frame = direction(node) * 5 + BYTE(node + 0xf9);
      break;
    case 6:
      slot = 0x006af0ec;
      frame = (BYTE(node + 0xea) >> 2) + (BYTE(node + 0xe5) >> 6) * 6;
      break;
    case 7:
      slot = 0x006af0e0;
      frame = direction(node) * 6 + BYTE(node + 0xf9);
      break;
    case 8:
      slot = BYTE(node + 0xf9) < 7 ? 0x0063f088 : 0x0069c740;
      entity_frame = 1;
      frame = BYTE(node + 0xf9) < 7 ? direction(node) * 7 + BYTE(node + 0xf9)
                                    : direction(node) * 6;
      break;
    case 9:
      slot = 0x006ad1b0;
      entity_frame = 1;
      frame = direction(node);
      break;
    case 16:
      slot = BYTE(node + 0xf9) < 5 ? 0x006ad1a8 : 0x006af0f0;
      entity_frame = 1;
      frame = BYTE(node + 0xf9) < 5 ? direction(node) * 5 + BYTE(node + 0xf9)
                                    : direction(node) * 4;
      break;
    case 19:
      slot = 0x00602438;
      frame = (direction(node) & 7) * 4 + BYTE(node + 0xf9);
      break;
    default:
      layer(node + 0x58, node, 255, 0, 0);
      goto finish;
    }
    break;
  case 9:
  case 10:
    slot = subtype == 9 ? (part == 1 ? 0x00602290 : 0x00602188)
                        : (part == 1 ? 0x00602328 : 0x005c77bc);
    entity_frame = 1;
    frame = (BYTE(node + 0xf9) >> 2) + (part != 1);
    y_offset = 0u - 20;
    break;
  case 11:
    slot = part == 4 ? 0x00602198 : 0x005c7788;
    frame = ((BYTE(node + 0xe5) >> 4) & ~3u) + (BYTE(node + 0xf9) >> 2);
    break;
  case 12:
    slot = part == 1 ? 0x005c78e0 : 0x005db178;
    frame = BYTE(node + 0xf9) >> 2;
    break;
  case 13:
  case 14:
    if (subtype == 13 || part == 3) {
      use_sprite(node, 0x005f9168);
      set_frame(0x005f9168, BYTE(node + 0xf9) >> 2);
      layer(0x005f9168, node, 255, 0, 0);
      set_frame(0x005e4788, BB_U32(BB_U32(0x005f9168) + 0x50));
      layer(0x005e4788, node, 255, 0, 0);
      goto finish;
    }
    if (part != 9)
      goto finish;
    slot = 0x00553f88;
    frame = BYTE(node + 0xf9) >> 2;
    break;
  case 16:
    slot = 0x006033cc;
    entity_frame = 1;
    frame = (BYTE(node + 0xe5) >> 7) + (part == 2   ? 8
                                        : part == 4 ? 0
                                        : part == 5 ? 2
                                                    : 6);
    break;
  case 0x5d:
    slot = 0x00602488;
    frame = BYTE(node + 0xf9) >> 2;
    break;
  default:
    goto finish;
  }
  use_sprite(node, slot);
  set_frame(entity_frame ? node + 0x58 : slot, frame);
  layer(entity_frame ? node + 0x58 : slot, node, 255, 0, y_offset);
finish:
  if (BYTE(node + 0x11e)) {
    set_frame(0x00602460, (BYTE(node + 0xfd) % 18) / 3);
    layer(0x00602460, node, 255, 0, 0);
  }
  fire_one(node, 0);
}

/* Kind 0x3a chooses its character resource from subtype and activity.
 * Sprite replacements mark the entity dirty. Some branches draw an explicit
 * frame, others update the resource's current frame and pass 255. */
static void character_layers(uint32_t node) {
  uint32_t subtype = BYTE(node + 0xf2), frame = 255;
  uint32_t slot = 0;
  int current = 0;
  switch (subtype) {
  case 0x1b:
    slot = 0x005e4730;
    switch (BYTE(node + 0xea)) {
    case 2:
      slot = 0x005f6e04;
      break;
    case 3:
      slot = 0x00513450;
      break;
    case 4:
      slot = 0x005db168;
      break;
    case 5:
      slot = 0x005e5dd8;
      break;
    case 6:
      slot = 0x005e42a8;
      break;
    case 7:
      slot = 0x0063f044;
      break;
    case 8:
      slot = 0x0063f038;
      break;
    case 9:
      slot = 0x005ecfb0;
      break;
    case 0x0b:
      slot = 0x0050faa0;
      break;
    case 0x0c:
      slot = 0x005d19cc;
      break;
    case 0x0d:
      slot = 0x00662c6c;
      break;
    case 0x0e:
      slot = 0x00602480;
      break;
    case 0x10:
      slot = 0x0060466c;
      break;
    case 0x13:
      slot = 0x00602444;
      break;
    case 0x14:
      slot = 0x005d19bc;
      break;
    case 0x20:
      slot = 0x005f51c8;
      break;
    case 0x21:
      slot = 0x005e42b0;
      break;
    }
    use_sprite(node, slot);
    layer(node + 0x58, node, BYTE(node + 0xf9) >> 2, 0, 0);
    slot = 0;
    break;
  case 0x1c:
    slot = 0x006045e8;
    use_sprite(node, slot);
    set_frame(slot, 0);
    break;
  case 0x1d:
    slot = 0x005cf3e0;
    break;
  case 0x1e:
    slot = 0x00604628;
    use_sprite(node, slot);
    set_frame(slot, BB_U32(BB_U32(slot) + 0x4c) - 1);
    break;
  case 0x1f:
    slot = 0x0066352c;
    current = 1;
    break;
  case 0x21:
    slot = 0x006b08a0;
    current = 2;
    break;
  case 0x22:
    slot = 0x006aff58;
    current = 2;
    break;
  case 0x23:
    slot = 0x006ad1bc;
    current = 2;
    break;
  case 0x24:
    slot = 0x0060469c;
    current = 2;
    break;
  case 0x25:
    slot = 0x006a9084;
    break;
  case 0x62:
    slot = BYTE(node + 0xea) == 23 ? 0x006a8ee4 : 0x0063f068;
    current = 1;
    break;
  case 0x63:
    slot = BYTE(node + 0xea) == 24 ? 0x005fe3fc : 0x00559118;
    current = 1;
    break;
  case 0x68:
    slot = 0x005c7794;
    current = 1;
    break;
  case 0x69:
    slot = 0x005fe3a4;
    break;
  case 0x71:
    slot = 0x00604660;
    break;
  case 0x72:
    slot = 0x00602274;
    current = 1;
    break;
  case 0x76:
    slot = 0x005d1138;
    current = 1;
    break;
  case 0x77:
    slot = 0x0063f050;
    current = 1;
    break;
  }
  if (slot) {
    use_sprite(node, slot);
    if (current)
      set_frame(current == 1 ? node + 0x58 : slot, BYTE(node + 0xf9) >> 2);
    /* The fixed-resource branches retain their resource even if the entity
     * sprite changes; the other branches re-read the entity's sprite. */
    if (subtype == 0x1d || subtype == 0x69 || subtype == 0x71 || current == 1)
      slot = node + 0x58;
    layer(slot, node, frame, 0, 0);
  }
  if (BYTE(node + 0x11e)) {
    set_frame(0x00602460, (BYTE(node + 0xfd) % 18) / 3);
    layer(0x00602460, node, 255, 0, 0);
  }
  fire_one(node, 0);
}

/* 0x0043de2b..0x0043e7d8: shared final layers for every entity kind.
 * Reset conditional blending, draw indicators/status, draw faction-selected
 * markers, restore the player color and draw the current command's cursor.
 * Unrecognized statuses still draw the status resource's previous frame.
 */
uint32_t BB_THISCALL bb_entity_render_finish(void *object) {
  uint32_t node = bb_address_encode(object);
  if (BB_U32(node + 0x9c) & 0x2000000)
    BYTE(0x004842c0) = 0;
  if (BYTE(node + 0x104))
    draw(0x006afd04, node, 255, 0);
  int status_shown = 0;
  if (BYTE(node + 0x105)) {
    status_shown = 1;
    if (BB_U32(node + 0x9c) & 0x40000000)
      set_frame(0x006b0898, 12);
    else if (BYTE(node + 0x12f) == 1)
      set_frame(0x006b0898, 13);
    else if (BYTE(node + 0x12f) == 2)
      set_frame(0x006b0898, 11);
  } else {
    uint32_t status = BYTE(node + 0x11a);
    if (status) {
      status_shown = 1;
      uint32_t frame = 0;
      int recognized = 1;
      switch (status) {
      case 0x9f:
        frame = 6;
        break;
      case 0xac:
        frame = 5;
        break;
      case 0xae:
        frame = 9;
        break;
      case 0xb1:
        frame = 7;
        break;
      case 0xb2:
        frame = 3;
        break;
      case 0xc0:
        frame = 0;
        break;
      case 0xc1:
        frame = 1;
        break;
      case 0xc6:
        frame = 2;
        break;
      case 0xc8:
        frame = 4;
        break;
      case 0xd3:
        frame = 10;
        break;
      case 0xd4:
        frame = 8;
        break;
      default:
        recognized = 0;
        break;
      }
      if (recognized)
        set_frame(0x006b0898, frame);
    }
  }
  if (status_shown)
    draw(0x006b0898, node, 255, 0u - 34);
  uint32_t selected_mask = BYTE(node + 0x12d);
  if (selected_mask && (selected_mask & (1u << (BYTE(0x004843f0) & 31))))
    bb_entity_selection(object);
  uint32_t palette = BB_U32(0x004844a8);
  uint32_t color = BYTE(palette + 0x49);
  BYTE(0x004842bc) = color;
  if (node != BB_U32(0x00484488))
    return (palette & 0xffffff00) | color;
  uint32_t command = BYTE(0x005f746c);
  uint32_t slot = 0;
  uint32_t frame = 255, animation = 0;
  int animated = 1;
  switch (command) {
  case 0x20: {
    uint32_t kind = BYTE(node + 0xd4);
    if (BB_U32(0x00663578 + kind * 914))
      return centered(0x005590cc, node, 255);
    slot = kind == 0x46 ? 0x006aff28 : 0x00604668;
    animation = BB_U32(0x0048445c);
    break;
  }
  case 0x22:
  case 0x27:
  case 0x29:
  case 0x30:
    slot = 0x006afcec;
    animation = BB_U32(0x00484464);
    break;
  case 0x23: {
    uint32_t kind = BYTE(node + 0xd4);
    if (BB_U32(0x00663578 + kind * 914)) {
      slot = 0x00602314;
      animation = BB_U32(0x00484460);
    } else {
      slot = 0x005db18c;
      animation = 0;
    }
    break;
  }
  case 0x24:
    slot = 0x00559b58;
    frame = BB_U32(0x00484464);
    animated = 0;
    break;
  case 0x25:
    slot = 0x005fd270;
    animation = BB_U32(0x00484464);
    break;
  case 0x26:
    slot = 0x006aff28;
    animation = BB_U32(0x0048445c);
    break;
  case 0x28:
    slot = 0x005f9180;
    animation = BB_U32(0x00484468);
    break;
  case 0x2a: {
    uint32_t kind = BYTE(node + 0xd4);
    if (!BB_U32(0x00663578 + kind * 914))
      return kind * 457;
    return centered(0x005590cc, node, 255);
  }
  case 0x2b:
    slot = 0x00604668;
    animation = BB_U32(0x0048445c);
    break;
  case 0x2c:
  case 0x2d:
    slot = command == 0x2c ? 0x006a98a8 : 0x006634f0;
    frame = BB_U32(0x00484464);
    animated = 0;
    break;
  case 0x2e:
  case 0x2f:
    slot = command == 0x2e ? 0x0063f02c : 0x005db18c;
    animation = BB_U32(0x00484464);
    break;
  default:
    return command - 32;
  }
  if (animated)
    set_frame(slot, animation);
  return centered(slot, node, frame);
}

/* Reconstructed primary layers for every byte kind. Read kind after reflection,
 * since its draw can change state before the original switch dispatches.
 */
static void primary_layers(uint32_t node) {
  uint32_t kind = BYTE(node + 0xd4), frame = 255;
  uint32_t slot = 0;
  if (kind > 0xe7)
    return;
  switch (kind) {
  case 0x67:
    unit_67(node);
    return;
  case 0x68:
    unit_68(node);
    return;
  case 0x01:
    layer(node + 0x58, node, 0, 0, 0);
    if (BB_U32(0x00484288)) {
      layer(0x005f9268, node, 255, 0, 0);
      layer(0x0060003c, node, 255, 0, 0);
    }
    if (BB_U32(node + 0x9c) & 0x20000)
      layer(0x005e479c, node, 0, 0, 0);
    fire_three(node, 0u - 50);
    return;
  case 0x02:
    layer(node + 0x58, node, 0, 0, 0);
    alternate_layer(node, 0x005ca304, 0x005db188, 0);
    if (BYTE(node + 0xf9))
      layer(0x005c778c, node, BYTE(node + 0xf9) - 1, 0u - 145, 0);
    if (BB_U32(0x00484768) == 3)
      layer(0x006a908c, node, BYTE(node + 0xfd) >> 2, 0, 0);
    fire_three(node, 0u - 50);
    return;
  case 0x05:
    if (BYTE(node + 0xf9))
      slot = 0x005f90d0;
    else
      slot = BB_U32(0x00484768) == 3 ? 0x00662c74 : node + 0x58;
    set_frame(slot, BYTE(node + 0xf9));
    break;
  case 0x06:
    if (BYTE(node + 0xf9)) {
      slot = 0x005f90d0;
      set_frame(slot, BYTE(node + 0xf9));
    } else if (BB_U32(0x00484768) == 3)
      slot = 0x006af0f8;
    break;
  case 0x0a: {
    if (!BB_U32(0x00484288))
      layer(node + 0x58, node, 255, 0, 0);
    layer(0x006634e8, node, 255, 54, 8);
    uint32_t sprite = BB_U32(0x006634e8);
    BB_U32(sprite + 0x50) = frame_remainder(BB_U32(sprite + 0x50) + 1,
                                            BB_U32(BB_U32(0x006634e8) + 0x4c));
    return;
  }
  case 0x19:
    layer(node + 0x58, node, 0, 0, 0);
    layer(0x006a904c, node, BYTE(node + 0xf9) >> 2, 0, 0u - 10);
    return;
  case 0x21:
    if (!BYTE(node + 0xf2)) {
      slot = 0x005c9550;
      frame = BYTE(node + 0xf9) >> 2;
    } else if (BYTE(node + 0xf2) == 25) {
      uint32_t animation = BYTE(node + 0xf9);
      set_frame(node + 0x58,
                (animation >> 2) +
                    (animation < 6 ? 0 : (BYTE(node + 0xe5) >> 6) * 24));
    } else if (BYTE(node + 0xf2) == 26)
      frame = 27;
    else
      return;
    break;
  case 0x24:
    if (!BYTE(node + 0xf2)) {
      if (BYTE(node + 0xf7) != BYTE(0x004843f0))
        return;
      set_frame(node + 0x58, 0);
    } else if (BYTE(node + 0xf2) == 18)
      set_frame(node + 0x58, BYTE(node + 0xf9) >> 1);
    else
      return;
    break;
  case 0x25:
    if (!BYTE(node + 0xea) && BYTE(node + 0xf7) != BYTE(0x004843f0))
      return;
    set_frame(node + 0x58, BYTE(node + 0xf9) >= 60);
    break;
  case 0x2a:
  case 0x2b: {
    uint32_t state = BYTE(node + 0xea);
    if (state > 2)
      return;
    set_frame(0x005e42c4, state == 1 ? 3 : BYTE(node + 0xf9) >> 2);
    set_frame(0x006634d0, state == 1 ? 3 : BYTE(node + 0xf9) >> 2);
    if (kind == 0x2a && state == 1)
      set_frame(0x005f9258, BYTE(node + 0xf9));
    layer(0x005e42c4, node, 255, 0, 0);
    if (state == 1) {
      if (kind == 0x2a)
        layer(0x005f9258, node, 255, 0, 0u - 80);
      else if (BYTE(node + 0xf2) == 17) {
        set_frame(0x0060244c, BYTE(node + 0xf9) >> 2);
        layer(0x0060244c, node, 255, 0, 0u - 80);
      }
    }
    layer(0x006634d0, node, 255, 0, 0u - 80);
    return;
  }
  case 0x2f:
    set_frame(node + 0x58, 0);
    layer(node + 0x58, node, 255, 0, 0);
    alternate_layer(node, 0x006ad1b8, 0x005fe3e0, 0);
    return;
  case 0x33:
    layer(node + 0x58, node, 0, 0, 0);
    alternate_layer(node, 0x0060243c, 0x005117d8, 0);
    fire_three(node, 0u - 50);
    if (BYTE(node + 0xea) != 1)
      layer(0x006aff4c, node, 0, 0, 0);
    switch (BYTE(node + 0xea)) {
    case 1:
      slot = 0x006afbd0;
      frame = BYTE(node + 0xf9);
      break;
    case 2:
      slot = 0x006aff4c;
      frame = BYTE(node + 0xf9);
      break;
    case 3:
      slot = 0x005d19d0;
      frame = 0;
      break;
    case 4:
      slot = 0x0063f090;
      frame = 0;
      break;
    default:
      return;
    }
    break;
  case 0x35:
    layer(node + 0x58, node, 0, 0, 0);
    alternate_layer(node, 0x005e47a0, 0x006ab578, 0);
    layer(0x005116c8, node, (BYTE(node + 0xfd) & 12) >> 2, 0, 0);
    fire_three(node, 0u - 50);
    return;
  case 0x36:
    layer(node + 0x58, node, 0, 0, 0);
    alternate_layer(node, 0x006b0848, 0x006634f8, 255);
    if (BYTE(node + 0xf2) == 100) {
      uint32_t animation = BYTE(node + 0xf9);
      layer(animation < 3 ? 0x006634e4 : 0x006634dc, node,
            animation < 3 ? animation : animation - 3, 0, 0);
    }
    fire_three(node, 0u - 50);
    return;
  case 0x38:
  case 0xde:
    layer(0x005e4708, node, 0, 0, 0);
    frame = BYTE(node + 0xfd) >> 2;
    break;
  case 0x3a:
    character_layers(node);
    return;
  case 0x42:
    layer(node + 0x58, node, 0, 0, 0);
    if (BB_U32(0x00484288))
      layer(0x00662c60, node, 255, 0, 0);
    else
      layer(0x006a9050, node, 0, 0, 0);
    if (BB_U32(node + 0x9c) & 0x20000)
      layer(0x006a98c4, node, 0, 0, 0);
    fire_three(node, 0u - 50);
    return;
  case 0x45:
  case 0x91:
    layer(node + 0x58, node, 0, 0, 0);
    if (kind == 0x45) {
      layer(0x00602294, node, BYTE(node + 0xf9), 0, 0);
      alternate_layer(node, 0x006a8eec, 0x00604670, 0);
    } else
      alternate_layer(node, 0x005e4758, 0x005d1958, 255);
    fire_three(node, 0);
    return;
  case 0x46:
    if (!BYTE(node + 0xf2))
      slot = node + 0x58;
    else if (BYTE(node + 0xf2) == 21 || BYTE(node + 0xf2) == 22)
      slot = 0x00662c68;
    else
      return;
    set_frame(slot, BYTE(node + 0xe5) >> 6);
    break;
  case 0x48:
    if (BYTE(node + 0xf7) != BYTE(0x004843f0))
      return;
    break;
  case 0x4e:
    layer(node + 0x58, node, BYTE(node + 0xf9), 0, 0);
    fire_one(node, 20);
    return;
  case 0x51:
    set_frame(node + 0x58, 0);
    layer(node + 0x58, node, 255, 0, 0);
    slot = 0x00602298;
    set_frame(slot, (BYTE(node + 0xe5) + 8) >> 4);
    break;
  case 0x52:
    slot = 0x005cf3e8;
    set_frame(slot, BYTE(node + 0xf9) * 3);
    break;
  case 0x56:
    layer(node + 0x58, node, 0, 0, 0);
    if (BB_U32(0x00484288))
      layer(0x00600038, node, 255, 0, 0);
    return;
  case 0x5c:
    switch (BYTE(node + 0xf2)) {
    case 0:
    case 0x2c: {
      uint32_t subtype = BYTE(node + 0xf2);
      layer(0x0063f0c0, node, 0, 0, 0);
      slot = 0x006024c4;
      frame = subtype ? BYTE(node + 0xfd) >> 7 : 2;
      break;
    }
    case 0x2d:
      slot = 0x0063f0c0;
      frame = ((BYTE(node + 0xf9) & 4) >> 2) + 1;
      break;
    case 0x2e:
      slot = 0x0063f0c0;
      frame = ((BYTE(node + 0xf9) & 4) >> 2) + 3;
      break;
    case 0x2f:
      slot = 0x0063f0c0;
      frame = 2 - ((BYTE(node + 0xf9) & 4) >> 2);
      break;
    case 0x30: {
      static const uint32_t parts[] = {0x006a8f20, 0x005cd7c0, 0x006a8eac,
                                       0x006ab58c};
      layer(0x00602498, node, 0, 0, 0);
      uint32_t part = BYTE(node + 0xf4);
      if (part < 1 || part > 4)
        return;
      slot = parts[part - 1];
      frame = BYTE(node + 0xf9) >> 2;
      break;
    }
    case 0x31:
      slot = 0x00602498;
      frame = (BYTE(node + 0xfd) >> 2) % 3;
      break;
    default:
      return;
    }
    break;
  case 0x5d:
    if (BYTE(node + 0xf2) != 20 && BYTE(node + 0xf7) != BYTE(0x004843f0))
      return;
    use_sprite(node, 0x005f9164);
    layer(node + 0x58, node, 0, 0, 0);
    if (BYTE(node + 0xf2) != 20)
      return;
    use_sprite(node, 0x0060463c);
    frame = BYTE(node + 0xf9) >> 1;
    break;
  case 0x62:
    layer(node + 0x58, node, 0, 0, 0);
    slot = 0x006ad1e4;
    frame = BYTE(node + 0xfd) >> 1;
    break;
  case 0x6a:
    set_frame(node + 0x58, (BYTE(node + 0xe5) + 4) >> 3);
    layer(node + 0x58, node, 255, 0, 0);
    slot = 0x006a9948;
    set_frame(slot, (BYTE(node + 0xe8) + 4) >> 3);
    break;
  case 0x6e:
    if (BYTE(node + 0xf2) != 0x5e) {
      use_sprite(node, 0x006a98ac);
      set_frame(node + 0x58,
                ((BYTE(node + 0xe5) + 16) >> 5) * 7 + (BYTE(node + 0xea) >> 1));
      layer(node + 0x58, node, 255, 0, 0);
    }
    fire_one(node, 0);
    return;
  case 0x6f:
    layer(node + 0x58, node, 255, 0, 0);
    set_frame(0x005e4734, ((BYTE(node + 0xe5) >> 5) & ~1u) + BYTE(node + 0xea));
    layer(0x005e4734, node, 255, 0, 20);
    if (BYTE(node + 0xf2) == 0x5c)
      layer(0x005f9250, node, 0, 12, 20);
    fire_one(node, 20);
    return;
  case 0x72:
  case 0x73:
    if (!BYTE(node + 0xf2))
      frame = 0;
    else if (BYTE(node + 0xf2) == (kind == 0x72 ? 0x3d : 0x3f))
      frame = 5;
    else
      return;
    break;
  case 0x81:
    use_sprite(node, BB_U32(0x00484768) == 3 ? 0x005c78e8 : 0x006b0844);
    set_frame(node + 0x58, BYTE(node + 0xf9));
    break;
  case 0x8d:
    if (BYTE(node + 0xf2) != 0x4b)
      return;
    use_sprite(node, 0x005116c0);
    frame = BYTE(node + 0xf9);
    break;
  case 0x96:
    layer(node + 0x58, node, 0, 0, 0);
    fire_three(node, 0u - 50);
    return;
  case 0x98:
  case 0x9a:
  case 0x9d:
  case 0xcf: {
    uint32_t overlay = kind == 0x98   ? 0x00604644
                       : kind == 0x9a ? 0x005d19dc
                       : kind == 0x9d ? 0x005590c8
                                      : 0x005590c4;
    layer(node + 0x58, node, 0, 0, 0);
    if (BB_U32(node + 0x9c) & 0x20000)
      layer(overlay, node, 255, 0, 0);
    fire_three(node, kind == 0x9a ? 0u - 10 : 0u - 50);
    return;
  }
  case 0xab:
    if (BYTE(0x004843e8) != 1)
      return;
    break;
  case 0xb0: {
    uint32_t state = BYTE(node + 0xea);
    BYTE(node + 0xf9) = state < 7                    ? state
                        : state >= 16 && state <= 22 ? state - 9
                                                     : 32;
    if (BYTE(node + 0xf9) >= 32) {
      slot = 0x006ad1ec;
      break;
    }
    layer(0x005d195c, node, 255, 0, 6);
    uint32_t direction = (BYTE(node + 0xfd) & 28) >> 2;
    BYTE(node + 0xe5) = direction;
    static const int8_t offsets[] = {-5, -3, 0, 3, 5, 3, 0, -3};
    layer(node + 0x58, node, BYTE(node + 0xf9), 0,
          (uint32_t)offsets[direction]);
    return;
  }
  case 0xb3:
    frame = BB_U32(0x00484768) == 3;
    break;
  case 0xc2:
    use_sprite(node, BYTE(node + 0xea) == 0x24 ? 0x006ad1d4 : 0x00554fac);
    frame = BYTE(node + 0xfd) >> 2;
    break;
  case 0xc5:
    if (BYTE(node + 0xea) < 2)
      use_sprite(node, BYTE(node + 0xea) ? 0x005e4724 : 0x005d195c);
    break;
  case 0xc7:
    if (BYTE(node + 0xf2) == 0x6a)
      frame = 5;
    else if (BYTE(node + 0xf2) == 0x6c || BYTE(node + 0xf2) == 0x6d)
      frame = BYTE(node + 0xf9);
    else
      frame = 0;
    break;
  case 0xdd:
    layer(node + 0x58, node, BB_U32(0x00484768) == 3, 0, 0);
    if (!BYTE(node + 0xf5)) {
      slot = 0x005fe3dc;
      frame = BYTE(node + 0xf9);
    } else {
      slot = 0x005db018;
      set_frame(slot, (BYTE(node + 0xfd) & 12) >> 2);
    }
    break;
  case 0x03:
  case 0x04:
  case 0x08:
  case 0x09:
  case 0x0b:
  case 0x0c:
  case 0x0d:
  case 0x0e:
  case 0x0f:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x1b:
  case 0x1e:
  case 0x1f:
  case 0x20:
  case 0x23:
  case 0x27:
  case 0x28:
  case 0x2d:
  case 0x34:
  case 0x3b:
  case 0x3f:
  case 0x40:
  case 0x41:
  case 0x5b:
  case 0x61:
  case 0x65:
  case 0x69:
  case 0x6b:
  case 0x6c:
  case 0x6d:
  case 0x70:
  case 0x74:
  case 0x78:
  case 0x79:
  case 0x7a:
  case 0x7d:
  case 0x80:
  case 0x82:
  case 0x85:
  case 0x89:
  case 0x8b:
  case 0x8c:
  case 0x90:
  case 0xa5:
  case 0xa6:
  case 0xa7:
  case 0xa8:
  case 0xaa:
  case 0xb4:
  case 0xb5:
  case 0xb6:
  case 0xb7:
  case 0xb8:
  case 0xb9:
  case 0xba:
  case 0xbb:
  case 0xbc:
  case 0xbd:
  case 0xce:
    return;
  case 0x30:
  case 0x31:
  case 0x3c:
  case 0x3e:
  case 0x4b:
  case 0x4c:
  case 0x57:
  case 0x58:
  case 0x59:
  case 0x5a:
  case 0x63:
  case 0x8a:
  case 0x8e:
  case 0xa2:
  case 0xa3:
  case 0xbe:
  case 0xbf:
  case 0xd5:
  case 0xe2:
  case 0xe4:
    break;
  case 0x10:
  case 0x29:
  case 0x4f:
  case 0x87:
  case 0x88:
  case 0xc9:
  case 0xcd:
    frame = 0;
    break;
  case 0x2c:
    frame = 12;
    slot = 0x006afb30;
    break;
  case 0x83:
    frame = 1;
    break;
  case 0x07:
  case 0x26:
  case 0x39:
  case 0x8f:
  case 0xc3:
  case 0xca:
  case 0xcc:
  case 0xe6:
  case 0xe7:
  case 0x84:
    frame = BYTE(node + 0xf9) >> 2;
    break;
  case 0x32:
  case 0x44:
  case 0x94:
  case 0x95:
  case 0xa4:
  case 0xcb:
    frame = BYTE(node + 0xf9) >> 1;
    break;
  case 0x1a:
  case 0x37:
  case 0x4d:
  case 0x5e:
  case 0x5f:
  case 0x60:
  case 0x86:
  case 0x93:
  case 0xd2:
    frame = BYTE(node + 0xf9);
    break;
  case 0x54:
    slot = 0x005c9508;
    break;
  case 0x55:
    frame = BYTE(node + 0xf9);
    slot = 0x005f918c;
    break;
  case 0x9f:
  case 0xa0:
  case 0xac:
  case 0xae:
  case 0xb1:
  case 0xb2:
  case 0xc0:
  case 0xc1:
  case 0xc6:
  case 0xc8:
  case 0xd3:
  case 0xd4:
  case 0xa1:
  case 0xe0:
  case 0xe3:
    frame = BYTE(node + 0xfd) >> 2;
    break;
  case 0xad:
    frame = BYTE(node + 0xfd) >> 1;
    break;
  case 0xd7:
  case 0xd8:
  case 0xda:
  case 0xdc:
  case 0xe5:
    frame = BYTE(node + 0xea);
    break;
  case 0xdf:
    frame = ((BYTE(node + 0xe5) + 16) & 255) >> 5;
    break;
  case 0x9c:
    frame = (BYTE(node + 0xe5) + 16) >> 5;
    break;
  case 0xc4:
  case 0xd1:
    frame = ((BYTE(node + 0xfd) & 12) >> 2) +
            (((BYTE(node + 0xe5) + 16) & 255) >> 5) * 4;
    break;
  case 0x71:
  case 0x75:
  case 0x76:
  case 0x77:
  case 0x7b:
  case 0x7c:
  case 0x7e:
  case 0x7f:
  case 0x92:
  case 0x97:
  case 0x99:
  case 0x9b:
  case 0x9e:
  case 0xaf:
  case 0xd0:
  case 0xe1:
    if (BYTE(node + 0xf2) != 'C')
      return;
    frame = 1;
    break;
  case 0xd6:
    frame = BB_U32(0x00484768) == 3 ? 2 : 0;
    break;
  case 0xd9:
    frame = BB_U32(0x00484768) == 3;
    break;
  case 0x00:
    BB_U32(BB_U32(node + 0x58) + 0x50) =
        BB_U32(0x00484768) == 3 ? 1 : BYTE(node + 0xf9);
    break;
  case 0x18:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xea) >> 1;
    break;
  case 0x1c:
  case 0x43:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xf9) >> 1;
    break;
  case 0x1d:
  case 0x2e:
  case 0x3d:
  case 0x4a:
  case 0xdb:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xf9);
    break;
  case 0x22:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xf9) >> 2;
    break;
  case 0x47:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xe5) >> 6;
    break;
  case 0x49:
    BB_U32(BB_U32(node + 0x58) + 0x50) = 0;
    break;
  case 0x50:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xfd) >> 2;
    break;
  case 0x53:
    BB_U32(BB_U32(node + 0x58) + 0x50) = BYTE(node + 0xea);
    break;
  case 0x64:
    BB_U32(BB_U32(node + 0x58) + 0x50) = (BYTE(node + 0xe5) + 8) >> 4;
    break;
  case 0x66:
    BB_U32(BB_U32(node + 0x58) + 0x50) = (BYTE(node + 0xe5) + 4) >> 3;
    break;
  case 0xa9:
    BB_U32(BB_U32(node + 0x58) + 0x50) =
        BYTE(node + 0xf9) * 3 + BYTE(node + 0xea);
    break;
  default:
    return;
  }
  bb_sprite_draw(BB_POINTER(slot ? slot : node + 0x58), BB_U32(node + 0x120),
                 BB_U32(node + 0x124), frame, 1);
  return;
}

/* 0x004384b0..0x0043853c: resource assignment and visibility/attachment/
 * entity/tile suppression precede reflection and conditional blend mode.
 * Keep the original early-return EAX, including its partial color byte.
 */
uint32_t BB_THISCALL bb_entity_draw(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t eax = BB_U32(node + 0x58);
  if (!eax)
    eax = bb_entity_sprite(object);
  uint32_t color = BYTE(node + 0xf8);
  eax = (eax & 0xffffff00) | color;
  BYTE(0x004842bc) = color;
  if (!BYTE(node + 0x128))
    return eax;
  eax = BB_U32(node + 0x28);
  if (eax)
    return eax;
  eax = BB_U32(node + 0x9c);
  if (eax & 0x1000000)
    return eax;
  uint32_t column = BYTE(node + 0xd8), row = BYTE(node + 0xd9);
  uint32_t flags = BB_U32(0x00559b68 + (row * 192 + column) * 12);
  if (flags & 0x4000)
    return column;
  bb_entity_reflection(object);
  if (BB_U32(node + 0x9c) & 0x2000000)
    BYTE(0x004842c0) = 2;
  primary_layers(node);
  return bb_entity_render_finish(object);
}
