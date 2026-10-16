/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "bootstrap_resources.h"
#include "interop.h"
#include "session_controls.h"
#include "worker_runtime.h"
#ifdef BB_MODEL_RUNTIME
#include "command_calls.h"
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#ifdef BB_MODEL_RUNTIME
#define CMD_TEXT(p) ((uint32_t)(p))
#else
#define CMD_TEXT(p) ((char *)(uintptr_t)(p))
#endif
#define CALL(a, type) ((type)BB_CODE(a))
typedef uint32_t (*plain_fn)(void);
typedef uint32_t (*one_fn)(uint32_t);
typedef uint32_t (*three_fn)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *method_fn)(void *, uint32_t);
typedef uint32_t(BB_THISCALL *two_fn)(void *, uint32_t, uint32_t);
typedef uint32_t(BB_THISCALL *four_fn)(void *, uint32_t, uint32_t, uint32_t,
                                       uint32_t);
typedef int32_t (*random_fn)(void);
typedef int (*format_fn)(char *, const char *, ...);
typedef uint32_t (*open_fn)(const char *, const char *);
typedef uint32_t(BB_STDCALL *registry_fn)(uint32_t, const char *);
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

#ifndef BB_MODEL_RUNTIME
#define CMD_plain_fn(id, ...) CALL(id, plain_fn)(__VA_ARGS__)
#define CMD_one_fn(id, ...) CALL(id, one_fn)(__VA_ARGS__)
#define CMD_three_fn(id, ...) CALL(id, three_fn)(__VA_ARGS__)
#define CMD_method_fn(id, ...) CALL(id, method_fn)(__VA_ARGS__)
#define CMD_two_fn(id, ...) CALL(id, two_fn)(__VA_ARGS__)
#define CMD_four_fn(id, ...) CALL(id, four_fn)(__VA_ARGS__)
#define CMD_format_fn(id, ...) CALL(id, format_fn)(__VA_ARGS__)
#define CMD_open_fn(id, ...) CALL(id, open_fn)(__VA_ARGS__)
#define CMD_registry_fn(id, ...) CALL(id, registry_fn)(__VA_ARGS__)
#endif
static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00u) | (uint8_t)byte;
}
static uintptr_t entity(uint32_t id) { return BB_U32(0x00553f90) + id * 314u; }
/* The original copies DWORDs forward, then its remaining bytes. Keeping that
 * order also retains overlap and aliases with counters or following packets.
 */
static void copy(uintptr_t destination, uintptr_t source, uint32_t length) {
  uint32_t words = length >> 2;
  for (uint32_t i = 0; i < words; ++i, source += 4, destination += 4)
    BB_U32(destination) = BB_U32(source);
  for (uint32_t i = 0; i < (length & 3); ++i)
    BYTE(destination + i) = BYTE(source + i);
}

/* 0x00401240: always copy a complete head slot when the signed count is
 * positive, then shift only each following packet's declared length. Empty
 * queues change just the output type WORD; partial slot tails remain intact.
 */
uint64_t bb_command_pop_c(void *output, uint32_t edx) {
  uint32_t eax = BB_U32(0x0048e754);
  if ((int32_t)eax <= 0) {
    WORD((uintptr_t)output) = 0;
    return ((uint64_t)edx << 32) | eax;
  }
  copy((uintptr_t)output, 0x004de758, 256);
  edx = BB_U32(0x0048e754) - 1u;
  if ((int32_t)edx > 0)
    for (uint32_t i = 0; i < edx; ++i) {
      eax = 0x004de858 + i * 256u;
      uint32_t length = WORD(eax + 2);
      copy(eax - 256u, eax, length);
    }
  BB_U32(0x0048e754) = edx;
  return ((uint64_t)edx << 32) | eax;
}

/* 0x004012b0: collect a slot even when the shared queue is empty. The
 * player's full DWORD index is reread after dequeue before incrementing.
 */
uint32_t BB_THISCALL bb_command_collect(void *manager) {
  uint32_t self = bb_address_encode(manager);
  uint32_t player = BB_U32(self + 12);
  uint32_t offset = (BB_U32(0x0048e740 + player * 4u) + (player << 8)) << 8;
  uint32_t eax = bb_command_pop_c((void *)(uintptr_t)(0x0048e758 + offset), 0);
  ++BB_U32(0x0048e740 + BB_U32(self + 12) * 4u);
  return eax;
}

uint32_t BB_THISCALL bb_command_ready(void *manager) {
  (void)manager;
  return 1;
}

/* 0x00401390: dispatch one current slot for every live signed player count. */
uint32_t BB_THISCALL bb_command_pump(void *manager) {
  uint32_t self = bb_address_encode(manager);
  uint32_t eax = BB_U32(self + 0x20028);
  for (int32_t i = 0; i < (int32_t)eax; ++i) {
    bb_command_dispatch(manager, (void *)(uintptr_t)(0x0048e758 + i * 65536u));
    eax = BB_U32(self + 0x20028);
  }
  return eax;
}

/* 0x004012f0: dispatch first, then consume/compact each player's queue.
 * Player limits stay live; each shift captures its signed decremented count.
 */
uint32_t BB_THISCALL bb_command_tick(void *manager) {
  uint32_t self = bb_address_encode(manager);
  bb_command_pump(manager);
  uint32_t eax = BB_U32(self + 0x20028);
  for (int32_t i = 0; i < BB_I32(self + 0x20028); ++i) {
    uint32_t count = BB_U32(0x0048e740 + i * 4u) - 1u;
    BB_U32(0x0048e740 + i * 4u) = count;
    uint32_t head = 0x0048e758 + i * 65536u;
    if ((int32_t)count > 0)
      for (uint32_t j = 0; j < count; ++j) {
        uint32_t next = head + (j + 1u) * 256u;
        copy(next - 256u, next, WORD(next + 2));
      }
    eax = head + 65536u;
  }
  return eax;
}

/* 0x0042dc60: fully initialized eight-byte random-sync request. */
uint32_t BB_THISCALL bb_command_sync(void *faction, uint32_t value) {
  uint8_t packet[8];
  WORD(packet) = 14;
  WORD(packet + 2) = 8;
  packet[4] = BYTE(bb_address_encode(faction) + 0x48);
  packet[5] = 1;
  WORD(packet + 6) = value;
  return bb_command_enqueue((void *)(uintptr_t)0x00513458, packet);
}

/* 0x004013d0: complete 25-type immediate dispatcher, including the unused
 * type 16 and every network/UI/audio branch. Owner is captured; saved packet
 * pointers, selection counts and pool addresses retain their live reads.
 */
uint32_t BB_THISCALL bb_command_dispatch(void *manager, const void *packet) {
  (void)manager;
  uint32_t p = bb_address_encode(packet);
  uint32_t type = WORD(p), eax = type - 1u;
  uint32_t owner = BYTE(p + 4), offset = owner * 0xd00u;
  uint32_t selection = 0x0055540e + offset;
  switch (type) {
  case 1:
  case 2:
  case 3:
  case 17:
  case 18:
  case 19: {
    uint32_t saved = type == 1    ? 0x004eeb90
                     : type == 2  ? 0x004eeb78
                     : type == 3  ? 0x004eeba0
                     : type == 17 ? 0x004eeb80
                     : type == 18 ? 0x004eeb88
                                  : 0x004eebb0;
    BB_U32(saved) = p;
    if (type == 1 && BB_U32(p + 8)) {
      uint32_t node = entity(BB_U32(p + 8));
      BB_U32(0x004eeb6c) = node;
      return bb_entity_walk_c((void *)(uintptr_t)node, BYTE(p + 5), BYTE(p + 6),
                              BB_U32(0x00555000 + offset) != 0,
                              low_byte(offset, BYTE(p + 6)));
    }
    if (type == 19 && BB_U32(p + 6)) {
      uint32_t node = entity(BB_U32(p + 6));
      BB_U32(0x004eeb6c) = node;
      if (BB_U32(node))
        return bb_entity_stat((void *)(uintptr_t)node, BYTE(p + 10),
                              BB_U32(p + 12));
      return CMD_format_fn(0x0046bae0, (char *)(uintptr_t)0x006033d8,
                           (const char *)(uintptr_t)0x0048417c, BB_U32(p + 6));
    }
    eax = low_byte(eax, BYTE(0x0055540d + offset));
    if (!(uint8_t)eax)
      return eax;
    unsigned i = 0;
    do {
      uint32_t id = BB_U32(selection + i * 4u);
      if (type == 18 || type == 19 || id < 5000) {
        uint32_t node = entity(id);
        BB_U32(0x004eeb6c) = node;
        if (type == 18) {
          uint32_t current = BB_U32(saved);
          bb_entity_cast((void *)(uintptr_t)node, BYTE(current + 5),
                         BYTE(current + 6), BYTE(current + 7));
        } else if (type == 19) {
          uint32_t current = BB_U32(saved);
          if (BB_U32(node))
            bb_entity_stat((void *)(uintptr_t)node, BYTE(current + 10),
                           BB_U32(current + 12));
          else
            CMD_format_fn(0x0046bae0, (char *)(uintptr_t)0x006033d8,
                          (const char *)(uintptr_t)0x0048417c,
                          BB_U32(current + 6));
        } else if (type == 17) {
          if (!BB_U32(node))
            CMD_format_fn(0x0046bae0, (char *)(uintptr_t)0x006033d8,
                          (const char *)(uintptr_t)0x004841a0,
                          BB_U32(BB_U32(0x004eebb0) + 6));
          else if (BYTE(node + 0xd4) == 0x67) {
            uint32_t current = BB_U32(saved);
            uint32_t target = entity(BB_U32(current + 6));
            current = BB_U32(saved);
            uint32_t state = low_byte(current, BYTE(current + 10));
            bb_entity_activity_c((void *)(uintptr_t)BB_U32(0x004eeb6c), state,
                                 (void *)(uintptr_t)target, state);
          }
        } else if (BYTE(node + 0xf7) == owner) {
          if (type == 1) {
            uint8_t kind = BYTE(node + 0xd4);
            if (BB_U32(0x006635a2 + kind * 914u) && kind == 0x67) {
              uint32_t ability = BB_U32(0x00555000 + offset) != 0;
              uint32_t current = BB_U32(saved);
              uint32_t y = low_byte(current, BYTE(current + 6));
              uint32_t again = BB_U32(saved);
              bb_entity_walk_c((void *)(uintptr_t)BB_U32(0x004eeb6c),
                               BYTE(again + 5), y, ability, y);
            }
          } else if (type == 2) {
            uint32_t current = BB_U32(saved);
            uint32_t y = low_byte(current, BYTE(current + 6));
            uint32_t again = BB_U32(saved);
            bb_entity_attack_tile_c((void *)(uintptr_t)BB_U32(0x004eeb6c),
                                    BYTE(again + 5), y, y);
          } else {
            uint32_t current = BB_U32(saved);
            uint32_t target = entity(BB_U32(current + 6));
            bb_entity_command_c((void *)(uintptr_t)BB_U32(0x004eeb6c),
                                (void *)(uintptr_t)target, 1, 1, target);
          }
        }
      }
      ++i;
      eax = BYTE(0x0055540d + offset);
    } while (i < eax);
    return eax;
  }
  case 4:
    BB_U32(0x004eeb8c) = p;
    return (uintptr_t)bb_entity_build(owner, BYTE(p + 5), BYTE(p + 6),
                                      BYTE(p + 7), BYTE(p + 8));
  case 5:
    BB_U32(0x004eebac) = p;
    return bb_terrain_set_entry((void *)(uintptr_t)0x005e470c, BYTE(p + 6),
                                BYTE(p + 7), BYTE(p + 5), 1);
  case 6:
    BB_U32(0x004eeb94) = p;
    BYTE(p + 244) = 0;
    p = BB_U32(0x004eeb94);
    return CMD_two_fn(0x00422040, (void *)(uintptr_t)0x005db050,
                      low_byte(p, BYTE(p + 4)), p + 5);
  case 7: {
    BB_U32(0x004eeb84) = p;
    eax = low_byte(eax, BYTE(p + 5));
    BYTE(0x0055540d + offset) = eax;
    if (!(uint8_t)eax)
      return eax;
    uint32_t source = p + 6, destination = selection;
    unsigned i = 0;
    do {
      BB_U32(destination) = BB_U32(source);
      source += 4;
      destination += 4;
      ++i;
    } while (i < BYTE(0x0055540d + offset));
    return destination;
  }
  case 8:
    BB_U32(0x004eebb8) = p;
    eax = low_byte(eax, BYTE(p + 5));
    BYTE(0x004842b8) = eax;
    return eax;
  case 9:
    BB_U32(0x004eebb4) = p;
    return CMD_three_fn(0x0041cab0, BB_U32(p + 8), BYTE(p + 5),
                        low_byte(eax, BYTE(p + 6)));
  case 10:
    eax = low_byte(eax, (uint8_t)(BYTE(0x004843dc) + 1));
    BYTE(0x004843dc) = eax;
    BB_U32(0x004842c8) = 0;
    return eax;
  case 11:
    eax = low_byte(eax, BYTE(0x004843dc));
    if ((uint8_t)eax) {
      eax = low_byte(eax, (uint8_t)(eax - 1));
      BYTE(0x004843dc) = eax;
    }
    return eax;
  case 12:
    BB_U32(0x004eeb98) = p;
    return CMD_three_fn(0x0041c8e0, owner, BYTE(p + 5),
                        low_byte(eax, BYTE(p + 6)));
  case 13:
    BB_U32(0x004eeb7c) = p;
    return eax;
  case 14:
    BB_U32(0x004eeb64) = p;
    if (!BYTE(p + 5)) {
      int32_t value = bb_random() % 65536;
      uint32_t player = BYTE(0x004843f0);
      WORD(0x004eeb58) = value;
      return bb_command_sync((void *)(uintptr_t)(0x00554fbc + player * 0xd00u),
                             value);
    }
    CMD_format_fn(0x0046bae0, (char *)(uintptr_t)0x006033d8,
                  (const char *)(uintptr_t)0x004841dc,
                  (int32_t)SHORT(0x004eeb58), (int32_t)SHORT(p + 6));
    eax = CMD_method_fn(0x0041de40, (void *)(uintptr_t)0x00662c98, 0x006033d8);
    if (WORD(BB_U32(0x004eeb64) + 6) != WORD(0x004eeb58))
      eax =
          CMD_method_fn(0x0041de40, (void *)(uintptr_t)0x00662c98, 0x004841c0);
    return eax;
  case 15: {
    BB_U32(0x004eeb68) = p;
    uint32_t source = p + 10;
    uint32_t length = 1;
    while (BYTE(source + length - 1))
      ++length;
    uint32_t old_offset = BB_U32(p + 6) * 314u;
    copy(BB_U32(BB_U32(0x00553f90) + old_offset + 0x3c), source, length);
    uint32_t current = BB_U32(0x004eeb68);
    uint32_t id = BB_U32(current + 6), pool = BB_U32(0x00553f90);
    return bb_entity_dirty_c((void *)(uintptr_t)(pool + id * 314u), pool,
                             old_offset);
  }
  case 20: {
    BB_U32(0x004eeb74) = p;
    bb_faction_end_state_entry((void *)(uintptr_t)(0x00554fbc + offset),
                               low_byte(eax, BYTE(p + 5)));
    bb_session_disc_check_entry();
    int32_t value = bb_random() % 30 + 1;
    CMD_format_fn(0x0046bae0, (char *)(uintptr_t)0x006033d8,
                  (const char *)(uintptr_t)0x00484170, CMD_TEXT(0x006afd08),
                  value);
    eax = CMD_open_fn(0x0046bdd0, (const char *)(uintptr_t)0x006033d8,
                      (const char *)(uintptr_t)0x0048416c);
    BB_U32(0x006a9894) = eax;
    if (!eax)
      CMD_registry_fn(BB_U32(0x006ca328), 0x80000001,
                      (const char *)(uintptr_t)0x00484134);
    return CMD_one_fn(0x0046bbd0, BB_U32(0x006a9894));
  }
  case 21:
    BB_U32(0x004eeb9c) = p;
    eax = BB_U32(p + 6);
    BB_U32(0x005e47b2 + BYTE(p + 5) * 4u) = eax;
    return eax;
  case 22:
    BB_U32(0x004eeb60) = p;
    return bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(p + 5),
                             BB_U32(p + 6));
  case 23:
    BB_U32(0x004eeba8) = p;
    eax = BB_U32(p + 6);
    return eax ? bb_entity_remove((void *)(uintptr_t)0x00553f90,
                                  (void *)(uintptr_t)entity(eax))
               : eax;
  case 24:
    BB_U32(0x004eeba4) = p;
    eax = BB_U32(p + 6);
    return eax ? CMD_one_fn(0x0042e000, eax) : CMD_plain_fn(0x0042e060);
  case 25:
    BB_U32(0x004eeb70) = p;
    eax = BB_U32(p + 6);
    if (!eax)
      return bb_sample_play_entry(BB_U32(p + 10));
    {
      uint32_t sample = (uintptr_t)bb_audio_resource_find_entry(
          (void *)(uintptr_t)0x005590e8, (const char *)(uintptr_t)(p + 14));
      uint32_t current = BB_U32(0x004eeb70);
      if (sample) {
        uint32_t id = BB_U32(current + 30);
        if (!id)
          return bb_sound_resource_play_entry((void *)(uintptr_t)sample, 0);
        uint32_t pool = BB_U32(0x00553f90);
        return bb_entity_feedback_c((void *)(uintptr_t)(pool + id * 314u),
                                    (void *)(uintptr_t)sample, 1, 1, pool);
      }
      CMD_format_fn(0x0046bae0, (char *)(uintptr_t)0x006033d8,
                    (const char *)(uintptr_t)0x00484118, current + 14);
      return CMD_method_fn(0x0041de40, (void *)(uintptr_t)0x00662c98,
                           0x006033d8);
    }
  }
  return eax;
}

#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_command_pop_entry(void) {
  __asm__ volatile("pushl %edx;pushl 8(%esp);call " BB_ASM(
      bb_command_pop_c) ";addl $8,%esp;ret $4");
}

#endif
