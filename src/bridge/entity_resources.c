/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"

#ifdef BB_MODEL_RUNTIME
#define EA_format_fn(id,out,fmt,...) EA_FORMAT(out,fmt,__VA_ARGS__)
#else
#define EA_format_fn(id,...) CALL(id,format_fn)(__VA_ARGS__)
#endif

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef int (*format_fn)(char *, const char *, ...);

static uint32_t low_byte(uint32_t value, uint8_t byte) {
  return (value & 0xffffff00u) | byte;
}
static uint32_t faction(uint32_t node) { return BYTE(node + 0xf7) * 0xd00u; }
static int32_t random_remainder(int32_t divisor) {
  return bb_random() % divisor;
}
static uintptr_t text(uintptr_t slot) {
  uintptr_t value = BB_U32(slot);
  return value ? value : 0x0048481c;
}
static uint32_t notice(void) {
  return bb_message_append_entry((void *)(uintptr_t)0x005ecf38, 0x006033d8, 0);
}

/* 0x00441ee0..0x00441f5a and 0x00441f60..0x00441fb3.
 * The resource argument is unused, but belongs to both original ABIs.
 * Keep signed remainders and reads after random callbacks in their order.
 */
uint32_t BB_THISCALL
bb_entity_can_consume(void *object, uint32_t target, uint32_t resource) {
  (void)resource;
  uint32_t node = bb_address_encode(object);
  if (!target)
    return 0;
  if (BYTE(target + 0xd4) != 1)
    return BYTE(node + 0xf7) == BYTE(target + 0xf7) &&
           WORD(BB_U32(target + 0x38)) != 0;
  if ((BYTE(node + 0x111) < 32 || BYTE(node + 0x10d) < 32) &&
      random_remainder(2) == 0)
    return 1;
  return random_remainder(10) == 0;
}
uint32_t BB_THISCALL
bb_entity_can_steal(void *object, uint32_t target, uint32_t resource) {
  (void)resource;
  if (!WORD(BB_U32(target + 0x38)))
    return 0;
  int32_t threshold = random_remainder(32) + 32;
  return BYTE(bb_address_encode(object) + 0x10d) < threshold &&
         random_remainder(3) == 0;
}

/* __ftol converted to a signed 64-bit integer and callers used its low word.
 * Preserve x87 arithmetic precision, the conversion's temporary truncation
 * mode, and the integer-indefinite result for a zero reference price.
 */
#ifdef BB_MODEL_RUNTIME
static uint32_t price_penalty(int32_t price,int64_t reference) {
 volatile double value=(double)price/(double)reference;
 value=value-1.0;value=value*60.0;
 return 0u-(uint32_t)bb_entity_integer(value);
}
#else
static uint32_t price_penalty(int32_t price, int64_t reference) {
  uint16_t original, truncate;
  int64_t result;
  double one = 1.0, sixty = 60.0;
  __asm__ volatile("fnstcw %0" : "=m"(original));
  truncate = original | 0x0c00;
  __asm__ volatile("fildl %2\n\tfildll %3\n\tfdivrp\n\tfsubl %4\n\t"
                   "fmull %5\n\tfldcw %6\n\tfistpll %0\n\tfldcw %1"
                   : "=m"(result)
                   : "m"(original), "m"(price), "m"(reference), "m"(one),
                     "m"(sixty), "m"(truncate)
                   : "st");
  return 0u - (uint32_t)result;
}
#endif

/* 0x00441fc0..0x004422ed: consumption uses the live arrival target, rather
 * than the unused target argument. Save the price before any callback.
 */
uint32_t BB_THISCALL
bb_entity_consume(void *object, uint32_t target, uint32_t resource) {
  (void)target;
  uint32_t node = bb_address_encode(object);
  resource = (uint8_t)resource;
  uint32_t price =
      WORD(0x0055500a + faction(BB_U32(node + 0x2c)) + resource * 2);
  uintptr_t counter = 0;
  switch (resource) {
  case 1:
    bb_entity_adjust(object, 0x101, random_remainder(32) + 32, 0);
    bb_entity_adjust(object, 0x11b, 1, 0);
    bb_entity_command_sound(object, BB_U32(0x005e474c));
    counter = 0x00555c3c;
    break;
  case 7:
    bb_entity_adjust(object, 0x100, random_remainder(32) + 32, 0);
    bb_entity_adjust(object, 0x11b, 1, 0);
    bb_entity_command_sound(object, BB_U32(0x005c78e4));
    counter = 0x00555c40;
    break;
  case 9:
    bb_entity_adjust(object, 0x111, random_remainder(8) + 8, 0);
    bb_entity_adjust(object, 0x101, random_remainder(32) + 32, 0);
    bb_entity_adjust(object, 0x100, random_remainder(8) + 4, 0);
    bb_entity_adjust(object, 0x11b, -3, 0);
    uintptr_t sound = 0x005e474c;
    if (random_remainder(2)) {
      uint8_t part = BYTE(node + 0xf4);
      sound = part == 2 || part == 14 || part == 12 ? 0x00662c78 : 0x005ecf90;
    }
    bb_entity_command_sound(object, BB_U32(sound));
    counter = 0x00555c48;
    break;
  case 12:
    bb_entity_adjust(object, 0x111, random_remainder(8) + 8, 0);
    bb_entity_adjust(object, 0x10d, random_remainder(4) + 4, 0);
    bb_entity_adjust(object, 0x11b, 1, 0);
    break;
  case 18:
    bb_entity_adjust(object, 0x100, random_remainder(32) + 32, 0);
    bb_entity_adjust(object, 0x101, random_remainder(32) + 32, 0);
    bb_entity_adjust(object, 0x11b, 5, 0);
    bb_entity_command_sound(object, BB_U32(0x005e474c));
    counter = 0x00555c44;
    break;
  }
  if (counter) {
    ++BB_U32(counter + faction(node));
    --WORD(BB_U32(BB_U32(node + 0x2c) + 0x38));
    BB_U32(0x00555c2c + faction(node)) += price;
  }
  bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(BB_U32(node + 0x2c) + 0xf7),
                    price);
  BB_U32(node + 0x96) -= price;
  if (BYTE(BB_U32(node + 0x2c) + 0xf7) == BYTE(0x004843f0))
    bb_notification_number_entry((void *)(uintptr_t)0x005eaca8, node, price);
  int32_t penalty = price_penalty(price, WORD(0x005e43f8 + resource * 36));
  uint32_t eax = bb_entity_adjust(object, 0x111, penalty, 0);
  if (penalty < -20)
    eax = bb_entity_adjust(object, 0x10d, penalty / 2, 0);
  if (penalty > 20)
    eax = bb_entity_adjust(object, 0x10d, penalty / 4, 0);
  return eax;
}

/* 0x00442320..0x00442486: the supplied target limits the amount; the live
 * arrival target loses that amount. The sound is played by the saved target.
 */
uint32_t BB_THISCALL
bb_entity_steal(void *object, uint32_t target, uint32_t resource) {
  uint32_t node = bb_address_encode(object);
  int32_t amount = random_remainder(3);
  uint32_t available = WORD(BB_U32(target + 0x38));
  if ((int32_t)available < amount)
    amount = available;
  uint32_t current = BB_U32(node + 0x2c);
  WORD(BB_U32(current + 0x38)) -= amount;
  bb_entity_action_c(object, 3, 8, current, amount);
  ++BB_U32(0x00555c30 + faction(node));
  bb_notification_entity_entry((void *)(uintptr_t)0x005eaca8, node, text(0x00603890));
  uint32_t eax =
      bb_entity_command_sound((void *)(uintptr_t)target, BB_U32(0x006ad1c0));
  eax = low_byte(eax, BYTE(0x004843f0));
  if (BYTE(node + 0xf7) != (uint8_t)eax)
    return eax;
  resource = (uint8_t)resource;
  uintptr_t slot;
  switch (resource) {
  case 7:
    slot = 0x00603880;
    break;
  case 9:
    slot = 0x00603888;
    break;
  case 18:
    slot = 0x00603884;
    break;
  default:
    return resource;
  }
  uintptr_t format = text(slot);
  eax = (uintptr_t)bb_entity_city_name(object);
  EA_format_fn(0x0046bae0,(char *)(uintptr_t)0x006033d8,(const char *)(uintptr_t)format,EA_WORD(eax));
  return notice();
}

/* 0x00454fc0..0x00455230: trade professions, with capacity/cash feedback,
 * faction bookkeeping and escape through the building's second entrance.
 */
uint32_t bb_entity_trade_c(void *object, uint32_t target, uint32_t first,
                           uint32_t second, uint32_t cost, uint32_t eax) {
  uint32_t node = bb_address_encode(object);
  uint8_t part = BYTE(node + 0xf4);
  eax = low_byte(eax, part);
  if ((part != (uint8_t)first && part != (uint8_t)second) || !target)
    return eax;
  if (part == (uint8_t)second)
    cost = 0;
  uint32_t kind = BYTE(target + 0xd4), definition = kind * 914;
  uint32_t owner = faction(target);
  uint32_t count = WORD(0x005559bc + owner + BYTE(0x006635be + definition) * 2);
  uint32_t capacity =
      WORD(0x0055503a + owner + kind * 2) * BYTE(0x006635bf + definition);
  if (part == (uint8_t)first && count >= capacity) {
    if (BYTE(node + 0xf7) == BYTE(0x004843f0)) {
      EA_format_fn(0x0046bae0,(char *)(uintptr_t)0x006033d8,(const char *)(uintptr_t)text(0x00603878),EA_WORD(capacity),EA_WORD(0x00663538 + definition));
      notice();
    }
  } else if (BB_I32(0x00555006 + faction(node)) < (int32_t)cost &&
             BYTE(node + 0xf7) == BYTE(0x004843f0)) {
    EA_format_fn(0x0046bae0,(char *)(uintptr_t)0x006033d8,(const char *)(uintptr_t)text(0x0060387c),EA_WORD(cost));
    notice();
  } else {
    bb_entity_partner_clear(object);
    bb_entity_target_c(object, 0, 0);
    bb_entity_profession(object, part == (uint8_t)first ? second : first);
    bb_entity_target_c(object, part == (uint8_t)first ? target : 0, 0);
    cost = 0u - cost;
    bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(node + 0xf7), cost);
    if (BYTE(node + 0xf7) == BYTE(0x004843f0))
      bb_notification_number_entry((void *)(uintptr_t)0x005eaca8, node, cost);
    BYTE(node + 0xfb) = 0;
  }
  BYTE(node + 0xf6) = 0;
  definition = BYTE(target + 0xd4) * 914;
  uint8_t y = BYTE(target + 0xd9) + BYTE(0x006638b2 + definition);
  uint8_t x = BYTE(target + 0xd8) + BYTE(0x006638ae + definition);
  eax = bb_entity_move_escape(object, x, y);
  BB_U32(node + 0x2c) = 0;
  return eax;
}

/* 0x00435070..0x0043517b: faction counts, goal-tile masks, colours and
 * child ownership. Eight-bit shifts keep the original x86 count masking.
 */
uint32_t bb_entity_transfer_c(void *manager, uint32_t requested,
                              uint32_t target, uint32_t eax) {
  (void)manager;
  uint8_t owner = (uint8_t)requested, old = BYTE(target + 0xf7);
  eax = low_byte(eax, old);
  if (owner == old)
    return eax;
  bb_faction_count_remove((void *)(uintptr_t)0x00554fb8, old, BYTE(target + 0xd4),
                          BYTE(target + 0xf4));
  uint32_t tile = BYTE(target + 0xde) * 192 + BYTE(target + 0xdd);
  BYTE(0x00559b64 + tile * 12) &= (uint8_t)~(1u << (BYTE(target + 0xf7) & 31));
  uint8_t part = BYTE(target + 0xf4), kind = BYTE(target + 0xd4);
  BYTE(target + 0xf7) = owner;
  eax = bb_faction_count_add((void *)(uintptr_t)0x00554fb8, owner, kind, part);
  if (owner == 4)
    BYTE(target + 0xf8) = 4;
  else {
    eax = BYTE(0x00555005 + faction(target));
    BYTE(target + 0xf8) = eax;
  }
  bb_entity_dirty_c((void *)(uintptr_t)target, eax, 0);
  tile = BYTE(target + 0xde) * 192 + BYTE(target + 0xdd);
  BYTE(0x00559b64 + tile * 12) |= (uint8_t)(1u << (BYTE(target + 0xf7) & 31));
  eax = BB_U32(target + 0x10);
  if (!eax)
    return eax;
  BYTE(eax + 0xf7) = BYTE(target + 0xf7);
  eax = BB_U32(target + 0x10);
  BYTE(eax + 0xf8) = BYTE(target + 0xf8);
  return bb_entity_dirty_c(BB_POINTER(target + 0x10), eax, BYTE(target + 0xf8));
}

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_trade_entry(void) {
  __asm__ volatile(
      "pushl %eax\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\t"
      "pushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl %ecx\n\t"
      "call " BB_ASM(bb_entity_trade_c) "\n\taddl $24, %esp\n\tret $16");
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_entity_transfer_entry(void) {
  __asm__ volatile("pushl %eax\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\t"
                   "pushl %ecx\n\tcall " BB_ASM(
                       bb_entity_transfer_c) "\n\t"
                                             "addl $16, %esp\n\tret $8");
}
#endif
