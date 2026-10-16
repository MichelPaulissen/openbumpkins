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
/* C forms of QMIXER wave descriptor loading and ownership.
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
void bb_qmix_wave_load_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[128] = {0}, sp = 112, a __attribute__((unused)),
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
  args[--sp] = 0x10001731u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  exception_link(esp);
  a = esp;
  b = 0xd0u;
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
  args[--sp] = 0x54u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100010b8u); /* 100010b3 */
  BB_U32(ebp + 0xffffffecu) = eax;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  esi = eax;
  eax = (eax ^ eax);
  BB_U32(ebp + 0xfffffffcu) = eax;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100010d6;
  BB_U32(esi + 0x44u) = eax;
  BB_U32(esi + 0x48u) = eax;
  BB_U32(esi + 0x4cu) = eax;
  eax = esi;
  goto L_100010d8;
L_100010d6:;
  eax = (eax ^ eax);
L_100010d8:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(ebp + 0xfffffffcu) = 0x1u;
  ecx = 0x8u;
  BB_U32(ebp + 0xfffffff0u) = eax;
  eax = BB_U32(ebp + 0xfffffff0u);
  eax = (eax + 0x10u);
  edi = eax;
  BB_U32(ebp + 0xffffffecu) = eax;
  eax = (eax ^ eax);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ebx = BB_U32(ebp + 0x10u);
  eax = BB_U32(ebp + 0xffffffecu);
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(eax + 0x10u) = ebx;
  if (flags & 0x40u)
    goto L_10001367;
  a = ebx;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001367;
  a = ebx;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100011cc;
  edi = (ebp + 0xffffff24u);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x12u;
  args[--sp] = 0x1001e070u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = BB_U32(ebp + 0x8u);
  esi = BB_U32(ebp + 0xcu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212a8u), 12, 0x10001143u); /* 1000113d indirect */
  edi = eax;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100011a3;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212a4u), 8, 0x10001151u); /* 1000114b indirect */
  BB_U32(ebp + 0xffffffc4u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100011a3;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212a0u), 4, 0x1000115fu); /* 10001159 indirect */
  BB_U32(ebp + 0xffffff3cu) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100011a3;
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002129cu), 8, 0x10001171u); /* 1000116b indirect */
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffff38u) = eax;
  BB_U32(ebp + 0xffffff28u) = 0x204d454du;
  BB_U32(ebp + 0xffffff54u) = 0x0u;
  eax = (ebp + 0xffffff24u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002137cu), 12, 0x1000119cu); /* 10001196 indirect */
  esi = eax;
  goto L_100013c8;
L_100011a3:;
  args[--sp] = 0xfu;
  esp -= 4u;
  BB_U32(esp) = args[sp];
L_100011a5:;
  SAMPLE_CALL(0x10005040u, 0, 0x100011aau); /* 100011a5 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100011ad:;
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x1000173bu, 0, 0x100011b9u); /* 100011b4 */
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_100011bb:;
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
L_100011cc:;
  a = ebx;
  b = 0x4u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100011f3;
  edi = (ebp + 0xffffff80u);
  esi = BB_U32(ebp + 0x8u);
  ecx = 0x12u;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = (ebp + 0xffffff80u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002137cu), 12, 0x100011ecu); /* 100011e6 indirect */
  esi = eax;
  goto L_100013c8;
L_100011f3:;
  a = ebx;
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100012a9;
  ecx = BB_U32(ebp + 0x8u);
  edx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(ecx);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  ebx = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  esi = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = esi;
  eax = BB_U32(ecx + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(edx + 0xcu) = eax;
  eax = BB_U32(ebp + 0xffffffecu);
  ebx = BB_U32(ecx + 0x4u);
  BB_U32(eax) = ebx;
  esi = BB_U32(ecx + 0x8u);
  BB_U32(eax + 0x4u) = esi;
  eax = (eax ^ eax);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0xcu)) & 0xffffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e068u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xfffffff0u);
  ebx = 0x12u;
  a = eax;
  b = 0x30u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021300u), 0, 0x10001249u); /* 10001243 indirect */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  eax = BB_U32(ecx);
  a = WORD(eax);
  b = 0x1u;
  flags = sub_flags(flags, a, b, 16);
  if (flags & 0x40u)
    goto L_10001260;
  ebx = (ebx ^ ebx);
  ebx = (ebx & 0xffff0000u) | (((uint32_t)(WORD(eax + 0x10u)) & 0xffffu) << 0);
  a = ebx;
  b = 0x12u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ebx = result;
L_10001260:;
  esi = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10001269u); /* 10001264 */
  BB_U32(esi + 0x40u) = eax;
  esi = BB_U32(ebp + 0xfffffff0u);
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  edi = BB_U32(esi + 0x40u);
  esi = BB_U32(ecx);
  ecx = ebx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = ebx;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  esi = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ebp + 0xfffffff0u) = 0x0u;
  BB_U32(ebp + 0xffffffecu) = esi;
  SAMPLE_CALL(0x1000173bu, 0, 0x100012a1u); /* 1000129c */
  eax = BB_U32(ebp + 0xffffffecu);
  goto L_100011bb;
L_100012a9:;
  a = ebx;
  b = 0x11u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100013c5;
  ecx = BB_U32(ebp + 0x8u);
  edx = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(ecx);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  ebx = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  esi = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = esi;
  eax = BB_U32(ecx + 0xcu);
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(edx + 0xcu) = eax;
  eax = BB_U32(ecx + 0x4u);
  edx = BB_U32(ebp + 0xffffffecu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(edx + 0x4u) = eax;
  SAMPLE_CALL(0x10001020u, 0, 0x100012e2u); /* 100012dd */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0xffffffecu);
  edx = BB_U32(ebp + 0x8u);
  BB_U32(ecx) = eax;
  eax = BB_U32(edx + 0xcu);
  BB_U32(ecx + 0xcu) = eax;
  eax = (eax ^ eax);
  esi = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x1cu) = esi;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edx + 0x10u)) & 0xffffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e060u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xfffffff0u);
  a = eax;
  b = 0x30u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021300u), 0, 0x10001312u); /* 1000130c indirect */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  esi = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = 0x10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x1000131fu); /* 1000131a */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(ebp + 0x8u);
  BB_U32(esi + 0x40u) = eax;
  esi = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(ecx);
  edx = BB_U32(esi + 0x40u);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  ebx = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  esi = BB_U32(ecx + 0x8u);
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  BB_U32(edx + 0x8u) = esi;
  eax = BB_U32(ecx + 0xcu);
  BB_U32(edx + 0xcu) = eax;
  ecx = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ebp + 0xfffffff0u) = 0x0u;
  BB_U32(ebp + 0xffffffecu) = ecx;
  SAMPLE_CALL(0x1000173bu, 0, 0x1000135fu); /* 1000135a */
  eax = BB_U32(ebp + 0xffffffecu);
  goto L_100011bb;
L_10001367:;
  esi = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x8000080u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0x8u);
  args[--sp] = 0x3u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x80000000u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021298u), 28, 0x10001386u); /* 10001380 indirect */
  BB_U32(esi + 0x44u) = eax;
  edx = BB_U32(ebp + 0xfffffff0u);
  edx = (edx + 0x44u);
  a = BB_U32(edx);
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000139b;
  args[--sp] = 0x10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  goto L_100011a5;
L_1000139b:;
  edi = (ebp + 0xffffff80u);
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
  ecx = 0x12u;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  BB_U32(ebp + 0xffffff84u) = 0x20534f44u;
  eax = BB_U32(edx);
  ecx = (ebp + 0xffffff80u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffffb0u) = eax;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x1002137cu), 12, 0x100013c1u); /* 100013bb indirect */
  esi = eax;
  goto L_100013c8;
L_100013c5:;
  esi = BB_U32(ebp + 0xfffffff0u);
L_100013c8:;
  eax = BB_U32(ebp + 0xffffffc4u);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  BB_U32(ebp + 0xffffffd0u) = eax;
  if (!(flags & 0x40u))
    goto L_100013ee;
  args[--sp] = 0x10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100013ddu); /* 100013d8 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x1000171cu, 0, 0x100013e9u); /* 100013e4 */
  goto L_100011ad;
L_100013ee:;
  a = ebx;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100013fd;
  eax = 0x0u;
  a = ebx;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10001402;
L_100013fd:;
  eax = 0x10u;
L_10001402:;
  args[--sp] = 0x20u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffffc8u) = esi;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffffccu) = eax;
  eax = (ebp + 0xffffff6cu);
  edi = BB_U32(0x10021380u);
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  BB_U32(ebp + 0xffffff74u) = 0x45564157u;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x1000142au); /* 10001428 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000144f;
  args[--sp] = 0x11u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10001435u); /* 10001430 */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x10001714u, 0, 0x10001441u); /* 1000143c */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x1000144au); /* 10001445 */
  goto L_100011ad;
L_1000144f:;
  args[--sp] = 0x10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffff6cu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd4u) = 0x20746d66u;
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x10001466u); /* 10001464 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000148b;
  args[--sp] = 0x11u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10001471u); /* 1000146c */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x10001714u, 0, 0x1000147du); /* 10001478 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x10001486u); /* 10001481 */
  goto L_100011ad;
L_1000148b:;
  eax = BB_U32(ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x10001494u); /* 1000148f */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffe8u) = eax;
  BYTE(ebp + 0xfffffffcu) = 0x4u;
  eax = BB_U32(ebp + 0xffffffe8u);
  ecx = BB_U32(ebp + 0xffffffd8u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(ebp + 0xffffff68u) = eax;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021384u), 12, 0x100014b3u); /* 100014ad indirect */
  a = eax;
  b = BB_U32(ebp + 0xffffffd8u);
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100014e2;
  args[--sp] = 0x11u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100014bfu); /* 100014ba */
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x1000170cu, 0, 0x100014cbu); /* 100014c6 */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  SAMPLE_CALL(0x10001714u, 0, 0x100014d4u); /* 100014cf */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x100014ddu); /* 100014d8 */
  goto L_100011ad;
L_100014e2:;
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (ebp + 0xffffffd4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021388u), 12, 0x100014efu); /* 100014e9 indirect */
  args[--sp] = 0x10u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = (ebp + 0xffffff6cu);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edx = (ebp + 0xffffffd4u);
  BB_U32(ebp + 0xffffffd4u) = 0x61746164u;
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 16, 0x10001506u); /* 10001504 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100016e2;
  a = BB_U32(ebp + 0xffffffd8u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100016e2;
  a = ebx;
  b = 0x10u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10001599;
  esi = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x2u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(esi + 0x44u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021200u), 24, 0x10001534u); /* 1000152e indirect */
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  BB_U32(esi + 0x48u) = eax;
  esi = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(esi + 0x48u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100211f8u), 20, 0x1000154cu); /* 10001546 indirect */
  ecx = BB_U32(ebp + 0xffffffecu);
  BB_U32(esi + 0x4cu) = eax;
  esi = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(esi + 0x4cu);
  eax = (eax + BB_U32(ebp + 0xffffffe0u));
  BB_U32(ecx) = eax;
  esi = BB_U32(ebp + 0xfffffff0u);
  a = BB_U32(esi + 0x4cu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000161d;
  SAMPLE_CALL(BB_U32(0x1002128cu), 0, 0x10001570u); /* 1000156a indirect */
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x10001576u); /* 10001571 */
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x1000170cu, 0, 0x10001582u); /* 1000157d */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  SAMPLE_CALL(0x10001714u, 0, 0x1000158bu); /* 10001586 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x10001594u); /* 1000158f */
  goto L_100011ad;
L_10001599:;
  eax = BB_U32(ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001020u, 0, 0x100015a2u); /* 1000159d */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(ebp + 0xffffffc4u) = eax;
  BYTE(ebp + 0xfffffffcu) = 0x5u;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xffffffd8u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ecx = BB_U32(ebp + 0xffffffc4u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x100017e0u, 0, 0x100015bau); /* 100015b5 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100015f4;
  args[--sp] = 0x11u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100015c8u); /* 100015c3 */
  BYTE(ebp + 0xfffffffcu) = 0x4u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x100016dau, 0, 0x100015d4u); /* 100015cf */
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  SAMPLE_CALL(0x1000170cu, 0, 0x100015ddu); /* 100015d8 */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  SAMPLE_CALL(0x10001714u, 0, 0x100015e6u); /* 100015e1 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x100015efu); /* 100015ea */
  goto L_100011ad;
L_100015f4:;
  eax = BB_U32(ebp + 0xfffffff0u);
  ecx = BB_U32(eax + 0x44u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021288u), 4, 0x10001601u); /* 100015fb indirect */
  edx = BB_U32(ebp + 0xfffffff0u);
  a = ecx;
  b = ecx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  BYTE(ebp + 0xfffffffcu) = 0x4u;
  BB_U32(edx + 0x44u) = ecx;
  edx = BB_U32(ebp + 0xffffffecu);
  eax = BB_U32(ebp + 0xffffffc4u);
  BB_U32(ebp + 0xffffffc4u) = ecx;
  BB_U32(edx) = eax;
  SAMPLE_CALL(0x100016dau, 0, 0x1000161du); /* 10001618 */
L_1000161d:;
  eax = BB_U32(ebp + 0xffffffd8u);
  ecx = BB_U32(ebp + 0xffffffecu);
  BB_U32(ecx + 0x4u) = eax;
  ecx = BB_U32(ebp + 0xffffff68u);
  edx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(ecx);
  BB_U32(edx) = eax;
  ebx = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = ebx;
  esi = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = esi;
  eax = BB_U32(ecx + 0xcu);
  BB_U32(edx + 0xcu) = eax;
  edx = BB_U32(ebp + 0xffffffe8u);
  ecx = BB_U32(ebp + 0xfffffff0u);
  eax = BB_U32(ebp + 0x8u);
  BB_U32(ebp + 0xffffffe8u) = 0x0u;
  BB_U32(ecx + 0x40u) = edx;
  ecx = eax;
  ecx = (ecx >> 16);
  a = ((uint16_t)(ecx));
  b = ((uint16_t)(ecx));
  flags = logic_flags(flags, a & b, 16);
  if (flags & 0x40u)
    goto L_10001685;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021284u), 4, 0x10001669u); /* 10001663 indirect */
  a = eax;
  b = 0xfu;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  if (!(flags & 0x80u))
    goto L_10001670;
  eax = (eax ^ eax);
L_10001670:;
  ecx = BB_U32(ebp + 0x8u);
  ecx = (ecx + eax);
  eax = BB_U32(ebp + 0xfffffff0u);
  args[--sp] = ecx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  a = eax;
  b = 0x30u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021280u), 8, 0x10001683u); /* 1000167d indirect */
  goto L_1000169e;
L_10001685:;
  eax = ((uint16_t)(eax));
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x1001e058u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = BB_U32(ebp + 0xfffffff0u);
  a = eax;
  b = 0x30u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021300u), 0, 0x1000169bu); /* 10001695 indirect */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000169e:;
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  eax = BB_U32(ebp + 0xfffffff0u);
  BB_U32(ebp + 0xfffffff0u) = 0x0u;
  BB_U32(ebp + 0xffffffecu) = eax;
  SAMPLE_CALL(0x1000170cu, 0, 0x100016b4u); /* 100016af */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  SAMPLE_CALL(0x10001714u, 0, 0x100016bdu); /* 100016b8 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x100016c6u); /* 100016c1 */
  BB_U32(ebp + 0xfffffffcu) = 0xffffffffu;
  SAMPLE_CALL(0x1000173bu, 0, 0x100016d2u); /* 100016cd */
  eax = BB_U32(ebp + 0xffffffecu);
  goto L_100011bb;
L_100016e2:;
  args[--sp] = 0x11u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10005040u, 0, 0x100016e9u); /* 100016e4 */
  BYTE(ebp + 0xfffffffcu) = 0x3u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  SAMPLE_CALL(0x1000170cu, 0, 0x100016f5u); /* 100016f0 */
  BYTE(ebp + 0xfffffffcu) = 0x2u;
  SAMPLE_CALL(0x10001714u, 0, 0x100016feu); /* 100016f9 */
  BYTE(ebp + 0xfffffffcu) = 0x1u;
  SAMPLE_CALL(0x1000171cu, 0, 0x10001707u); /* 10001702 */
  goto L_100011ad;
}
#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked))
bb_qmix_wave_load_entry(uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused)),
                        uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_load_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_data_cleanup_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffc4u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100017d0u, 0, BB_U32(esp - 4u)); /* tail call 100016dd */
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
uint32_t __attribute__((naked)) bb_qmix_wave_data_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_data_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_format_cleanup_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffe8u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x100017d0u, 0, BB_U32(esp - 4u)); /* tail call 1000170f */
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
uint32_t __attribute__((naked)) bb_qmix_wave_format_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_format_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_stream_cleanup_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffc8u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10001750u, 0, BB_U32(esp - 4u)); /* tail call 10001717 */
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
uint32_t __attribute__((naked)) bb_qmix_wave_stream_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_stream_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_resource_cleanup_c(uint32_t *input) {
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
  ecx = (ebp + 0xffffffd0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10001770u, 0, BB_U32(esp - 4u)); /* tail call 1000171f */
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
uint32_t __attribute__((naked)) bb_qmix_wave_resource_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_resource_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_alloc_cleanup_c(uint32_t *input) {
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
  eax = BB_U32(ebp + 0xffffffecu);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x1000172du); /* 10001728 */
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
uint32_t __attribute__((naked)) bb_qmix_wave_alloc_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_alloc_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_descriptor_cleanup_c(uint32_t *input) {
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
  ecx = (ebp + 0xfffffff0u);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10001780u, 0, BB_U32(esp - 4u)); /* tail call 1000173e */
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
uint32_t __attribute__((naked)) bb_qmix_wave_descriptor_cleanup_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_descriptor_cleanup_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_stream_close_c(uint32_t *input) {
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
  edx = BB_U32(ecx);
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10001761;
  eax = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = edx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021378u), 8, 0x10001761u); /* 1000175b indirect */
L_10001761:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_stream_close_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_stream_close_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_resource_close_c(uint32_t *input) {
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
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000177d;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212acu), 4, 0x1000177du); /* 10001777 indirect */
L_1000177d:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_resource_close_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_resource_close_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_descriptor_close_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  esi = BB_U32(ecx);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100017c7;
  a = BB_U32(esi + 0x44u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100017be;
  eax = BB_U32(esi + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000179c;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212b0u), 4, 0x1000179cu); /* 10001796 indirect */
L_1000179c:;
  eax = BB_U32(esi + 0x48u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100017ae;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(0x10021288u);
  SAMPLE_CALL(edi, 4, 0x100017acu); /* 100017aa indirect */
  goto L_100017b4;
L_100017ae:;
  edi = BB_U32(0x10021288u);
L_100017b4:;
  eax = BB_U32(esi + 0x44u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100017be;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 4, 0x100017beu); /* 100017bc indirect */
L_100017be:;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100017c4u); /* 100017bf */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100017c7:;
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
uint32_t __attribute__((naked)) bb_qmix_wave_descriptor_close_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_descriptor_close_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_pointer_free_c(uint32_t *input) {
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
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100017d8u); /* 100017d3 */
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
uint32_t __attribute__((naked)) bb_qmix_wave_pointer_free_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_pointer_free_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_stream_read_c(uint32_t *input) {
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
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(esp + 0x18u);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000181d;
  esi = BB_U32(esp + 0x14u);
  ebx = BB_U32(esp + 0x1cu);
L_100017f4:;
  ebp = 0x4000u;
  a = edi;
  b = 0x4000u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_10001803;
  ebp = edi;
L_10001803:;
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebx;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x10021384u), 12, 0x1000180cu); /* 10001806 indirect */
  a = eax;
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10001827;
  a = edi;
  b = ebp;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edi = result;
  SAMPLE_CALL(0x10001830u, 0, 0x10001817u); /* 10001812 */
  esi = (esi + ebp);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100017f4;
L_1000181d:;
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
L_10001827:;
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
bb_qmix_wave_stream_read_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_stream_read_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_message_pump_c(uint32_t *input) {
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
  eax = (esp + 0x8u);
  args[--sp] = edi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = ebp;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  ebx = BB_U32(0x10021328u);
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 20, 0x1000184cu); /* 1000184a indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10001888;
  esi = BB_U32(0x100212f8u);
  edi = BB_U32(0x10021334u);
  ebp = 0x12u;
L_10001861:;
  a = BB_U32(esp + 0x14u);
  b = ebp;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10001888;
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(esi, 4, 0x1000186eu); /* 1000186c indirect */
  eax = (esp + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 4, 0x10001875u); /* 10001873 indirect */
  eax = (esp + 0x10u);
  args[--sp] = 0x1u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(ebx, 20, 0x10001884u); /* 10001882 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10001861;
L_10001888:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x1cu;
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
uint32_t __attribute__((naked)) bb_qmix_wave_message_pump_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_message_pump_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_wave_free_c(uint32_t *input) {
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
  esi = BB_U32(esp + 0xcu);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10001918;
  a = BB_U32(esi + 0x44u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_100018c9;
  a = BB_U32(esi + 0x10u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_100018c9;
  args[--sp] = 0x4u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  eax = (esi + 0x30u);
  args[--sp] = 0x1001e078u;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10011c00u, 0, 0x100018b6u); /* 100018b1 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100018c9;
  eax = BB_U32(esi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100018c6u); /* 100018c1 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_100018c9:;
  eax = BB_U32(esi + 0x40u);
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x100018d2u); /* 100018cd */
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10001918;
  a = BB_U32(esi + 0x44u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000190f;
  eax = BB_U32(esi + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100018ed;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(BB_U32(0x100212b0u), 4, 0x100018edu); /* 100018e7 indirect */
L_100018ed:;
  eax = BB_U32(esi + 0x48u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100018ff;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  edi = BB_U32(0x10021288u);
  SAMPLE_CALL(edi, 4, 0x100018fdu); /* 100018fb indirect */
  goto L_10001905;
L_100018ff:;
  edi = BB_U32(0x10021288u);
L_10001905:;
  eax = BB_U32(esi + 0x44u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000190f;
  args[--sp] = eax;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(edi, 4, 0x1000190fu); /* 1000190d indirect */
L_1000190f:;
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(esp) = args[sp];
  SAMPLE_CALL(0x10001070u, 0, 0x10001915u); /* 10001910 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10001918:;
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
bb_qmix_wave_free_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_wave_free_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "512(%esp),%esp;ret $0");
}
#endif
