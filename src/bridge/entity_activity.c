/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
static uint32_t low_byte(uint32_t value, uint8_t byte) {
  return (value & 0xffffff00u) | byte;
}

/* 0x0043f740..0x0043f8b3: complete activity dispatch, including its table.
 * The requested target is captured before command/subtype callbacks; later
 * movement uses the live activity target. Disabled units retain EAX's prefix.
 */
uint32_t bb_entity_activity_c(void *object, uint32_t state, void *target,
                              uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  eax = low_byte(eax, BYTE(node + 0xf2));
  if ((uint8_t)eax == 0x73)
    return eax;
  eax = low_byte(eax, state);
  BYTE(node + 0xf6) = state;
  BB_SET_POINTER(node + 0x34, target);
  bb_entity_command_c(object, 0, 1, 1, eax);
  BYTE(node + 0xe3) = 0;
  BYTE(node + 0xe4) = 0;
  bb_entity_subtype(object, 0);
  BB_U32(node + 0x18) = 0;
  state = BYTE(node + 0xf6);
  eax = state - 2u;
  if (state == 2) {
    eax = bb_entity_can_work(object, BYTE((uintptr_t)target + 0xd4));
    if (!eax) {
      eax = bb_entity_can_train(object, BYTE((uintptr_t)target + 0xd4));
      if (!eax) {
        BYTE(node + 0xf6) = 0;
        return eax;
      }
    }
    eax = BB_U32(node + 0x34);
    if (BB_U32(node + 0x18) == eax)
      return eax;
    return bb_entity_approach_c(object, (void *)(uintptr_t)eax, 1, eax);
  }
  switch (state) {
  case 3:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
    eax = BB_U32(node + 0x34);
    if (eax) {
      eax = bb_entity_approach_c(object, (void *)(uintptr_t)eax, state != 16,
                                 eax);
      if (state == 7 || state == 14)
        eax = bb_entity_set_ability(object, state == 7 ? 5 : 0x17);
    }
  }
  return eax;
}

/* 0x00453ae0/0x00453b00: queue feedback using an explicit byte delay or
 * the live sound duration. A null feedback leaves the previous queue intact.
 */
uint32_t BB_THISCALL
bb_entity_feedback_delay(void *object, uint32_t sound, uint32_t ticks) {
  if (sound) {
    BB_U32(bb_address_encode(object) + 0x5c) = sound;
    BYTE(bb_address_encode(object) + 0x60) = ticks;
    return low_byte(sound, ticks);
  }
  return 0;
}
uint32_t BB_THISCALL
bb_entity_feedback_queue(void *object, uint32_t feedback, void *sound) {
  return sound ? bb_entity_feedback_delay(object, feedback,
                                          BB_U32((uintptr_t)sound + 0x82))
               : 0;
}

/* 0x004479a0: link the partners to the source's live home, then set that
 * home's secondary occupant. Reads remain live even when records alias.
 */
uint32_t BB_THISCALL bb_entity_partner_link(void *object,
                                                          void *other) {
  uint32_t node = bb_address_encode(object), partner = bb_address_encode(other);
  uint32_t home = BB_U32(node + 0x1c);
  if (home) {
    BB_U32(node + 0x30) = partner;
    BB_U32(partner + 0x30) = node;
    BB_U32(partner + 0x1c) = BB_U32(node + 0x1c);
    home = BB_U32(node + 0x1c);
    BB_U32(home + 0x24) = partner;
  }
  return home;
}

/* 0x00446400: move both partners to the source's live home, randomly
 * choose one feedback pair, link them and issue two activity-three orders.
 */
uint32_t BB_THISCALL bb_entity_partner_begin(void *object,
                                                           void *other) {
  uint32_t node = bb_address_encode(object), partner = bb_address_encode(other);
  uint32_t home = BB_U32(node + 0x1c);
  bb_entity_approach_c(object, (void *)(uintptr_t)home, 1, home);
  home = BB_U32(node + 0x1c);
  bb_entity_approach_c(other, (void *)(uintptr_t)home, 1, home);
  int32_t choice = bb_random() % 2;
  if (!choice) {
    uint32_t sound = BB_U32(0x00602464);
    bb_entity_feedback_c(object, (void *)(uintptr_t)sound, 0, 0, sound);
    bb_entity_feedback_queue(other, BB_U32(0x006022a0), BB_POINTER(0x00602464));
  } else {
    uint32_t sound = BB_U32(0x00642c3c);
    bb_entity_feedback_c(other, (void *)(uintptr_t)sound, 0, 0, sound);
    bb_entity_feedback_queue(object, BB_U32(0x00602308),
                             BB_POINTER(0x00642c3c));
  }
  BB_U32(node + 0x30) = partner;
  BB_U32(partner + 0x30) = node;
  home = BB_U32(node + 0x1c);
  bb_entity_activity_c(object, 3, (void *)(uintptr_t)home, home);
  home = BB_U32(node + 0x1c);
  bb_entity_activity_c(other, 3, (void *)(uintptr_t)home, home);
  BYTE(node + 0x114) = 255;
  BYTE(partner + 0x114) = 255;
  return 255;
}

/* 0x004479c0: copy the terminated surname in forward DWORD/byte order,
 * retaining overlap behavior, before setting flags and starting the pair.
 */
uint32_t BB_THISCALL bb_entity_partner_name(void *object,
                                                          void *other) {
  uint32_t node = bb_address_encode(object), partner = bb_address_encode(other);
  uintptr_t source = partner + 0x4c, dest = node + 0x4c;
  uint32_t length = 1;
  while (BYTE(source + length - 1))
    ++length;
  for (unsigned i = 0; i < length / 4; ++i) {
    BB_U32(dest) = BB_U32(source);
    dest += 4;
    source += 4;
  }
  for (unsigned i = 0; i < length % 4; ++i)
    BYTE(dest + i) = BYTE(source + i);
  BB_U32(node + 0x9c) |= 0x20000000;
  BB_U32(partner + 0x9c) |= 0x20000000;
  return bb_entity_partner_begin(object, other);
}

/* 0x00447a20: clear the partner's flags/link/home first, then this unit's
 * live home and link. The stored partner is re-read before home cleanup.
 */
uint32_t BB_THISCALL bb_entity_partner_clear(void *object) {
  uint32_t node = bb_address_encode(object), partner = BB_U32(node + 0x30);
  if (partner) {
    BB_U32(partner + 0x9c) &= 0xdfffffff;
    BB_U32(BB_U32(node + 0x30) + 0x30) = 0;
    bb_entity_target_c(BB_POINTER(node + 0x30), 0, 0);
  }
  bb_entity_target_c(object, 0, 0);
  BB_U32(node + 0x30) = 0;
  uint32_t eax = BB_U32(node + 0x9c) & 0xdfffffff;
  BB_U32(node + 0x9c) = eax;
  return eax;
}

/* 0x004464b0: choose the last eligible same-faction male. A young unit
 * already carrying a partner cannot choose another one; no distance gate.
 */
void *BB_THISCALL bb_entity_partner_find(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  if (BYTE(0x005e5eff + BYTE(node + 0xf4) * 292) != 2 ||
      (BB_U32(node + 0x30) && BYTE(node + 0x113) < 80))
    return 0;
  for (uint32_t at = BB_U32(0x00554f98); at; at = BB_U32(at + 4))
    if (BYTE(at + 0xd4) == 0x67 && BYTE(at + 0xf7) == BYTE(node + 0xf7) &&
        BYTE(0x005e5eff + BYTE(at + 0xf4) * 292) == 1 &&
        !(BB_U32(at + 0x9c) & 0x200000) &&
        (!(BB_U32(at + 0x9c) & 0x20000000) || BYTE(at + 0x113) >= 80))
      chosen = at;
  if (chosen)
    bb_entity_partner_link(object, (void *)(uintptr_t)chosen);
  return (void *)(uintptr_t)chosen;
}

/* 0x00446580: first strictly nearest same-faction home within 32 tiles. */
void *BB_THISCALL bb_entity_home_nearest(void *object) {
  uint32_t node = bb_address_encode(object), chosen = 0;
  int32_t best = 0x200000;
  for (uint32_t at = BB_U32(0x00554f98); at; at = BB_U32(at + 4))
    if (BYTE(at + 0xd4) == 1 && BYTE(at + 0xf7) == BYTE(node + 0xf7)) {
      int32_t distance =
          bb_entity_distance(BYTE(node + 0xd8), BYTE(node + 0xd9),
                             BYTE(at + 0xd8), BYTE(at + 0xd9));
      if (distance < best) {
        best = distance;
        chosen = at;
      }
    }
  return (void *)(uintptr_t)chosen;
}

/* 0x00446600: choose a 17x17 child site around the nearest home. Preserve
 * the zero/zero failure sentinel and height read at the final search cursor,
 * rather than the chosen site. The child's pointer survives approach calls.
 */
void *BB_THISCALL bb_entity_child_site(void *object) {
  uint32_t node = bb_address_encode(object);
  uint32_t home = (uintptr_t)bb_entity_home_nearest(object);
  uint8_t chosen_x = 0, chosen_y = 0;
  int32_t best = 0x200000, x = best, y = 0;
  if (home) {
    y = (int32_t)BYTE(home + 0xd9) - 8;
    int32_t row = y * 192;
    for (; y <= (int32_t)BYTE(home + 0xd9) + 8; ++y, row += 192) {
      x = (int32_t)BYTE(home + 0xd8) - 8;
      uintptr_t flags = 0x00559b68 + (uint32_t)(row + x) * 12;
      for (; x <= (int32_t)BYTE(home + 0xd8) + 8; ++x, flags += 12) {
        if (row < 0 || row >= 0x9000 || x < 0 || x >= 192)
          continue;
        uint32_t state = BB_U32(flags);
        if ((state & 2) || BYTE(flags - 2) ||
            !(BB_U32(0x006a9092 + BYTE(flags - 8) * 8) & 4) ||
            BYTE(flags - 7) || (state & 0x200001) ||
            (BYTE(flags - 4) & (1u << (BYTE(node + 0xf7) & 31))))
          continue;
        int32_t distance =
            bb_entity_distance(x, y, BYTE(home + 0xd8), BYTE(home + 0xd9));
        if (distance < best) {
          chosen_x = x;
          chosen_y = y;
          best = distance;
        }
      }
    }
  }
  if (!chosen_x && !chosen_y) {
    BYTE(node + 0xfb) = 0;
    if (BB_U32(node + 0x14))
      bb_entity_remove((void *)(uintptr_t)0x00553f90, BB_POINTER(node + 0x14));
    return 0;
  }
  uint32_t height = (uint32_t)BYTE(0x00559b65 + (uint32_t)(y * 192 + x) * 12)
                    << 16;
  void *created =
      bb_entity_create((void *)(uintptr_t)0x00553f90, low_byte(height, BYTE(node + 0xf7)),
                       0x8c, ((uint32_t)chosen_x << 16) + 0x8000,
                       ((uint32_t)chosen_y << 16) + 0x8000, height);
  BB_SET_POINTER(node + 0x10, created);
  bb_entity_approach_c(object, created, 1, (uintptr_t)created);
  return created;
}

#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_activity_entry(void) {
  __asm__ volatile(
      "pushl %eax;pushl 12(%esp);pushl 12(%esp);pushl %ecx;call " BB_ASM(
          bb_entity_activity_c) ";addl $16,%esp;ret $8");
}
#endif
