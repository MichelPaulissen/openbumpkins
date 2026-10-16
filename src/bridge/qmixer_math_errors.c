/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* CRT math reporting in C. Result normalization and FPIEEE construction use
 * the shared C dispatcher. Callback records retain their mutable result field. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned index) {
  return BB_U32(stack(input) + 4u * (index + 1u));
}
static void call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 128u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
}
static long double floating_call(uint32_t *input, uintptr_t target,
                                 uint32_t args[16]) {
  call(input, target, args);
  long double result;
  __asm__ volatile("fstpt %0" : "=m"(result) : : "st");
  return result;
}
static double number(uint32_t address) {
  double value;
  memcpy(&value, (const void *)(uintptr_t)address, sizeof(value));
  return value;
}
static void restore_control(uint32_t *input, uint32_t control) {
  uint32_t args[16] = {control, 0xffff};
  call(input, 0x10014020u, args);
}
static void set_errno(uint32_t *input, uint32_t kind) {
  uint32_t args[16] = {kind};
  call(input, 0x10013f40u, args);
}
static long double report(uint32_t *input, uint32_t kind, uint32_t operation,
                          uint32_t x, uint32_t y, uint32_t result,
                          uint32_t control) {
  uint32_t args[16] = {
      kind,           operation,          BB_U32(x),
      BB_U32(x + 4),  y ? BB_U32(y) : 0,  y ? BB_U32(y + 4) : 0,
      BB_U32(result), BB_U32(result + 4), control};
  return floating_call(input, 0x10013e90u, args);
}
static void output(uint32_t *input, long double value) {
  *(long double *)(uintptr_t)input[9] = value;
}

/* 0x100135f0..0x1001363f: unary quiet-NaN domain reporting. */
void bb_qmix_math_domain_unary_c(uint32_t *input) {
  uint32_t s = stack(input), operation = arg(input, 0), control = arg(input, 3);
  if (!BB_U32(0x10020250u))
    output(input, report(input, 1, operation, s + 8, 0, s + 8, control));
  else {
    BB_U32(0x1001fe44u) = 33;
    restore_control(input, control);
    output(input, number(s + 8));
  }
}
/* 0x10013640..0x100136af: binary quiet-NaN sum and domain reporting. */
void bb_qmix_math_domain_binary_c(uint32_t *input) {
  uint32_t s = stack(input), operation = arg(input, 0), control = arg(input, 5);
  double result = number(s + 8) + number(s + 16);
  memcpy((void *)(uintptr_t)(s - 8), &result, sizeof(result));
  if (!BB_U32(0x10020250u))
    output(input, report(input, 1, operation, s + 8, s + 16, s - 8, control));
  else {
    BB_U32(0x1001fe44u) = 33;
    restore_control(input, control);
    output(input, result);
  }
}
static long double exception_result(uint32_t *input, unsigned binary) {
  uint32_t s = stack(input), flags = arg(input, 0), operation = arg(input, 1);
  uint32_t result = s + (binary ? 28u : 20u);
  uint32_t control = result + 8u;
  uint32_t args[16] = {flags, result, BB_U32(control)};
  call(input, 0x10013bd0u, args);
  if (!input[7]) {
    /* Preserve the original logical record storage, including fields left
     * unspecified by the CRT. Its exception builder fills the defined data. */
    uint32_t record = s - 88u;
    if (binary) {
      BB_U32(record + 56) = (BB_U32(record + 56) & ~0x1cu) | 3u;
      BB_U32(record + 40) = BB_U32(s + 20);
      BB_U32(record + 44) = BB_U32(s + 24);
    } else
      BB_U32(record + 56) &= ~1u;
    args[0] = record;
    args[1] = control;
    args[2] = flags;
    args[3] = operation;
    args[4] = s + 12;
    args[5] = result;
    call(input, 0x10013860u, args);
  }
  args[0] = flags;
  call(input, 0x10013fa0u, args);
  uint32_t kind = input[7];
  if (!BB_U32(0x10020250u) && kind)
    return report(input, kind, operation, s + 12, binary ? s + 20 : 0, result,
                  BB_U32(control));
  set_errno(input, kind);
  restore_control(input, BB_U32(control));
  return number(result);
}
/* 0x100136b0..0x1001376b and 0x10013770..0x10013858: unary/binary errors. */
void bb_qmix_math_exception_unary_c(uint32_t *input) {
  output(input, exception_result(input, 0));
}
void bb_qmix_math_exception_binary_c(uint32_t *input) {
  output(input, exception_result(input, 1));
}
/* 0x10013e90..0x10013f3e: operation lookup and mutable _exception callback. */
void bb_qmix_math_report_c(uint32_t *input) {
  uint32_t s = stack(input), kind = arg(input, 0), control = arg(input, 8);
  uint32_t args[16] = {arg(input, 1)};
  call(input, 0x10013f70u, args);
  uint32_t name = input[7];
  if (!name) {
    restore_control(input, control);
    set_errno(input, kind);
    output(input, number(s + 28));
    return;
  }
  /* The original _exception holds type, name, arg1, arg2 and retval. */
  uint32_t record = s - 32u;
  BB_U32(record) = kind;
  BB_U32(record + 4) = name;
  memcpy((void *)(uintptr_t)(record + 8), (const void *)(uintptr_t)(s + 12),
         24);
  restore_control(input, control);
  args[0] = record;
  call(input, 0x10015dc0u, args);
  if (!input[7])
    set_errno(input, kind);
  output(input, number(record + 24));
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  double __attribute__((naked)) name##_entry signature {                       \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;"              \
        "movl %eax,36(%esp);pushl %esp;call " BB_ASM(                          \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;"   \
                      "fldt -2044(%esp);ret");                                 \
  }
ENTRY(bb_qmix_math_domain_unary, (ARG(0), ARG(1), ARG(2), ARG(3)))
ENTRY(bb_qmix_math_domain_binary,
      (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)))
ENTRY(bb_qmix_math_exception_unary,
      (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)))
ENTRY(bb_qmix_math_exception_binary,
      (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)))
ENTRY(bb_qmix_math_report,
      (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)))
#undef ENTRY
#undef ARG
