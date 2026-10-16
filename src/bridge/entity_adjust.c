/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "entity_calls.h"

#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define SHORT(a) BB_I16(a)
#define SCRATCH BB_U32(0x006ba938)

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

static uint32_t low_byte(uint32_t value, uint32_t byte) {
  return (value & 0xffffff00) | (uint8_t)byte;
}
static uint32_t test_flags(uint32_t value, uint32_t sign) {
  return (value ? 0 : 0x40) | (value & sign ? 0x80 : 0) |
         (__builtin_parity((uint8_t)value) ? 0 : 4);
}
static uint32_t cmp_flags(uint32_t a, uint32_t b, uint32_t mask) {
  uint32_t value = (a - b) & mask, sign = (mask >> 1) + 1;
  return test_flags(value, sign) | (a < b) | ((a ^ b ^ value) & 0x10) |
         (((a ^ b) & (a ^ value) & sign) ? 0x800 : 0);
}

/* Keep the executable's full scratch stores, wrapping additions and signed
 * comparisons. The return precedes the negative clamp: subsequent AL writes
 * can leave its upper bytes intact even though the stored field is zero.
 */
static uint32_t add_limit(uint32_t value, uint32_t delta, uint32_t limit) {
  SCRATCH = value;
  value += delta;
  SCRATCH = value;
  if ((int32_t)SCRATCH > (int32_t)limit)
    SCRATCH = limit;
  value = SCRATCH;
  if ((int32_t)value < 0)
    SCRATCH = 0;
  return value;
}
static uint32_t add_current(uint32_t value, uint32_t delta, uint32_t maximum) {
  SCRATCH = value;
  value += delta;
  SCRATCH = value;
  if ((int32_t)maximum < (int32_t)SCRATCH)
    SCRATCH = maximum;
  value = SCRATCH;
  if ((int32_t)value < 0)
    SCRATCH = 0;
  return value;
}

/* The feedback entry is itself reconstructed C. Its register ABI allows us
 * to capture the same volatile outputs and flags on this optional call path.
 */
#ifdef BB_MODEL_RUNTIME
static void feedback(void *object,uint32_t edx,struct bb_entity_adjust_result *result) {
 uint32_t sound=BB_U32(0x5c5b68);struct bb_call_state state={sound,bb_address_encode(object),edx,0x202};
 bb_model_invoke(&state,0x453a00,(uint32_t[]){sound,0,0},3);
 result->eax=state.eax;result->edx=state.edx;result->ecx=state.ecx;result->flags=state.flags&0x8d5;
}
#else
static void feedback(void *object, uint32_t edx,
                     struct bb_entity_adjust_result *result) {
  uint32_t eax = BB_U32(0x005c5b68), flags;
  uintptr_t ecx = bb_address_encode(object);
  __asm__ volatile("pushl $0\n\tpushl $0\n\tpushl %0\n\tcall " BB_ASM(
                       bb_entity_feedback_entry) "\n\tpushfl\n\tpopl %3"
                   : "+a"(eax), "+d"(edx), "+c"(ecx), "=&r"(flags)
                   :
                   : "cc", "memory");
  result->eax = eax;
  result->edx = edx;
  result->ecx = ecx;
  result->flags = flags & 0x8d5;
}
#endif

/* Complete entries: 0x00453ee0, 0x00453f80, 0x00454050, 0x004540a0,
 * 0x00454140, 0x004541e0, 0x00454280, 0x004542d0, 0x00454320 and
 * 0x00453430. Field IDs select the original packed entity offsets; paired
 * byte fields put their maximum immediately after their current value.
 */
void bb_entity_adjust_c(void *object, uint32_t field, uint32_t amount,
                        uint32_t maximum_delta, uint32_t edx,
                        struct bb_entity_adjust_result *result) {
  uint32_t node = bb_address_encode(object);
  result->ecx = node;
  if (field == 0x38) {
    uint32_t eax = BB_U32(node + 0x38);
    uint32_t flags = test_flags(eax, 0x80000000);
    if (eax) {
      edx = amount;
      WORD(eax) += (uint16_t)amount;
      eax = BB_U32(node + 0x38);
      uint32_t value = WORD(eax);
      flags = cmp_flags(value, 24, 0xffff);
      if (value > 24)
        WORD(eax) = 24;
    }
    result->eax = eax;
    result->edx = edx;
    result->flags = flags;
    return;
  }
  if (field == 0xa0) {
    add_limit((uint32_t)(int32_t)SHORT(node + 0xa2), maximum_delta, 65535);
    uint32_t maximum = SCRATCH;
    edx = (uint32_t)(int32_t)SHORT(node + 0xa0);
    WORD(node + 0xa2) = maximum;
    uint32_t before =
        add_current(edx, amount, (uint32_t)(int32_t)SHORT(node + 0xa2));
    uint32_t eax = SCRATCH;
    WORD(node + 0xa0) = eax;
    result->eax = eax;
    result->edx = edx;
    result->flags = test_flags(before, 0x80000000);
    return;
  }
  uint32_t eax;
  if (field == 0x109 || field == 0x111 || field == 0x10d || field == 0x10f) {
    uint8_t old = BYTE(node + field);
    if (field == 0x109)
      edx = low_byte(edx, old);
    add_limit(BYTE(node + field + 1), maximum_delta, 255);
    BYTE(node + field + 1) = (uint8_t)SCRATCH;
    if (field != 0x109)
      edx = amount;
    eax = add_current(BYTE(node + field), amount, BYTE(node + field + 1));
    result->flags = test_flags(eax, 0x80000000);
    if (field == 0x109) {
      uint8_t current = (uint8_t)SCRATCH;
      eax = low_byte(eax, BYTE(node + 0xf4));
      BYTE(node + field) = current;
      result->flags = cmp_flags((uint8_t)eax, 9, 255);
      if ((uint8_t)eax == 9) {
        eax = low_byte(eax, BYTE(node + field + 1));
        result->flags = cmp_flags(current, (uint8_t)eax, 255);
        if (current == (uint8_t)eax) {
          result->flags = cmp_flags((uint8_t)eax, old, 255);
          if ((uint8_t)eax > old) {
            feedback(object, edx, result);
            return;
          }
        }
      }
    } else {
      eax = low_byte(eax, (uint8_t)SCRATCH);
      BYTE(node + field) = eax;
    }
  } else {
    edx = amount;
    eax = add_limit(BYTE(node + field), amount, field == 0x113 ? 100 : 255);
    result->flags = test_flags(eax, 0x80000000);
    eax = low_byte(eax, (uint8_t)SCRATCH);
    BYTE(node + field) = eax;
  }
  result->eax = eax;
  result->edx = edx;
}

uint32_t bb_entity_adjust(void *object, uint32_t field, uint32_t amount,
                          uint32_t maximum_delta) {
  struct bb_entity_adjust_result result;
  bb_entity_adjust_c(object, field, amount, maximum_delta, 0, &result);
  return result.eax;
}

/* Preserve the original ECX/EDX and arithmetic flags even when a negative
 * value was clamped in memory but returned through a partial-byte write.
 * Callback outputs come from the reconstructed feedback entry above.
 */
#ifndef BB_MODEL_RUNTIME
#define ENTRY(name, field, maximum, cleanup)                                   \
  __attribute__((naked)) void name(void) {                                     \
    __asm__ volatile(                                                          \
        "pushfl\n\tpushl %ecx\n\tsubl $16, %esp\n\tmovl %esp, %eax\n\t"        \
        "pushl %eax\n\tpushl %edx\n\t" maximum                                 \
        "pushl 40(%esp)\n\tpushl $" #field "\n\tpushl %ecx\n\tcall " BB_ASM(   \
            bb_entity_adjust_c) "\n\taddl $24, %esp\n\tmovl 20(%esp), "        \
                                "%edx\n\t"                                     \
                                "andl $0xfffff72a, %edx\n\torl 12(%esp), "     \
                                "%edx\n\t"                                     \
                                "pushl %edx\n\tpopfl\n\tmovl (%esp), %eax\n\t" \
                                "movl 4(%esp), %edx\n\tmovl 8(%esp), %ecx\n\t" \
                                "leal 24(%esp), %esp\n\tret $" #cleanup);      \
  }
#else
#define ENTRY(...)
#endif
ENTRY(bb_entity_adjust_health_entry, 0xa0, "pushl 40(%esp)\n\t", 8)
ENTRY(bb_entity_adjust_109_entry, 0x109, "pushl 40(%esp)\n\t", 8)
ENTRY(bb_entity_adjust_113_entry, 0x113, "pushl $0\n\t", 4)
ENTRY(bb_entity_adjust_111_entry, 0x111, "pushl 40(%esp)\n\t", 8)
ENTRY(bb_entity_adjust_10d_entry, 0x10d, "pushl 40(%esp)\n\t", 8)
ENTRY(bb_entity_adjust_10f_entry, 0x10f, "pushl 40(%esp)\n\t", 8)
ENTRY(bb_entity_adjust_100_entry, 0x100, "pushl $0\n\t", 4)
ENTRY(bb_entity_adjust_101_entry, 0x101, "pushl $0\n\t", 4)
ENTRY(bb_entity_adjust_11b_entry, 0x11b, "pushl $0\n\t", 4)
ENTRY(bb_entity_adjust_resources_entry, 0x38, "pushl $0\n\t", 4)
