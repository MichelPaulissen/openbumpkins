/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"

#define BYTE(address) BB_U8(address)

/* 0x00433d50..0x00433e16: adjust one node in the depth-ordered double
 * list. Equal depths keep their order; height is only clamped above four.
 * All coordinate/depth arithmetic wraps at 32 bits.
 */
uint32_t BB_THISCALL bb_entity_sort(void *manager, void *object) {
  uint32_t self = bb_address_encode(manager), node = bb_address_encode(object);
  uint32_t height = BB_U32(node + 0x6a);
  BB_U32(node + 0x86) = BB_U32(node + 0x62) + BB_U32(node + 0x66);
  if ((int32_t)height > 0x40000)
    height = 0x40000;
  BB_U32(node + 0x86) += height;
  for (;;) {
    uint32_t previous = BB_U32(node + 8), next = BB_U32(node + 4);
    if (next && BB_I32(next + 0x86) < BB_I32(node + 0x86)) {
      uint32_t after = BB_U32(next + 4);
      if (after)
        BB_U32(after + 8) = node;
      BB_U32(node + 4) = BB_U32(next + 4);
      BB_U32(node + 8) = next;
      BB_U32(next + 4) = node;
      BB_U32(next + 8) = previous;
      if (previous)
        BB_U32(previous + 4) = next;
      else
        BB_U32(self + 0x1008) = next;
    } else if (previous && BB_I32(previous + 0x86) > BB_I32(node + 0x86)) {
      if (next)
        BB_U32(next + 8) = previous;
      uint32_t before = BB_U32(previous + 8);
      if (before)
        BB_U32(before + 4) = node;
      else
        BB_U32(self + 0x1008) = node;
      BB_U32(previous + 4) = BB_U32(node + 4);
      BB_U32(node + 4) = previous;
      BB_U32(node + 8) = BB_U32(previous + 8);
      BB_U32(previous + 8) = node;
    } else {
      return BB_U32(node + 0x86);
    }
  }
}

static int footprint(uint32_t node, uint32_t x, uint32_t y) {
  uint32_t kind = BYTE(node + 0xd4), offset = kind * 914;
  if (!BB_U32(0x00663578 + offset))
    return x == BYTE(node + 0xd8) && y == BYTE(node + 0xd9);
  uint32_t dx = x - BYTE(node + 0xd8), dy = y - BYTE(node + 0xd9);
  uint32_t ix = dx + 3, iy = dy + 3;
  if (ix < 7 && iy < 7 && BB_U32(0x006635c4 + offset + (iy * 7 + ix) * 4))
    return 1;
  ix = dx + 5;
  iy = dy + 5;
  return ix > 0 && ix < 11 && iy > 0 && iy < 11 &&
         BB_U32(0x00663688 + offset + (iy * 11 + ix) * 4);
}

/* 0x00433e20..0x00433f0d and 0x00433f10..0x00434018: the first/next
 * matching tile footprint, including point entities. Coordinates retain
 * their full argument width; the outer mask excludes its zero row/column.
 */
void *BB_THISCALL bb_entity_at(void *manager, uint32_t x, uint32_t y) {
  uint32_t node = BB_U32(bb_address_encode(manager) + 0x1008);
  while (node && !footprint(node, x, y))
    node = BB_U32(node + 4);
  return (void *)(uintptr_t)node;
}
void *BB_THISCALL bb_entity_at_next(void *manager, void *previous, uint32_t x,
                                    uint32_t y) {
  if (!previous)
    return bb_entity_at(manager, x, y);
  uint32_t node = BB_U32(bb_address_encode(previous) + 4);
  while (node && !footprint(node, x, y))
    node = BB_U32(node + 4);
  return (void *)(uintptr_t)node;
}

/* 0x00434020..0x004340ad: this outer-footprint-only search deliberately
 * performs its coordinate differences/additions as wrapping bytes.
 */
void *BB_THISCALL bb_entity_outer_at(void *manager, uint32_t x, uint32_t y) {
  for (uint32_t node = BB_U32(bb_address_encode(manager) + 0x1008); node;
       node = BB_U32(node + 4)) {
    uint32_t offset = BYTE(node + 0xd4) * 914;
    if (!BB_U32(0x00663578 + offset))
      continue;
    uint8_t ix = x - BYTE(node + 0xd8) + 5;
    uint8_t iy = y - BYTE(node + 0xd9) + 5;
    if (ix && ix < 11 && iy && iy < 11 &&
        BB_U32(0x00663688 + offset + (iy * 11 + ix) * 4))
      return (void *)(uintptr_t)node;
  }
  return 0;
}

static void *near(uint32_t node, uint32_t x, uint32_t y, uint32_t radius) {
  int32_t limit = radius << 16;
  for (; node; node = BB_U32(node + 4)) {
    int32_t dx = (uint32_t)BYTE(node + 0xd8) - x;
    int32_t dy = (uint32_t)BYTE(node + 0xd9) - y;
    if (dx > -32 && dx < 32 && dy > -32 && dy < 32) {
      uint32_t index = (uint32_t)dy * 64 + (uint32_t)dx;
      if (BB_I32(0x005fb2f0 + index * 4) <= limit)
        return (void *)(uintptr_t)node;
    }
  }
  return 0;
}

/* 0x004340b0..0x00434110 and 0x00434120..0x0043419d: first/next
 * within a signed Q16 radius, using the game's signed distance table.
 */
void *BB_THISCALL bb_entity_near(void *manager, uint32_t x, uint32_t y,
                                 uint32_t radius) {
  return near(BB_U32(bb_address_encode(manager) + 0x1008), x, y, radius);
}
void *BB_THISCALL bb_entity_near_next(void *manager, void *previous, uint32_t x,
                                      uint32_t y, uint32_t radius) {
  if (!previous)
    return bb_entity_near(manager, x, y, radius);
  return near(BB_U32(bb_address_encode(previous) + 4), x, y, radius);
}

/* 0x00434de0..0x00434e55: signed bounds, wrapping subtraction, an early
 * same-position zero, and the original out-of-table sentinel of 32.0.
 */
uint32_t bb_entity_distance(uint32_t x1, uint32_t y1, uint32_t x2,
                            uint32_t y2) {
  if (x1 == x2 && y1 == y2)
    return 0;
  uint32_t ax = (int32_t)x2 >= (int32_t)x1 ? x2 - x1 : x1 - x2;
  uint32_t ay = (int32_t)y2 >= (int32_t)y1 ? y2 - y1 : y1 - y2;
  if ((int32_t)ax >= 28 || (int32_t)ay >= 28)
    return 0x200000;
  return BB_U32(0x005fb2f0 + ((y2 - y1) * 64 + x2 - x1) * 4);
}

/* 0x004341a0..0x00434210: kind/faction and coordinates use only their
 * low byte. Strict improvement keeps the first node at equal distance.
 */
void *BB_THISCALL bb_entity_nearest(void *manager, uint32_t faction,
                                    uint32_t kind, uint32_t x, uint32_t y) {
  uint32_t result = 0;
  int32_t best = 0x1000000;
  for (uint32_t node = BB_U32(bb_address_encode(manager) + 0x1008); node;
       node = BB_U32(node + 4)) {
    if (BYTE(node + 0xd4) != (uint8_t)kind ||
        BYTE(node + 0xf7) != (uint8_t)faction)
      continue;
    int32_t distance = bb_entity_distance((uint8_t)x, (uint8_t)y,
                                          BYTE(node + 0xd8), BYTE(node + 0xd9));
    if (distance < best) {
      best = distance;
      result = node;
    }
  }
  return (void *)(uintptr_t)result;
}

/* 0x00434220..0x004342bc: class two reserves the first unassigned kind
 * two in this manager; other classes query kind 0x8f in the global manager.
 * A failed reservation retains the entity's existing target.
 */
void *BB_THISCALL bb_entity_assign_target(void *manager, void *object) {
  uint32_t node = bb_address_encode(object);
  if (BYTE(0x005e5eff + BYTE(node + 0xf4) * 292) != 2) {
    void *target =
        bb_entity_nearest((void *)0x00553f90, BYTE(node + 0xf7), 0x8f,
                          BYTE(node + 0xd8), BYTE(node + 0xd9));
    BB_U32(node + 0x1c) = bb_address_encode(target);
    return target;
  }
  for (uint32_t target = BB_U32(bb_address_encode(manager) + 0x1008); target;
       target = BB_U32(target + 4)) {
    if (BYTE(target + 0xd4) == 2 && BYTE(target + 0xf7) == BYTE(node + 0xf7) &&
        !BB_U32(target + 0x20)) {
      BB_U32(target + 0x20) = node;
      BB_U32(node + 0x1c) = target;
      return (void *)(uintptr_t)target;
    }
  }
  return BB_POINTER(node + 0x1c);
}

/* 0x00435270..0x004352c4 and 0x004352d0..0x00435324: update wrapping
 * tile occupancy for active-subtype kind 0xa9 nodes. Retain the original
 * empty-list EAX and byte-register behavior at the binary boundary.
 */
static uint64_t occupancy(void *manager, uint32_t eax, uint32_t edx,
                          int delta) {
  uint32_t node = BB_U32(bb_address_encode(manager) + 0x1008);
  if (node)
    eax = 0xa9;
  for (; node; node = BB_U32(node + 4)) {
    edx = (edx & 0xffffff00) | BYTE(node + 0xd4);
    if ((uint8_t)edx != 0xa9)
      continue;
    edx = (edx & 0xffffff00) | BYTE(node + 0xf2);
    if ((uint8_t)edx) {
      edx = BYTE(node + 0xd9) * 192 + BYTE(node + 0xd8);
      BYTE(0x00559b66 + edx * 12) += delta;
    }
  }
  return ((uint64_t)edx << 32) | eax;
}
uint64_t bb_entity_occupancy_add_c(void *self, uint32_t eax, uint32_t edx) {
  return occupancy(self, eax, edx, 1);
}
uint64_t bb_entity_occupancy_remove_c(void *self, uint32_t eax, uint32_t edx) {
  return occupancy(self, eax, edx, -1);
}

/* 0x00437f10..0x00437f27: a visible entity gets twice the buffer count,
 * with byte overflow; invisible entities leave dirty state unchanged.
 */
uint32_t bb_entity_force_dirty_c(void *object, uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  eax = (eax & 0xffffff00) | BYTE(node + 0x128);
  if (BYTE(node + 0x128)) {
    eax = BB_U32(0x00484454);
    eax = (eax & 0xffffff00) | (uint8_t)((uint8_t)eax * 2);
    BYTE(node + 0x129) = eax;
  }
  return eax;
}

/* 0x00438220..0x0043828d and 0x00438290..0x004382d3: ability lists
 * use a signed count and the entity's part byte. Leaving ability 0x1a
 * clears its flag even when the requested replacement is unavailable.
 */
uint32_t BB_THISCALL bb_entity_set_ability(void *object, uint32_t ability) {
  uint32_t node = bb_address_encode(object);
  uint8_t requested = ability;
  if (BYTE(node + 0xfc) == 0x1a && requested != 0x1a)
    BB_U32(node + 0x9c) &= 0xfbffffff;
  uint32_t offset = BYTE(node + 0xf4) * 292;
  int32_t count = BB_I32(0x005e5eee + offset);
  for (int32_t i = 0; i < count; ++i) {
    if (BYTE(0x005e5ee8 + offset + i) == requested) {
      BYTE(node + 0xfc) = requested;
      return count;
    }
  }
  BYTE(node + 0xfc) = 0;
  return count;
}
uint32_t BB_THISCALL bb_entity_has_ability(void *object, uint32_t ability) {
  uint32_t offset = BYTE(bb_address_encode(object) + 0xf4) * 292;
  int32_t count = BB_I32(0x005e5eee + offset);
  for (int32_t i = 0; i < count; ++i)
    if (BYTE(0x005e5ee8 + offset + i) == (uint8_t)ability)
      return 1;
  return 0;
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_SORT_ASM "_bb_entity_sort"
#define BB_OCC_ADD_ASM "_bb_entity_occupancy_add_c"
#define BB_OCC_REMOVE_ASM "_bb_entity_occupancy_remove_c"
#define BB_FORCE_DIRTY_ASM "_bb_entity_force_dirty_c"
#else
#define BB_SORT_ASM "bb_entity_sort"
#define BB_OCC_ADD_ASM "bb_entity_occupancy_add_c"
#define BB_OCC_REMOVE_ASM "bb_entity_occupancy_remove_c"
#define BB_FORCE_DIRTY_ASM "bb_entity_force_dirty_c"
#endif

/* The sort and occupancy leaves preserve observable ECX/EDX state and
 * end with a zero test. Force-dirty preserves EDX and the flags from
 * testing visibility or doubling the buffer-count byte.
 */
void __attribute__((naked)) bb_entity_sort_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl 8(%esp)\n\tcall " BB_SORT_ASM
                   "\n\tpopl %ecx\n\tmovl 4(%esp), %edx\n\t"
                   "testl $0, %eax\n\tret $4");
}
#define BB_OCCUPANCY_ENTRY(symbol)                                             \
  __asm__ volatile("pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl %ecx\n\t"  \
                   "call " symbol "\n\taddl $12, %esp\n\tpopl %ecx\n\t"        \
                   "cmpl $0, 0x1008(%ecx)\n\tje 1f\n\txorl %ecx, %ecx\n"       \
                   "1: testl $0, %eax\n\tret")
void __attribute__((naked)) bb_entity_occupancy_add_entry(void) {
  BB_OCCUPANCY_ENTRY(BB_OCC_ADD_ASM);
}
void __attribute__((naked)) bb_entity_occupancy_remove_entry(void) {
  BB_OCCUPANCY_ENTRY(BB_OCC_REMOVE_ASM);
}
void __attribute__((naked)) bb_entity_force_dirty_entry(void) {
  __asm__ volatile("pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl %ecx\n\t"
                   "call " BB_FORCE_DIRTY_ASM
                   "\n\taddl $8, %esp\n\tpopl %edx\n\tpopl %ecx\n\t"
                   "cmpb $0, 0x128(%ecx)\n\tje 1f\n\tpushl %edx\n\t"
                   "movl 0x00484454, %edx\n\taddb %dl, %dl\n\tpopl %edx\n"
                   "1: ret");
}

#endif
