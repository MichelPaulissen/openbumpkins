/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
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
static int32_t random_value(void) { return bb_random(); }

/* 0x00427530: cancel every group order targeting this entity. Re-read its
 * ID for each group, including when the entity aliases an order record.
 */
uint32_t BB_THISCALL bb_faction_group_cancel(void *object,
                                                          void *entity) {
  uint32_t at = bb_address_encode(object) + 0x402, node = bb_address_encode(entity);
  for (unsigned i = 0; i < 10; ++i, at += 8) {
    uint32_t id = BB_U32(node + 0x8e);
    if (BB_U32(at) == id)
      BYTE(at + 4) = 0;
  }
  return 0;
}

/* 0x00429230: set one byte flag using full-width boolean conversion. */
uint32_t BB_THISCALL bb_faction_flag(void *object, uint32_t key,
                                                  uint32_t value) {
  BYTE(bb_address_encode(object) + 0x9e0 + (uint8_t)key) = value != 0;
  return value;
}

/* 0x00427760..0x0042780b: remove the first current-selection occurrence,
 * only when the entity carries this faction's selection bit. Search captures
 * count/ID; compaction reads count live, and the callback precedes decrement.
 */
uint32_t BB_THISCALL bb_faction_selection_remove(void *object,
                                                               void *entity) {
  uint32_t self = bb_address_encode(object), node = bb_address_encode(entity);
  uint32_t eax = 1u << (BYTE(self + 0x48) & 31);
  if (!(eax & BYTE(node + 0x12d)))
    return eax;
  uint8_t count = BYTE(self + 0x451);
  eax = low_byte(eax, count);
  if (!count)
    return eax;
  uint32_t id = BB_U32(node + 0x8e);
  unsigned i = 0;
  while (BB_U32(self + 0x452 + i * 4) != id)
    if (++i >= count)
      return eax;
  if (i < (unsigned)count - 1) {
    uint32_t at = self + 0x452 + i * 4;
    do {
      BB_U32(at) = BB_U32(at + 4);
      at += 4;
      ++i;
      eax = BYTE(self + 0x451) - 1u;
    } while ((int32_t)eax > (int32_t)i);
  }
  uint8_t mask = ~(uint8_t)(1u << (BYTE(self + 0x48) & 31));
  eax = low_byte(eax, mask);
  BYTE(node + 0x12d) &= mask;
  uint8_t player = BYTE(0x004843f0);
  eax = low_byte(eax, BYTE(self + 0x48));
  if ((uint8_t)eax == player)
    eax = bb_entity_dirty_c(entity, eax, player);
  --BYTE(self + 0x451);
  return eax;
}

/* 0x00427560..0x004276fe, including its complete profession sound switch.
 * There is no capacity guard: index arithmetic and byte count wrap as before.
 */
uint32_t BB_THISCALL
bb_faction_selection_add(void *object, void *entity, uint32_t notify) {
  uint32_t self = bb_address_encode(object), node = bb_address_encode(entity);
  bb_faction_selection_remove(object, entity);
  uint32_t id = BB_U32(node + 0x8e);
  uint32_t index = BYTE(self + 0x451) * 4u;
  BB_U32(self + 0x452 + index) = id;
  uint8_t mask = (uint8_t)(1u << (BYTE(self + 0x48) & 31));
  uint32_t eax = low_byte(index, mask);
  BYTE(node + 0x12d) |= mask;
  uint8_t player = BYTE(0x004843f0);
  if (BYTE(self + 0x48) == player) {
    eax = bb_entity_dirty_c(entity, eax, low_byte(id, player));
    eax = low_byte(eax, BYTE(0x004843f0));
    if (BYTE(node + 0xf7) == (uint8_t)eax) {
      eax = notify;
      if (notify) {
        eax = BYTE(node + 0xd4);
        if (eax == 0x67 || eax == 0x68) {
          uint32_t profession = BYTE(node + 0xf4);
          eax = profession - 1u;
          uintptr_t slot = 0;
          if (profession == 1 || profession == 2) {
            int32_t choice = random_value() % 2;
            eax = choice;
            if (choice == 0 || choice == 1)
              slot = profession == 1 ? (choice ? 0x0063f0bc : 0x0063f0cc)
                                     : (choice ? 0x00602184 : 0x00602180);
          } else {
            switch (profession) {
            case 3:
              slot = 0x005116b8;
              break;
            case 4:
              slot = 0x006a98cc;
              break;
            case 5:
              slot = 0x005fe3f4;
              break;
            case 6:
              slot = 0x006b0890;
              break;
            case 7:
              slot = 0x006af0e4;
              break;
            case 8:
              slot = 0x00604614;
              break;
            case 9:
              slot = 0x006ad1d8;
              break;
            case 10:
              slot = 0x005ecfa8;
              break;
            case 16:
              slot = 0x006a993c;
              break;
            case 19:
              slot = 0x00602334;
              break;
            case 26:
              slot = 0x00559108;
              break;
            }
          }
          if (slot) {
            uint32_t sound = BB_U32(slot);
            eax = bb_entity_feedback_c(entity, (void *)(uintptr_t)sound, 0, 0,
                                       sound);
          }
        }
      }
    }
  }
  ++BYTE(self + 0x451);
  return eax;
}

/* 0x004279c0..0x00427a70: remove the first ID occurrence in each of ten
 * cached groups. Read the entity ID again for every nonempty group.
 */
uint32_t BB_THISCALL bb_faction_group_remove(void *object,
                                                           void *entity) {
  uint32_t self = bb_address_encode(object), node = bb_address_encode(entity);
  for (unsigned group = 0; group < 10; ++group) {
    uint8_t count = BYTE(self + 0x4d2 + group);
    if (!count)
      continue;
    uint32_t id = BB_U32(node + 0x8e);
    uintptr_t ids = self + 0x4dc + group * 128;
    for (unsigned i = 0; i < count; ++i)
      if (BB_U32(ids + i * 4) == id) {
        if (i < (unsigned)count - 1) {
          uint32_t at = ids + i * 4;
          do {
            BB_U32(at) = BB_U32(at + 4);
            at += 4;
            ++i;
          } while ((int32_t)i < (int32_t)BYTE(self + 0x4d2 + group) - 1);
        }
        --BYTE(self + 0x4d2 + group);
        break;
      }
  }
  return 0x140;
}

/* 0x00427930..0x004279b7: restore a group, notifying one random member.
 * Random callbacks may change the divisor; a zero divisor still traps.
 */
uint32_t bb_faction_group_restore_c(void *object, uint32_t group,
                                    uint32_t eax) {
  uint32_t self = bb_address_encode(object);
  eax = bb_faction_selection_clear_c(object, eax);
  group = (uint8_t)group;
  eax = low_byte(eax, BYTE(self + 0x4d2 + group));
  if (!(uint8_t)eax)
    return eax;
  int32_t value = random_value();
  int32_t divisor = BYTE(self + 0x4d2 + group);
  int32_t selected = value % divisor;
  eax = value / divisor;
  if (divisor) {
    unsigned i = 0;
    do {
      uint32_t id = BB_U32(self + 0x4dc + group * 128 + i * 4);
      uint32_t node = BB_U32(0x00553f90) + id * 314u;
      bb_faction_selection_add(object, (void *)(uintptr_t)node,
                               i == (uint32_t)selected);
      ++i;
      eax = BYTE(self + 0x4d2 + group);
    } while (i < eax);
  }
  return eax;
}

/* 0x00427cd0..0x00427f3a: choose a target using the complete three-mode
 * scoring rules and issue a ten-byte attack packet. Stored scores exclude
 * random noise; mode two subtracts only the low health byte, with byte wrap.
 */
uint32_t bb_faction_attack_c(void *object, uint32_t target_owner,
                             uint32_t padding, uint32_t eax) {
  uint32_t self = bb_address_encode(object);
  eax = bb_faction_group_restore_c(object, 0, eax);
  if (!BYTE(self + 0x451)) {
    BB_U32(self + 0xc04) = 0;
    return eax;
  }
  uint32_t mode = BB_U32(self + 0xc04), chosen = 0;
  uint8_t best = 0;
  for (uint32_t node = BB_U32(0x00554f98); node; node = BB_U32(node + 4)) {
    if (BYTE(node + 0xf7) != (uint8_t)target_owner)
      continue;
    uint8_t kind = BYTE(node + 0xd4);
    if (mode == 1 || mode == 2) {
      uint8_t score;
      if (kind == 0x67 || kind == 0x68 || (mode == 2 && kind == 0x6e)) {
        score = BYTE(0x005e5f03 + BYTE(node + 0xf4) * 292);
        if (mode == 2)
          score -= BYTE(node + 0xa0);
      } else
        score = BYTE(0x006638c8 + kind * 914);
      int32_t noise = random_value() % 11;
      if ((int32_t)score + noise - 5 > best) {
        chosen = node;
        best = score;
      }
    } else if (kind == 0x6e || kind == 0x6f || kind == 0x4e) {
      uint8_t score = random_value() % 100;
      if (score > best) {
        chosen = node;
        best = score;
      }
    }
  }
  if (!chosen)
    return 0;
  uint8_t packet[10];
  WORD(packet) = 3;
  WORD(packet + 2) = 10;
  packet[4] = BYTE(self + 0x48);
  packet[5] = padding; /* Original type-3 dispatch ignores this byte. */
  BB_U32(packet + 6) = BB_U32(chosen + 0x8e);
  bb_command_dispatch((void *)(uintptr_t)0x00513458, packet);
  BYTE(self + 0x406) = 1;
  BYTE(self + 0x400) = target_owner;
  eax = BB_U32(chosen + 0x8e);
  BB_U32(self + 0x402) = eax;
  return eax;
}

/* 0x004286f0..0x0042892e: choose a conversion candidate or request its
 * missing training building. Profession one uses minimum strength and the
 * strict twenty-tile distance gate; the other branch finds capacity first.
 */
uint32_t bb_faction_recruit_c(void *object, uint32_t profession, uint32_t eax) {
  uint32_t self = bb_address_encode(object);
  profession = (uint8_t)profession;
  eax = low_byte(eax, profession);
  if (profession == 1) {
    if (!WORD(self + 0x14c))
      return eax;
    uint32_t minimum = 100, chosen = 0;
    for (uint32_t node = BB_U32(0x00554f98); node; node = BB_U32(node + 4)) {
      eax = low_byte(eax, BYTE(self + 0x48));
      if (BYTE(node + 0xf7) != (uint8_t)eax)
        continue;
      eax = low_byte(eax, BYTE(node + 0xd4));
      if ((uint8_t)eax != 0x67)
        continue;
      eax = BYTE(node + 0xf4) * 73u;
      if (!BYTE(0x005e5efb + eax * 4))
        continue;
      eax = low_byte(eax, BYTE(node + 0xf6));
      if ((uint8_t)eax || !BB_U32(node + 0x1c))
        continue;
      uint32_t target = BB_U32(node + 0x1c);
      uint32_t y2 = BYTE(target + 0xd9), x2 = BYTE(target + 0xd8);
      uint32_t y1 = BYTE(node + 0xd9), x1 = BYTE(node + 0xd8);
      eax = bb_entity_distance(x1, y1, x2, y2);
      if ((int32_t)eax >= 0x140000)
        continue;
      eax = BYTE(0x005e5f03 + BYTE(node + 0xf4) * 292);
      if (eax < minimum) {
        minimum = eax;
        chosen = node;
      }
    }
    if (chosen) {
      uint32_t target = BB_U32(chosen + 0x1c);
      return bb_entity_activity_c((void *)(uintptr_t)chosen, 2,
                                    (void *)(uintptr_t)target, target);
    }
    return eax;
  }
  uint32_t offset = profession * 292;
  uint8_t building = BYTE(0x005e5efb + offset);
  eax = building;
  if (!WORD(self + 0x7e + building * 2)) {
    uint8_t fallback = BYTE(0x005e5efa + offset);
    eax = fallback;
    if (WORD(self + 0x7e + fallback * 2))
      return eax;
    uint32_t city = BB_U32(0x00553f90) + BB_U32(self) * 314u;
    uint32_t kind = low_byte(offset, fallback);
    unsigned force = profession == 7 || profession == 8 || profession == 9 ||
                     profession == 16 || profession == 19;
    return bb_faction_construction(object, kind, (void *)(uintptr_t)city,
                                   force);
  }
  uint32_t chosen = 0;
  for (uint32_t node = BB_U32(0x00554f98); node; node = BB_U32(node + 4)) {
    eax = low_byte(eax, BYTE(self + 0x48));
    if (BYTE(node + 0xf7) != (uint8_t)eax)
      continue;
    eax = low_byte(eax, BYTE(node + 0xd4));
    if ((uint8_t)eax != building)
      continue;
    uint8_t capacity = BYTE(0x006635bf + (uint8_t)eax * 914);
    eax = BYTE(node + 0xe5);
    if (capacity > (uint8_t)eax) {
      chosen = node;
      break;
    }
  }
  if (!chosen)
    return eax;
  for (uint32_t node = BB_U32(0x00554f98); node; node = BB_U32(node + 4)) {
    eax = low_byte(eax, BYTE(self + 0x48));
    if (BYTE(node + 0xf7) != (uint8_t)eax)
      continue;
    uint8_t kind = BYTE(chosen + 0xd4);
    eax = BYTE(node + 0xf4);
    if (BYTE(0x006635bd + kind * 914) != (uint8_t)eax)
      continue;
    eax = BYTE(node + 0xf6);
    if (!(uint8_t)eax)
      return bb_entity_activity_c((void *)(uintptr_t)node, 2, (void *)(uintptr_t)chosen, eax);
  }
  return eax;
}

/* 0x0041ce80..0x0041ceb4: membership in the captured availability list.
 * 0x0041cec0 is an unconditional success gate in this executable.
 */
uint32_t BB_THISCALL bb_faction_can_build(void *object,
                                                        uint32_t kind) {
  uint32_t self = bb_address_encode(object);
  unsigned count = BYTE(self + 0x2be);
  for (unsigned i = 0; i < count; ++i)
    if (BYTE(self + 0x27e + i) == (uint8_t)kind)
      return 1;
  return 0;
}
uint32_t BB_THISCALL bb_faction_build_allowed(void *object,
                                                            uint32_t kind) {
  (void)object;
  (void)kind;
  return 1;
}

static int inside(uint32_t self, uint32_t x, uint32_t y) {
  return BB_I32(self + 0xc38) < (int32_t)x &&
         BB_I32(self + 0xc3c) < (int32_t)y &&
         (int32_t)x < BB_I32(self + 0xc40) && (int32_t)y < BB_I32(self + 0xc44);
}
static uint32_t build(uint32_t self, uint32_t kind, uint32_t x, uint32_t y) {
  uint32_t height = BYTE(0x00559b65 + (y * 192u + x) * 12u);
  bb_entity_build(BYTE(self + 0x48), kind, x, y, height);
  uint32_t price = WORD(0x006638b8 + (uint8_t)kind * 914);
  return bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(self + 0x48), 0u - price);
}

/* 0x00428dd0..0x00429114: exhaustive 7x7 local cost search, then eight
 * expanding random attempts around the captured city location. Probe and
 * cleanup run on every in-bounds site; the first strictly lower cost wins.
 */
uint32_t BB_THISCALL bb_faction_construction(void *object,
                                                           uint32_t kind,
                                                           void *target,
                                                           uint32_t force) {
  uint32_t self = bb_address_encode(object), node = (uintptr_t)target;
  if (!bb_faction_can_build(object, kind))
    return 0;
  if (!bb_faction_build_allowed(object, kind))
    return 0;
  uint32_t eax = BB_U32(self + 0xc00);
  if (eax & 1)
    return eax;
  if ((uint32_t)WORD(self + 0x162) + WORD(self + 0x164) > 12 && !force)
    return 0;
  eax = 0;
  if (node) {
    int32_t best = 0x200000;
    uint32_t best_x = 0, best_y = 0;
    for (unsigned row = 0; row < 7; ++row)
      for (unsigned col = 0; col < 7; ++col) {
        uint32_t definition = BYTE(node + 0xd4) * 914;
        uint32_t x = BYTE(node + 0xd8) + BB_U32(0x0066389e + definition) + col;
        uint32_t y = BYTE(node + 0xd9) + BB_U32(0x006638a2 + definition) + row;
        if (inside(self, x, y)) {
          if (bb_placement_probe(BYTE(self + 0x48), kind, x, y)) {
            int32_t cost = BB_I32(0x005fb2f0 + (row * 64 + col) * 4);
            if (best > cost) {
              best = cost;
              best_x = x;
              best_y = y;
            }
          }
          bb_placement_clear(BYTE(self + 0x48), kind, x, y);
        }
      }
    eax = best;
    if (best < 0x200000)
      return build(self, kind, best_x, best_y);
  }
  uint32_t id = BB_U32(self);
  if (!id)
    return eax;
  uint32_t city = BB_U32(0x00553f90) + id * 314u;
  uint32_t centre_x = BYTE(city + 0xd8), centre_y = BYTE(city + 0xd9);
  for (unsigned range = 8; range < 72; range += 8) {
    uint32_t x =
        centre_x + (uint32_t)(random_value() % (int32_t)range) - range / 2;
    uint32_t y =
        centre_y + (uint32_t)(random_value() % (int32_t)range) - range / 2;
    int found = 0;
    if (inside(self, x, y)) {
      found = bb_placement_probe(BYTE(self + 0x48), kind, x, y) != 0;
      bb_placement_clear(BYTE(self + 0x48), kind, x, y);
    }
    if (found)
      return build(self, kind, x, y);
  }
  return 0;
}

/* 0x0042e4e0: construction kind-specific centering, using the C allocator. */
void *bb_entity_build(uint32_t owner, uint32_t kind, uint32_t x, uint32_t y,
                      uint32_t height) {
  owner = (uint8_t)owner;
  kind = (uint8_t)kind;
  uint32_t centre = kind == 0x4e || kind == 0x6f || kind == 0x72 ||
                            kind == 0x73 || kind == 0xd9 || kind == 0xe5
                        ? 0
                        : 0x8000;
  return bb_entity_create(
      (void *)(uintptr_t)0x00553f90, owner, kind, ((uint32_t)(uint8_t)x << 16) + centre,
      ((uint32_t)(uint8_t)y << 16) + centre, (uint32_t)(uint8_t)height << 15);
}

#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_group_restore_entry(void) {
  __asm__ volatile("pushl %eax;pushl 8(%esp);pushl %ecx;call " BB_ASM(
      bb_faction_group_restore_c) ";addl $12,%esp;ret $4");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_recruit_entry(void) {
  __asm__ volatile("pushl %eax;pushl 8(%esp);pushl %ecx;call " BB_ASM(
      bb_faction_recruit_c) ";addl $12,%esp;ret $4");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_attack_entry(void) {
  __asm__ volatile("movzbl -7(%esp),%edx;pushl %eax;pushl %edx;"
                   "pushl 12(%esp);pushl %ecx;call " BB_ASM(
                       bb_faction_attack_c) ";addl $16,%esp;ret $4");
}
#endif
