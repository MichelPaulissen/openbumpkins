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
/* C forms of QMIXER public playback, query and property entry points.
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

#ifndef BB_MODEL_RUNTIME
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
#endif
#define F32(a) BB_FLOAT(a)
#define F64(a) BB_DOUBLE(a)
/* C forms of QMIXER sample buffers and mixing state. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */

#ifndef BB_MODEL_RUNTIME
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
#include "qmixer_api_models.h"
#define exception_head bb_qmix_model_exception_head
#define exception_link bb_qmix_model_exception_link
#else
/* Temporary i386 exception-chain boundary. C constructors retain the original
 * physical cleanup records and stages so remaining allocation/unwind code can
 * observe them. Per-owner handlers and shared dispatch now run in C; metadata
 * and some catch continuations still depend on the original DLL data/layout. */
static uint32_t exception_head(void) {
  uint32_t value;
  __asm__ volatile("movl %%fs:0,%0" : "=r"(value));
  return value;
}
static void exception_link(uint32_t value) {
  __asm__ volatile("movl %0,%%fs:0" ::"r"(value) : "memory");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_activate_body(uint32_t *input) {
#else
void bb_qmix_export_activate_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x100063e7u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000634b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000633bu); /* 10006335 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000634b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006347u); /* 10006342 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006370;
L_1000634b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006352u); /* 1000634d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006370u); /* 1000636b */
L_10006370:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000637du); /* 10006377 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x4u), 4, 0x1000638du); /* 1000638a indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100063b2u, 0, 0x10006396u); /* 10006391 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_activate_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_activate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_close_channel_body(uint32_t *input) {
#else
void bb_qmix_export_close_channel_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100064fdu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000645b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000644bu); /* 10006445 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000645b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006457u); /* 10006452 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006480;
L_1000645b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006462u); /* 1000645d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006480u); /* 1000647b */
L_10006480:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000648du); /* 10006487 indirect */
  ecx = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  SAMPLE_CALL(0x10005760u, 8, 0x100064a3u); /* 1000649e */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100064c8u, 0, 0x100064acu); /* 100064a7 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_close_channel_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_close_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_close_session_body(uint32_t *input) {
#else
void bb_qmix_export_close_session_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10006598u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x18u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(ebp + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10006300u, 8, 0x10006543u); /* 1000653e */
  ecx = esi;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000654f;
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax), 4, 0x1000654fu); /* 1000654d indirect */
L_1000654f:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_close_session_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_close_session_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_configure_init_body(uint32_t *input) {
#else
void bb_qmix_export_configure_init_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
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
  args[--sp] = 0x10006615u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  eax = (eax ^ eax);
  a = esp;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  BB_U32(ebp + 0xffffffecu) = eax;
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = eax;
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005250u, 0, 0x100065e2u); /* 100065dd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffecu) = eax;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  eax = BB_U32(ebp + 0xffffffecu);
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
bb_qmix_export_configure_init_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_configure_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "2048(%esp),%esp;ret $4");
}
#endif
void bb_qmix_export_enable_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x8u);
  ecx = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10006640u, 16, 0x10006633u); /* 1000662e */
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
bb_qmix_export_enable_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_enable_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_enable_channel_body(uint32_t *input) {
#else
void bb_qmix_export_enable_channel_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x1000678eu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000668b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000667bu); /* 10006675 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000668b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006687u); /* 10006682 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100066b0;
L_1000668b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006692u); /* 1000668d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100066b0u); /* 100066ab */
L_100066b0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100066bdu); /* 100066b7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000670d;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_100066d7:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100066f4;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100066f4;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_100066f6;
L_100066f4:;
  ebx = (ebx ^ ebx);
L_100066f6:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006724;
  a = BB_U32(ebx + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100066d7;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0xcu), 4, 0x1000670bu); /* 10006708 indirect */
  goto L_100066d7;
L_1000670d:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10006719u); /* 10006714 */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0xcu), 4, 0x10006724u); /* 10006721 indirect */
L_10006724:;
  a = BYTE(ebp + 0x10u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10006734;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10006734:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10006759u, 0, 0x1000673du); /* 10006738 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_enable_channel_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused)),
                                    uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_enable_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_flush_channel_body(uint32_t *input) {
#else
void bb_qmix_export_flush_channel_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100068e4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100067eb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100067dbu); /* 100067d5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100067eb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100067e7u); /* 100067e2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006810;
L_100067eb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100067f2u); /* 100067ed */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006810u); /* 1000680b */
L_10006810:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000681du); /* 10006817 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10006867;
  esi = (esi ^ esi);
  BB_U32(ebp + 0xcu) = esi;
  edi = BB_U32(ebp + 0xffffffe8u);
  ebx = BB_U32(ebp + 0xffffffdcu);
L_10006832:;
  ebx = esi;
  BB_U32(ebp + 0xffffffdcu) = ebx;
  esi = (esi + 1u);
  BB_U32(ebp + 0xcu) = esi;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000684f;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000684f;
  eax = BB_U32(eax + 0x20u);
  edi = BB_U32(eax + ebx * 4u);
  goto L_10006851;
L_1000684f:;
  edi = (edi ^ edi);
L_10006851:;
  BB_U32(ebp + 0xffffffe8u) = edi;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000687a;
  a = BB_U32(edi + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10006832;
  eax = BB_U32(edi);
  ecx = edi;
  SAMPLE_CALL(BB_U32(eax + 0x10u), 0, 0x10006865u); /* 10006862 indirect */
  goto L_10006832;
L_10006867:;
  esi = BB_U32(ebp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10006873u); /* 1000686e */
  edx = BB_U32(eax);
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x10u), 0, 0x1000687au); /* 10006877 indirect */
L_1000687a:;
  a = BYTE(ebp + 0x10u);
  b = 0x2u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000688a;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_1000688a:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100068afu, 0, 0x10006893u); /* 1000688e */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_flush_channel_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_flush_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_free_wave_body(uint32_t *input) {
#else
void bb_qmix_export_free_wave_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10006a2au;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x28u;
  esp -= b;
  sp -= b / 4u;
  a = BB_U32(ebp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xfffffff0u) = esp;
  if (!(flags & 0x40u))
    goto L_1000692c;
L_10006917:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
L_1000692c:;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100069d7;
  BB_U32(ebp + 0xffffffecu) = eax;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000694au); /* 10006944 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000695a;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006956u); /* 10006951 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000697f;
L_1000695a:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006961u); /* 1000695c */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd0u);
  BB_U32(ebp + 0xffffffd4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x1000697fu); /* 1000697a */
L_1000697f:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000698cu); /* 10006986 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  edi = (edi ^ edi);
  esi = BB_U32(ebp + 0xffffffd8u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10006998:;
  esi = edi;
  BB_U32(ebp + 0xffffffd8u) = esi;
  edi = (edi + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100069b2;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100069b2;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_100069b4;
L_100069b2:;
  ebx = (ebx ^ ebx);
L_100069b4:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100069ce;
  a = BB_U32(ebx + 0x4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10006998;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x100069ccu); /* 100069c9 indirect */
  goto L_10006998;
L_100069ce:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100069efu, 0, 0x100069d7u); /* 100069d2 */
L_100069d7:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001890u, 0, 0x100069e0u); /* 100069db */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  goto L_10006917;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_export_free_wave_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_free_wave_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_direct_sound_body(uint32_t *input) {
#else
void bb_qmix_export_get_direct_sound_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10006b0cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006a8b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006a7bu); /* 10006a75 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006a8b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006a87u); /* 10006a82 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006ab0;
L_10006a8b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006a92u); /* 10006a8d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006ab0u); /* 10006aab */
L_10006ab0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10006abdu); /* 10006ab7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x38u), 0, 0x10006ac9u); /* 10006ac6 indirect */
  ecx = BB_U32(ebp + 0xcu);
  BB_U32(ecx) = eax;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10006af3u, 0, 0x10006ad7u); /* 10006ad2 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_direct_sound_entry(uint32_t arg0 __attribute__((unused)),
                                      uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_direct_sound_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_distance_mapping_body(uint32_t *input) {
#else
void bb_qmix_export_get_distance_mapping_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10006c03u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006b6b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006b5bu); /* 10006b55 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006b6b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006b67u); /* 10006b62 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006b90;
L_10006b6b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006b72u); /* 10006b6d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006b90u); /* 10006b8b */
L_10006b90:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10006b9du); /* 10006b97 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10005590u, 4, 0x10006bafu); /* 10006baa */
  ecx = eax;
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x50u), 4, 0x10006bbau); /* 10006bb7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10006bdfu, 0, 0x10006bc3u); /* 10006bbe */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_distance_mapping_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1 __attribute__((unused)),
                                          uint32_t arg2
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_distance_mapping_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_export_get_error_text_c(uint32_t *input) {
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
  edi = BB_U32(esp + 0x18u);
  eax = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(0x100209e0u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(0x10021318u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 16, 0x10006c31u); /* 10006c2f indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006c4c;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(0x100209e0u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x3u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 16, 0x10006c41u); /* 10006c3f indirect */
  eax = 0xbu;
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
L_10006c4c:;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_error_text_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_error_text_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_frequency_body(uint32_t *input) {
#else
void bb_qmix_export_get_frequency_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10006d51u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006cab;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006c9bu); /* 10006c95 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006cab;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006ca7u); /* 10006ca2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006cd0;
L_10006cab:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006cb2u); /* 10006cad */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006cd0u); /* 10006ccb */
L_10006cd0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10006cddu); /* 10006cd7 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10005590u, 4, 0x10006cefu); /* 10006cea */
  ecx = BB_U32(eax + 0x24u);
  eax = BB_U32(ebp + 0x10u);
  BB_U32(eax) = ecx;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10006d1cu, 0, 0x10006d00u); /* 10006cfb */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_frequency_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_frequency_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_export_get_info_c(uint32_t *input) {
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
  edi = BB_U32(esp + 0xcu);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006d91;
  args[--sp] = 0x14u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006d73u); /* 10006d6d indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006d91;
  a = WORD(edi);
  b = 0x14u;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_10006d91;
  esi = 0x1001a2d8u;
  ecx = 0x5u;
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  result = (a ^ b) & 0xffffu;
  flags = logic_flags(flags, result, 16);
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
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
L_10006d91:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(0x14u) & 0xffffu) << 0);
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
bb_qmix_export_get_info_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_info_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_export_get_last_error_c(uint32_t *input) {
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
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10005030u, 0, BB_U32(esp - 4u)); /* tail call 10006da0 */
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
uint32_t __attribute__((naked)) bb_qmix_export_get_last_error_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_last_error_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_listener_orientation_body(uint32_t *input) {
#else
void bb_qmix_export_get_listener_orientation_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10006e7fu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006dfb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006debu); /* 10006de5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006dfb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006df7u); /* 10006df2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006e20;
L_10006dfb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006e02u); /* 10006dfd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006e20u); /* 10006e1b */
L_10006e20:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10006e2du); /* 10006e27 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x44u), 8, 0x10006e41u); /* 10006e3e indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10006e66u, 0, 0x10006e4au); /* 10006e45 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_get_listener_orientation_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused)),
    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_listener_orientation_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_listener_position_body(uint32_t *input) {
#else
void bb_qmix_export_get_listener_position_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10006f5bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006edb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006ecbu); /* 10006ec5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006edb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006ed7u); /* 10006ed2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006f00;
L_10006edb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006ee2u); /* 10006edd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006f00u); /* 10006efb */
L_10006f00:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10006f0du); /* 10006f07 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x48u), 4, 0x10006f1du); /* 10006f1a indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10006f42u, 0, 0x10006f26u); /* 10006f21 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_get_listener_position_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_listener_position_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_listener_velocity_body(uint32_t *input) {
#else
void bb_qmix_export_get_listener_velocity_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x1000703bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10006fbb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10006fabu); /* 10006fa5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006fbb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10006fb7u); /* 10006fb2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10006fe0;
L_10006fbb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10006fc2u); /* 10006fbd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10006fe0u); /* 10006fdb */
L_10006fe0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10006fedu); /* 10006fe7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x4cu), 4, 0x10006ffdu); /* 10006ffa indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007022u, 0, 0x10007006u); /* 10007001 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_get_listener_velocity_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_listener_velocity_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_options_body(uint32_t *input) {
#else
void bb_qmix_export_get_options_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10007127u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x14u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000709b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000708bu); /* 10007085 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000709b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007097u); /* 10007092 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100070c0;
L_1000709b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100070a2u); /* 1000709d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100070c0u); /* 100070bb */
L_100070c0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100070cdu); /* 100070c7 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(eax);
  ecx = BB_U32(eax + 0x18u);
  eax = BB_U32(ebp + 0xcu);
  BB_U32(eax) = ecx;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007103u, 0, 0x100070e7u); /* 100070e2 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_options_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_options_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_pan_body(uint32_t *input) {
#else
void bb_qmix_export_get_pan_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100071d3u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100072e0u, 12, 0x10007179u); /* 10007174 */
  eax = BB_U32(ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x1000ba50u, 0, 0x10007182u); /* 1000717d */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x10u);
  BB_U32(ecx) = eax;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_pan_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_pan_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_pan_rate_body(uint32_t *input) {
#else
void bb_qmix_export_get_pan_rate_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100072d1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000722b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000721bu); /* 10007215 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000722b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007227u); /* 10007222 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007250;
L_1000722b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007232u); /* 1000722d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007250u); /* 1000724b */
L_10007250:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000725du); /* 10007257 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10005590u, 4, 0x1000726fu); /* 1000726a */
  ecx = BB_U32(eax + 0x20u);
  eax = BB_U32(ebp + 0x10u);
  BB_U32(eax) = ecx;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x1000729cu, 0, 0x10007280u); /* 1000727b */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_pan_rate_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_pan_rate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_position_body(uint32_t *input) {
#else
void bb_qmix_export_get_position_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100073f4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x28u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000732b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000731bu); /* 10007315 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000732b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007327u); /* 10007322 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007350;
L_1000732b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007332u); /* 1000732d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007350u); /* 1000734b */
L_10007350:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000735du); /* 10007357 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10005590u, 4, 0x1000736fu); /* 1000736a */
  a = eax;
  b = 0x14u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  edx = (ebp + 0xffffffccu);
  ecx = BB_U32(eax);
  BB_U32(edx) = ecx;
  ecx = BB_U32(eax + 0x4u);
  BB_U32(edx + 0x4u) = ecx;
  eax = BB_U32(eax + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  ecx = edx;
  edx = BB_U32(ebp + 0x10u);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  eax = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = eax;
  eax = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = eax;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100073bfu, 0, 0x100073a3u); /* 1000739e */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_position_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_room_size_body(uint32_t *input) {
#else
void bb_qmix_export_get_room_size_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x100074ccu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000744b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000743bu); /* 10007435 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000744b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007447u); /* 10007442 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007470;
L_1000744b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007452u); /* 1000744d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007470u); /* 1000746b */
L_10007470:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000747du); /* 10007477 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x50u), 0, 0x10007489u); /* 10007486 indirect */
  BB_QMIX_CAPTURE_FLOAT(fp[depth++]);
  eax = BB_U32(ebp + 0xcu);
  F32(eax) = (float)(fp[depth - 1]);
  --depth;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100074b3u, 0, 0x10007497u); /* 10007492 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_room_size_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_room_size_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_source_cone_body(uint32_t *input) {
#else
void bb_qmix_export_get_source_cone_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
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
  args[--sp] = 0x100075c0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000752b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000751bu); /* 10007515 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000752b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007527u); /* 10007522 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007550;
L_1000752b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007532u); /* 1000752d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007550u); /* 1000754b */
L_10007550:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000755du); /* 10007557 indirect */
  ecx = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  SAMPLE_CALL(0x10005590u, 4, 0x1000756fu); /* 1000756a */
  ecx = eax;
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x4cu), 12, 0x10007582u); /* 1000757f indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100075a7u, 0, 0x1000758bu); /* 10007586 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_source_cone_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused)),
                                     uint32_t arg3 __attribute__((unused)),
                                     uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_source_cone_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $20");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_source_position_body(uint32_t *input) {
#else
void bb_qmix_export_get_source_position_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100076a8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000761b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000760bu); /* 10007605 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000761b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007617u); /* 10007612 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007640;
L_1000761b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007622u); /* 1000761d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007640u); /* 1000763b */
L_10007640:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000764du); /* 10007647 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10005590u, 4, 0x1000765fu); /* 1000765a */
  ecx = eax;
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0x10u);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x5cu), 4, 0x1000766au); /* 10007667 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x1000768fu, 0, 0x10007673u); /* 1000766e */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_source_position_entry(uint32_t arg0 __attribute__((unused)),
                                         uint32_t arg1 __attribute__((unused)),
                                         uint32_t arg2
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_source_position_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_source_velocity_body(uint32_t *input) {
#else
void bb_qmix_export_get_source_velocity_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10007798u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000770b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100076fbu); /* 100076f5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000770b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007707u); /* 10007702 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007730;
L_1000770b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007712u); /* 1000770d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007730u); /* 1000772b */
L_10007730:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000773du); /* 10007737 indirect */
  ecx = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  SAMPLE_CALL(0x10005590u, 4, 0x1000774fu); /* 1000774a */
  ecx = eax;
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x60u), 4, 0x1000775au); /* 10007757 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x1000777fu, 0, 0x10007763u); /* 1000775e */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_source_velocity_entry(uint32_t arg0 __attribute__((unused)),
                                         uint32_t arg1 __attribute__((unused)),
                                         uint32_t arg2
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_source_velocity_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_speed_of_sound_body(uint32_t *input) {
#else
void bb_qmix_export_get_speed_of_sound_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x1000787bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100077fb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100077ebu); /* 100077e5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100077fb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100077f7u); /* 100077f2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007820;
L_100077fb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007802u); /* 100077fd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007820u); /* 1000781b */
L_10007820:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000782du); /* 10007827 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x54u), 4, 0x1000783du); /* 1000783a indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007862u, 0, 0x10007846u); /* 10007841 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_speed_of_sound_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_speed_of_sound_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_play_position_body(uint32_t *input) {
#else
void bb_qmix_export_get_play_position_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
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
  args[--sp] = 0x1000798cu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100078db;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100078cbu); /* 100078c5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100078db;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100078d7u); /* 100078d2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007900;
L_100078db:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100078e2u); /* 100078dd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007900u); /* 100078fb */
L_10007900:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000790du); /* 10007907 indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10005590u, 4, 0x1000791fu); /* 1000791a */
  ecx = eax;
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x58u), 12, 0x10007932u); /* 1000792f indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007957u, 0, 0x1000793bu); /* 10007936 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_play_position_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused)),
                                       uint32_t arg2 __attribute__((unused)),
                                       uint32_t arg3 __attribute__((unused)),
                                       uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_play_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $20");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_get_volume_body(uint32_t *input) {
#else
void bb_qmix_export_get_volume_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10007a91u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100079eb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100079dbu); /* 100079d5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100079eb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100079e7u); /* 100079e2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007a10;
L_100079eb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100079f2u); /* 100079ed */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007a10u); /* 10007a0b */
L_10007a10:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10007a1du); /* 10007a17 indirect */
  ecx = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  SAMPLE_CALL(0x10005590u, 4, 0x10007a2fu); /* 10007a2a */
  ecx = BB_U32(eax + 0x10u);
  eax = BB_U32(ebp + 0x10u);
  BB_U32(eax) = ecx;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007a5cu, 0, 0x10007a40u); /* 10007a3b */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_get_volume_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_volume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_export_init_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 2052u;
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
  a = esp;
  b = 0x14u;
  esp -= b;
  sp -= b / 4u;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = (esp);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ecx) = eax;
  BB_U32(ecx + 0x4u) = eax;
  BB_U32(ecx + 0x8u) = eax;
  BB_U32(ecx + 0xcu) = eax;
  BB_U32(ecx + 0x10u) = eax;
  WORD(esp + 0x4u) = 0x14u;
  args[sp + 1u] = BB_U32(esp + 4u);
  SAMPLE_CALL(0x100065b0u, 4, 0x10007ac4u); /* 10007abf */
  a = esp;
  b = 0x14u;
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
uint32_t __attribute__((naked)) bb_qmix_export_init_entry(void) {
  __asm__ volatile("leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_init_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "2048(%esp),%esp;ret $0");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_is_channel_done_body(uint32_t *input) {
#else
void bb_qmix_export_is_channel_done_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10007bb3u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x20u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007b1b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10007b0bu); /* 10007b05 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007b1b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007b17u); /* 10007b12 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007b40;
L_10007b1b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007b22u); /* 10007b1d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007b40u); /* 10007b3b */
L_10007b40:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10007b4du); /* 10007b47 indirect */
  ecx = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  SAMPLE_CALL(0x10005590u, 4, 0x10007b5fu); /* 10007b5a */
  ecx = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x64u), 0, 0x10007b66u); /* 10007b63 indirect */
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
  a = eax;
  b = a;
  a = 0u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(ebp + 0xffffffe4u) = eax;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007b8fu, 0, 0x10007b79u); /* 10007b74 */
  eax = BB_U32(ebp + 0xffffffe4u);
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
bb_qmix_export_is_channel_done_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_is_channel_done_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_open_channel_body(uint32_t *input) {
#else
void bb_qmix_export_open_channel_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10007cbbu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007c0b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10007bfbu); /* 10007bf5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007c0b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007c07u); /* 10007c02 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007c30;
L_10007c0b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007c12u); /* 10007c0d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007c30u); /* 10007c2b */
L_10007c30:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10007c3du); /* 10007c37 indirect */
  ecx = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ecx);
  SAMPLE_CALL(0x100057c0u, 8, 0x10007c53u); /* 10007c4e */
  BB_U32(ebp + 0xcu) = eax;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007c7bu, 0, 0x10007c5fu); /* 10007c5a */
  a = BB_U32(ebp + 0x10u);
  b = 0x3u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10007cb0;
  eax = BB_U32(ebp + 0xcu);
L_10007c68:;
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
L_10007cb0:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_10007c68;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_export_open_channel_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_open_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_export_open_wave_c(uint32_t *input) {
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
  eax = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0xcu);
  edx = BB_U32(esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001090u, 0, 0x10007ce4u); /* 10007cdf */
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
bb_qmix_export_open_wave_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused)),
                               uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_open_wave_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_pause_channel_body(uint32_t *input) {
#else
void bb_qmix_export_pause_channel_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10007e2eu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007d3b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10007d2bu); /* 10007d25 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007d3b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007d37u); /* 10007d32 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007d60;
L_10007d3b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007d42u); /* 10007d3d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007d60u); /* 10007d5b */
L_10007d60:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10007d6du); /* 10007d67 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10007db1;
  esi = (esi ^ esi);
  BB_U32(ebp + 0xcu) = esi;
  edi = BB_U32(ebp + 0xffffffdcu);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10007d82:;
  edi = esi;
  BB_U32(ebp + 0xffffffdcu) = edi;
  esi = (esi + 1u);
  BB_U32(ebp + 0xcu) = esi;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10007d9f;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10007d9f;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + edi * 4u);
  goto L_10007da1;
L_10007d9f:;
  ebx = (ebx ^ ebx);
L_10007da1:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007dc4;
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x10007dafu); /* 10007dac indirect */
  goto L_10007d82;
L_10007db1:;
  esi = BB_U32(ebp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10007dbdu); /* 10007db8 */
  edx = BB_U32(eax);
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x18u), 0, 0x10007dc4u); /* 10007dc1 indirect */
L_10007dc4:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10007dd4;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10007dd4:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007df9u, 0, 0x10007dddu); /* 10007dd8 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_pause_channel_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_pause_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_play_body(uint32_t *input) {
#else
void bb_qmix_export_play_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10007f29u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  esi = BB_U32(ebp + 0x8u);
  eax = BB_U32(esi + 0x2u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007e8e;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10007e7eu); /* 10007e78 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007e8e;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007e8au); /* 10007e85 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007eb3;
L_10007e8e:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007e95u); /* 10007e90 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10007eb3u); /* 10007eae */
L_10007eb3:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10007ec0u); /* 10007eba indirect */
  eax = (ebp + 0xffffffecu);
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(eax);
  SAMPLE_CALL(0x10009850u, 4, 0x10007ecfu); /* 10007eca */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10007ef4u, 0, 0x10007ed8u); /* 10007ed3 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_play_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_play_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_export_pump_c(uint32_t *input) {
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
  a = BB_U32(0x10020aecu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10007f81;
  ecx = BB_U32(0x10020aecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007f54u); /* 10007f4f */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007f81;
  eax = BB_U32(0x10020aecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10007f67u); /* 10007f61 indirect */
  ecx = BB_U32(0x10020aecu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x10007f72u); /* 10007f6f indirect */
  eax = BB_U32(0x10020aecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021290u), 4, 0x10007f81u); /* 10007f7b indirect */
L_10007f81:;
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
uint32_t __attribute__((naked)) bb_qmix_export_pump_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_pump_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "512(%esp),%esp;ret $0");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_restart_channel_body(uint32_t *input) {
#else
void bb_qmix_export_restart_channel_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100080d6u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x34u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10007fdb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10007fcbu); /* 10007fc5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10007fdb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10007fd7u); /* 10007fd2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008000;
L_10007fdb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10007fe2u); /* 10007fdd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008000u); /* 10007ffb */
L_10008000:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000800du); /* 10008007 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10008051;
  ebx = (ebx ^ ebx);
  BB_U32(ebp + 0xcu) = ebx;
  esi = BB_U32(ebp + 0xffffffe8u);
  edi = BB_U32(ebp + 0xffffffdcu);
L_10008022:;
  edi = ebx;
  BB_U32(ebp + 0xffffffdcu) = edi;
  ebx = (ebx + 1u);
  BB_U32(ebp + 0xcu) = ebx;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000803f;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000803f;
  eax = BB_U32(eax + 0x20u);
  esi = BB_U32(eax + edi * 4u);
  goto L_10008041;
L_1000803f:;
  esi = (esi ^ esi);
L_10008041:;
  BB_U32(ebp + 0xffffffe8u) = esi;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008064;
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x44u), 0, 0x1000804fu); /* 1000804c indirect */
  goto L_10008022;
L_10008051:;
  ebx = BB_U32(ebp + 0xcu);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x1000805du); /* 10008058 */
  edx = BB_U32(eax);
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x44u), 0, 0x10008064u); /* 10008061 indirect */
L_10008064:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10008074;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10008074:;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 0, 0x1000807cu); /* 10008079 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100080a1u, 0, 0x10008085u); /* 10008080 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_restart_channel_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_restart_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_distance_mapping_body(uint32_t *input) {
#else
void bb_qmix_export_set_distance_mapping_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10008228u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000812b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000811bu); /* 10008115 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000812b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008127u); /* 10008122 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008150;
L_1000812b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008132u); /* 1000812d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008150u); /* 1000814b */
L_10008150:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000815du); /* 10008157 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100081a7;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10008177:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10008194;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10008194;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_10008196;
L_10008194:;
  ebx = (ebx ^ ebx);
L_10008196:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100081be;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x24u), 4, 0x100081a5u); /* 100081a2 indirect */
  goto L_10008177;
L_100081a7:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x100081b3u); /* 100081ae */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x24u), 4, 0x100081beu); /* 100081bb indirect */
L_100081be:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100081ce;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_100081ce:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100081f3u, 0, 0x100081d7u); /* 100081d2 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_distance_mapping_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1 __attribute__((unused)),
                                          uint32_t arg2 __attribute__((unused)),
                                          uint32_t arg3
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_distance_mapping_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_frequency_body(uint32_t *input) {
#else
void bb_qmix_export_set_frequency_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10008388u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000828b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000827bu); /* 10008275 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000828b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008287u); /* 10008282 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100082b0;
L_1000828b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008292u); /* 1000828d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100082b0u); /* 100082ab */
L_100082b0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100082bdu); /* 100082b7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10008307;
  BB_U32(ebp + 0xcu) = 0x0u;
  edi = BB_U32(ebp + 0x14u);
  esi = BB_U32(ebp + 0xffffffdcu);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_100082d7:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100082f4;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100082f4;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_100082f6;
L_100082f4:;
  ebx = (ebx ^ ebx);
L_100082f6:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000831e;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x28u), 4, 0x10008305u); /* 10008302 indirect */
  goto L_100082d7;
L_10008307:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10008313u); /* 1000830e */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x28u), 4, 0x1000831eu); /* 1000831b indirect */
L_1000831e:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000832e;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_1000832e:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008353u, 0, 0x10008337u); /* 10008332 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_frequency_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused)),
                                   uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_frequency_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_listener_orientation_body(uint32_t *input) {
#else
void bb_qmix_export_set_listener_orientation_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x1000849bu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100083eb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100083dbu); /* 100083d5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100083eb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100083e7u); /* 100083e2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008410;
L_100083eb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100083f2u); /* 100083ed */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008410u); /* 1000840b */
L_10008410:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000841du); /* 10008417 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0xcu), 8, 0x10008431u); /* 1000842e indirect */
  a = BYTE(ebp + 0x14u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10008441;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10008441:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008466u, 0, 0x1000844au); /* 10008445 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_set_listener_orientation_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused)),
    uint32_t arg2 __attribute__((unused)),
    uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_listener_orientation_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_listener_position_body(uint32_t *input) {
#else
void bb_qmix_export_set_listener_position_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100085a7u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100084fb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100084ebu); /* 100084e5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100084fb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100084f7u); /* 100084f2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008520;
L_100084fb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008502u); /* 100084fd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008520u); /* 1000851b */
L_10008520:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000852du); /* 10008527 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x10u), 4, 0x1000853du); /* 1000853a indirect */
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000854d;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_1000854d:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008572u, 0, 0x10008556u); /* 10008551 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_set_listener_position_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused)),
    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_listener_position_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_listener_velocity_body(uint32_t *input) {
#else
void bb_qmix_export_set_listener_velocity_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100086b7u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000860b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100085fbu); /* 100085f5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000860b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008607u); /* 10008602 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008630;
L_1000860b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008612u); /* 1000860d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008630u); /* 1000862b */
L_10008630:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000863du); /* 10008637 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x14u), 4, 0x1000864du); /* 1000864a indirect */
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000865d;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_1000865d:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008682u, 0, 0x10008666u); /* 10008661 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_set_listener_velocity_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused)),
    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_listener_velocity_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_options_body(uint32_t *input) {
#else
void bb_qmix_export_set_options_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100087aau;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000871b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000870bu); /* 10008705 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000871b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008717u); /* 10008712 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008740;
L_1000871b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008722u); /* 1000871d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008740u); /* 1000873b */
L_10008740:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000874du); /* 10008747 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ecx);
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(edx + 0x18u), 8, 0x10008761u); /* 1000875e indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008786u, 0, 0x1000876au); /* 10008765 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_options_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused)),
                                 uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_options_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_pan_body(uint32_t *input) {
#else
void bb_qmix_export_set_pan_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x100088afu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000880b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100087fbu); /* 100087f5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000880b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008807u); /* 10008802 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008830;
L_1000880b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008812u); /* 1000880d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008830u); /* 1000882b */
L_10008830:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000883du); /* 10008837 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100058f0u, 12, 0x10008855u); /* 10008850 */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x1000887au, 0, 0x1000885eu); /* 10008859 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_pan_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused)),
                             uint32_t arg2 __attribute__((unused)),
                             uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_pan_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_pan_rate_body(uint32_t *input) {
#else
void bb_qmix_export_set_pan_rate_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10008a08u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000890b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100088fbu); /* 100088f5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000890b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008907u); /* 10008902 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008930;
L_1000890b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008912u); /* 1000890d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008930u); /* 1000892b */
L_10008930:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000893du); /* 10008937 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10008987;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10008957:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10008974;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10008974;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_10008976;
L_10008974:;
  ebx = (ebx ^ ebx);
L_10008976:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000899e;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x2cu), 4, 0x10008985u); /* 10008982 indirect */
  goto L_10008957;
L_10008987:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10008993u); /* 1000898e */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x2cu), 4, 0x1000899eu); /* 1000899b indirect */
L_1000899e:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100089ae;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_100089ae:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100089d3u, 0, 0x100089b7u); /* 100089b2 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_pan_rate_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused)),
                                  uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_pan_rate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_position_body(uint32_t *input) {
#else
void bb_qmix_export_set_position_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10008b68u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008a6b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10008a5bu); /* 10008a55 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008a6b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008a67u); /* 10008a62 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008a90;
L_10008a6b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008a72u); /* 10008a6d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008a90u); /* 10008a8b */
L_10008a90:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10008a9du); /* 10008a97 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10008ae7;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffe8u);
  ebx = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
L_10008ab7:;
  ebx = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = ebx;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10008ad4;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = ebx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10008ad4;
  eax = BB_U32(eax + 0x20u);
  esi = BB_U32(eax + ebx * 4u);
  goto L_10008ad6;
L_10008ad4:;
  esi = (esi ^ esi);
L_10008ad6:;
  BB_U32(ebp + 0xffffffe8u) = esi;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008afe;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x34u), 4, 0x10008ae5u); /* 10008ae2 indirect */
  goto L_10008ab7;
L_10008ae7:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10008af3u); /* 10008aee */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x34u), 4, 0x10008afeu); /* 10008afb indirect */
L_10008afe:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10008b0e;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10008b0e:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008b33u, 0, 0x10008b17u); /* 10008b12 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_position_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused)),
                                  uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_remix_buffer_size_body(uint32_t *input) {
#else
void bb_qmix_export_set_remix_buffer_size_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10008c67u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008bcb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10008bbbu); /* 10008bb5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008bcb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008bc7u); /* 10008bc2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008bf0;
L_10008bcb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008bd2u); /* 10008bcd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008bf0u); /* 10008beb */
L_10008bf0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10008bfdu); /* 10008bf7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 4, 0x10008c0du); /* 10008c0a indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008c32u, 0, 0x10008c16u); /* 10008c11 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
uint32_t __attribute__((naked)) bb_qmix_export_set_remix_buffer_size_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_remix_buffer_size_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_room_size_body(uint32_t *input) {
#else
void bb_qmix_export_set_room_size_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10008d77u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008ccb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10008cbbu); /* 10008cb5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008ccb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008cc7u); /* 10008cc2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008cf0;
L_10008ccb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008cd2u); /* 10008ccd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008cf0u); /* 10008ceb */
L_10008cf0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10008cfdu); /* 10008cf7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x20u), 4, 0x10008d0du); /* 10008d0a indirect */
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10008d1d;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10008d1d:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008d42u, 0, 0x10008d26u); /* 10008d21 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_room_size_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_room_size_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_source_cone_body(uint32_t *input) {
#else
void bb_qmix_export_set_source_cone_c(uint32_t *input) {
#endif
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 28u), 28u);
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
  args[--sp] = 0x10008ee8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008ddb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10008dcbu); /* 10008dc5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008ddb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008dd7u); /* 10008dd2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008e00;
L_10008ddb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008de2u); /* 10008ddd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008e00u); /* 10008dfb */
L_10008e00:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10008e0du); /* 10008e07 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10008e5f;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0x14u);
  edi = BB_U32(ebp + 0xffffffdcu);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10008e27:;
  edi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = edi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10008e44;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10008e44;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + edi * 4u);
  goto L_10008e46;
L_10008e44:;
  ebx = (ebx ^ ebx);
L_10008e46:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008e7e;
  eax = BB_U32(ebp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x20u), 12, 0x10008e5du); /* 10008e5a indirect */
  goto L_10008e27;
L_10008e5f:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10008e6bu); /* 10008e66 */
  edx = BB_U32(eax);
  ecx = BB_U32(ebp + 0x1cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(ebp + 0x14u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x20u), 12, 0x10008e7eu); /* 10008e7b indirect */
L_10008e7e:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10008e8e;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10008e8e:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10008eb3u, 0, 0x10008e97u); /* 10008e92 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_source_cone_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused)),
                                     uint32_t arg3 __attribute__((unused)),
                                     uint32_t arg4 __attribute__((unused)),
                                     uint32_t arg5 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_source_cone_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $24");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_source_position_body(uint32_t *input) {
#else
void bb_qmix_export_set_source_position_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10009048u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008f4b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x10008f3bu); /* 10008f35 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008f4b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10008f47u); /* 10008f42 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10008f70;
L_10008f4b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10008f52u); /* 10008f4d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10008f70u); /* 10008f6b */
L_10008f70:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x10008f7du); /* 10008f77 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10008fc7;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10008f97:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10008fb4;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10008fb4;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_10008fb6;
L_10008fb4:;
  ebx = (ebx ^ ebx);
L_10008fb6:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10008fde;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x30u), 4, 0x10008fc5u); /* 10008fc2 indirect */
  goto L_10008f97;
L_10008fc7:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10008fd3u); /* 10008fce */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x30u), 4, 0x10008fdeu); /* 10008fdb indirect */
L_10008fde:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10008fee;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10008fee:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10009013u, 0, 0x10008ff7u); /* 10008ff2 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_source_position_entry(uint32_t arg0 __attribute__((unused)),
                                         uint32_t arg1 __attribute__((unused)),
                                         uint32_t arg2 __attribute__((unused)),
                                         uint32_t arg3
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_source_position_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_source_velocity_body(uint32_t *input) {
#else
void bb_qmix_export_set_source_velocity_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x100091a8u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100090ab;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000909bu); /* 10009095 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100090ab;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100090a7u); /* 100090a2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100090d0;
L_100090ab:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100090b2u); /* 100090ad */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100090d0u); /* 100090cb */
L_100090d0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100090ddu); /* 100090d7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10009127;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_100090f7:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10009114;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10009114;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_10009116;
L_10009114:;
  ebx = (ebx ^ ebx);
L_10009116:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000913e;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x38u), 4, 0x10009125u); /* 10009122 indirect */
  goto L_100090f7;
L_10009127:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x10009133u); /* 1000912e */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x38u), 4, 0x1000913eu); /* 1000913b indirect */
L_1000913e:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000914e;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_1000914e:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10009173u, 0, 0x10009157u); /* 10009152 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_source_velocity_entry(uint32_t arg0 __attribute__((unused)),
                                         uint32_t arg1 __attribute__((unused)),
                                         uint32_t arg2 __attribute__((unused)),
                                         uint32_t arg3
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_source_velocity_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_speaker_placement_body(uint32_t *input) {
#else
void bb_qmix_export_set_speaker_placement_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x100092c0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x28u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000920b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100091fbu); /* 100091f5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000920b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10009207u); /* 10009202 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10009230;
L_1000920b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10009212u); /* 1000920d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd8u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd8u);
  BB_U32(ebp + 0xffffffdcu) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffdcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10009230u); /* 1000922b */
L_10009230:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000923du); /* 10009237 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x24u), 4, 0x1000924du); /* 1000924a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10009277;
  BB_U32(ebp + 0xffffffe0u) = 0x8u;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x1000928bu, 0, 0x10009261u); /* 1000925c */
  eax = BB_U32(ebp + 0xffffffe0u);
L_10009264:;
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
L_10009277:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x1000928bu, 0, 0x10009280u); /* 1000927b */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  goto L_10009264;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked)) bb_qmix_export_set_speaker_placement_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_speaker_placement_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $8");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_speed_of_sound_body(uint32_t *input) {
#else
void bb_qmix_export_set_speed_of_sound_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x100093c7u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x24u;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000931b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000930bu); /* 10009305 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000931b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10009317u); /* 10009312 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10009340;
L_1000931b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10009322u); /* 1000931d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffdcu) = 0x5u;
  eax = BB_U32(ebp + 0xffffffdcu);
  BB_U32(ebp + 0xffffffe0u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe0u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10009340u); /* 1000933b */
L_10009340:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000934du); /* 10009347 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x28u), 4, 0x1000935du); /* 1000935a indirect */
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_1000936d;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_1000936d:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10009392u, 0, 0x10009376u); /* 10009371 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_speed_of_sound_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused)),
                                        uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_speed_of_sound_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_set_volume_body(uint32_t *input) {
#else
void bb_qmix_export_set_volume_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10009528u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000942b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000941bu); /* 10009415 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000942b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10009427u); /* 10009422 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10009450;
L_1000942b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10009432u); /* 1000942d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10009450u); /* 1000944b */
L_10009450:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000945du); /* 10009457 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100094a7;
  BB_U32(ebp + 0xcu) = 0x0u;
  esi = BB_U32(ebp + 0xffffffdcu);
  edi = BB_U32(ebp + 0x14u);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_10009477:;
  esi = BB_U32(ebp + 0xcu);
  BB_U32(ebp + 0xffffffdcu) = esi;
  BB_U32(ebp + 0xcu) = (BB_U32(ebp + 0xcu) + 1u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_10009494;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = esi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_10009494;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + esi * 4u);
  goto L_10009496;
L_10009494:;
  ebx = (ebx ^ ebx);
L_10009496:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100094be;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x3cu), 4, 0x100094a5u); /* 100094a2 indirect */
  goto L_10009477;
L_100094a7:;
  eax = BB_U32(ebp + 0xcu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x100094b3u); /* 100094ae */
  edx = BB_U32(eax);
  edi = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x3cu), 4, 0x100094beu); /* 100094bb indirect */
L_100094be:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_100094ce;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_100094ce:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100094f3u, 0, 0x100094d7u); /* 100094d2 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_set_volume_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused)),
                                uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_volume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $16");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_start_profile_body(uint32_t *input) {
#else
void bb_qmix_export_start_profile_c(uint32_t *input) {
#endif
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
  args[--sp] = 0x10009612u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000958b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000957bu); /* 10009575 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000958b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10009587u); /* 10009582 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100095b0;
L_1000958b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10009592u); /* 1000958d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100095b0u); /* 100095ab */
L_100095b0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100095bdu); /* 100095b7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x30u), 0, 0x100095c9u); /* 100095c6 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x100095eeu, 0, 0x100095d2u); /* 100095cd */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_start_profile_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_start_profile_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $4");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_stop_channel_body(uint32_t *input) {
#else
void bb_qmix_export_stop_channel_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1000975eu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x2cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000966b;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x1000965bu); /* 10009655 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000966b;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x10009667u); /* 10009662 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10009690;
L_1000966b:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10009672u); /* 1000966d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffd4u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd8u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x10009690u); /* 1000968b */
L_10009690:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x1000969du); /* 10009697 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = BYTE(ebp + 0x10u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_100096e1;
  esi = (esi ^ esi);
  BB_U32(ebp + 0xcu) = esi;
  edi = BB_U32(ebp + 0xffffffdcu);
  ebx = BB_U32(ebp + 0xffffffe8u);
L_100096b2:;
  edi = esi;
  BB_U32(ebp + 0xffffffdcu) = edi;
  esi = (esi + 1u);
  BB_U32(ebp + 0xcu) = esi;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_100096cf;
  eax = BB_U32(ebp + 0xffffffecu);
  a = BB_U32(eax + 0x1cu);
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_100096cf;
  eax = BB_U32(eax + 0x20u);
  ebx = BB_U32(eax + edi * 4u);
  goto L_100096d1;
L_100096cf:;
  ebx = (ebx ^ ebx);
L_100096d1:;
  BB_U32(ebp + 0xffffffe8u) = ebx;
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100096f4;
  eax = BB_U32(ebx);
  ecx = ebx;
  SAMPLE_CALL(BB_U32(eax + 0x48u), 0, 0x100096dfu); /* 100096dc indirect */
  goto L_100096b2;
L_100096e1:;
  esi = BB_U32(ebp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x10005590u, 4, 0x100096edu); /* 100096e8 */
  edx = BB_U32(eax);
  ecx = eax;
  SAMPLE_CALL(BB_U32(edx + 0x48u), 0, 0x100096f4u); /* 100096f1 indirect */
L_100096f4:;
  a = BYTE(ebp + 0x10u);
  b = 0x4u;
  flags = logic_flags(flags, a & b, 8);
  if (!(flags & 0x40u))
    goto L_10009704;
  eax = BB_U32(ebp + 0xffffffecu);
  BB_U32(eax + 0x14u) = 0x1u;
L_10009704:;
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10009729u, 0, 0x1000970du); /* 10009708 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_stop_channel_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_stop_channel_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $12");
}
#endif
#ifdef BB_MODEL_RUNTIME
static void bb_qmix_export_stop_profile_body(uint32_t *input) {
#else
void bb_qmix_export_stop_profile_c(uint32_t *input) {
#endif
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
  eax = exception_head();
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebp = esp;
  args[--sp] = 0xffffffffu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x10009846u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0x1cu;
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
  BB_U32(ebp + 0xfffffff0u) = esp;
  BB_U32(ebp + 0xfffffffcu) = 0x0u;
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffecu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100097bb;
  args[--sp] = 0x70u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212d0u), 8, 0x100097abu); /* 100097a5 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100097bb;
  ecx = BB_U32(ebp + 0xffffffecu);
  SAMPLE_CALL(0x100062e0u, 0, 0x100097b7u); /* 100097b2 */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100097e0;
L_100097bb:;
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100097c2u); /* 100097bd */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe0u) = 0x5u;
  eax = BB_U32(ebp + 0xffffffe0u);
  BB_U32(ebp + 0xffffffe4u) = eax;
  args[--sp] = 0x1001ae90u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffe4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100116c0u, 0, 0x100097e0u); /* 100097db */
L_100097e0:;
  eax = BB_U32(ebp + 0xffffffecu);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021294u), 4, 0x100097edu); /* 100097e7 indirect */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  ecx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ecx);
  edx = BB_U32(ebp + 0xcu);
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(eax + 0x34u), 4, 0x100097fdu); /* 100097fa indirect */
  BYTE(ebp + 0xfffffffcu) = 0x0u;
  SAMPLE_CALL(0x10009822u, 0, 0x10009806u); /* 10009801 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
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
bb_qmix_export_stop_profile_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_stop_profile_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_export_unlock_c(uint32_t *input) {
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
  eax = BB_U32(ecx);
  a = eax;
  b = 0x50u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021290u), 4, 0x1000640cu); /* 10006406 indirect */
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
uint32_t __attribute__((naked)) bb_qmix_export_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif


void bb_qmix_export_activate_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100063b5 */
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
uint32_t __attribute__((naked)) bb_qmix_export_activate_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_activate_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_close_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100064cb */
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
bb_qmix_export_close_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_close_channel_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_enable_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 1000675c */
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
bb_qmix_export_enable_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_enable_channel_unlock_c) ";leal "
                                              "4(%esp),%esp;popal;popfl;leal "
                                              "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_flush_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100068b2 */
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
bb_qmix_export_flush_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_flush_channel_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_free_wave_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100069f2 */
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
uint32_t __attribute__((naked)) bb_qmix_export_free_wave_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_free_wave_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_direct_sound_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10006af6 */
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
bb_qmix_export_get_direct_sound_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_direct_sound_unlock_c) ";leal "
                                                "4(%esp),%esp;popal;popfl;leal "
                                                "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_distance_mapping_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10006be2 */
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
bb_qmix_export_get_distance_mapping_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_distance_mapping_unlock_c) ";leal "
                                                    "4(%esp),%esp;popal;popfl;"
                                                    "leal 512(%esp),%esp;ret "
                                                    "$0");
}
#endif
void bb_qmix_export_get_frequency_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10006d1f */
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
bb_qmix_export_get_frequency_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_frequency_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_listener_orientation_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10006e69 */
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
bb_qmix_export_get_listener_orientation_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_listener_orientation_unlock_c) ";leal "
                                                        "4(%esp),%esp;popal;"
                                                        "popfl;leal "
                                                        "512(%esp),%esp;ret "
                                                        "$0");
}
#endif
void bb_qmix_export_get_listener_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10006f45 */
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
bb_qmix_export_get_listener_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_listener_position_unlock_c) ";leal "
                                                     "4(%esp),%esp;popal;popfl;"
                                                     "leal 512(%esp),%esp;ret "
                                                     "$0");
}
#endif
void bb_qmix_export_get_listener_velocity_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007025 */
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
bb_qmix_export_get_listener_velocity_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_listener_velocity_unlock_c) ";leal "
                                                     "4(%esp),%esp;popal;popfl;"
                                                     "leal 512(%esp),%esp;ret "
                                                     "$0");
}
#endif
void bb_qmix_export_get_options_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007106 */
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
uint32_t __attribute__((naked)) bb_qmix_export_get_options_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_options_unlock_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_pan_rate_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 1000729f */
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
uint32_t __attribute__((naked)) bb_qmix_export_get_pan_rate_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_pan_rate_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100073c2 */
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
uint32_t __attribute__((naked)) bb_qmix_export_get_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_position_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_room_size_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100074b6 */
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
bb_qmix_export_get_room_size_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_room_size_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_source_cone_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100075aa */
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
bb_qmix_export_get_source_cone_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_source_cone_unlock_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_source_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007692 */
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
bb_qmix_export_get_source_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_source_position_unlock_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_export_get_source_velocity_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007782 */
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
bb_qmix_export_get_source_velocity_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_source_velocity_unlock_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_export_get_speed_of_sound_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007865 */
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
bb_qmix_export_get_speed_of_sound_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_speed_of_sound_unlock_c) ";leal "
                                                  "4(%esp),%esp;popal;popfl;"
                                                  "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_play_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 1000795a */
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
bb_qmix_export_get_play_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_play_position_unlock_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_get_volume_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007a5f */
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
uint32_t __attribute__((naked)) bb_qmix_export_get_volume_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_get_volume_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_is_channel_done_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007b92 */
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
bb_qmix_export_is_channel_done_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_is_channel_done_unlock_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_open_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007c7e */
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
uint32_t __attribute__((naked)) bb_qmix_export_open_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_open_channel_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_pause_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007dfc */
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
bb_qmix_export_pause_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_pause_channel_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_play_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10007ef7 */
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
uint32_t __attribute__((naked)) bb_qmix_export_play_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_play_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_restart_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100080a4 */
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
bb_qmix_export_restart_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_restart_channel_unlock_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_distance_mapping_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100081f6 */
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
bb_qmix_export_set_distance_mapping_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_distance_mapping_unlock_c) ";leal "
                                                    "4(%esp),%esp;popal;popfl;"
                                                    "leal 512(%esp),%esp;ret "
                                                    "$0");
}
#endif
void bb_qmix_export_set_frequency_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008356 */
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
bb_qmix_export_set_frequency_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_frequency_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_listener_orientation_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008469 */
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
bb_qmix_export_set_listener_orientation_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_listener_orientation_unlock_c) ";leal "
                                                        "4(%esp),%esp;popal;"
                                                        "popfl;leal "
                                                        "512(%esp),%esp;ret "
                                                        "$0");
}
#endif
void bb_qmix_export_set_listener_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008575 */
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
bb_qmix_export_set_listener_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_listener_position_unlock_c) ";leal "
                                                     "4(%esp),%esp;popal;popfl;"
                                                     "leal 512(%esp),%esp;ret "
                                                     "$0");
}
#endif
void bb_qmix_export_set_listener_velocity_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008685 */
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
bb_qmix_export_set_listener_velocity_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_listener_velocity_unlock_c) ";leal "
                                                     "4(%esp),%esp;popal;popfl;"
                                                     "leal 512(%esp),%esp;ret "
                                                     "$0");
}
#endif
void bb_qmix_export_set_options_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008789 */
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
uint32_t __attribute__((naked)) bb_qmix_export_set_options_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_options_unlock_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_pan_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 1000887d */
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
uint32_t __attribute__((naked)) bb_qmix_export_set_pan_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_pan_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_pan_rate_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100089d6 */
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
uint32_t __attribute__((naked)) bb_qmix_export_set_pan_rate_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_pan_rate_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008b36 */
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
uint32_t __attribute__((naked)) bb_qmix_export_set_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_position_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_remix_buffer_size_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008c35 */
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
bb_qmix_export_set_remix_buffer_size_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_remix_buffer_size_unlock_c) ";leal "
                                                     "4(%esp),%esp;popal;popfl;"
                                                     "leal 512(%esp),%esp;ret "
                                                     "$0");
}
#endif
void bb_qmix_export_set_room_size_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008d45 */
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
bb_qmix_export_set_room_size_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_room_size_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_source_cone_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10008eb6 */
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
bb_qmix_export_set_source_cone_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_source_cone_unlock_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_source_position_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10009016 */
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
bb_qmix_export_set_source_position_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_source_position_unlock_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_export_set_source_velocity_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10009176 */
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
bb_qmix_export_set_source_velocity_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_source_velocity_unlock_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$0");
}
#endif
void bb_qmix_export_set_speaker_placement_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 1000928e */
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
bb_qmix_export_set_speaker_placement_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_speaker_placement_unlock_c) ";leal "
                                                     "4(%esp),%esp;popal;popfl;"
                                                     "leal 512(%esp),%esp;ret "
                                                     "$0");
}
#endif
void bb_qmix_export_set_speed_of_sound_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10009395 */
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
bb_qmix_export_set_speed_of_sound_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_speed_of_sound_unlock_c) ";leal "
                                                  "4(%esp),%esp;popal;popfl;"
                                                  "leal 512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_set_volume_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100094f6 */
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
uint32_t __attribute__((naked)) bb_qmix_export_set_volume_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_set_volume_unlock_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_start_profile_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 100095f1 */
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
bb_qmix_export_start_profile_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_start_profile_unlock_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_stop_channel_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 1000972c */
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
uint32_t __attribute__((naked)) bb_qmix_export_stop_channel_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_stop_channel_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_export_stop_profile_unlock_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffecu);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10006400u, 0, BB_U32(esp - 4u)); /* tail call 10009825 */
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
uint32_t __attribute__((naked)) bb_qmix_export_stop_profile_unlock_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_export_stop_profile_unlock_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif

#ifdef BB_MODEL_RUNTIME
#define API_WRAPPER(handler, name, frame, high, error, memory)                 \
  void name##_c(uint32_t *input) {                                            \
    bb_qmix_model_api_call(name##_body, input, frame, high, error, memory);    \
  }
BB_QMIX_API_SCOPES(API_WRAPPER)
#undef API_WRAPPER
#endif
