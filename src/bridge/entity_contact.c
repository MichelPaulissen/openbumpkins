/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "entity_calls.h"
#include "world_controls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define CALL(a, type) ((type)BB_CODE(a))
typedef int32_t (*random_fn)(void);
typedef uint32_t(BB_THISCALL * two_fn)(void *, uint32_t,
                                                     uint32_t);
typedef uint32_t(BB_THISCALL * one_fn)(void *, uint32_t);
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00u) | (uint8_t)byte;
}
static int32_t random_value(void) { return bb_random(); }
/* Retain both the quotient and i386 division faults, including INT_MIN/-1. */
#ifdef BB_MODEL_RUNTIME
static int32_t divide(int32_t value,int32_t divisor,int32_t *remainder) {
 if(!divisor || (value==INT32_MIN && divisor==-1)){raise(SIGFPE);__builtin_unreachable();}
 *remainder=value%divisor;return value/divisor;
}
#else
static int32_t divide(int32_t value, int32_t divisor, int32_t *remainder) {
  int32_t quotient, rest;
  __asm__ volatile("cltd;idivl %2"
                   : "=a"(quotient), "=&d"(rest)
                   : "r"(divisor), "0"(value)
                   : "cc");
  *remainder = rest;
  return quotient;
}
#endif
static uint32_t damage(void *object, uint32_t amount) {
  return
#ifdef BB_MODEL_RUNTIME
BB_GAME_CALL(0x4508a0,bb_address_encode(object), amount, 0)
#else
((two_fn)(uintptr_t)bb_entity_damage_entry)(object, amount, 0)
#endif
;
}
static uint32_t feedback(void *object, uintptr_t slot) {
  uint32_t sample = BB_U32(slot);
  return bb_entity_feedback_c(object, (void *)(uintptr_t)sample, 0, 0, sample);
}
static uintptr_t text(uintptr_t slot) {
  uint32_t value = BB_U32(slot);
  return value ? value : 0x0048481c;
}

/* 0x00449c10: first same-faction city, or the shared UI text buffer. */
void *BB_THISCALL bb_entity_city_name(void *object) {
  uint32_t node = bb_address_encode(object);
  for (uintptr_t p = BB_U32(0x00554f98); p; p = BB_U32(p + 4))
    if (BYTE(p + 0xd4) == 0x62 && BYTE(node + 0xf7) == BYTE(p + 0xf7))
      return BB_POINTER(p + 0x3c);
  return (void *)(uintptr_t)0x006033d8;
}

/* 0x00453450: treatment fee, charged before local-player notification. */
#ifdef BB_MODEL_RUNTIME
static uint32_t fee_penalty(int32_t amount,uint64_t reference) {
 volatile double value=(double)amount/(double)(int64_t)reference;
 value=value-1.0;value=value*60.0;
 return 0u-(uint32_t)bb_entity_integer(value);
}
#else
static uint32_t fee_penalty(int32_t amount, uint64_t reference) {
  uint16_t original, truncate;
  int64_t result;
  double one = 1.0, sixty = 60.0;
  __asm__ volatile("fnstcw %0" : "=m"(original));
  truncate = original | 0x0c00;
  __asm__ volatile(
      "fildl %2;fildll %3;fdivrp;fsubl %4;fmull %5;fldcw %6;fistpll %0;fldcw %1"
      : "=m"(result)
      : "m"(original), "m"(amount), "m"(reference), "m"(one), "m"(sixty),
        "m"(truncate)
      : "st");
  return 0u - (uint32_t)result;
}
#endif
uint32_t BB_THISCALL bb_entity_treatment_fee(void *object,
                                                           uint32_t cost) {
  uint32_t node = bb_address_encode(object);
  BB_U32(node + 0x96) -= cost;
  uint32_t eax = bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(node + 0xf7), cost);
  eax = low_byte(eax, BYTE(node + 0xf7));
  if ((uint8_t)eax == BYTE(0x004843f0))
    bb_notification_number_entry((void *)(uintptr_t)0x005eaca8, node, cost);
  BYTE(node + 0x117) = 6;
  BB_U32(0x00555c28 + BYTE(node + 0xf7) * 0xd00u) += cost;
  return bb_entity_adjust(object, 0x111, fee_penalty(cost, WORD(0x005e4584)),
                          0);
}

/* 0x00453580: selection/attack response by profession. The second DWORD
 * argument is unused but belongs to the original eight-byte stack cleanup.
 */
uint32_t BB_THISCALL
bb_entity_event_feedback(void *object, uint32_t event, uint32_t unused) {
  (void)unused;
  uint32_t node = bb_address_encode(object);
  uint32_t eax = event, part;
  if (event == 2) {
    eax = low_byte(eax, BYTE(node + 0x138));
    if ((uint8_t)eax)
      return eax;
    BYTE(node + 0x138) = 100;
    part = BYTE(node + 0xf4);
    eax = part - 1u;
    uintptr_t slot = 0;
    switch (part) {
    case 1:
    case 2: {
      int32_t choice = random_value() % 2;
      eax = choice;
      if (choice == 0)
        slot = part == 1 ? 0x006634c4 : 0x0060015c;
      else if (choice == 1)
        slot = part == 1 ? 0x00662c80 : 0x00600154;
      break;
    }
    case 3:
      slot = 0x006afd00;
      break;
    case 4:
      slot = 0x00602470;
      break;
    case 5:
      slot = 0x0051344c;
      break;
    case 6:
      slot = 0x006ad1ac;
      break;
    case 7:
      slot = 0x005f9104;
      break;
    case 8:
      slot = 0x005c78d8;
      break;
    case 9:
      slot = 0x005d1134;
      break;
    case 10:
      slot = 0x006ad1c4;
      break;
    case 16:
      slot = 0x006ad1e0;
      break;
    case 19:
      slot = 0x005ecf98;
      break;
    case 26:
      eax = low_byte(eax, BYTE(0x005f746c));
      if ((uint8_t)eax == 0x23)
        slot = 0x005ecf94;
      break;
    }
    return slot ? feedback(object, slot) : eax;
  }
  if (event != 3)
    return eax;
  part = BYTE(node + 0xf4);
  eax = part - 1u;
  switch (part) {
  case 1:
  case 32:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x005e47a8));
  case 2:
  case 33:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x005f9128));
  case 3:
    bb_entity_command_sound(object, BB_U32(0x006634f4));
    return bb_entity_sound(object, BB_U32(0x004843c0));
  case 4:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x006634e0));
  case 6:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x005f51dc));
  case 7:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x00604630));
  case 8:
    bb_entity_command_sound(object, BB_U32(0x006afbd8));
    return bb_entity_sound(object, BB_U32(0x004843c0));
  case 9:
    bb_entity_command_sound(object, BB_U32(0x005e4748));
    return bb_entity_sound(object, BB_U32(0x004843c0));
  case 10:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x005d1148));
  case 15:
  case 22:
  case 23:
  case 25:
  case 29:
  case 30:
    return eax;
  case 16:
    bb_entity_command_sound(object, BB_U32(0x005f915c));
    return bb_entity_sound(object, BB_U32(0x004843c0));
  case 18:
    return bb_entity_command_sound(object, BB_U32(0x006afbe0));
  case 19:
    bb_entity_sound(object, BB_U32(0x004843c0));
    return bb_entity_command_sound(object, BB_U32(0x006b089c));
  case 21:
    return bb_entity_command_sound(object, BB_U32(0x00513424));
  case 24:
  case 28:
    return bb_entity_sound(object, BB_U32(0x004843c4));
  case 26:
    return bb_entity_command_sound(object, BB_U32(0x006a9048));
  case 27:
    return bb_entity_command_sound(object, BB_U32(0x005f51d0));
  case 31:
    return bb_entity_command_sound(object, BB_U32(0x0051343c));
  default:
    return bb_entity_sound(object, BB_U32(0x004843c0));
  }
}

/* 0x00448d70: complete tile-contact dispatch. The target is unchecked;
 * subtype changes can apply to either participant. Disease division uses
 * the captured strain while transmitted bytes remain live after callbacks.
 */
uint32_t BB_THISCALL bb_entity_contact(void *object,
                                                     void *other) {
  uint32_t node = bb_address_encode(object), target = bb_address_encode(other);
  uint32_t kind = BYTE(node + 0xd4), eax = kind - 0x2bu, tk;
  switch (kind) {
  case 0x2b:
    eax = BYTE(target + 0xd4);
    if (eax == 7) {
      eax = low_byte(eax, BYTE(node + 0xf2));
      if ((uint8_t)eax == 0x11) {
        WORD(target + 0xa0) = 1;
        return bb_entity_sound(object, BB_U32(0x00484318));
      }
    }
    return eax;
  case 0x47:
    eax = low_byte(eax, BYTE(node + 0xf2));
    if ((uint8_t)eax != 0x17)
      return eax;
    tk = BYTE(target + 0xd4);
    if (tk == 0x67 || tk == 0x6c)
      return damage(other, random_value() % 50 + 50);
    if (tk == 0x1d || tk == 0x81 || tk == 0xa9) {
      bb_entity_sound(object, BB_U32(0x00484360));
      damage(other, random_value() % 50 + 50);
      BB_U32(node + 0x72) = 0u - BB_U32(node + 0x72);
      eax = 0u - BB_U32(node + 0x6e);
      BB_U32(node + 0x6e) = eax;
      return eax;
    }
    eax = tk * 7u;
    if (BB_U32(0x00663578 + tk * 914u)) {
      BB_U32(node + 0x72) = 0u - BB_U32(node + 0x72);
      BB_U32(node + 0x6e) = 0u - BB_U32(node + 0x6e);
      return bb_entity_sound(object, BB_U32(0x00484360));
    }
    return eax;
  case 0x54:
    eax = BYTE(target + 0xd4);
    return eax == 0x67 ? damage(other, 1000) : eax;
  case 0x5c:
    eax = BYTE(target + 0xd4);
    if (eax != 0x67)
      return eax;
    bb_entity_subtype(object, 0x31);
    BYTE(node + 0xf4) = BYTE(target + 0xf4);
    BYTE(node + 0xf9) = 0;
    return bb_entity_remove((void *)(uintptr_t)0x00553f90, other);
  case 0x67:
    tk = BYTE(target + 0xd4);
    eax = tk - 7u;
    switch (tk) {
    case 7:
      eax = low_byte(eax, BYTE(node + 0xf4));
      if ((uint8_t)eax == 4) {
        WORD(target + 0xa0) = 1;
        return bb_entity_sound(object, BB_U32(0x00484318));
      }
      return (uint8_t)eax == 17 ? damage(object, 1000) : eax;
    case 0x24:
    case 0x5d:
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax)
        return eax;
      eax = low_byte(eax, BYTE(node + 0xf7));
      if (BYTE(target + 0xf7) == (uint8_t)eax)
        return eax;
      BYTE(target + 0xf2) = tk == 0x24 ? 0x12 : 0x14;
      return bb_entity_sound(object,
                             BB_U32(tk == 0x24 ? 0x00484358 : 0x0048435c));
    case 0x25:
      bb_entity_command_sound(object, BB_U32(0x006b0838));
      return bb_entity_subtype(other, 0x18);
    case 0x3a: {
      uint32_t disease = BYTE(target + 0x11e);
      if (!disease)
        return eax;
      eax = low_byte(eax, BYTE(node + 0xf4));
      if ((uint8_t)eax == 3) {
        BYTE(target + 0x11e) = 0;
        return eax;
      }
      int32_t rest;
      int32_t value = random_value();
      eax = divide(value, BB_I32(0x006afb58 + disease * 36u), &rest);
      if (!rest) {
        eax = low_byte(eax, BYTE(target + 0x11e));
        BYTE(node + 0x11e) = eax;
      }
      return eax;
    }
    case 0x47:
      eax = low_byte(eax, BYTE(target + 0xf2));
      return (uint8_t)eax == 0x17 ? damage(object, random_value() % 50 + 50)
                                  : eax;
    case 0x48:
      eax = low_byte(eax, BYTE(node + 0xf7));
      if (BYTE(target + 0xf7) == (uint8_t)eax)
        return eax;
      eax = low_byte(eax, BYTE(target + 0xea));
      if ((uint8_t)eax)
        return eax;
      if (BB_U32(target + 0x1c) && !BYTE(BB_U32(target + 0x1c) + 0xf2))
        bb_entity_subtype(BB_POINTER(target + 0x1c), 0x15);
      return bb_entity_command_sound(object, BB_U32(0x006b0838));
    case 0x5c:
      bb_entity_subtype(other, 0x31);
      BYTE(target + 0xf4) = BYTE(node + 0xf4);
      BYTE(target + 0xf9) = 0;
      return bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
    case 0x67: {
      uint32_t disease = BYTE(node + 0x11e);
      if (disease) {
        if (BYTE(target + 0xf4) == 3)
          BYTE(node + 0x11e) = 0;
        else {
          int32_t rest;
          int32_t value = random_value();
          divide(value, BB_I32(0x006afb58 + disease * 36u), &rest);
          if (!rest)
            BYTE(target + 0x11e) = BYTE(node + 0x11e);
        }
      } else if ((disease = BYTE(target + 0x11e))) {
        if (BYTE(node + 0xf4) == 3)
          BYTE(target + 0x11e) = 0;
        else {
          int32_t rest;
          int32_t value = random_value();
          divide(value, BB_I32(0x006afb58 + disease * 36u), &rest);
          if (!rest)
            BYTE(node + 0x11e) = BYTE(target + 0x11e);
        }
      }
      int strike = 0;
      if (BYTE(node + 0xf7) == BYTE(0x004843f0)) {
        int32_t threshold = random_value() % 48;
        strike = BYTE(node + 0x10d) < threshold &&
                 !(BB_U32(node + 0x9c) & 0x200000) && random_value() % 8 == 0;
      }
      if (strike) {
        eax = damage(other, random_value() % 64 + 32);
        if (!target)
          return eax;
        uint32_t state = SHORT(target + 0xa0) > 0 ? 4 : 5,
                 ticks = state == 4 ? 8 : 16;
        bb_entity_action_c(object, state, ticks, eax, 0);
        uintptr_t message = text(state == 4 ? 0x00603898 : 0x00603894);
        eax = bb_notification_entity_entry((void *)(uintptr_t)0x005eaca8, node, message);
        ++BB_U32(0x00555c30 + BYTE(node + 0xf7) * 0xd00u);
        return eax;
      }
      if (BYTE(node + 0xf4) == 2 && BYTE(target + 0xf4) == 4 &&
          random_value() % 2 == 0)
        feedback(other, 0x006a98c8);
      if (BYTE(node + 0xf4) == 5) {
        if (!BYTE(target + 0x117)) {
          eax = bb_entity_treatment_fee(other, WORD(BB_U32(0x004844a8) + 0x64));
          bb_entity_feedback_c(object, BB_POINTER(0x006b08d8), 0, 0, eax);
        }
      } else if (BYTE(target + 0xf4) == 5 && !BYTE(node + 0x117)) {
        eax = bb_entity_treatment_fee(object, WORD(BB_U32(0x004844a8) + 0x64));
        bb_entity_feedback_c(other, BB_POINTER(0x006b08d8), 0, 0, eax);
      }
      if (BYTE(node + 0xf4) == 2) {
        if (BYTE(target + 0xf4) == 2) {
          if (random_value() % 64 == 0) {
            feedback(object, 0x00600168);
            if (BYTE(target + 0x111) < 64)
              bb_entity_feedback_queue(other, BB_U32(0x006a992c),
                                       BB_POINTER(0x00600168));
          }
        } else if (BYTE(target + 0xf4) == 14) {
          if (random_value() % 32 == 0)
            feedback(object, 0x00662c58);
        }
      } else if (BYTE(node + 0xf4) == 1 && BYTE(target + 0xf4) == 12) {
        if (random_value() % 32 == 0)
          feedback(object, 0x0066350c);
      }
      int32_t rest;
      eax = divide(random_value(), 24, &rest);
      if (rest)
        return eax;
      eax = BB_U32(0x00484288);
      return eax ? eax : bb_entity_pair_entry(object, (void *)(uintptr_t)target);
    }
    case 0x6c:
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax)
        return eax;
      eax = BYTE(node + 0xf4) - 1u;
      switch (BYTE(node + 0xf4)) {
      case 1:
      case 3:
      case 4:
      case 17:
        bb_entity_command_sound(object, BB_U32(0x005e47a8));
        BYTE(target + 0xf2) = 0x27;
        break;
      case 2:
        bb_entity_command_sound(object, BB_U32(0x005f912c));
        BYTE(target + 0xf2) = 0x26;
      }
      return bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
    case 0x8e:
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax == 0x45)
        WORD(target + 0xa0) = 1;
      return eax;
    case 0xa1:
      bb_faction_cash_c((void *)(uintptr_t)0x00554fb8, BYTE(node + 0xf7), 1000);
      bb_entity_remove((void *)(uintptr_t)0x00553f90, other);
      return bb_entity_sound(object, BB_U32(0x00484354));
    case 0xa9:
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax != 0x5f)
        return eax;
      bb_entity_subtype(other, 0x61);
      eax = bb_entity_sound(other, BB_U32(0x00484320));
      BYTE(target + 0xf5) = 24;
      return bb_entity_dirty_c(other, eax, 0);
    default:
      return eax;
    }
  case 0x68:
  case 0x6e:
    eax = BYTE(target + 0xd4);
    if (eax != 0x24 && eax != 0x5d)
      return eax;
    tk = eax;
    eax = BYTE(target + 0xf2);
    if (eax)
      return eax;
    if (kind == 0x68) {
      eax = BYTE(target + 0xf7);
      if (BYTE(node + 0xf7) == eax)
        return eax;
    }
    BYTE(target + 0xf2) = tk == 0x24 ? 0x12 : 0x14;
    return bb_entity_sound(object,
                           BB_U32(tk == 0x24 ? 0x00484358 : 0x0048435c));
  case 0x6c:
    tk = BYTE(target + 0xd4);
    eax = tk - 0x24u;
    switch (tk) {
    case 0x24:
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax)
        return eax;
      bb_entity_subtype(other, 0x12);
      bb_entity_command_sound(object, BB_U32(0x006b0838));
      bb_entity_command_sound(object, BB_U32(0x006afbe0));
      bb_entity_sound(object, BB_U32(0x00484364));
      return bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
    case 0x25:
      bb_entity_command_sound(object, BB_U32(0x006b0838));
      return bb_entity_subtype(other, 0x18);
    case 0x48:
      eax = low_byte(eax, BYTE(target + 0xea));
      if ((uint8_t)eax)
        return eax;
      BYTE(target + 0xea) = 1;
      return bb_entity_command_sound(object, BB_U32(0x006b0838));
    case 0x5d:
      eax = low_byte(eax, BYTE(target + 0xf2));
      if ((uint8_t)eax)
        return eax;
      bb_entity_subtype(other, 0x14);
      bb_entity_command_sound(object, BB_U32(0x006b0838));
      return bb_entity_remove((void *)(uintptr_t)0x00553f90, object);
    case 0x67:
      eax = low_byte(eax, BYTE(node + 0xf2));
      if ((uint8_t)eax)
        return eax;
      switch (BYTE(target + 0xf4)) {
      case 1:
      case 3:
      case 4:
      case 17:
        bb_entity_command_sound(object, BB_U32(0x005e47a8));
        bb_entity_subtype(object, 0x27);
        break;
      case 2:
        bb_entity_command_sound(object, BB_U32(0x005f912c));
        bb_entity_subtype(object, 0x26);
      }
      return bb_entity_remove((void *)(uintptr_t)0x00553f90, other);
    default:
      return eax;
    }
  case 0x8e:
    eax = BYTE(target + 0xd4);
    if (eax == 0x67 || eax == 0x6c) {
      eax = BYTE(node + 0xf2);
      if (eax == 0x45)
        WORD(node + 0xa0) = 1;
    }
    return eax;
  default:
    return eax;
  }
}
