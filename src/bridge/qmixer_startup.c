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
/* C forms of QMIXER session and channel lifecycle with backend selection.
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
static uint32_t exception_head(void) {
  uint32_t value;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(value));
  return value;
}
static void exception_link(uint32_t value) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(value) : "memory");
}
#endif
void bb_qmix_session_constructor_c(uint32_t *input) {
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
  args[--sp] = 0x10002884u;
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
  BB_U32(ebp + 0xfffffff0u) = ecx;
  esi = ecx;
  SAMPLE_CALL(0x10005050u, 0, 0x100027d6u); /* 100027d1 */
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  BB_U32(ebp + 0xfffffffcu) = eax;
  BB_U32(esi + 0x70u) = eax;
  BB_U32(esi + 0x7cu) = eax;
  BB_U32(esi + 0x80u) = eax;
  BB_U32(esi + 0x84u) = eax;
  BB_U32(esi + 0xccu) = eax;
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_session);
  BB_U32(esi + 0x90u) = eax;
  BB_U32(esi + 0x8cu) = eax;
  ecx = 0x3f800000u;
  BB_U32(esi + 0x88u) = eax;
  BB_U32(esi + 0x94u) = ecx;
  BB_U32(esi + 0x98u) = eax;
  BB_U32(esi + 0x9cu) = eax;
  BB_U32(esi + 0xa0u) = eax;
  BB_U32(esi + 0xa4u) = ecx;
  BB_U32(esi + 0xa8u) = eax;
  BB_U32(esi + 0xacu) = eax;
  BB_U32(esi + 0xb0u) = eax;
  BB_U32(esi + 0xb4u) = ecx;
  BB_U32(esi + 0xbcu) = eax;
  BB_U32(esi + 0xc0u) = eax;
  BB_U32(esi + 0xb8u) = eax;
  BB_U32(esi + 0xc4u) = 0x43a5ae14u;
  BB_U32(esi + 0xc8u) = 0x42c80000u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  eax = esi;
  ecx = BB_U32(ebp + 0xfffffff4u);
  exception_link(ecx);
  esi = args[sp++];
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
uint32_t __attribute__((naked)) bb_qmix_session_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_constructor_cleanup_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10005130u, 0, BB_U32(esp - 4u)); /* tail call 10002891 */
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
bb_qmix_session_constructor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_constructor_cleanup_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_delete_c(uint32_t *input) {
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
  SAMPLE_CALL(0x100028c0u, 0, 0x100028a8u); /* 100028a3 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100028b8;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100028b5u); /* 100028b0 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100028b8:;
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
bb_qmix_session_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_destructor_c(uint32_t *input) {
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
  args[--sp] = 0x10002932u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_session);
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
  eax = BB_U32(ecx + 0x7cu);
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100028f4u); /* 100028ef */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(ecx + 0x80u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10002906u); /* 10002901 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(edx + 0x70u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10002919;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10002919u); /* 10002917 indirect */
L_10002919:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x1000293cu, 0, 0x10002925u); /* 10002920 */
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
uint32_t __attribute__((naked)) bb_qmix_session_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_destructor_cleanup_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10005130u, 0, BB_U32(esp - 4u)); /* tail call 1000293f */
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
uint32_t __attribute__((naked)) bb_qmix_session_destructor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_destructor_cleanup_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_initialize_c(uint32_t *input) {
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
  args[--sp] = 0x10002a47u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  eax = BB_U32(ebp + 0x8u);
  a = esp;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
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
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100054e0u, 4, 0x10002979u); /* 10002974 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10002984;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_10002a34;
L_10002984:;
  SAMPLE_CALL(0x1000ba10u, 0, 0x10002989u); /* 10002984 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100029a1;
  SAMPLE_CALL(0x1000ba30u, 0, 0x10002992u); /* 1000298d */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100029a1;
  edi = (esi + 0x78u);
  BB_U32(edi) = 0x400u;
  goto L_100029aa;
L_100029a1:;
  edi = (esi + 0x78u);
  BB_U32(edi) = 0x100u;
L_100029aa:;
  eax = BB_U32(edi);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100029b4u); /* 100029af */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x7cu) = eax;
  eax = BB_U32(edi);
  edi = (edi ^ edi);
  a = eax;
  b = eax;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100029c6u); /* 100029c1 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x80u) = eax;
  a = BB_U32(esi + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10002a25;
  a = ebx;
  b = ebx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
L_100029d6:;
  args[--sp] = 0xd8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100029e0u); /* 100029db */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffff0u) = eax;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  eax = 0x0u;
  if (flags & 0x40u)
    goto L_10002a0e;
  eax = BB_U32(esi + 0x80u);
  ecx = BB_U32(esi + 0x7cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = (esi + 0x28u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xfffffff0u);
  SAMPLE_CALL(0x10003030u, 16, 0x10002a0eu); /* 10002a09 */
L_10002a0e:;
  ebx = (ebx + 0x4u);
  edi = (edi + 1u);
  ecx = BB_U32(esi + 0x20u);
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(ecx + ebx + 0xfffffffcu) = eax;
  a = BB_U32(esi + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_100029d6;
L_10002a25:;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x24u), 4, 0x10002a2eu); /* 10002a2b indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  eax = result;
  a = eax;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  eax = result;
L_10002a34:;
  ecx = BB_U32(ebp + 0xfffffff4u);
  edi = args[sp++];
  esp += 4u;
  exception_link(ecx);
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
bb_qmix_session_initialize_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_channel_free_failed_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10001070u, 0, 0x10002a5au); /* 10002a55 */
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
uint32_t __attribute__((naked))
bb_qmix_session_channel_free_failed_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_channel_free_failed_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_backend_select_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  ebx = BB_U32(esp + 0x10u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10002b4f;
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10002b4f;
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = BB_U32(0x100212c0u);
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e170u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e168u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebp, 16, 0x10002a91u); /* 10002a8f indirect */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10002a9c;
  a = edi;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10002acc;
L_10002a9c:;
  SAMPLE_CALL(0x1000ba10u, 0, 0x10002aa1u); /* 10002a9c */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10002ab3;
  edi = 0x2u;
  SAMPLE_CALL(0x1000ba30u, 0, 0x10002aafu); /* 10002aaa */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10002acc;
L_10002ab3:;
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e15cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e168u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebp, 16, 0x10002ac6u); /* 10002ac4 indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = edi;
  b = edi;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  edi = result;
  edi = (edi + 1u);
L_10002acc:;
  ecx = BB_U32(esi + 0x70u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10002ad9;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10002ad9u); /* 10002ad7 indirect */
L_10002ad9:;
  eax = (ebx + ebx * 2u);
  eax = (eax + edi);
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  SAMPLE_CALL(BB_U32(eax * 4u + 0x1001e150u), 0,
              0x10002ae7u); /* 10002ae0 indirect */
  ecx = eax;
  BB_U32(esi + 0x70u) = eax;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10002b3c;
  eax = BB_U32(esi + 0x78u);
  edx = BB_U32(esi + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(ecx);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(ebx + 0x4u), 8, 0x10002afdu); /* 10002afa indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10002b3c;
  ebx = (ebx ^ ebx);
  a = BB_U32(esi + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10002b21;
L_10002b08:;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(esi + 0x70u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ebx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  ecx = BB_U32(eax + edi + 0xfffffffcu);
  SAMPLE_CALL(0x10003300u, 4, 0x10002b1cu); /* 10002b17 */
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10002b08;
L_10002b21:;
  ecx = BB_U32(esi + 0x70u);
  eax = BB_U32(esi + 0xc8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(ebx + 0xcu), 4, 0x10002b30u); /* 10002b2d indirect */
  eax = 0x1u;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
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
L_10002b3c:;
  args[--sp] = 0x21u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10002b43u); /* 10002b3e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
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
L_10002b4f:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
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
bb_qmix_session_backend_select_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_backend_select_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_channel_constructor_c(uint32_t *input) {
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000317bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  eax = BB_U32(ebp + 0xcu);
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
  BB_U32(ebp + 0xfffffff0u) = ecx;
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = ebx;
  b = ebx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ebx = result;
  SAMPLE_CALL(0x100059f0u, 4, 0x1000305du); /* 10003058 */
  BB_U32(ebp + 0xfffffffcu) = ebx;
  BB_U32(esi + 0x34u) = ebx;
  BB_U32(esi + 0x38u) = ebx;
  BB_U32(esi + 0x3cu) = ebx;
  BB_U32(esi + 0x44u) = ebx;
  eax = 0x2u;
  BB_U32(esi + 0x48u) = ebx;
  BB_U32(esi + 0x40u) = 0xau;
  BYTE(ebp + 0xfffffffcu) = ((uint8_t)(ebx));
  BYTE(ebp + 0xfffffffcu) = ((uint8_t)(eax));
  BB_U32(esi + 0x4cu) = ebx;
  BB_U32(esi + 0x50u) = ebx;
  eax = BB_U32(ebp + 0x10u);
  BB_U32(esi + 0x58u) = ebx;
  ecx = BB_U32(ebp + 0x14u);
  edx = BB_U32(ebp + 0x8u);
  BB_U32(esi + 0x54u) = 0xau;
  BYTE(ebp + 0xfffffffcu) = 0x4u;
  BB_U32(esi + 0x5cu) = ebx;
  BB_U32(esi + 0x60u) = eax;
  BB_U32(esi + 0x64u) = ecx;
  BB_U32(esi + 0x68u) = edx;
  BB_U32(esi + 0x94u) = 0x43b40000u;
  BB_U32(esi + 0x98u) = 0xbf800000u;
  ecx = (esi + 0xa8u);
  BB_U32(esi + 0x6cu) = ebx;
  BB_U32(esi + 0x9cu) = 0x3dcccccdu;
  BB_U32(esi + 0xa0u) = 0x3f800000u;
  BB_U32(esi + 0xa4u) = 0x42c80000u;
  SAMPLE_CALL(0x10004000u, 0, 0x100030eau); /* 100030e5 */
  BB_U32(esi + 0xd0u) = ebx;
  BB_U32(esi + 0xd4u) = ebx;
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_channel);
  BB_U32(esi + 0x78u) = ebx;
  BB_U32(esi + 0x74u) = ebx;
  BB_U32(esi + 0x70u) = ebx;
  BB_U32(esi + 0x80u) = ebx;
  BB_U32(esi + 0x84u) = ebx;
  BB_U32(esi + 0x7cu) = ebx;
  BB_U32(esi + 0x90u) = ebx;
  BB_U32(esi + 0xccu) = 0x3f800000u;
  BB_U32(esi + 0x8cu) = 0x3f800000u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(esi + 0x88u) = ebx;
  eax = esi;
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
bb_qmix_channel_constructor_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused)),
                                  uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_channel_pending_pool_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x54u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10004160u, 0, BB_U32(esp - 4u)); /* tail call 10003155 */
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
bb_qmix_channel_pending_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_pending_pool_cleanup_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_pending_list_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x4cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100031c0u, 0, BB_U32(esp - 4u)); /* tail call 10003160 */
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
bb_qmix_channel_pending_list_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_pending_list_cleanup_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_pool_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x40u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10004160u, 0, BB_U32(esp - 4u)); /* tail call 1000316b */
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
uint32_t __attribute__((naked)) bb_qmix_channel_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_pool_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_list_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x38u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100031c0u, 0, BB_U32(esp - 4u)); /* tail call 10003176 */
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
uint32_t __attribute__((naked)) bb_qmix_channel_list_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_list_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_constructor_cleanup_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10005a90u, 0, BB_U32(esp - 4u)); /* tail call 10003188 */
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
bb_qmix_channel_constructor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_constructor_cleanup_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_delete_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10003210u, 0, 0x100031a8u); /* 100031a3 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100031b8;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100031b5u); /* 100031b0 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100031b8:;
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
bb_qmix_channel_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_list_destructor_c(uint32_t *input) {
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
  args[--sp] = 0x100031f7u;
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
  SAMPLE_CALL(0x10003201u, 0, 0x100031eau); /* 100031e5 */
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
uint32_t __attribute__((naked)) bb_qmix_list_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_list_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_list_pool_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10004160u, 0, BB_U32(esp - 4u)); /* tail call 10003207 */
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
uint32_t __attribute__((naked)) bb_qmix_list_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_list_pool_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_destructor_c(uint32_t *input) {
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
  args[--sp] = 0x100032e1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_channel);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0x2u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = ecx;
  ecx = BB_U32(ecx + 0x34u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003249;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10003249u); /* 10003247 indirect */
L_10003249:;
  eax = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(eax + 0x38u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000329f;
L_10003252:;
  eax = BB_U32(ebp + 0xfffffff0u);
  edx = BB_U32(eax + 0x38u);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003260;
  esi = (esi ^ esi);
  goto L_10003282;
L_10003260:;
  esi = BB_U32(edx + 0x4u);
  eax = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(eax + 0x3cu);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003272;
  BB_U32(eax + 0x3cu) = 0x0u;
L_10003272:;
  ecx = BB_U32(edx);
  eax = BB_U32(ebp + 0xfffffff0u);
  BB_U32(eax + 0x38u) = ecx;
  ecx = BB_U32(eax + 0x44u);
  BB_U32(edx) = ecx;
  BB_U32(eax + 0x44u) = edx;
L_10003282:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003296;
  ecx = esi;
  SAMPLE_CALL(0x10005c70u, 0, 0x1000328du); /* 10003288 */
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10003293u); /* 1000328e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_10003296:;
  eax = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(eax + 0x38u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003252;
L_1000329f:;
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x100032cbu, 0, 0x100032a8u); /* 100032a3 */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100032d6u, 0, 0x100032b1u); /* 100032ac */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x100032ebu, 0, 0x100032bdu); /* 100032b8 */
  eax = BB_U32(ebp + 0xfffffff4u);
  esi = args[sp++];
  esp += 4u;
  exception_link(eax);
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
uint32_t __attribute__((naked)) bb_qmix_channel_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_pending_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x4cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100031c0u, 0, BB_U32(esp - 4u)); /* tail call 100032d1 */
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
uint32_t __attribute__((naked)) bb_qmix_channel_pending_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_pending_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_voices_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x38u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100031c0u, 0, BB_U32(esp - 4u)); /* tail call 100032dc */
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
uint32_t __attribute__((naked)) bb_qmix_channel_voices_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_voices_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_base_cleanup_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10005a90u, 0, BB_U32(esp - 4u)); /* tail call 100032ee */
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
uint32_t __attribute__((naked)) bb_qmix_channel_base_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_base_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_pool_release_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10004179;
L_10004168:;
  esi = BB_U32(eax);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10004170u); /* 1000416b */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  eax = esi;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10004168;
L_10004179:;
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
uint32_t __attribute__((naked)) bb_qmix_pool_release_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pool_release_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_profile_integer_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(0x100212c0u);
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e250u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x10005023u); /* 10005021 indirect */
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x1000502cu); /* 1000502a indirect */
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
bb_qmix_profile_integer_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_profile_integer_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_base_constructor_c(uint32_t *input) {
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
  args[--sp] = 0x100050efu;
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
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = ecx;
  BB_U32(ecx + 0x8u) = eax;
  BB_U32(ecx + 0xcu) = eax;
  BB_U32(ecx + 0x10u) = eax;
  BB_U32(ecx + 0x14u) = eax;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx + 0x20u) = eax;
  BB_U32(ecx + 0x24u) = eax;
  BB_U32(ecx + 0x3cu) = eax;
  BB_U32(ecx + 0x40u) = eax;
  BB_U32(ecx + 0x48u) = eax;
  esi = ecx;
  ecx = 0x5432u;
  BB_U32(esi + 0x4cu) = eax;
  BB_U32(esi + 0x18u) = 0x3u;
  WORD(esi + 0x4u) = ((uint16_t)(ecx));
  eax = (esi + 0x50u);
  BB_U32(esi + 0x1cu) = 0x8u;
  BB_U32(esi + 0x44u) = 0xau;
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  WORD(esi + 0x6cu) = ((uint16_t)(ecx));
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esi) = BB_QMIX_VTABLE(bb_qmix_vtable_session_base);
  SAMPLE_CALL(BB_U32(0x100212b8u), 4, 0x100050cbu); /* 100050c5 indirect */
  eax = esi;
  edi = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked)) bb_qmix_session_base_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_base_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_base_pool_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x44u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10004160u, 0, BB_U32(esp - 4u)); /* tail call 100050ea */
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
uint32_t __attribute__((naked)) bb_qmix_session_base_pool_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_base_pool_cleanup_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_base_list_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x3cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100031c0u, 0, BB_U32(esp - 4u)); /* tail call 100050ff */
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
uint32_t __attribute__((naked)) bb_qmix_session_base_list_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_base_list_cleanup_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_base_delete_c(uint32_t *input) {
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
  SAMPLE_CALL(0x10005130u, 0, 0x10005118u); /* 10005113 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005128;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10005125u); /* 10005120 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10005128:;
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
bb_qmix_session_base_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_base_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_base_destructor_c(uint32_t *input) {
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
  args[--sp] = 0x1000522du;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_session_base);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xfffffff0u) = ecx;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = edi;
  b = edi;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edi = result;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffffcu) = edi;
#ifdef BB_MODEL_RUNTIME
  /* A failed initialization can leave the active-channel array absent.
   * Other owned fields still need their ordinary destruction below. */
  if (BB_U32(BB_U32(ebp + 0xfffffff0u) + 0x24u)) {
#endif
    SAMPLE_CALL(0x10005760u, 8, 0x10005163u); /* 1000515e */
#ifdef BB_MODEL_RUNTIME
  } else {
    sp += 2;
    esp += 8;
  }
#endif
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x20u);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100051a0;
  esi = (esi ^ esi);
  a = BB_U32(edx + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10005191;
L_10005172:;
  edx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(edx + 0x20u);
  ecx = BB_U32(eax + edi);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005185;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x10005185u); /* 10005183 indirect */
L_10005185:;
  edx = BB_U32(ebp + 0xfffffff0u);
  edi = (edi + 0x4u);
  esi = (esi + 1u);
  a = BB_U32(edx + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10005172;
L_10005191:;
  edx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(edx + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000519du); /* 10005198 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100051a0:;
  edx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(edx + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100051acu); /* 100051a7 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x3cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005205;
L_100051b8:;
  edx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(edx + 0x3cu);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100051c6;
  esi = (esi ^ esi);
  goto L_100051e8;
L_100051c6:;
  esi = BB_U32(ecx + 0x4u);
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x40u);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100051d8;
  BB_U32(edx + 0x40u) = 0x0u;
L_100051d8:;
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(edx + 0x3cu) = eax;
  eax = BB_U32(edx + 0x48u);
  BB_U32(ecx) = eax;
  BB_U32(edx + 0x48u) = ecx;
L_100051e8:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100051fc;
  ecx = esi;
  SAMPLE_CALL(0x10005c70u, 0, 0x100051f3u); /* 100051ee */
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100051f9u); /* 100051f4 */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
L_100051fc:;
  edx = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(edx + 0x3cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100051b8;
L_10005205:;
  eax = BB_U32(ebp + 0xfffffff0u);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212bcu), 4, 0x10005212u); /* 1000520c indirect */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x10005237u, 0, 0x1000521eu); /* 10005219 */
  eax = BB_U32(ebp + 0xfffffff4u);
  edi = args[sp++];
  esp += 4u;
  exception_link(eax);
  esi = args[sp++];
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
uint32_t __attribute__((naked)) bb_qmix_session_base_destructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_base_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_base_destructor_cleanup_c(uint32_t *input) {
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
  a = ecx;
  b = 0x3cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100031c0u, 0, BB_U32(esp - 4u)); /* tail call 1000523d */
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
bb_qmix_session_base_destructor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_base_destructor_cleanup_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_validate_format_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x4u);
  a = BYTE(eax + 0x30u);
  b = 0x10u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000543c;
  args[--sp] = 0x10020af0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = eax;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e288u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212c0u), 16, 0x10005430u); /* 1000542a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000543c;
  eax = 0x1u;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000543c:;
  args[--sp] = 0x8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10005443u); /* 1000543e */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_session_validate_format_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_validate_format_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_configure_format_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = ecx;
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000546a;
  a = BYTE(esi + 0x2u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000546a;
  eax = (eax ^ eax);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0x8u)) & 0xffffu) << 0);
  goto L_10005481;
L_1000546a:;
  eax = BB_U32(esp + 0x10u);
  args[--sp] = 0xbu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e298u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = eax;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005000u, 0, 0x1000547eu); /* 10005479 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_10005481:;
  a = eax;
  b = 0x16u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005494;
  a = eax;
  b = 0x2cu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000549d;
  BB_U32(edi + 0x2cu) = 0x2b11u;
  goto L_100054a4;
L_10005494:;
  BB_U32(edi + 0x2cu) = 0x5622u;
  goto L_100054a4;
L_1000549d:;
  BB_U32(edi + 0x2cu) = 0xac44u;
L_100054a4:;
  WORD(edi + 0x28u) = 0x1u;
  WORD(edi + 0x36u) = 0x10u;
  WORD(edi + 0x2au) = 0x2u;
  WORD(edi + 0x34u) = 0x4u;
  eax = BB_U32(edi + 0x2cu);
  eax = (eax << 2);
  BB_U32(edi + 0x30u) = eax;
  a = BYTE(esi + 0x2u);
  b = 0x10u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100054d4;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0xeu)) & 0xffffu) << 0);
  BB_U32(edi + 0x1cu) = eax;
L_100054d4:;
  eax = 0x1u;
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
bb_qmix_session_configure_format_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_configure_format_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_session_allocate_channels_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx + 0x1cu);
  esi = ecx;
  a = eax;
  result = (a << 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 0);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100054f0u); /* 100054eb */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(esi + 0x1cu);
  ecx = (ecx << 2);
  edi = eax;
  ecx = (ecx >> 2);
  BB_U32(esi + 0x20u) = eax;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = BB_U32(esi + 0x1cu);
  edx = (ecx * 4u + 0x4u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10005515u); /* 10005510 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0x24u) = eax;
  BB_U32(eax) = 0x0u;
  eax = 0x1u;
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
bb_qmix_session_allocate_channels_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_allocate_channels_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_backend_c(uint32_t *input) {
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
  eax = 0x1u;
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
bb_qmix_session_default_backend_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_backend_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_orientation_c(uint32_t *input) {
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
bb_qmix_session_default_orientation_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_orientation_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_session_default_position_c(uint32_t *input) {
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
bb_qmix_session_default_position_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_velocity_c(uint32_t *input) {
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
bb_qmix_session_default_velocity_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_velocity_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_channel_get_c(uint32_t *input) {
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
  edx = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100055a9;
  a = BB_U32(ecx + 0x1cu);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100055a9;
  eax = BB_U32(ecx + 0x20u);
  esi = BB_U32(eax + edx * 4u);
  goto L_100055ab;
L_100055a9:;
  esi = (esi ^ esi);
L_100055ab:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100055d8;
  args[--sp] = 0xau;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100055b6u); /* 100055b1 */
  BB_U32(esp + 0x8u) = 0xbu;
  args[sp + 2u] = BB_U32(esp + 8u);
  eax = BB_U32(esp + 0x8u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esp + 0x4u) = eax;
  args[sp + 1u] = BB_U32(esp + 4u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100055d8u); /* 100055d3 */
L_100055d8:;
  eax = esi;
  esi = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked))
bb_qmix_session_channel_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_channel_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_speed_c(uint32_t *input) {
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
bb_qmix_session_default_speed_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_speed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_rate_c(uint32_t *input) {
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
bb_qmix_session_default_rate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_rate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_profile_c(uint32_t *input) {
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
uint32_t __attribute__((naked)) bb_qmix_session_default_profile_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_profile_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_active_channels_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(ecx + 0x24u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = (ebx ^ ebx);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = (esi ^ esi);
  a = BB_U32(ecx + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000564e;
  edi = (edi ^ edi);
L_10005631:;
  eax = BB_U32(ecx + 0x20u);
  eax = BB_U32(eax + edi);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005645;
  a = BB_U32(eax + 0x4u);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005645;
  BB_U32(edx) = eax;
  edx = (edx + 0x4u);
L_10005645:;
  edi = (edi + 0x4u);
  esi = (esi + 1u);
  a = BB_U32(ecx + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10005631;
L_1000564e:;
  edi = args[sp++];
  esp += 4u;
  BB_U32(edx) = 0x0u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
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
uint32_t __attribute__((naked)) bb_qmix_session_active_channels_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_active_channels_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_default_profile_end_c(uint32_t *input) {
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
bb_qmix_session_default_profile_end_entry(uint32_t arg0
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_profile_end_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_activate_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100056de;
  a = BB_U32(ecx + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100056da;
  a = BB_U32(0x10020aecu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100056c7;
  a = BB_U32(ecx + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100056fd;
  a = ecx;
  b = BB_U32(0x10020aecu);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100056fd;
  args[--sp] = 0x9u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x1000569fu); /* 1000569a */
  BB_U32(esp + 0x4u) = 0x4u;
  args[sp + 1u] = BB_U32(esp + 4u);
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esp) = eax;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100056c1u); /* 100056bc */
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
L_100056c7:;
  BB_U32(0x10020aecu) = ecx;
  BB_U32(ecx + 0x8u) = 0x1u;
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
L_100056da:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100056fd;
L_100056de:;
  a = BB_U32(ecx + 0x8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100056fd;
  a = ecx;
  b = BB_U32(0x10020aecu);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100056f6;
  BB_U32(0x10020aecu) = 0x0u;
L_100056f6:;
  BB_U32(ecx + 0x8u) = 0x0u;
L_100056fd:;
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
uint32_t __attribute__((naked))
bb_qmix_session_activate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_activate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_orientation_get_c(uint32_t *input) {
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
uint32_t __attribute__((naked)) bb_qmix_session_default_orientation_get_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_orientation_get_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_session_default_position_get_c(uint32_t *input) {
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
bb_qmix_session_default_position_get_entry(uint32_t arg0
                                           __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_position_get_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_velocity_get_c(uint32_t *input) {
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
bb_qmix_session_default_velocity_get_entry(uint32_t arg0
                                           __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_velocity_get_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_default_speed_get_c(uint32_t *input) {
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
  fp[depth] = F32(0x1001a268u);
  ++depth;
  BB_QMIX_RETURN_FLOAT(input, fp[depth - 1]);
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
double __attribute__((naked)) bb_qmix_session_default_speed_get_entry(void) {
  __asm__ volatile(
      "leal -512(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;movl "
      "%eax,36(%esp);pushl %esp;call " BB_ASM(
          bb_qmix_session_default_speed_get_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;fldt "
                                               "-508(%esp);ret $0");
}
#endif
void bb_qmix_session_default_gain_c(uint32_t *input) {
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
bb_qmix_session_default_gain_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_default_gain_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_session_flush_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = esi;
  b = 0x1u;
  flags = logic_flags(flags, a & b, 32);
  edi = ecx;
  if (flags & 0x40u)
    goto L_1000578a;
  ebx = BB_U32(edi + 0x24u);
  a = BB_U32(ebx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000579d;
L_10005779:;
  ecx = BB_U32(ebx);
  a = ebx;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x10005783u); /* 10005780 indirect */
  a = BB_U32(ebx);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10005779;
  goto L_1000579d;
L_1000578a:;
  eax = BB_U32(esp + 0x10u);
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005590u, 4, 0x10005796u); /* 10005791 */
  ecx = eax;
  eax = BB_U32(eax);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 0, 0x1000579du); /* 1000579a indirect */
L_1000579d:;
  ecx = edi;
  SAMPLE_CALL(0x10005620u, 0, 0x100057a4u); /* 1000579f */
  a = esi;
  b = 0x2u;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100057b3;
  BB_U32(edi + 0x14u) = 0x1u;
L_100057b3:;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
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
bb_qmix_session_flush_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_flush_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_session_open_channels_c(uint32_t *input) {
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
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  eax = BB_U32(esp + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = eax;
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = 0xffffffffu;
  if (!(flags & 0x41u))
    goto L_100057de;
  switch (eax) {
  case 0:
    goto L_1000580c;
  case 1:
    goto L_10005824;
  case 2:
    goto L_1000586a;
  case 3:
    goto L_10005829;
  default:
    __builtin_unreachable();
  }
L_100057de:;
  args[--sp] = 0xbu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100057e5u); /* 100057e0 */
  BB_U32(esp + 0x14u) = 0xau;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10005807u); /* 10005802 */
  goto L_100058c5;
L_1000580c:;
  ebx = BB_U32(esp + 0x18u);
  ecx = esi;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005590u, 4, 0x10005818u); /* 10005813 */
  edx = BB_U32(eax);
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x14u), 0, 0x1000581fu); /* 1000581c indirect */
  goto L_100058c5;
L_10005824:;
  ebx = BB_U32(esi + 0x1cu);
  goto L_1000586e;
L_10005829:;
  ebp = (ebp ^ ebp);
  ebx = (ebx ^ ebx);
  edx = BB_U32(esi + 0x1cu);
  a = edx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10005859;
L_10005834:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10005844;
  a = edx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10005844;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(eax + ebx * 4u);
  goto L_10005846;
L_10005844:;
  ecx = (ecx ^ ecx);
L_10005846:;
  a = BB_U32(ecx + 0x4u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005852;
  ebx = (ebx + 1u);
  a = edx;
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10005834;
  goto L_10005859;
L_10005852:;
  eax = BB_U32(ecx);
  edi = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x14u), 0, 0x10005859u); /* 10005856 indirect */
L_10005859:;
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100058c5;
  args[--sp] = 0xeu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10005865u); /* 10005860 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  goto L_100058c5;
L_1000586a:;
  ebx = BB_U32(esp + 0x18u);
L_1000586e:;
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10005877;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_100058a0;
L_10005877:;
  args[--sp] = 0xau;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x1000587eu); /* 10005879 */
  BB_U32(esp + 0x14u) = 0xbu;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100058a0u); /* 1000589b */
L_100058a0:;
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  if (flags & 0x80u)
    goto L_100058c5;
  ebp = (ebp ^ ebp);
L_100058a5:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100058b6;
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100058b6;
  eax = BB_U32(esi + 0x20u);
  ecx = BB_U32(eax + ebx * 4u);
  goto L_100058b8;
L_100058b6:;
  ecx = (ecx ^ ecx);
L_100058b8:;
  a = BB_U32(ecx + 0x4u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100058c2;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x14u), 0, 0x100058c2u); /* 100058bf indirect */
L_100058c2:;
  a = ebx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ebx = result;
  if (!(flags & 0x80u))
    goto L_100058a5;
L_100058c5:;
  ecx = esi;
  SAMPLE_CALL(0x10005620u, 0, 0x100058ccu); /* 100058c7 */
  eax = edi;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked))
bb_qmix_session_open_channels_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_open_channels_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_session_set_pan_c(uint32_t *input) {
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
  b = 0xcu;
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
  esi = ecx;
  edi = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10005906;
  a = edi;
  b = 0x3cu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000592f;
L_10005906:;
  args[--sp] = 0xcu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x1000590du); /* 10005908 */
  BB_U32(esp + 0x14u) = 0xbu;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esp + 0x14u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x1000592fu); /* 1000592a */
L_1000592f:;
  a = BYTE(esp + 0x24u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005991;
  ebx = (ebx ^ ebx);
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100059cf;
L_10005941:;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10005952;
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10005952;
  eax = BB_U32(esi + 0x20u);
  ebp = BB_U32(eax + ebx * 4u);
  goto L_10005954;
L_10005952:;
  ebp = (ebp ^ ebp);
L_10005954:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005989;
  eax = (ebp + 0x14u);
  ecx = (esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = BB_U32(eax);
  BB_U32(ecx) = edx;
  edx = BB_U32(eax + 0x4u);
  BB_U32(ecx + 0x4u) = edx;
  eax = BB_U32(eax + 0x8u);
  BB_U32(ecx + 0x8u) = eax;
  SAMPLE_CALL(0x1000ba70u, 0, 0x10005975u); /* 10005970 */
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = ebp;
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp);
  SAMPLE_CALL(BB_U32(eax + 0x34u), 4, 0x10005989u); /* 10005986 indirect */
L_10005989:;
  ebx = (ebx + 1u);
  a = BB_U32(esi + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10005941;
  goto L_100059cf;
L_10005991:;
  eax = BB_U32(esp + 0x20u);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005590u, 4, 0x1000599du); /* 10005998 */
  edx = (eax + 0x14u);
  ebx = eax;
  ecx = (esp + 0x10u);
  eax = BB_U32(edx);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = eax;
  ebp = BB_U32(edx + 0x4u);
  BB_U32(ecx + 0x4u) = ebp;
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x8u) = edx;
  SAMPLE_CALL(0x1000ba70u, 0, 0x100059bcu); /* 100059b7 */
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  edi = BB_U32(ebx);
  ecx = ebx;
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edi + 0x34u), 4, 0x100059cfu); /* 100059cc indirect */
L_100059cf:;
  a = BYTE(esp + 0x24u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100059dd;
  BB_U32(esi + 0x14u) = 0x1u;
L_100059dd:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0xcu;
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
bb_qmix_session_set_pan_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_set_pan_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_integer_factory_initialize_c(uint32_t *input) {
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
  BB_U32(0x10020bf4u) = BB_QMIX_VTABLE(bb_qmix_vtable_float_factory);
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
uint32_t __attribute__((naked)) bb_qmix_integer_factory_initialize_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_factory_initialize_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_factory_initialize_c(uint32_t *input) {
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
  BB_U32(0x10020bf8u) = BB_QMIX_VTABLE(bb_qmix_vtable_integer_factory);
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
uint32_t __attribute__((naked)) bb_qmix_short_factory_initialize_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_factory_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_float_factory_initialize_c(uint32_t *input) {
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
  BB_U32(0x10020bfcu) = BB_QMIX_VTABLE(bb_qmix_vtable_short_factory);
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
uint32_t __attribute__((naked)) bb_qmix_float_factory_initialize_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_float_factory_initialize_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif

/* CPU queries are a hardware boundary, not calls into the original DLL.
 * Packed mixing itself is implemented with C integers. */
#ifndef BB_MODEL_RUNTIME
#include <cpuid.h>
#endif
void bb_qmix_cpu_id_available_c(uint32_t *input) {
#ifdef BB_MODEL_RUNTIME
  /* The packed backend is implemented in C and available on every host.
   * These logical capability queries no longer require x86 instructions. */
  input[7] = 1;
#else
  uint32_t saved, observed, probe;
  __asm__ volatile("pushfl;popl %0" : "=r"(saved));
  probe = saved | 0x200000u;
  __asm__ volatile("pushl %0;popfl" ::"r"(probe) : "cc", "memory");
  __asm__ volatile("pushfl;popl %0" : "=r"(observed));
  __asm__ volatile("pushl %0;popfl" ::"r"(saved) : "cc", "memory");
  input[7] = (observed >> 21) & 1u;
#endif
  input[8] = (input[8] & ~0x2008d5u) | 0x44u;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_cpu_id_available_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_cpu_id_available_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret");
}
#endif
void bb_qmix_cpu_mmx_available_c(uint32_t *input) {
#ifdef BB_MODEL_RUNTIME
  input[7] = 1;
  input[6] = input[5] = 0; /* CPUID scratch has no portable meaning. */
#else
  unsigned a, b, c, d;
  __cpuid(0, a, b, c, d);
  __cpuid(1, a, b, c, d);
  input[7] = (d >> 23) & 1u;
  input[6] = c;
  input[5] = d;
#endif
  input[8] = (input[8] & ~0x8d5u) | (input[7] ? 0 : 0x44u);
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_cpu_mmx_available_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_cpu_mmx_available_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret");
}
#endif
