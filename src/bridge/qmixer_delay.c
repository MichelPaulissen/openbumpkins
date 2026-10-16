/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <signal.h>
#include <string.h>
#define QWORD(a) BB_U64(a)
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
/* C forms of QMIXER float and wide-integer delay effects.
 * Address-derived labels retain provenance for auditing phase transitions,
 * arithmetic boundaries and callback ordering.
 */
static uint32_t mask_for(unsigned bits) {
  return bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
}
static uint32_t common_flags(uint32_t value, unsigned bits) {
  value &= mask_for(bits);
  uint32_t low = value & 255u;
  low ^= low >> 4;
  uint32_t parity = (0x9669u >> (low & 15u)) & 1u;
  return (!value ? 0x40u : 0u) | (value & (1u << (bits - 1)) ? 0x80u : 0u) |
         (parity << 2);
}
static uint32_t logic_flags(uint32_t flags, uint32_t value, unsigned bits) {
  return (flags & ~0x8d5u) | common_flags(value, bits);
}
static uint32_t add_flags(uint32_t flags, uint32_t a, uint32_t b,
                          unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1);
  a &= mask;
  b &= mask;
  uint32_t result = (a + b) & mask;
  return logic_flags(flags, result, bits) | ((uint64_t)a + b > mask ? 1u : 0u) |
         (((a ^ b ^ result) & 16u) ? 16u : 0u) |
         ((~(a ^ b) & (a ^ result) & sign) ? 0x800u : 0u);
}
static uint32_t sub_flags(uint32_t flags, uint32_t a, uint32_t b,
                          unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1);
  a &= mask;
  b &= mask;
  uint32_t result = (a - b) & mask;
  return logic_flags(flags, result, bits) | (a < b ? 1u : 0u) |
         (((a ^ b ^ result) & 16u) ? 16u : 0u) |
         (((a ^ b) & (a ^ result) & sign) ? 0x800u : 0u);
}
static uint32_t __attribute__((unused))
shift_flags(uint32_t flags, uint32_t before, uint32_t after, unsigned bits,
            unsigned count, unsigned right) {
  count &= 31u;
  if (!count)
    return flags;
  uint32_t carry = right ? ((before >> (count - 1u)) & 1u)
                         : ((before >> (bits - count)) & 1u);
  uint32_t overflow = right == 2 ? 0
                      : right    ? (before >> (bits - 1u)) & 1u
                                 : ((after >> (bits - 1u)) & 1u) ^ carry;
  return logic_flags(flags, after, bits) | carry | (overflow ? 0x800u : 0u);
}
static void __attribute__((noreturn, unused)) division_fault(void) {
  raise(SIGFPE);
  __builtin_unreachable();
}
static uint32_t __attribute__((unused))
borrow_flags(uint32_t flags, uint32_t a, uint32_t b, unsigned bits) {
  uint32_t mask = mask_for(bits), sign = 1u << (bits - 1), carry = flags & 1u;
  a &= mask;
  b &= mask;
  uint32_t r = (a - b - carry) & mask;
  return logic_flags(flags, r, bits) |
         ((uint64_t)a < (uint64_t)b + carry ? 1u : 0u) |
         (((a ^ b ^ r) & 16u) ? 16u : 0u) |
         (((a ^ b) & (a ^ r) & sign) ? 0x800u : 0u);
}

#ifdef BB_MODEL_RUNTIME
#include "qmixer_numeric_portable.h"
#else
/* Preserve the original x87 truncation boundary, including control-word
 * restoration and 64-bit invalid/overflow results used by original callers. */
static void __attribute__((unused))
sample_float_to_int(struct bb_call_state *state,
                    const long double *value) {
  uint16_t control, truncated;
  int64_t result;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  truncated = control | 0x0c00u;
  __asm__ volatile("fldt %2;fldcw %3;fistpq %0;fldcw %1"
                   : "=m"(result)
                   : "m"(control), "m"(*value), "m"(truncated)
                   : "st", "memory");
  state->eax = (uint32_t)result;
  state->edx = (uint64_t)result >> 32;
  state->flags = logic_flags(state->flags, (control >> 8) | 12u, 8);
}
/* Numeric comparison compatibility while the i386 boundary remains. */
static uint16_t __attribute__((unused)) sample_compare(const long double *a,
                                                       const long double *b) {
  uint16_t status;
  __asm__ volatile("fldt %1;fldt %2;fxch;fcompp;fnstsw %0"
                   : "=am"(status)
                   : "m"(*a), "m"(*b)
                   : "st", "memory");
  return status & 0x4500u;
}
#define F32(a) BB_FLOAT(a)
#define F64(a) BB_DOUBLE(a)
/* C forms of QMIXER sample buffers and mixing state. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */

static uint16_t __attribute__((unused)) qmix_status(uint16_t comparison,
                                                    unsigned depth) {
  uint16_t status;
  __asm__ volatile("fnstsw %0" : "=am"(status));
  return (status & 0x2ffu) | comparison | (((8u - depth) & 7u) << 11);
}
#endif
#define SAMPLE_CALL(target, consumed, returned)                                \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                   \
    BB_QMIX_DISPATCH(&call, (uintptr_t)(target), args + sp, esp, edi, esi, ebp,    \
                 ebx, returned);                                               \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

#ifdef BB_MODEL_RUNTIME
#include "qmixer_lifetime.h"
#define exception_head bb_qmix_model_exception_head
#define exception_link bb_qmix_model_exception_link
#else
/* The working i386 path retains its registration chain; portable cleanup
 * uses the same C-owned state maps through explicit logical owner frames. */
static uint32_t exception_head(void) {
  uint32_t value;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(value));
  return value;
}
static void exception_link(uint32_t value) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(value) : "memory");
}
#endif
void bb_qmix_float_delay_create_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000eb4eu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000eb13u); /* 1000eb0e */
  BB_U32(ebp + 0xfffffff0u) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  esi = eax;
  eax = 0x0u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000eb36;
  eax = BB_U32(ebp + 0x8u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000eb70u, 4, 0x1000eb36u); /* 1000eb31 */
L_1000eb36:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_create_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_float_delay_free_failed_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000eb61u); /* 1000eb5c */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_float_delay_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_free_failed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000ec3fu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = (ebx ^ ebx);
  BB_U32(ebp + 0xfffffff0u) = ecx;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_base);
  edi = ecx;
  esi = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xfffffffcu) = ebx;
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  BB_U32(edi) = BB_QMIX_VTABLE(bb_qmix_vtable_delay_base);
  eax = BB_U32(esi + 0x4u);
  BB_U32(edi + 0x4u) = eax;
  BB_U32(edi) = BB_QMIX_VTABLE(bb_qmix_vtable_float_delay);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000ebc3u); /* 1000ebbe */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(edi + 0xcu) = eax;
  BB_U32(edi + 0x8u) = ebx;
  a = BB_U32(edi + 0x4u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ec02;
  ecx = (ecx ^ ecx);
L_1000ebd3:;
  eax = BB_U32(esi);
  edx = BB_U32(edi + 0xcu);
  eax = BB_U32(eax + ecx + 0x4u);
  BB_U32(edx + ecx + 0x4u) = eax;
  edx = BB_U32(esi);
  eax = BB_U32(edx + ecx);
  edx = BB_U32(edi + 0xcu);
  BB_U32(edx + ecx) = eax;
  edx = BB_U32(edi + 0xcu);
  eax = BB_U32(edx + ecx);
  a = BB_U32(edi + 0x8u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000ebf9;
  BB_U32(edi + 0x8u) = eax;
L_1000ebf9:;
  ecx = (ecx + 0x8u);
  ebx = (ebx + 1u);
  a = BB_U32(edi + 0x4u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000ebd3;
L_1000ec02:;
  eax = BB_U32(edi + 0x8u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000ec0eu); /* 1000ec09 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = edi;
  BB_U32(edi + 0x10u) = eax;
  SAMPLE_CALL(0x1000f120u, 0, 0x1000ec1bu); /* 1000ec16 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  eax = edi;
  ecx = BB_U32(ebp + 0xfffffff4u);
  exception_link(ecx);
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_constructor_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_float_delay_unwind_base_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 1000ec3a */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_float_delay_unwind_base_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_unwind_base_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_unwind_intermediate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000ec80u, 0, BB_U32(esp - 4u)); /* tail call 1000ec4c */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_unwind_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_unwind_intermediate_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_delete_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x1000ed40u, 0, 0x1000ec68u); /* 1000ec63 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000ec78;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000ec75u); /* 1000ec70 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000ec78:;
  eax = esi;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_delay_base_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000ecb7u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(ebp + 0xfffffff0u) = ecx;
  SAMPLE_CALL(0x1000ecc1u, 0, 0x1000ecaau); /* 1000eca5 */
  eax = BB_U32(ebp + 0xfffffff4u);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  exception_link(eax);
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_delay_base_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_delay_base_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_delay_base_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 1000ecc4 */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_delay_base_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_delay_base_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_delay_base_delete_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000ed1fu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(ebp + 0xfffffff0u) = ecx;
  SAMPLE_CALL(0x1000ed29u, 0, 0x1000ecfau); /* 1000ecf5 */
  a = BYTE(ebp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000ed0c;
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000ed09u); /* 1000ed04 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000ed0c:;
  eax = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(ebp + 0xfffffff4u);
  exception_link(ecx);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_delay_base_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_delay_base_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_delay_base_delete_cleanup_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 1000ed2c */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_delay_base_delete_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_delay_base_delete_cleanup_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000ed9fu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_float_delay);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ecx + 0x10u);
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000ed74u); /* 1000ed6f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000ed83u); /* 1000ed7e */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x1000eda9u, 0, 0x1000ed92u); /* 1000ed8d */
  eax = BB_U32(ebp + 0xfffffff4u);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  exception_link(eax);
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_float_delay_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_cleanup_intermediate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000ec80u, 0, BB_U32(esp - 4u)); /* tail call 1000edac */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_cleanup_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_cleanup_intermediate_c) ";leal "
                                                  "4(%esp),%esp;popal;popfl;"
                                                  "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_process_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x18u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = ecx;
  esi = BB_U32(esp + 0x2cu);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x10u);
  a = BB_U32(esp + 0x34u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000edf3;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000ede7;
  eax = BB_U32(ebx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x1000eddfu); /* 1000eddc indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000efe4;
L_1000ede7:;
  eax = BB_U32(esp + 0x34u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000c030u, 4, 0x1000edf3u); /* 1000edee */
L_1000edf3:;
  edx = BB_U32(esp + 0x2cu);
  ebp = BB_U32(esi + 0x4u);
  BB_U32(esp + 0x18u) = ebp;
  args[sp + 6u] = BB_U32(esp + 24u);
  eax = BB_U32(edx + 0x4u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(edx + 0x10u);
  a = BB_U32(esp + 0x34u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000ee47;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000ee3b;
  ecx = BB_U32(edx + 0xcu);
  esi = BB_U32(esp + 0x34u);
  ecx = (ecx << 2);
  eax = BB_U32(esp + 0x14u);
  ecx = (ecx >> 2);
  edi = (eax + esi * 4u);
  eax = (eax ^ eax);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(esp + 0x10u) = 0x1000eff0u;
  args[sp + 4u] = BB_U32(esp + 16u);
  esi = BB_U32(esp + 0x34u);
  BB_U32(edx + 0x10u) = esi;
  goto L_1000ee4f;
L_1000ee3b:;
  eax = BB_U32(esp + 0x34u);
  ecx = edx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000c030u, 4, 0x1000ee47u); /* 1000ee42 */
L_1000ee47:;
  BB_U32(esp + 0x10u) = 0x1000f060u;
  args[sp + 4u] = BB_U32(esp + 16u);
L_1000ee4f:;
  eax = (eax ^ eax);
  BB_U32(esp + 0x24u) = eax;
  args[sp + 9u] = BB_U32(esp + 36u);
  a = BB_U32(ebx + 0x4u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ef29;
  BB_U32(esp + 0x20u) = eax;
  args[sp + 8u] = BB_U32(esp + 32u);
L_1000ee62:;
  edi = BB_U32(ebx + 0xcu);
  eax = (esp + 0x34u);
  edi = (edi + BB_U32(esp + 0x20u));
  ecx = BB_U32(edi);
  BB_U32(esp + 0x1cu) = ecx;
  args[sp + 7u] = BB_U32(esp + 28u);
  a = BB_U32(esp + 0x34u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000ee7d;
  eax = (esp + 0x1cu);
L_1000ee7d:;
  esi = BB_U32(eax);
  ecx = BB_U32(ebx + 0x14u);
  a = ecx;
  b = BB_U32(esp + 0x1cu);
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x80u))
    goto L_1000ee8b;
  ecx = (ecx + BB_U32(ebx + 0x8u));
L_1000ee8b:;
  ebp = BB_U32(ebx + 0x8u);
  ebp = (ebp - ecx);
  a = ebp;
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000eec8;
  eax = BB_U32(edi + 0x4u);
  edi = (edi + 0x4u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ecx * 4u);
  ecx = BB_U32(esp + 0x1cu);
  a = eax;
  b = BB_U32(ebx + 0x10u);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(esp + 0x20u), 0, 0x1000eeb0u); /* 1000eeac indirect */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  eax = esi;
  a = eax;
  b = ebp;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  ecx = BB_U32(edi);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(ebx + 0x10u);
  eax = BB_U32(esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (eax + ebp * 4u);
  goto L_1000eedf;
L_1000eec8:;
  eax = BB_U32(edi + 0x4u);
  edi = (edi + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ecx * 4u);
  ecx = BB_U32(esp + 0x1cu);
  a = eax;
  b = BB_U32(ebx + 0x10u);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
L_1000eedf:;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(esp + 0x20u), 0, 0x1000eee4u); /* 1000eee0 indirect */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = BB_U32(esp + 0x34u);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ef0b;
  eax = BB_U32(esp + 0x34u);
  ecx = BB_U32(edi);
  a = eax;
  b = esi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  ebp = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x1cu);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (eax + esi * 4u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(esp + 0x20u), 0, 0x1000ef08u); /* 1000ef04 indirect */
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
L_1000ef0b:;
  BB_U32(esp + 0x10u) = 0x1000f060u;
  args[sp + 4u] = BB_U32(esp + 16u);
  BB_U32(esp + 0x20u) = (BB_U32(esp + 0x20u) + 0x8u);
  args[sp + 8u] = BB_U32(esp + 32u);
  BB_U32(esp + 0x24u) = (BB_U32(esp + 0x24u) + 1u);
  args[sp + 9u] = BB_U32(esp + 36u);
  eax = BB_U32(esp + 0x24u);
  a = BB_U32(ebx + 0x4u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000ee62;
L_1000ef29:;
  edx = BB_U32(ebx + 0x8u);
  a = BB_U32(esp + 0x34u);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000ef5b;
  ecx = (edx * 4u);
  eax = BB_U32(esp + 0x34u);
  ecx = (ecx >> 2);
  eax = (eax - edx);
  edi = BB_U32(ebx + 0x10u);
  ebp = BB_U32(esp + 0x18u);
  BB_U32(ebx + 0x14u) = 0x0u;
  esi = (ebp + eax * 4u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  goto L_1000efdd;
L_1000ef5b:;
  eax = BB_U32(ebx + 0x14u);
  edx = (edx - eax);
  a = BB_U32(esp + 0x34u);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000ef95;
  ecx = BB_U32(esp + 0x34u);
  edi = (eax * 4u);
  ecx = (ecx << 2);
  edi = (edi + BB_U32(ebx + 0x10u));
  ecx = (ecx >> 2);
  esi = BB_U32(esp + 0x18u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  a = BB_U32(esp + 0x34u);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000ef8f;
  BB_U32(ebx + 0x14u) = 0x0u;
  goto L_1000efdd;
L_1000ef8f:;
  eax = BB_U32(esp + 0x34u);
  goto L_1000efda;
L_1000ef95:;
  ebp = (edx * 4u);
  edi = (eax * 4u);
  edi = (edi + BB_U32(ebx + 0x10u));
  esi = BB_U32(esp + 0x18u);
  ecx = ebp;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = ebp;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = BB_U32(esp + 0x34u);
  esi = BB_U32(esp + 0x18u);
  eax = (eax - edx);
  esi = (esi + ebp);
  edi = BB_U32(ebx + 0x10u);
  ecx = (eax * 4u);
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = BB_U32(esp + 0x34u);
  eax = (eax - BB_U32(ebx + 0x8u));
L_1000efda:;
  BB_U32(ebx + 0x14u) = (BB_U32(ebx + 0x14u) + eax);
L_1000efdd:;
  BB_U32(ebx + 0x18u) = 0x0u;
L_1000efe4:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x18u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_process_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_process_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_float_delay_multiply_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  fp[depth] = F32(esp + 0x14u);
  ++depth;
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000f036;
L_1000f007:;
  eax = BB_U32(edi);
  fp[depth] = F32(esi);
  ++depth;
  fp[depth] = F32(esi + 0x4u);
  ++depth;
  fp[depth] = F32(esi + 0x8u);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth] = F32(esi + 0xcu);
  ++depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth] = fp[depth - 1u - 4u];
  ++depth;
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 3u] = fp[depth - 1u - 3u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 4u] = fp[depth - 1u - 4u] * fp[depth - 1];
  --depth;
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  esi = (esi + 0x10u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000f007;
L_1000f036:;
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000f04e;
L_1000f03f:;
  fp[depth] = F32(esi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  esi = (esi + 0x4u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000f03f;
L_1000f04e:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_multiply_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused)),
                                   uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_multiply_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  fp[depth] = F32(esp + 0x14u);
  ++depth;
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000f0b7;
L_1000f077:;
  fp[depth] = F32(esi);
  ++depth;
  fp[depth] = F32(esi + 0x4u);
  ++depth;
  fp[depth] = F32(esi + 0x8u);
  ++depth;
  fp[depth] = F32(esi + 0xcu);
  ++depth;
  fp[depth] = fp[depth - 1u - 4u];
  ++depth;
  fp[depth - 1u - 4u] = fp[depth - 1u - 4u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 3u] = fp[depth - 1u - 3u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 2u] = fp[depth - 1u - 2u] * fp[depth - 1u - 0u];
  fp[depth - 1u - 1u] = fp[depth - 1u - 1u] * fp[depth - 1];
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0x4u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0x8u);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 3u];
    fp[depth - 1u - 3u] = v;
  }
  fp[depth - 1] = fp[depth - 1] + F32(edi + 0xcu);
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 2u];
    fp[depth - 1u - 2u] = v;
  }
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  {
    long double v = fp[depth - 1];
    fp[depth - 1] = fp[depth - 1u - 1u];
    fp[depth - 1u - 1u] = v;
  }
  F32(edi + 0x8u) = (float)(fp[depth - 1]);
  --depth;
  F32(edi + 0xcu) = (float)(fp[depth - 1]);
  --depth;
  esi = (esi + 0x10u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000f077;
L_1000f0b7:;
  ecx = BB_U32(esp + 0x18u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000f0d1;
L_1000f0c0:;
  fp[depth] = F32(esi);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * fp[depth - 1u - 1u];
  fp[depth - 1] = fp[depth - 1] + F32(edi);
  F32(edi) = (float)(fp[depth - 1]);
  --depth;
  esi = (esi + 0x4u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000f0c0;
L_1000f0d1:;
  fp[depth - 1u - 0u] = fp[depth - 1];
  --depth;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_float_delay_add_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused)),
                              uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_silent_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  a = BB_U32(ecx + 0x18u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000f0f0;
  eax = 0x1u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000f0f0:;
  eax = BB_U32(esi + 0x8u);
  ecx = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000c4a0u, 0, 0x1000f0fdu);
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  {
    long double other = F32(0x1001a5c8u);
    fp_status = sample_compare(&fp[depth - 1], &other);
    --depth;
  }
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)(qmix_status(fp_status, depth)) & 0xffffu) << 0);
  a = ((uint8_t)(eax >> 8));
  b = 0x41u;
  flags = logic_flags(flags, a & b, 8);
  eax = 0x1u;
  if (!(flags & 0x40u))
    goto L_1000f114;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_1000f114:;
  BB_U32(esi + 0x18u) = eax;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_float_delay_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_delay_clear_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = ecx;
  eax = (eax ^ eax);
  ecx = BB_U32(edx + 0x8u);
  ecx = (ecx << 2);
  edi = BB_U32(edx + 0x10u);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  edi = args[sp++];
  esp += 4u;
  BB_U32(edx + 0x14u) = eax;
  BB_U32(edx + 0x18u) = 0x1u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_float_delay_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_delay_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_create_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000fcb4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x18u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000fc73u); /* 1000fc6e */
  BB_U32(ebp + 0xfffffff0u) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  esi = eax;
  eax = 0x0u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000fc9c;
  eax = BB_U32(ebp + 0x8u);
  ecx = BB_U32(eax + 0x4u);
  edx = BB_U32(eax);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x1000fcd0u, 8, 0x1000fc9cu); /* 1000fc97 */
L_1000fc9c:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_create_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_create_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_delay_free_failed_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000fcc7u); /* 1000fcc2 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_integer_delay_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_free_failed_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000fdb0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x8u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = ecx;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_filter_base);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  edi = (edi ^ edi);
  eax = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xfffffffcu) = edi;
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_delay_base);
  BB_U32(esi + 0x4u) = eax;
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_integer_delay);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000fd20u); /* 1000fd1b */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0xcu) = eax;
  BB_U32(esi + 0x8u) = edi;
  BB_U32(ebp + 0xffffffecu) = edi;
  a = BB_U32(esi + 0x4u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000fd73;
  ebx = BB_U32(ebp + 0x8u);
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
L_1000fd37:;
  fp[depth] = F32(ebx);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a678u);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  ecx = BB_U32(esi + 0xcu);
  BB_U32(ecx + edi + 0x4u) = eax;
  eax = BB_U32(ebx + 0xfffffffcu);
  ecx = BB_U32(esi + 0xcu);
  BB_U32(ecx + edi) = eax;
  ecx = BB_U32(esi + 0xcu);
  eax = BB_U32(ecx + edi);
  a = BB_U32(esi + 0x8u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000fd62;
  BB_U32(esi + 0x8u) = eax;
L_1000fd62:;
  ebx = (ebx + 0x8u);
  edi = (edi + 0x8u);
  BB_U32(ebp + 0xffffffecu) = (BB_U32(ebp + 0xffffffecu) + 1u);
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(esi + 0x4u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000fd37;
L_1000fd73:;
  eax = BB_U32(esi + 0x8u);
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000fd7fu); /* 1000fd7a */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = esi;
  BB_U32(esi + 0x10u) = eax;
  SAMPLE_CALL(0x10010020u, 0, 0x1000fd8cu); /* 1000fd87 */
  eax = esi;
  edi = args[sp++];
  esp += 4u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  ecx = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(ecx);
  ebx = args[sp++];
  esp += 4u;
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_constructor_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_constructor_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_integer_delay_unwind_base_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000e160u, 0, BB_U32(esp - 4u)); /* tail call 1000fdab */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_integer_delay_unwind_base_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_unwind_base_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_unwind_intermediate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000ec80u, 0, BB_U32(esp - 4u)); /* tail call 1000fdbd */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_unwind_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_unwind_intermediate_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_integer_delay_delete_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  SAMPLE_CALL(0x1000fdf0u, 0, 0x1000fdd8u); /* 1000fdd3 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000fde8;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000fde5u); /* 1000fde0 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000fde8:;
  eax = esi;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_delay_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000fe4fu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_integer_delay);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ecx + 0x10u);
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000fe24u); /* 1000fe1f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000fe33u); /* 1000fe2e */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x1000fe59u, 0, 0x1000fe42u); /* 1000fe3d */
  eax = BB_U32(ebp + 0xfffffff4u);
  sp += ((uint32_t)(ebp)-esp) / 4u;
  esp = ebp;
  exception_link(eax);
  ebp = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_integer_delay_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_cleanup_intermediate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  ecx = BB_U32(ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x1000ec80u, 0, BB_U32(esp - 4u)); /* tail call 1000fe5c */
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_cleanup_intermediate_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_cleanup_intermediate_c) ";leal "
                                                    "4(%esp),%esp;popal;popfl;"
                                                    "leal 512(%esp),%esp;ret "
                                                    "$0");
}
#endif
void bb_qmix_integer_delay_process_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x20u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(esp + 0x28u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = ecx;
  eax = BB_U32(esi + 0xcu);
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000fea2;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000fe9a;
  eax = BB_U32(ebp);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x1000fe92u); /* 1000fe8f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000ffc9;
L_1000fe9a:;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = esi;
  SAMPLE_CALL(0x1000cc60u, 4, 0x1000fea2u); /* 1000fe9d */
L_1000fea2:;
  edx = BB_U32(esp + 0x24u);
  ecx = BB_U32(esi + 0x4u);
  BB_U32(esp + 0x18u) = ecx;
  args[sp + 6u] = BB_U32(esp + 24u);
  eax = BB_U32(edx + 0x4u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(edx + 0xcu);
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000fee8;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000fee0;
  ecx = BB_U32(edx + 0x8u);
  eax = BB_U32(esp + 0x14u);
  ecx = (ecx << 2);
  edi = (eax + ebx * 4u);
  ecx = (ecx >> 2);
  eax = (eax ^ eax);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(esp + 0x10u) = 0x10010040u;
  args[sp + 4u] = BB_U32(esp + 16u);
  BB_U32(edx + 0xcu) = ebx;
  goto L_1000fef0;
L_1000fee0:;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = edx;
  SAMPLE_CALL(0x1000cc60u, 4, 0x1000fee8u); /* 1000fee3 */
L_1000fee8:;
  BB_U32(esp + 0x10u) = 0x10010050u;
  args[sp + 4u] = BB_U32(esp + 16u);
L_1000fef0:;
  esi = BB_U32(ebp + 0xcu);
  eax = BB_U32(ebp + 0x4u);
  ecx = (esi + eax * 8u);
  BB_U32(esp + 0x1cu) = ecx;
  args[sp + 7u] = BB_U32(esp + 28u);
  a = esi;
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 1u))
    goto L_1000ff69;
L_1000ff01:;
  edi = BB_U32(esi);
  a = ebx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ff3b;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x4u);
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x8u);
  eax = (eax - edi);
  eax = (eax << 2);
  a = eax;
  b = BB_U32(ebp + 0x10u);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(esp + 0x20u), 0, 0x1000ff21u); /* 1000ff1d indirect */
  edx = BB_U32(esp + 0x28u);
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  eax = ebx;
  ecx = BB_U32(esi + 0x4u);
  eax = (eax - edi);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x1cu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (eax + edi * 4u);
  goto L_1000ff50;
L_1000ff3b:;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi + 0x4u);
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x8u);
  eax = (eax - edi);
  eax = (eax << 2);
  eax = (eax + BB_U32(ebp + 0x10u));
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
L_1000ff50:;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  esi = result;
  SAMPLE_CALL(BB_U32(esp + 0x20u), 0, 0x1000ff58u); /* 1000ff54 indirect */
  BB_U32(esp + 0x20u) = 0x10010050u;
  args[sp + 8u] = BB_U32(esp + 32u);
  a = esp;
  b = 0x10u;
  esp += b;
  sp += b / 4u;
  a = esi;
  b = BB_U32(esp + 0x1cu);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 1u)
    goto L_1000ff01;
L_1000ff69:;
  eax = BB_U32(ebp + 0x8u);
  a = ebx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ff8a;
  ecx = (eax * 4u);
  ebx = (ebx - eax);
  eax = BB_U32(esp + 0x18u);
  ecx = (ecx >> 2);
  edi = BB_U32(ebp + 0x10u);
  esi = (eax + ebx * 4u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  goto L_1000ffc2;
L_1000ff8a:;
  edi = BB_U32(ebp + 0x10u);
  edx = (ebx * 4u);
  eax = (eax - ebx);
  esi = (edi + edx);
  ecx = (eax * 4u);
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  edi = BB_U32(ebp + 0x8u);
  esi = BB_U32(esp + 0x18u);
  edi = (edi - ebx);
  ecx = edx;
  edi = (edi << 2);
  ecx = (ecx >> 2);
  edi = (edi + BB_U32(ebp + 0x10u));
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = edx;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
L_1000ffc2:;
  BB_U32(ebp + 0x14u) = 0x0u;
L_1000ffc9:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_process_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_process_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_integer_delay_silent_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  a = BB_U32(ecx + 0x14u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000fff0;
  eax = 0x1u;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000fff0:;
  eax = BB_U32(esi + 0x8u);
  ecx = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000d070u, 0, 0x1000fffdu); /* 1000fff8 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x64u;
  flags = sub_flags(flags, a, b, 32);
  eax = 0x1u;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1001000c;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_1001000c:;
  BB_U32(esi + 0x14u) = eax;
  esi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_integer_delay_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_clear_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = ecx;
  eax = (eax ^ eax);
  ecx = BB_U32(edx + 0x8u);
  ecx = (ecx << 2);
  edi = BB_U32(edx + 0x10u);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(edx + 0x14u) = 0x1u;
  edi = args[sp++];
  esp += 4u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_integer_delay_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_multiply_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_multiply_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused)),
                                     uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_multiply_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_delay_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_integer_delay_add_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused)),
                                uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_delay_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
