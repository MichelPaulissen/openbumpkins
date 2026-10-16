/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#define bb_entity_nearest(p,a,b,c,d) ((void*)(uintptr_t)BB_GAME_CALL(0x4341a0,bb_address_encode(p),a,b,c,d))
#define bb_faction_ai(p) BB_GAME_CALL(0x427bf0,bb_address_encode(p))
#endif

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

/* 0x0041aef0..0x0041af38: cash plus separate income/expense totals.
 * Return the original block pointer and updated total in EAX/EDX.
 */
uint64_t bb_faction_cash_c(void *manager, uint32_t owner, uint32_t amount) {
  uintptr_t block = (uintptr_t)manager + (uint8_t)owner * 0xd00u;
  BB_U32(block + 0x4e) += amount;
  uintptr_t total = block + ((int32_t)amount < 0 ? 0xc6c : 0xc68);
  uint32_t updated = BB_U32(total);
  updated = (int32_t)amount < 0 ? updated - amount : updated + amount;
  BB_U32(total) = updated;
  return ((uint64_t)updated << 32) | (uint32_t)block;
}

/* 0x0041a9e0..0x0041aa29: construction-menu scroll bounds. Negative
 * clamped positions still return the pre-zero-clamp signed value.
 */
uint64_t bb_faction_scroll_c(void *object, uint32_t delta) {
  uintptr_t self = (uintptr_t)object;
  uint32_t value = BB_U32(self + 8) + delta;
  BB_U32(self + 8) = value;
  uintptr_t faction = BB_U32(0x004844a8), sprite = BB_U32(0x005e4794);
  uint32_t limit = BYTE(faction + 0x2be) * (BB_U32(sprite + 0x44) - 1u) -
                   BB_U32(0x00484444) + 1u;
  if ((int32_t)limit < (int32_t)value)
    BB_U32(self + 8) = limit;
  value = BB_U32(self + 8);
  if ((int32_t)value < 0)
    BB_U32(self + 8) = 0;
  return ((uint64_t)limit << 32) | value;
}

/* Reproduce the executable's AL loads as well as the ordered list writes.
 * Definitions put their technology threshold at kind*914 + 0x6638c5.
 */
static uint32_t offer(uintptr_t self, uint8_t kind, int threshold_in_eax,
                      uint32_t eax) {
  uint8_t threshold, stage;
  if (threshold_in_eax) {
    threshold = BYTE(0x006638c5 + kind * 914);
    stage = BYTE(0x005e5cc0);
    eax = low_byte(eax, threshold);
  } else {
    stage = BYTE(0x005e5cc0);
    threshold = BYTE(0x006638c5 + kind * 914);
    eax = low_byte(eax, stage);
  }
  if (threshold <= stage) {
    eax = BYTE(self + 0x2be);
    BYTE(self + 0x27e + eax) = kind;
    ++BYTE(self + 0x2be);
  }
  return eax;
}
static int prerequisite(uintptr_t self, uint32_t offset, uint32_t *eax) {
  if (WORD(self + offset))
    return 1;
  *eax = low_byte(*eax, BYTE(0x004843e8));
  if ((uint8_t)*eax != 1)
    return 0;
  *eax = low_byte(*eax, BYTE(0x004843f0));
  return BYTE(self + 0x48) == (uint8_t)*eax;
}

/* 0x0041ced0..0x0041d661: complete construction availability list.
 * Only the first 32 list bytes are cleared. Later unused bytes survive,
 * and cheat-enabled entries deliberately repeat existing offers.
 */
uint32_t BB_THISCALL bb_faction_buildable(void *object) {
  uintptr_t self = (uintptr_t)object;
  for (uint32_t i = 0; i < 8; ++i)
    BB_U32(self + 0x27e + i * 4) = 0;
  BYTE(self + 0x2be) = 0;
  uint32_t eax = BYTE(0x004843e8);
  if ((uint8_t)eax == 1) {
    eax = low_byte(eax, BYTE(0x004843f0));
    if (BYTE(self + 0x48) == (uint8_t)eax) {
      static const uint8_t special[] = {0xb4, 0xb5, 0xb9, 0xb7, 0xb6,
                                        0xba, 0xbd, 0xb8, 0xa2, 0xab,
                                        0xbf, 0xdb, 0xdc};
      for (unsigned i = 0; i < sizeof special; ++i) {
        uint8_t k = special[i];
        eax = offer(
            self, k,
            k == 0xb9 || k == 0xb6 || k == 0xa2 || k == 0xbf || k == 0xdc, eax);
      }
    }
  }
  eax = offer(self, 0x71, 0, eax);
  eax = offer(self, 0x7b, 0, eax);
  eax = offer(self, 0x6f, 0, eax);
  if (prerequisite(self, 0x108, &eax)) {
    eax = offer(self, 0x75, 0, eax);
    eax = offer(self, 0x4e, 1, eax);
    if (prerequisite(self, 0xe8, &eax)) {
      eax = offer(self, 0x77, 0, eax);
      if (prerequisite(self, 0xe4, &eax)) {
        eax = offer(self, 0x82, 1, eax);
        eax = offer(self, 0xaa, 0, eax);
      }
    }
    eax = offer(self, 0x76, 1, eax);
    eax = offer(self, 0xce, 0, eax);
    eax = offer(self, 0x7e, 0, eax);
    if (prerequisite(self, 0xe8, &eax)) {
      eax = offer(self, 0x7c, 1, eax);
      if (prerequisite(self, 0xea, &eax))
        eax = offer(self, 0x5e, 0, eax);
    }
    if (prerequisite(self, 0x102, &eax))
      eax = offer(self, 0x9b, 0, eax);
    eax = offer(self, 0xe1, 0, eax);
    eax = offer(self, 0xd0, 1, eax);
    eax = offer(self, 0x92, 0, eax);
    eax = offer(self, 0x7f, 1, eax);
    eax = offer(self, 0xaf, 0, eax);
    eax = offer(self, 0x97, 0, eax);
    if (prerequisite(self, 0x1a0, &eax))
      eax = offer(self, 0x99, 1, eax);
    if (prerequisite(self, 0x1ae, &eax)) {
      eax = offer(self, 0x9e, 0, eax);
      eax = offer(self, 0x24, 0, eax);
      eax = offer(self, 0x5d, 0, eax);
    }
    eax = BB_U32(0x0048474c);
    if (eax) {
      eax = offer(self, 0x7e, 0, eax);
      eax = offer(self, 0x9b, 0, eax);
      eax = offer(self, 0x99, 1, eax);
      eax = offer(self, 0x9e, 0, eax);
      eax = offer(self, 0x24, 0, eax);
      eax = offer(self, 0x5d, 0, eax);
    }
  }
  eax = low_byte(eax, BYTE(0x004843f0));
  return BYTE(self + 0x48) == (uint8_t)eax
             ? (uint32_t)bb_faction_scroll_c((void *)0x005f90d8, 0)
             : eax;
}

/* 0x0041b0c0..0x0041b120 and 0x0041b130..0x0041b18e. Addition uses
 * the global manager; subtraction uses the passed manager. Keep count-word
 * wrapping, zero/one transitions and the counter routine's partial EAX.
 */
uint32_t BB_THISCALL bb_faction_count_add(void *manager,
                                                        uint32_t owner,
                                                        uint32_t kind,
                                                        uint32_t part) {
  (void)manager;
  owner = (uint8_t)owner;
  kind = (uint8_t)kind;
  part = (uint8_t)part;
  uint32_t index = owner * 0x680 + kind;
  uintptr_t count = 0x0055503a + index * 2;
  uint16_t value = WORD(count) + 1;
  WORD(count) = value;
  uint32_t eax = (index & 0xffff0000u) | value;
  if (value == 1)
    eax = bb_faction_buildable((void *)(uintptr_t)(0x00554fbc + owner * 0xd00));
  if (kind == 0x67 || kind == 0x68) {
    eax = owner * 0x680 + part;
    ++WORD(0x005559bc + eax * 2);
  }
  return eax;
}
uint32_t BB_THISCALL bb_faction_count_remove(void *manager,
                                                           uint32_t owner,
                                                           uint32_t kind,
                                                           uint32_t part) {
  uintptr_t self = (uintptr_t)manager;
  owner = (uint8_t)owner;
  kind = (uint8_t)kind;
  part = (uint8_t)part;
  uintptr_t count = self + 0x82 + (owner * 0x680 + kind) * 2;
  uint16_t value = WORD(count) - 1;
  WORD(count) = value;
  uint32_t eax = owner;
  if (!value)
    eax = bb_faction_buildable((void *)(self + 4 + owner * 0xd00));
  if (kind == 0x67 || kind == 0x68) {
    eax = owner * 0x680 + part;
    --WORD(self + 0xa04 + eax * 2);
  }
  return eax;
}

/* 0x00427a80..0x00427be8: faction defaults leave all unlisted bytes
 * intact. Copy 21 resource prices and exactly the original cleared ranges.
 */
void bb_faction_initialize_c(void *object, uint32_t owner) {
  uintptr_t self = (uintptr_t)object;
  BYTE(self + 0x450) = 0;
  BYTE(self + 0x48) = owner;
  BYTE(self + 0x49) = owner;
  BB_U32(self + 0x44) = 0;
  BB_U32(self + 0x4a) = 8000;
  BYTE(self + 0x451) = 0;
  BB_U32(self + 0x4d2) = 0;
  BB_U32(self + 0x4d6) = 0;
  WORD(self + 0x4da) = 0;
  for (unsigned i = 0; i < 9; i++)
    BYTE(self + 4 + i) = BYTE(0x00488560 + i);
  for (unsigned i = 0; i < 21; i++)
    WORD(self + 0x4e + i * 2) = WORD(0x005e43f8 + i * 36);
  BB_U32(self + 0x78) = 0;
  WORD(self + 0x7c) = 0;
  for (unsigned i = 0; i < 64; i++) {
    BYTE(self + 0x2bf + i) = 0;
    BB_U32(self + 0x300 + i * 4) = 0;
    WORD(self + 0xa00 + i * 2) = 0;
  }
  for (unsigned i = 0; i < 128; i++)
    BB_U32(self + 0x7e + i * 4) = 0;
  BYTE(self + 0x2c0) = 8;
  BYTE(self + 0x2c1) = 8;
  BYTE(self + 0x2c6) = 9;
  BYTE(self + 0x2c8) = 8;
  BYTE(self + 0x2c7) = 9;
  BYTE(self + 0x2c5) = 8;
  BYTE(self + 0x2c3) = 8;
  BYTE(self + 0x2c2) = 7;
  BYTE(self + 0x2d3) = 7;
  BYTE(self + 0x2c4) = 6;
  BYTE(self + 0x2d2) = 10;
  static const uint16_t zeroes[] = {0,     0x9dc, 0xc98, 0xc9c, 0xca0,
                                    0xc90, 0xc38, 0xc3c, 0xc00, 0xc04,
                                    0xc08, 0xc0c, 0xc18};
  for (unsigned i = 0; i < sizeof zeroes / sizeof *zeroes; i++)
    BB_U32(self + zeroes[i]) = 0;
  BB_U32(self + 0xc40) = 191;
  BB_U32(self + 0xc44) = 191;
}
static uint32_t copy_name(uintptr_t source, uintptr_t target) {
  uint32_t length = 0;
  do {
    ++length;
  } while (BYTE(source + length - 1));
  unsigned i = 0;
  for (; i + 4 <= length; i += 4)
    BB_U32(target + i) = BB_U32(source + i);
  for (; i < length; i++)
    BYTE(target + i) = BYTE(source + i);
  return length;
}
/* 0x0041af40..0x0041afb2: reset five factions; scenario names copy
 * into the global manager, even when a different manager was passed.
 */
uint32_t BB_THISCALL bb_faction_manager_reset(void *manager) {
  uintptr_t self = (uintptr_t)manager;
  BYTE(self + 0x4104) = 0;
  for (unsigned i = 0; i < 5; i++)
    bb_faction_initialize_c((void *)(self + 4 + i * 0xd00), i);
  uint32_t eax = BB_U32(0x00533480);
  for (int32_t i = 0; i < BB_I32(0x00533480); i++)
    eax = copy_name(0x00533584 + i * 0x20a, 0x00554fc0 + i * 0xd00);
  return eax;
}

/* 0x0041afe0..0x0041b0bb: rotate four active factions, locate a city,
 * occasionally recount its population and dispatch AI from the live slot.
 */
uint32_t BB_THISCALL bb_faction_tick(void *manager) {
  uintptr_t self = (uintptr_t)manager;
  uint32_t owner = (BYTE(self + 0x4104) + 1u) & 3;
  BYTE(self + 0x4104) = owner;
  uintptr_t block = 0x00554fbc + owner * 0xd00;
  if (!BB_U32(block) && WORD(block + 0x142)) {
    uintptr_t city =
        (uintptr_t)bb_entity_nearest((void *)0x00553f90, owner, 0x62, 96, 96);
    if (city)
      BB_U32(0x00554fbc + BYTE(self + 0x4104) * 0xd00) = BB_U32(city + 0x8e);
  }
  if (bb_random() % 400 == 0)
    bb_faction_recount_c(
        (void *)(uintptr_t)(0x00554fbc + BYTE(self + 0x4104) * 0xd00));
  owner = BYTE(self + 0x4104);
  uint32_t eax = BB_U32(0x00555000 + owner * 0xd00);
  if (eax)
    return eax;
  eax = BYTE(0x004843f0);
  return owner == eax || owner == 4
             ? eax
             : bb_faction_ai((void *)(uintptr_t)(0x00554fbc + owner * 0xd00));
}
uint32_t bb_faction_enable(void) {
  BB_U32(0x00555000) = 1;
  return 0x00555d00;
}
/* 0x004274f0, 0x00427500 and 0x0041aec0: the nested constructors
 * perform no field initialization until the manager writes its count.
 */
void *BB_THISCALL bb_faction_subconstruct(void *self) {
  return self;
}
void *BB_THISCALL bb_faction_construct(void *self) {
  return self;
}

#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_faction_cash_entry(void) {
  __asm__ volatile("pushl 8(%esp);pushl 8(%esp);pushl %ecx;call " BB_ASM(
      bb_faction_cash_c) ";addl $12,%esp;movl 8(%esp),%ecx;testl %ecx,%ecx;js "
                         "1f;"
                         "subl %ecx,%edx;addl %ecx,%edx;ret $8;1: addl "
                         "%ecx,%edx;subl %ecx,%edx;ret $8");
}
void __attribute__((naked)) bb_faction_scroll_entry(void) {
  __asm__ volatile("pushl %ecx;pushl 8(%esp);pushl %ecx;call " BB_ASM(
      bb_faction_scroll_c) ";addl $8,%esp;popl %ecx;testl %eax,%eax;ret $4");
}
void __attribute__((naked)) bb_faction_initialize_entry(void) {
  __asm__ volatile("pushl %ecx;pushl 8(%esp);pushl %ecx;call " BB_ASM(
      bb_faction_initialize_c) ";addl $8,%esp;popl %edx;movl $191,%ecx;xorl "
                               "%eax,%eax;ret $4");
}
void __attribute__((naked)) bb_faction_enable_entry(void) {
  __asm__ volatile("pushl %ecx;pushl %edx;call " BB_ASM(
      bb_faction_enable) ";popl %edx;popl %ecx;cmpl $0x555d00,%eax;ret");
}
void __attribute__((naked)) bb_faction_construct_entry(void) {
  __asm__ volatile("pushl %edx;call " BB_ASM(
      bb_faction_construct) ";movl %eax,%ecx;addl $0x450,%ecx;movl $0,%edx;"
                            "decl %edx;leal 0x448(%eax),%ecx;popl %edx;ret");
}
void __attribute__((naked)) bb_faction_manager_construct_entry(void) {
  __asm__ volatile("pushl %edx;call " BB_ASM(
      bb_faction_manager_construct) ";movl %eax,%ecx;addl $0x3854,%ecx;"
                                    "movl $0,%edx;decl %edx;leal "
                                    "0x384c(%eax),%ecx;popl %edx;ret");
}

#endif

/* Shared from faction_ai.c. */
uint64_t bb_faction_recount_c(void *object) {
  uintptr_t self = (uintptr_t)object, node = BB_U32(0x00554f98);
  for (unsigned i = 0; i < 255; ++i)
    WORD(self + 0x7e + i * 2) = 0;
  for (unsigned i = 0; i < 255; ++i)
    WORD(self + 0xa00 + i * 2) = 0;
  uint32_t eax = 0, owner = 0;
  while (node) {
    owner = BYTE(node + 0xf7);
    eax = low_byte(eax, BYTE(self + 0x48));
    if (owner == (uint8_t)eax) {
      eax = BYTE(node + 0xd4);
      ++WORD(self + 0x7e + eax * 2);
      eax = low_byte(eax, BYTE(node + 0xd4));
      if (eax == 0x67 || eax == 0x68) {
        eax = BYTE(node + 0xf4);
        ++WORD(self + 0xa00 + eax * 2);
      }
    }
    node = BB_U32(node + 4);
  }
  return ((uint64_t)owner << 32) | eax;
}
