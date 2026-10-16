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
static uint32_t request(void *self, uint32_t kind, void *target) {
  return bb_faction_construction(self, kind, target, 0);
}

/* 0x00429120..0x0042918c: rebuild kind/profession populations from the
 * linked list. The last word of each 256-word table is deliberately retained.
 * Capture the list head before clearing; inactive records are counted too.
 * Return the final source faction in EDX for the entry's ECX restoration.
 */

/* 0x00428c60..0x00428cd3: military strength and the final product in EAX.
 * Fixed byte weights and word counts are read in the original order.
 */
uint64_t bb_faction_strength_c(void *object) {
  uint32_t self = bb_address_encode(object);
  uint32_t count = WORD(self + 0xa10);
  uint32_t total = count * BYTE(0x005e6823);
  uint32_t weight = BYTE(0x005e6947);
  total += WORD(self + 0xa12) * weight;
  weight = BYTE(0x005e66ff);
  total += WORD(self + 0xa0e) * weight;
  weight = BYTE(0x005e7143);
  total += WORD(self + 0xa20) * weight;
  weight = BYTE(0x005e74af);
  uint32_t last = WORD(self + 0xa26) * weight;
  total += last;
  BB_U32(self + 0xc94) = total;
  return ((uint64_t)total << 32) | last;
}

/* 0x00427810..0x00427878: clear current selection. Player-owned selections
 * also clear entity selection bits and dirty their sprites. Counts, owner and
 * pool stay live across calls; preserve partial EAX on the short paths.
 */
uint32_t bb_faction_selection_clear_c(void *object, uint32_t eax) {
  uint32_t self = bb_address_encode(object);
  eax = low_byte(eax, BYTE(0x004843f0));
  if (BYTE(self + 0x48) == (uint8_t)eax) {
    eax = low_byte(eax, BYTE(self + 0x451));
    if ((uint8_t)eax) {
      unsigned i = 0;
      do {
        uint32_t id = BB_U32(self + 0x452 + i++ * 4);
        uint8_t owner = BYTE(self + 0x48);
        uint32_t pool = BB_U32(0x00553f90);
        uint32_t node = pool + id * 314u;
        uint8_t mask = ~(uint8_t)(1u << (owner & 31));
        BYTE(node + 0x12d) &= mask;
        bb_entity_dirty_c((void *)(uintptr_t)node, low_byte(pool, mask), node);
        eax = BYTE(self + 0x451);
      } while (i < eax);
    }
  }
  BYTE(self + 0x451) = 0;
  return eax;
}

/* 0x00427880..0x0042792d: copy current IDs into a cached group. Removing
 * previous group memberships can alter current IDs/counts before the copy.
 */
uint32_t BB_THISCALL bb_faction_group_cache(void *object,
                                                          uint32_t group) {
  uint32_t self = bb_address_encode(object);
  group = (uint8_t)group;
  BYTE(self + 0x4d2 + group) = 0;
  for (unsigned i = 0; i < BYTE(self + 0x451); ++i) {
    uint32_t id = BB_U32(self + 0x452 + i * 4);
    uint32_t node = BB_U32(0x00553f90) + id * 314u;
    bb_faction_group_remove(object, (void *)(uintptr_t)node);
  }
  for (unsigned i = 0; i < BYTE(self + 0x451); ++i)
    BB_U32(self + 0x4dc + group * 128 + i * 4) = BB_U32(self + 0x452 + i * 4);
  BYTE(self + 0x4d2 + group) = BYTE(self + 0x451);
  return group;
}

/* 0x00427f40..0x00427fd1: fill an empty military group from the live entity
 * list, using the five eligible professions and the existing selection helper.
 */
uint32_t BB_THISCALL bb_faction_group_refresh(void *object,
                                                            uint32_t group) {
  uint32_t self = bb_address_encode(object);
  uint32_t eax = group = (uint8_t)group;
  if (BYTE(self + 0x4d2 + group))
    return eax;
  BYTE(self + 0x406 + group * 8) = 0;
  eax = bb_faction_selection_clear_c(object, eax);
  uint32_t node = BB_U32(0x00554f98);
  while (node) {
    eax = low_byte(eax, BYTE(self + 0x48));
    if (BYTE(node + 0xf7) == (uint8_t)eax) {
      eax = low_byte(eax, BYTE(node + 0xd4));
      if ((uint8_t)eax == 0x67 || (uint8_t)eax == 0x68) {
        eax = BYTE(node + 0xf4) - 7u;
        if (eax == 0 || eax == 1 || eax == 2 || eax == 9 || eax == 12)
          eax = bb_faction_selection_add(object, (void *)(uintptr_t)node, 0);
      }
    }
    node = BB_U32(node + 4);
  }
  eax = low_byte(eax, BYTE(self + 0x451));
  return (uint8_t)eax ? bb_faction_group_cache(object, group) : eax;
}

/* 0x004281b0: the original leaf also preserves volatile registers/flags. */
uint32_t BB_THISCALL bb_faction_stage(void *object,
                                                    uint32_t value) {
  BB_U32(bb_address_encode(object) + 0xc08) = value;
  return value;
}

/* 0x004281c0..0x00428240: restore a group, issue its immediate move command
 * and record its AI action. Type 1 ignores packet byte 7; the binary entry
 * carries the original stack padding through for callers that inspect it.
 */
uint32_t bb_faction_group_command_c(void *object, uint32_t group, uint32_t x,
                                    uint32_t y, uint32_t padding,
                                    uint32_t eax) {
  uint32_t self = bb_address_encode(object);
  group = (uint8_t)group;
  eax = bb_faction_group_restore_c(object, group, eax);
  eax = low_byte(eax, BYTE(self + 0x451));
  if (!(uint8_t)eax)
    return eax;
  uint8_t packet[12];
  WORD(packet) = 1;
  WORD(packet + 2) = 12;
  packet[4] = BYTE(self + 0x48);
  packet[5] = x;
  packet[6] = y;
  packet[7] = padding;
  BB_U32(packet + 8) = 0;
  bb_command_dispatch((void *)(uintptr_t)0x00513458, packet);
  uintptr_t control = self + group * 8;
  BYTE(control + 0x406) = 3;
  BYTE(control + 0x400) = BYTE(self + 0xc18);
  BB_U32(control + 0x402) = 0;
  return control;
}

/* 0x00428250..0x004282aa: all cached members must have finished moving.
 * Count is captured once; the entity pool is read for every member.
 */
uint32_t BB_THISCALL bb_faction_group_ready(void *object,
                                                          uint32_t group) {
  uint32_t self = bb_address_encode(object);
  group = (uint8_t)group;
  unsigned count = BYTE(self + 0x4d2 + group);
  uint32_t result = 1;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t id = BB_U32(self + 0x4dc + group * 128 + i * 4);
    uint32_t node = BB_U32(0x00553f90) + id * 314u;
    if (node && BYTE(node + 0x11d))
      result = 0;
  }
  return result;
}

/* 0x004282b0..0x004283fb: select plan/target, then choose a point around
 * its city, retrying the exact city centre. Signed random remainders and
 * live callback changes to the stored centre/coordinates are intentional.
 */
uint32_t BB_THISCALL bb_faction_plan_seed(void *object) {
  uint32_t self = bb_address_encode(object);
  int32_t value = random_value();
  BB_U32(self + 0xc08) = 0;
  BB_U32(self + 0xc04) = value % 5;
  if (random_value() % 2 == 0)
    BB_U32(self + 0xc18) = 0;
  else {
    int32_t maximum = 0;
    for (unsigned i = 0; i < 5; ++i)
      if (BB_I32(self + 0xc24 + i * 4) > maximum) {
        BB_U32(self + 0xc18) = i;
        maximum = BB_I32(self + 0xc24 + i * 4);
      }
  }
  uint32_t target = BB_U32(self + 0xc18);
  if (BYTE(self + 0x48) == target)
    BB_U32(self + 0xc18) = (target + 1u) % 5u;
  target = BB_U32(self + 0xc18);
  uint32_t id = BB_U32(0x00554fbc + target * 0xd00u);
  if (!id || !BB_U32(self)) {
    BB_U32(self + 0xc04) = 0;
    return id;
  }
  uint32_t city = BB_U32(0x00553f90) + id * 314u;
  BB_U32(self + 0xc1c) = BYTE(city + 0xd8);
  BB_U32(self + 0xc20) = BYTE(city + 0xd9);
  for (;;) {
    value = random_value() % 7;
    BB_U32(self + 0xc10) = BB_U32(self + 0xc1c) + (uint32_t)value * 10u - 30u;
    value = random_value() % 7;
    uint32_t x = BB_U32(self + 0xc10);
    uint32_t centre_x = BB_U32(self + 0xc1c);
    uint32_t y = BB_U32(self + 0xc20) + (uint32_t)value * 10u - 30u;
    BB_U32(self + 0xc14) = y;
    if (x != centre_x || y != BB_U32(self + 0xc20))
      return y;
  }
}

/* 0x00428010..0x0042818a: the complete eight-stage planning dispatcher.
 * Stage zero commands group zero on every iteration, including empty groups;
 * mode two can select an empty group. Preserve that original control flow.
 */
uint32_t BB_THISCALL bb_faction_plan(void *object) {
  uint32_t self = bb_address_encode(object);
  if (!BB_U32(self + 0xc04)) {
    bb_faction_plan_seed(object);
    if (!BB_U32(self + 0xc04))
      return 0;
  }
  bb_faction_strength_c(object);
  bb_faction_strength_c((void *)(uintptr_t)BB_U32(0x004844a8));
  uint32_t stage = BB_U32(self + 0xc08);
  switch (stage) {
  case 0: {
    int selected = 0;
    for (unsigned i = 0; i < 10; ++i) {
      uint8_t count = BYTE(self + 0x4d2 + i);
      if ((count && BB_U32(self + 0xc04) == 1 &&
           count > BB_U32(self + 0xc94)) ||
          (BB_U32(self + 0xc04) == 2 && count < 3)) {
        selected = 1;
        BB_U32(self + 0xca0) = i;
      }
      uint32_t y = BB_U32(self + 0xc14), x = BB_U32(self + 0xc10);
      bb_faction_group_command_c(object, 0, x, y, 0, y);
      bb_faction_stage(object, 1);
    }
    if (!selected)
      return bb_faction_stage(object, 6);
    uint32_t y = BB_U32(self + 0xc14), x = BB_U32(self + 0xc10);
    uint32_t group = BB_U32(self + 0xca0);
    bb_faction_group_command_c(object, group, x, y, 0, y);
    return bb_faction_stage(object, 1);
  }
  case 1:
    return bb_faction_group_ready(object, BB_U32(self + 0xca0))
               ? bb_faction_stage(object, 2)
               : 0;
  case 2: {
    uint32_t target = BB_U32(self + 0xc18);
    return bb_faction_attack_c(object, target, 0, target);
  }
  case 5: {
    uint32_t id = BB_U32(self);
    if (id) {
      uint32_t city = BB_U32(0x00553f90) + id * 314u;
      uint32_t group = BB_U32(self + 0xca0);
      uint8_t y = BYTE(city + 0xd9), x = BYTE(city + 0xd8);
      bb_faction_group_command_c(object, group, x, y, 0, low_byte(city, x));
    }
    return bb_faction_stage(object, 7);
  }
  case 6:
    return bb_faction_stage(object, 5);
  case 7:
    BB_U32(self + 0xc04) = 0;
    break;
  }
  return stage;
}

/* 0x00428400..0x004286ba: workforce percentages, profession planning and
 * population-driven expansion. Housing and population totals are captured
 * before any helper; later availability/profession fields remain live.
 */
uint32_t BB_THISCALL bb_faction_workforce(void *object) {
  uint32_t self = bb_address_encode(object);
  uint32_t housing = WORD(self + 0x82) + WORD(self + 0x160);
  uint32_t first = WORD(self + 0xa02), second = WORD(self + 0xa04);
  uint32_t population =
      WORD(self + 0xa1a) + WORD(self + 0xa1c) + first + second;
  if (!population)
    return 0;
  static const uint16_t offsets[] = {0xa0c, 0xa0e, 0xa10, 0xa12, 0xa26,
                                     0xa06, 0xa28, 0xa08, 0xa0a};
  uint32_t denominator = 0;
  for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; ++i)
    denominator += WORD(self + offsets[i]);
  denominator += first + second;
  for (unsigned i = 0; i < 64; ++i)
    BB_U32(self + 0x300 + i * 4) =
        denominator ? WORD(self + 0xa00 + i * 2) * 100u / denominator : 0;
  uint8_t profession = 0;
  uint32_t eax = WORD(self + 0x108) + WORD(self + 0x174);
  if (!eax)
    profession = 4;
  else {
    uint16_t available = WORD(self + 0xa02);
    eax = (eax & 0xffff0000u) | available;
    if (available < 2)
      profession = 1;
    else if (available != 2) {
      uint32_t remaining = BB_U32(self + 0xc98);
      if (remaining) {
        profession = BYTE(self + 0xc9c);
        eax = remaining - 1;
        BB_U32(self + 0xc98) = eax;
      } else {
        static const uint16_t buildings[] = {0x21c, 0x1a0, 0x1ae};
        static const uint8_t planned[] = {7, 19, 8};
        for (unsigned i = 0; i < 3; ++i)
          if (WORD(self + buildings[i]) && random_value() % 2 == 0) {
            BB_U32(self + 0xc9c) = planned[i];
            BB_U32(self + 0xc98) = (uint32_t)(random_value() % 3) + 1u;
          }
        int32_t value = random_value();
        int32_t choice = value % 10;
        eax = value / 10;
        static const uint8_t choices[] = {3, 9, 8, 19, 7, 20, 6, 4, 5, 16};
        if ((uint32_t)choice < 10)
          profession = choices[choice];
        if (!WORD(self + 0x1a0) && !WORD(self + 0x1a2) && !WORD(self + 0x21c) &&
            !WORD(self + 0x21e)) {
          eax = random_value() % 2;
          profession = eax == 0 ? 7 : 19;
        }
      }
    }
  }
  if (profession)
    bb_faction_recruit_c(object, profession, eax);
  return population / 2 > housing ? request(object, 0x71, 0) : population / 2;
}

/* 0x00428b40..0x00428c3f: military recruitment against the live player.
 * Negative random remainders skip recruitment and return the quotient.
 */
uint32_t BB_THISCALL bb_faction_military(void *object) {
  uint32_t self = bb_address_encode(object);
  static const uint16_t offsets[] = {0x1b0, 0x1ae, 0x1a0, 0x12a, 0x1a2,
                                     0x21c, 0x21e, 0x1b8, 0x1ba, 0x1dc};
  uint32_t buildings = 0;
  for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; ++i)
    buildings += WORD(self + offsets[i]);
  bb_faction_strength_c(object);
  bb_faction_strength_c((void *)(uintptr_t)BB_U32(0x004844a8));
  uintptr_t player = BB_U32(0x004844a8);
  if (BB_U32(player + 0xc94) <= BB_U32(self + 0xc94)) {
    uint32_t quarter = WORD(self + 0x14c) >> 2;
    if (quarter <= buildings)
      return quarter;
  }
  int32_t value = random_value();
  int32_t choice = value % 5;
  static const uint8_t professions[] = {19, 7, 8, 16, 9};
  return (uint32_t)choice < 5
             ? bb_faction_recruit_c(object, professions[choice], value / 5)
             : (uint32_t)(value / 5);
}

static uint32_t nearest_building(void *object, uint8_t kind) {
  uint32_t self = bb_address_encode(object);
  int32_t y = random_value() % 192;
  int32_t x = random_value() % 192;
  return (uintptr_t)bb_entity_nearest((void *)(uintptr_t)0x00553f90, BYTE(self + 0x48),
                                      kind, (uint8_t)x, (uint8_t)y);
}
/* 0x00428960..0x00428b34: population/housing/food capacity and the complete
 * construction decision sequence. Incoming EAX survives the empty-list path.
 */
uint32_t bb_faction_buildings_c(void *object, uint32_t eax) {
  uint32_t self = bb_address_encode(object), node = BB_U32(0x00554f98);
  uint32_t population = 0, housing = 0, food = 0;
  while (node) {
    eax = low_byte(eax, BYTE(node + 0xf7));
    if ((uint8_t)eax == BYTE(self + 0x48)) {
      eax = low_byte(eax, BYTE(node + 0xd4));
      uint8_t kind = eax;
      if (kind == 0x67)
        ++population;
      if (kind == 0x4e)
        housing += 3;
      if (kind == 0x4f || kind == 0x5e) {
        housing += 2;
        food += 4;
      }
      if (kind == 0x2f || kind == 0x76)
        food += 8;
      if (kind == 0x90)
        food += 5;
      if (kind == 0x6f)
        housing += 2;
    }
    node = BB_U32(node + 4);
  }
  if (population > housing) {
    if (!WORD(self + 0xe8) && !WORD(self + 0x168)) {
      eax = BB_U32(self + 0xc00);
      if (!(eax & 1))
        eax = request(object, 0x75, 0);
    } else {
      eax = BB_U32(0x00484768);
      if (eax <= 1) {
        eax = nearest_building(object, 0x35);
        if (eax)
          eax = request(object, 0x4e, (void *)(uintptr_t)eax);
      }
    }
    if (!WORD(self + 0xe4) && !WORD(self + 0x16c)) {
      eax = BB_U32(self + 0xc00);
      if (!(eax & 1))
        eax = request(object, 0x77, 0);
    }
    if (!WORD(self + 0xea) && !WORD(self + 0x176)) {
      eax = BB_U32(self + 0xc00);
      if (!(eax & 1))
        eax = request(object, 0x7c, 0);
    } else {
      eax = nearest_building(object, 0x36);
      if (eax)
        eax = request(object, 0x5e, (void *)(uintptr_t)eax);
    }
    int32_t value = random_value();
    eax = value / 10;
    if (value % 10 == 0) {
      eax = nearest_building(object, 0x62);
      if (eax)
        eax = request(object, 0x6f, (void *)(uintptr_t)eax);
    }
  }
  if (population > food) {
    eax = BB_U32(self + 0xc00);
    if (!(eax & 1))
      eax = request(object, 0x76, 0);
  }
  return eax;
}

/* 0x00427bf0..0x00427ccb: preserve random-call order, policy gates, signed
 * quotients and the disabled handler's unchanged EAX.
 */
uint32_t BB_THISCALL bb_faction_ai(void *object) {
  uint32_t self = bb_address_encode(object);
  int32_t value = random_value();
  if (value % 10 == 0) {
    uint32_t policy = BB_U32(self + 0xc00);
    return policy & 1 ? policy : bb_faction_workforce(object);
  }
  if (random_value() % 10 == 0)
    return bb_faction_military(object);
  value = random_value();
  if (value % 10 == 0)
    return bb_faction_buildings_c(object, value / 10);
  if (random_value() % 50 == 0)
    return bb_faction_group_refresh(object, random_value() % 10);
  if (random_value() % 10 == 0) {
    uint32_t policy = BB_U32(self + 0xc00);
    if ((policy & 2) && !BYTE(0x004843f0))
      return policy;
    if (policy & 4) {
      policy = low_byte(policy, BYTE(0x004843f0));
      if ((uint8_t)policy)
        return policy;
    }
    return bb_faction_plan(object);
  }
  return random_value() / 10;
}

#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_recount_entry(void) {
  __asm__ volatile("pushl %ecx;call " BB_ASM(
      bb_faction_recount_c) ";addl $4,%esp;movl %edx,%ecx;xorl %edx,%edx;ret");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_strength_entry(void) {
  __asm__ volatile("pushl %ecx;pushl %ecx;call " BB_ASM(
      bb_faction_strength_c) ";addl $4,%esp;popl %ecx;subl %eax,%edx;addl "
                             "%eax,%edx;ret");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_selection_clear_entry(void) {
  __asm__ volatile("pushl %eax;pushl %ecx;call " BB_ASM(
      bb_faction_selection_clear_c) ";addl $8,%esp;ret");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_group_command_entry(void) {
  __asm__ volatile("movzbl -5(%esp),%edx;pushl %eax;pushl %edx;pushl 20(%esp);"
                   "pushl 20(%esp);pushl 20(%esp);pushl %ecx;call " BB_ASM(
                       bb_faction_group_command_c) ";addl $24,%esp;ret $12");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_buildings_entry(void) {
  __asm__ volatile("pushl %eax;pushl %ecx;call " BB_ASM(
      bb_faction_buildings_c) ";addl $8,%esp;ret");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_idle_entry(void) {
  __asm__ volatile("ret");
}
#else
void bb_faction_idle_entry(void) {}

#endif
