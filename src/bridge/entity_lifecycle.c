/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "object_initializers.h"
#include "interop.h"

#define BYTE(address) BB_U8(address)
#define WORD(address) BB_U16(address)
#define SHORT(address) BB_I16(address)
#define POOL_COUNT 5000u
#define RECORD_SIZE 314u
#define POOL_SIZE (POOL_COUNT * RECORD_SIZE)

#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#define LC_C0(id) BB_GAME_CALL(id, 0)
#define LC_C1(id,a) BB_GAME_CALL(id, 0, a)
#define LC_M0(id,p) BB_GAME_CALL(id, bb_game_word((uintptr_t)(p)))
#define LC_M1(id,p,a) BB_GAME_CALL(id, bb_game_word((uintptr_t)(p)), bb_game_word((uintptr_t)(a)))
#define LC_M4(id,p,a,b,c,d) BB_GAME_CALL(id, bb_game_word((uintptr_t)(p)), a,b,c,d)
#define LC_FORMAT(fmt) BB_GAME_CALL(0x46bae0,0,0x6033d8,fmt)
#define bb_faction_manager_reset(p) LC_M0(0x41af40,p)
#define bb_faction_cash_c(p,a,b) BB_GAME_CALL(0x41aef0,bb_game_word((uintptr_t)(p)),a,b)
#define bb_entity_sort(p,a) LC_M1(0x433d50,p,a)
#define bb_faction_count_add(p,a,b,c) BB_GAME_CALL(0x41b0c0,bb_game_word((uintptr_t)(p)),a,b,c)
#define bb_faction_count_remove(p,a,b,c) BB_GAME_CALL(0x41b130,bb_game_word((uintptr_t)(p)),a,b,c)
#define bb_entity_initialize(p,a,b,c,d) LC_M4(0x435340,p,a,b,c,d)
#define bb_entity_partner_clear(p) LC_M0(0x447a20,p)
#define bb_entity_clear_subtype(p) LC_M0(0x454de0,p)
#define bb_faction_selection_remove(p,a) LC_M1(0x427760,p,a)
#define bb_faction_group_remove(p,a) LC_M1(0x4279c0,p,a)
#define bb_faction_group_cancel(p,a) LC_M1(0x427530,p,a)
#define bb_entity_command_c(p,a,b,c,d) LC_M4(0x43f440,p,a,b,c,d)
#define bb_entity_release_tiles(p) LC_M0(0x448890,p)
#define bb_message_append_entry(p,a,b) BB_GAME_CALL(0x41e310,bb_game_word((uintptr_t)(p)),a,b)
#define bb_message_destroy_entry(p) LC_M0(0x41e2e0,p)
#define bb_sample_play_entry(a) LC_C1(0x415860,a)
#else
/* These type-specific, faction and UI routines remain binary bridges. */
typedef uint32_t(BB_THISCALL * object_fn)(void *);
typedef uint32_t(BB_THISCALL * one_fn)(void *, uint32_t);
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef uint32_t(BB_THISCALL * three_fn)(void *, uint32_t,
                                                       uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *
                 four_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*plain_one_fn)(uint32_t);
typedef uint32_t (*plain_fn)(void);
typedef uint32_t (*format_fn)(char *, const char *, ...);
#define CALL(address, type) ((type)BB_CODE(address))


#define LC_C0(id) CALL(id,plain_fn)()
#define LC_C1(id,a) CALL(id,plain_one_fn)(a)
#define LC_M0(id,p) CALL(id,object_fn)(p)
#define LC_M1(id,p,a) CALL(id,one_fn)(p,a)
#define LC_M4(id,p,a,b,c,d) CALL(id,four_fn)(p,a,b,c,d)
#define LC_FORMAT(fmt) CALL(0x46bae0,format_fn)((char*)0x6033d8,(const char*)(fmt))
#endif

/* 0x00433870..0x0043388e and 0x00435330..0x00435339: construction
 * clears only the original fields, leaving the remaining storage intact.
 */

void *BB_THISCALL bb_entity_construct(void *object) {
  BB_U32((uintptr_t)object + 0x38) = 0;
  return object;
}

/* 0x004338a0..0x00433900: the allocated header precedes 5,000 packed
 * records. A failed allocation retains the original null-store fault.
 * Capture operator-new's EDX for the otherwise register-preserving loop.
 */
uint64_t bb_entity_pool_allocate_c(void *object) {
  uintptr_t allocation;
  uint32_t edx;
#ifdef BB_MODEL_RUNTIME
  struct bb_call_state call = {0, 0, 0, 0x202};
  uint32_t bytes = POOL_SIZE + 4;
  bb_model_invoke(&call, 0x46c060, &bytes, 1);
  allocation = call.eax;
  edx = call.edx;
#else
  __asm__ volatile("pushl $0x17f4d4\n\tcall *%2\n\taddl $4, %%esp"
                   : "=a"(allocation), "=d"(edx)
                   : "r"(BB_CODE(0x0046c060))
                   : "ecx", "cc", "memory");
#endif
  if (allocation) {
    BB_U32(allocation) = POOL_COUNT;
    for (uint32_t i = 0; i < POOL_COUNT; ++i)
      bb_entity_construct((void *)(allocation + 4 + i * RECORD_SIZE));
    BB_U32((uintptr_t)object) = allocation + 4;
  } else {
    BB_U32((uintptr_t)object) = 0;
  }
  for (uint32_t i = 0; i < POOL_COUNT; ++i)
    BB_U32(BB_U32((uintptr_t)object) + i * RECORD_SIZE) = 0;
  return ((uint64_t)edx << 32) | POOL_SIZE;
}

/* 0x00433b50..0x00433b8e: clear the global pool, then the passed
 * manager's list/index fields. Other entity fields and allocations survive.
 */
uint32_t BB_THISCALL bb_entity_manager_clear(void *object) {
  for (uint32_t i = 0; i < POOL_COUNT; ++i) {
    uintptr_t node = BB_U32(0x00553f90) + i * RECORD_SIZE;
    if (BB_U32(node))
      BB_U32(node) = 0;
  }
  uintptr_t self = (uintptr_t)object;
  BB_U32(self + 0x1008) = 0;
  BB_U32(self + 0x1004) = 0;
  BB_U32(self + 0x100c) = 0;
  BB_U32(self + 0x1010) = 0;
  return 0;
}

/* 0x00433910..0x00433b4f: mission-wide reset, preserving the write
 * widths, child-call order, two random draws per tile and player-color read.
 */
uint32_t BB_THISCALL bb_entity_manager_reset(void *object) {
  bb_entity_manager_clear(object);
  bb_faction_manager_reset((void *)0x00554fb8);
  static const uintptr_t first_words[] = {
      0x4842c4, 0x4842c8, 0x484408, 0x48440c, 0x484410, 0x484270, 0x484290};
  BB_U32(0x4842c4) = 0;
  BB_U32(0x4842c8) = 0;
  BYTE(0x5e4778) = 0;
  BYTE(0x5e4740) = 0;
  for (uint32_t i = 2; i < sizeof first_words / sizeof *first_words; ++i)
    BB_U32(first_words[i]) = 0;
  uintptr_t self = (uintptr_t)object;
  BB_U32(self + 0x1008) = 0;
  BB_U32(self + 0x1004) = 0;
  BB_U32(self + 0x100c) = 0;
  BB_U32(self + 0x1010) = 0;
  static const uintptr_t words[] = {0x484414, 0x484418, 0x48441c, 0x484420,
                                    0x484440, 0x484458, 0x48445c, 0x484470,
                                    0x484474, 0x4846b4};
  for (uint32_t i = 0; i < sizeof words / sizeof *words; ++i)
    BB_U32(words[i]) = 0;
  BB_U32(0x6024b8) = 1500;
  BB_U32(0x602484) = 3500;
  BB_U32(0x5fe288) = 1500;
  BB_U32(0x5fe290) = 3500;
  BB_U32(0x6a8f28) = 1500;
  BB_U32(0x6a8f24) = 3500;
  for (uintptr_t address = 0x4846ec; address <= 0x484718; address += 4)
    BB_U32(address) = 0;
  BB_U32(0x484488) = 0;
  BB_U32(0x48471c) = 0;
  static const uintptr_t bytes[] = {0x5c5b64, 0x63f07c, 0x554fa4, 0x5f746c,
                                    0x5e4714, 0x484724, 0x602288, 0x6021a0,
                                    0x602280, 0x60219c};
  for (uint32_t i = 0; i < sizeof bytes / sizeof *bytes; ++i)
    BYTE(bytes[i]) = 0;
  BB_U32(0x484728) = 0;
  static const uintptr_t resets[][2] = {
      {0x419a80, 0x5d19a0}, {0x41a410, 0x5f90d8}, {0x41c2d0, 0x640ce8},
      {0x41bda0, 0x6b0880}, {0x41b1b0, 0x6b0858}, {0x41e2e0, 0x5ecf38},
      {0x42aa90, 0x559128}};
  for (uint32_t i = 0; i < sizeof resets / sizeof *resets; ++i) {
    if (resets[i][0] == 0x41e2e0)
      bb_message_destroy_entry((void *)resets[i][1]);
    else
      LC_M0(resets[i][0], (void *)resets[i][1]);
  }
  for (uint32_t i = 0; i < POOL_COUNT; ++i)
    BB_U32(BB_U32(self) + i * RECORD_SIZE) = 0;
  for (uint32_t i = 0; i < 192 * 192; ++i) {
    uintptr_t tile = 0x00559b60 + i * 12;
    BYTE(tile + 5) = 0;
    int32_t first = bb_random();
    int32_t second = bb_random();
    BYTE(tile) = second % 3 + (first % 10 < 9 ? 0 : 3);
    BYTE(tile + 6) = 0;
    BYTE(tile + 2) = 4;
    BYTE(tile + 3) = 0;
    BYTE(tile + 4) = 0;
    BB_U32(tile + 8) = 0;
    BYTE(tile + 1) = 0;
    BYTE(tile + 7) = 0;
  }
  LC_C0(0x004186f0);
  uint32_t color_owner = BB_U32(0x004844a8);
  uint32_t eax = (color_owner & 0xffffff00) | BYTE(color_owner + 0x49);
  BYTE(0x004842bc) = eax;
  return eax;
}

/* 0x00433ba0..0x00433d4b: rotating allocation, cost notification/debit,
 * head insertion and depth ordering precede counters and type initialization.
 * A full pool keeps searching, as in the original. Stale part bytes are
 * deliberately observed by the counter callback before initialization.
 */
void *BB_THISCALL bb_entity_create(void *manager,
                                                 uint32_t faction,
                                                 uint32_t kind, uint32_t x,
                                                 uint32_t y, uint32_t z) {
  uintptr_t self = (uintptr_t)manager, pool = BB_U32(self), node;
  do {
    BB_U32(self + 0x1010) = (BB_U32(self + 0x1010) + 1u) % POOL_COUNT;
    node = pool + BB_U32(self + 0x1010) * RECORD_SIZE;
  } while (BB_U32(node));
  BB_U32(node) = 1;
  BB_U32(node + 0x8e) = BB_U32(self + 0x1010);
  BB_U32(node + 4) = 0;
  BB_U32(node + 8) = 0;
  BYTE(node + 0xf7) = faction;
  if ((uint8_t)faction == 4) {
    BYTE(node + 0xf8) = 4;
  } else {
    BYTE(node + 0xf8) = BYTE(0x00555005 + (uint8_t)faction * 0xd00);
    uintptr_t cost = 0x006638b8 + kind * 914;
    if (WORD(cost) && !BYTE(0x004843e8)) {
      if ((uint8_t)faction == BYTE(0x004843f0))
        bb_sample_play_entry(BB_U32(0x0048431c));
      bb_faction_cash_c((void *)0x00554fb8, faction, 0u - WORD(cost));
    }
  }
  BB_U32(node + 0x62) = x;
  BB_U32(node + 0x66) = y;
  BB_U32(node + 0x6a) = z;
  BB_U32(node + 4) = BB_U32(self + 0x1008);
  BB_U32(node + 8) = 0;
  uintptr_t head = BB_U32(self + 0x1008);
  if (head)
    BB_U32(head + 8) = node;
  BB_U32(self + 0x1008) = node;
  bb_entity_sort(manager, (void *)node);
  bb_faction_count_add((void *)0x00554fb8, faction, kind, BYTE(node + 0xf4));
  bb_entity_initialize((void *)node, kind, x, y, z);
  return (void *)node;
}

/* 0x004342c0..0x004344ae: mark pending before callbacks/recursion so
 * linked children and cycles are processed once. Queue all previous-buffer
 * bounds and the current bound only when the entity remains visible.
 */
uint32_t BB_THISCALL bb_entity_remove(void *manager,
                                                    void *object) {
  uintptr_t self = (uintptr_t)manager, node = (uintptr_t)object;
  uint32_t eax = BB_U32(node);
  if (!eax)
    return eax;
  eax = BB_U32(node + 0x9c);
  if (eax & 0x1000)
    return eax;
  BB_U32(self + 4 + BB_U32(self + 0x1004) * 4) = BB_U32(node + 0x8e);
  ++BB_U32(self + 0x1004);
  BB_U32(node + 0x9c) |= 0x1000;
  if (BYTE(node + 0xd4) == 0x67)
    bb_entity_partner_clear(object);
  bb_faction_count_remove((void *)0x00554fb8, BYTE(node + 0xf7),
                          BYTE(node + 0xd4), BYTE(node + 0xf4));
  if (BYTE(node + 0xd4) == 0x67) {
    ++BB_U32(0x00555c14 + BYTE(node + 0xf7) * 0xd00);
    if (BYTE(node + 0xd4) == 0x67 && BYTE(node + 0xf7) == BYTE(0x004843f0) &&
        (BYTE(node + 0xf4) == 2 || BYTE(node + 0xf4) == 14)) {
      uintptr_t faction = BYTE(node + 0xf7) * 0xd00;
      if (!WORD(0x005559c0 + faction) && !WORD(0x005559d8 + faction)) {
        uintptr_t format = BB_U32(0x00603758);
        if (!format)
          format = 0x0048481c;
        LC_FORMAT(format);
        bb_message_append_entry((void *)0x005ecf38, 0x006033d8, 0);
      }
    }
  }
  uintptr_t child = BB_U32(node + 0x10);
  if (child)
    bb_entity_remove(manager, (void *)child);
  eax = bb_entity_clear_subtype(object);
  eax = (eax & 0xffffff00) | BYTE(node + 0xd4);
  if ((uint8_t)eax == 0x4e) {
    eax = BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8);
    BB_U32(0x00559b68 + eax * 12) &= 0xffdfffff;
  }
  eax = (eax & 0xffffff00) | BYTE(node + 0x11a);
  if ((uint8_t)eax)
    eax = LC_M1(0x00441600, object, 0);
  eax = (eax & 0xffffff00) | BYTE(node + 0x128);
  if (!(uint8_t)eax)
    return eax;
  eax = BB_U32(0x00484454);
  for (uint32_t i = 0; (int32_t)i < BB_I32(0x00484454); ++i)
    LC_M4(0x00418a10, (void *)0x005d19e0, SHORT(node + 0xa6 + i * 2),
                              SHORT(node + 0xac + i * 2),
                              SHORT(node + 0xb2 + i * 2),
                              SHORT(node + 0xb8 + i * 2));
  return LC_M4(0x00418a10, (void *)0x005d19e0, SHORT(node + 0xbe),
                                   SHORT(node + 0xc0), SHORT(node + 0xc2),
                                   SHORT(node + 0xc4));
}

/* 0x004344b0..0x004344f1: removal by full signed integer world tiles,
 * not the cached byte coordinates. Re-read next links after removal.
 */
uint32_t BB_THISCALL bb_entity_remove_at(void *manager,
                                                       uint32_t x, uint32_t y) {
  uint32_t eax = 0;
  for (uintptr_t node = BB_U32((uintptr_t)manager + 0x1008); node;
       node = BB_U32(node + 4)) {
    eax = BB_I32(node + 0x62) >> 16;
    if (eax != x)
      continue;
    eax = BB_I32(node + 0x66) >> 16;
    if (eax == y)
      eax = bb_entity_remove(manager, (void *)node);
  }
  return eax;
}

/* 0x00434680..0x004347a4: five faction lists, global UI references and
 * every manager entity's related-object fields. The command cancellation
 * callback runs before the remaining fields are read and cleared.
 */
uint32_t BB_THISCALL bb_entity_clear_references(void *manager,
                                                              void *object) {
  uintptr_t target = (uintptr_t)object;
  for (uintptr_t faction = 0x00554fbc; faction < 0x005590bc; faction += 0xd00) {
    bb_faction_selection_remove((void *)faction, (void *)target);
    bb_faction_group_remove((void *)faction, (void *)target);
    bb_faction_group_cancel((void *)faction, (void *)target);
  }
  if (BB_U32(0x00484408) == target)
    BB_U32(0x00484408) = 0;
  uint32_t eax = BB_U32(0x004847a4);
  if (eax == target) {
    BB_U32(0x004847a4) = 0;
    eax = LC_M1(0x0041c490, (void *)0x00640ce8, 0);
  }
  for (uintptr_t node = BB_U32((uintptr_t)manager + 0x1008); node;
       node = BB_U32(node + 4)) {
    if (BB_U32(node + 0xc) == target)
      bb_entity_command_c((void *)node, 0, 1, 1, 0);
    for (uint32_t offset = 0x10; offset <= 0x34; offset += 4) {
      eax = BB_U32(node + offset);
      if (eax == target)
        BB_U32(node + offset) = 0;
    }
  }
  return eax;
}

/* 0x00434500..0x00434610: release terrain/faction reservations, clear
 * related references, unlink, destroy optional data and release type state.
 * Preserve the x86 byte-shift behavior for faction bits greater than seven.
 */
uint32_t BB_THISCALL bb_entity_destroy(void *manager,
                                                     void *object) {
  uintptr_t self = (uintptr_t)manager, node = (uintptr_t)object;
  bb_entity_release_tiles(object);
  if (BB_U32(0x006635aa + BYTE(node + 0xd4) * 914)) {
    if (!BYTE(node + 0x11d))
      BB_U32(0x00559b68 + (BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8)) * 12) &=
          0xfffffffe;
    uint8_t mask = 1u << (BYTE(node + 0xf7) & 31);
    BYTE(0x00559b64 + (BYTE(node + 0xde) * 192 + BYTE(node + 0xdd)) * 12) &=
        (uint8_t)~mask;
  }
  if (BYTE(node + 0xd4) == 0x67)
    LC_M1(0x00453b20, object, 0);
  bb_entity_clear_references(manager, object);
  uintptr_t head = BB_U32(self + 0x1008);
  if (head == node) {
    uintptr_t next = BB_U32(head + 4);
    BB_U32(self + 0x1008) = next;
    if (next)
      BB_U32(next + 8) = 0;
  } else {
    uintptr_t next = BB_U32(node + 4);
    if (next)
      BB_U32(next + 8) = BB_U32(node + 8);
    uintptr_t previous = BB_U32(node + 8);
    if (previous)
      BB_U32(previous + 4) = BB_U32(node + 4);
  }
  uintptr_t data = BB_U32(node + 0x38);
  if (data) {
    LC_M0(0x0041c2a0, (void *)data);
    LC_C1(0x0046c110, data);
    BB_U32(node + 0x38) = 0;
  }
  uint32_t result = LC_M0(0x00447f20, object);
  BB_U32(node) = 0;
  return result;
}

/* 0x004347e0..0x00434827: callbacks may append pending indices or
 * replace the pool. Re-read both, then clear the count after processing.
 */
uint32_t BB_THISCALL bb_entity_remove_pending(void *manager) {
  uintptr_t self = (uintptr_t)manager;
  uint32_t result = BB_U32(self + 0x1004);
  for (uint32_t i = 0; (int32_t)i < BB_I32(self + 0x1004); ++i) {
    uint32_t index = BB_U32(self + 4 + i * 4);
    uintptr_t node = BB_U32(self) + index * RECORD_SIZE;
    result = bb_entity_destroy(manager, (void *)node);
  }
  BB_U32(self + 0x1004) = 0;
  return result;
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_MANAGER_CTOR_ASM "_bb_entity_manager_construct"
#define BB_ENTITY_CTOR_ASM "_bb_entity_construct"
#define BB_POOL_ASM "_bb_entity_pool_allocate_c"
#define BB_CLEAR_ASM "_bb_entity_manager_clear"
#define BB_REMOVE_AT_ASM "_bb_entity_remove_at"
#else
#define BB_MANAGER_CTOR_ASM "bb_entity_manager_construct"
#define BB_ENTITY_CTOR_ASM "bb_entity_construct"
#define BB_POOL_ASM "bb_entity_pool_allocate_c"
#define BB_CLEAR_ASM "bb_entity_manager_clear"
#define BB_REMOVE_AT_ASM "bb_entity_remove_at"
#endif

void __attribute__((naked)) bb_entity_manager_construct_entry(void) {
  __asm__ volatile("pushl %edx\n\tpushl %ecx\n\tcall " BB_MANAGER_CTOR_ASM
                   "\n\tpopl %ecx\n\tpopl %edx\n\ttestl $0, %eax\n\tret");
}
void __attribute__((naked)) bb_entity_construct_entry(void) {
  __asm__ volatile(
      "pushfl\n\tpushl %edx\n\tpushl %ecx\n\tcall " BB_ENTITY_CTOR_ASM
      "\n\tpopl %ecx\n\tpopl %edx\n\tpopfl\n\tret");
}
void __attribute__((naked)) bb_entity_pool_allocate_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl %ecx\n\tcall " BB_POOL_ASM
                   "\n\taddl $4, %esp\n\tpopl %ecx\n\tmovl (%ecx), %ecx\n\t"
                   "testl $0, %eax\n\tret");
}
void __attribute__((naked)) bb_entity_manager_clear_entry(void) {
  __asm__ volatile("pushl %ecx\n\tcall " BB_CLEAR_ASM
                   "\n\tpopl %ecx\n\tmovl $0x17f4d0, %edx\n\t"
                   "testl $0, %eax\n\tret");
}
void __attribute__((naked)) bb_entity_remove_at_entry(void) {
  __asm__ volatile("cmpl $0, 0x1008(%ecx)\n\tjne " BB_REMOVE_AT_ASM
                   "\n\tret $8");
}
#endif
