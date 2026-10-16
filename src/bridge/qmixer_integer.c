/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
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
/* C forms of QMIXER integer and short sample mixing.
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
#define F32(a) BB_FLOAT(a)
/* C forms of QMIXER sample buffers and mixing state. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */

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

/* Packed sample arithmetic expressed with ordinary C values. The legacy
 * blocks hold each gain for eight samples and saturate their ramp updates.
 * Keeping those boundaries avoids new clipping when several voices overlap.
 * This code needs no MMX processor instructions or MMX register state. */
static uint64_t packed_unpack(uint64_t left, uint64_t right, unsigned bits,
                              unsigned high) {
  uint64_t result = 0, mask = (1ull << bits) - 1;
  unsigned lanes = 32 / bits, start = high ? lanes : 0;
  for (unsigned i = 0; i < lanes; i++) {
    result |= ((left >> ((i + start) * bits)) & mask) << (2 * i * bits);
    result |= ((right >> ((i + start) * bits)) & mask) << ((2 * i + 1) * bits);
  }
  return result;
}
static uint64_t packed_shift_words(uint64_t value, unsigned count) {
  uint64_t result = 0;
  if (count >= 16)
    return 0;
  for (unsigned i = 0; i < 4; i++)
    result |= (uint64_t)(uint16_t)((uint16_t)(value >> (16 * i)) << count)
              << (16 * i);
  return result;
}
static uint64_t packed_multiply_high(uint64_t left, uint64_t right) {
  uint64_t result = 0;
  for (unsigned i = 0; i < 4; i++) {
    int32_t product = (int32_t)(int16_t)(left >> (16 * i)) *
                      (int32_t)(int16_t)(right >> (16 * i));
    result |= (uint64_t)(uint16_t)((uint32_t)product >> 16) << (16 * i);
  }
  return result;
}
static uint64_t packed_add_saturate(uint64_t left, uint64_t right) {
  uint64_t result = 0;
  for (unsigned i = 0; i < 4; i++) {
    int32_t sum =
        (int16_t)(left >> (16 * i)) + (int32_t)(int16_t)(right >> (16 * i));
    if (sum > 32767)
      sum = 32767;
    else if (sum < -32768)
      sum = -32768;
    result |= (uint64_t)(uint16_t)sum << (16 * i);
  }
  return result;
}
void bb_qmix_integer_backend_name_c(uint32_t *input) {
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
  eax = 0x10020bf4u;
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
uint32_t __attribute__((naked)) bb_qmix_integer_backend_name_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_backend_name_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_ramp_constructor_c(uint32_t *input) {
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
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_integer_ramp);
  esi = ecx;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(0x1000c7a0u, 8, 0x1000c792u); /* 1000c78d */
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
uint32_t __attribute__((naked)) bb_qmix_integer_ramp_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_ramp_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_ramp_set_c(uint32_t *input) {
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
  fp[depth] = F32(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a488u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  esi = eax;
  ecx = BB_U32(esp + 0x10u);
  BB_U32(edi + 0xcu) = esi;
  BB_U32(edi + 0x4u) = ecx;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000c7d2;
  BB_U32(edi + 0x10u) = 0x0u;
  BB_U32(edi + 0x8u) = esi;
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
L_1000c7d2:;
  eax = esi;
  eax = (eax - BB_U32(edi + 0x8u));
  edx = (uint32_t)((int32_t)eax >> 31);
  b = ecx;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  BB_U32(edi + 0x10u) = eax;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000c7eb;
  BB_U32(edi + 0x4u) = 0x0u;
  BB_U32(edi + 0x8u) = esi;
L_1000c7eb:;
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
bb_qmix_integer_ramp_set_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_ramp_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_integer_ramp_mono8_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(ecx + 0x4u);
  edi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000c82a;
  eax = BB_U32(edi + 0x8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000c8a7;
  ebx = BB_U32(esp + 0x20u);
  esi = BB_U32(esp + 0x1cu);
  ebp = BB_U32(esp + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000cb00u, 20, 0x1000c823u); /* 1000c81e */
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
L_1000c82a:;
  esi = BB_U32(esp + 0x1cu);
  ebx = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000c861;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(edi + 0x8u);
  ebp = BB_U32(esp + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000cb00u, 20, 0x1000c84eu); /* 1000c849 */
  eax = BB_U32(edi + 0x10u);
  ebp = args[sp++];
  esp += 4u;
  a = eax;
  b = esi;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  BB_U32(edi + 0x8u) = (BB_U32(edi + 0x8u) + eax);
  a = BB_U32(edi + 0x4u);
  b = esi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(edi + 0x4u) = result;
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
L_1000c861:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(edi + 0x8u);
  eax = BB_U32(edi + 0x10u);
  ebp = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000cb00u, 20, 0x1000c878u); /* 1000c873 */
  ecx = BB_U32(edi + 0x4u);
  eax = BB_U32(edi + 0xcu);
  ebx = (ebx + ecx);
  esi = (esi - ecx);
  args[--sp] = ebx;
  esp -= 4u;
  a = ecx;
  b = ebp;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  SAMPLE_CALL(0x1000cb00u, 20, 0x1000c893u); /* 1000c88e */
  BB_U32(edi + 0x10u) = 0x0u;
  BB_U32(edi + 0x4u) = 0x0u;
  ecx = BB_U32(edi + 0xcu);
  BB_U32(edi + 0x8u) = ecx;
L_1000c8a7:;
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
bb_qmix_integer_ramp_mono8_entry(uint32_t arg0 __attribute__((unused)),
                                 uint32_t arg1 __attribute__((unused)),
                                 uint32_t arg2 __attribute__((unused)),
                                 uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_ramp_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_integer_ramp_mono16_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(ecx + 0x4u);
  edi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000c8ea;
  eax = BB_U32(edi + 0x8u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000c969;
  ebx = BB_U32(esp + 0x20u);
  esi = BB_U32(esp + 0x1cu);
  ebp = BB_U32(esp + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000cbb0u, 20, 0x1000c8e3u); /* 1000c8de */
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
L_1000c8ea:;
  esi = BB_U32(esp + 0x1cu);
  ebx = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000c921;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(edi + 0x10u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(edi + 0x8u);
  ebp = BB_U32(esp + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000cbb0u, 20, 0x1000c90eu); /* 1000c909 */
  eax = BB_U32(edi + 0x10u);
  ebp = args[sp++];
  esp += 4u;
  a = eax;
  b = esi;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  BB_U32(edi + 0x8u) = (BB_U32(edi + 0x8u) + eax);
  a = BB_U32(edi + 0x4u);
  b = esi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(edi + 0x4u) = result;
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
L_1000c921:;
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(edi + 0x8u);
  eax = BB_U32(edi + 0x10u);
  ebp = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000cbb0u, 20, 0x1000c938u); /* 1000c933 */
  ecx = BB_U32(edi + 0x4u);
  eax = BB_U32(edi + 0xcu);
  ebx = (ebx + ecx);
  a = esi;
  b = ecx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  args[--sp] = ebx;
  esp -= 4u;
  ecx = (ebp + ecx * 2u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  SAMPLE_CALL(0x1000cbb0u, 20, 0x1000c955u); /* 1000c950 */
  BB_U32(edi + 0x10u) = 0x0u;
  BB_U32(edi + 0x4u) = 0x0u;
  ecx = BB_U32(edi + 0xcu);
  BB_U32(edi + 0x8u) = ecx;
L_1000c969:;
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
bb_qmix_integer_ramp_mono16_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused)),
                                  uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_ramp_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_integer_accumulator_delete_c(uint32_t *input) {
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
  esi = ecx;
  SAMPLE_CALL(0x1000ca10u, 0, 0x1000c9f8u); /* 1000c9f3 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000ca08;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10001070u, 0, 0x1000ca05u); /* 1000ca00 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000ca08:;
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
bb_qmix_integer_accumulator_delete_entry(uint32_t arg0
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_delete_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_accumulator_feed_c(uint32_t *input) {
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
  BB_U32(ecx + 0x8u) = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 0, 0x1000cafcu); /* 1000caf9 indirect */
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
bb_qmix_integer_accumulator_feed_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_feed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_accumulator_mono8_c(uint32_t *input) {
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
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(esi + 0x8u);
  edi = (edi + BB_U32(esp + 0x28u));
  eax = BB_U32(esi + 0xcu);
  edi = (edi << 2);
  eax = (eax - BB_U32(esp + 0x28u));
  edi = (edi + BB_U32(esi + 0x4u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esp + 0x24u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x24u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000cb2f;
  eax = (esp + 0x10u);
L_1000cb2f:;
  ebx = BB_U32(eax);
  ebp = BB_U32(esp + 0x24u);
  ebp = (ebp - ebx);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cb54;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000cec0u, 0, 0x1000cb51u); /* 1000cb4c */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000cb54:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cb7d;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = ebx;
  a = eax;
  b = BB_U32(esp + 0x28u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  eax = (eax + BB_U32(esp + 0x24u));
  ecx = (edi + ebx * 4u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esp + 0x24u);
  a = eax;
  b = ebx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x1000ce20u, 0, 0x1000cb7au); /* 1000cb75 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000cb7d:;
  eax = BB_U32(esp + 0x24u);
  eax = (eax + BB_U32(esp + 0x28u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esi + 0xcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esi + 0xcu);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000cb95;
  eax = (esp + 0x10u);
L_1000cb95:;
  eax = BB_U32(eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0xcu) = eax;
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
bb_qmix_integer_accumulator_mono8_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused)),
                                        uint32_t arg2 __attribute__((unused)),
                                        uint32_t arg3 __attribute__((unused)),
                                        uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_mono8_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_integer_accumulator_mono16_c(uint32_t *input) {
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
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(esi + 0x8u);
  edi = (edi + BB_U32(esp + 0x28u));
  eax = BB_U32(esi + 0xcu);
  edi = (edi << 2);
  eax = (eax - BB_U32(esp + 0x28u));
  edi = (edi + BB_U32(esi + 0x4u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esp + 0x24u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x24u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000cbdf;
  eax = (esp + 0x10u);
L_1000cbdf:;
  ebx = BB_U32(eax);
  ebp = BB_U32(esp + 0x24u);
  ebp = (ebp - ebx);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cc04;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000cff0u, 0, 0x1000cc01u); /* 1000cbfc */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000cc04:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cc2e;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  eax = ebx;
  edx = (ecx + ebx * 2u);
  a = eax;
  b = BB_U32(esp + 0x28u);
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  a = eax;
  b = BB_U32(esp + 0x24u);
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = (edi + ebx * 4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x1000cf60u, 0, 0x1000cc2bu); /* 1000cc26 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000cc2e:;
  eax = BB_U32(esp + 0x24u);
  eax = (eax + BB_U32(esp + 0x28u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esi + 0xcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esi + 0xcu);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000cc46;
  eax = (esp + 0x10u);
L_1000cc46:;
  eax = BB_U32(eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0xcu) = eax;
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
bb_qmix_integer_accumulator_mono16_entry(uint32_t arg0 __attribute__((unused)),
                                         uint32_t arg1 __attribute__((unused)),
                                         uint32_t arg2 __attribute__((unused)),
                                         uint32_t arg3 __attribute__((unused)),
                                         uint32_t arg4
                                         __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_mono16_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_integer_accumulator_zero_extend_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  edx = BB_U32(ecx + 0xcu);
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  esi = BB_U32(esp + 0x10u);
  a = esi;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cc90;
  eax = esi;
  edi = BB_U32(ebx + 0x8u);
  eax = (eax - edx);
  edi = (edi + edx);
  edi = (edi << 2);
  ecx = (eax * 4u);
  ecx = (ecx >> 2);
  edi = (edi + BB_U32(ebx + 0x4u));
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
  BB_U32(ebx + 0xcu) = esi;
L_1000cc90:;
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
bb_qmix_integer_accumulator_zero_extend_entry(uint32_t arg0
                                              __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_zero_extend_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_accumulator_merge_c(uint32_t *input) {
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
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x8u);
  args[--sp] = edi;
  esp -= 4u;
  eax = (eax + BB_U32(ecx + 0xcu));
  args[--sp] = ebp;
  esp -= 4u;
  ebx = ecx;
  esi = BB_U32(esp + 0x24u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esi + 0x8u);
  eax = (eax + BB_U32(esi + 0xcu));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = BB_U32(esp + 0x14u);
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x10u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000cccf;
  eax = (esp + 0x14u);
L_1000cccf:;
  ebp = BB_U32(eax);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cce6;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(esi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebx + 0x4u);
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x1000d0b0u, 0, 0x1000cce3u); /* 1000ccde */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_1000cce6:;
  BB_U32(esp + 0x1cu) = 0x0u;
  args[sp + 7u] = BB_U32(esp + 28u);
  eax = BB_U32(esp + 0x10u);
  a = eax;
  b = BB_U32(esp + 0x14u);
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(esp + 0x18u) = eax;
  args[sp + 6u] = BB_U32(esp + 24u);
  eax = (esp + 0x18u);
  if (!(flags & 0x80u))
    goto L_1000cd04;
  eax = (esp + 0x1cu);
L_1000cd04:;
  eax = BB_U32(eax);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cd2f;
  edx = (ebp * 4u);
  ecx = (eax * 4u);
  ecx = (ecx >> 2);
  esi = BB_U32(esi + 0x4u);
  edi = BB_U32(ebx + 0x4u);
  esi = (esi + edx);
  edi = (edi + edx);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  eax = (eax - BB_U32(ebx + 0x8u));
  eax = (eax + ebp);
  BB_U32(ebx + 0xcu) = eax;
L_1000cd2f:;
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
bb_qmix_integer_accumulator_merge_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_merge_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_accumulator_stereo_output_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ebx = BB_U32(esp + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  SAMPLE_CALL(0x1000cc60u, 4, 0x1000cd4fu); /* 1000cd4a */
  edi = BB_U32(esp + 0x14u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = edi;
  SAMPLE_CALL(0x1000cc60u, 4, 0x1000cd5bu); /* 1000cd56 */
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(esi + 0x4u);
  esi = BB_U32(esp + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x1000d100u, 0, 0x1000cd6au); /* 1000cd65 */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  ecx = BB_U32(edi + 0x4u);
  a = esi;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  esi = result;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x1000d100u, 0, 0x1000cd7bu); /* 1000cd76 */
  a = esp;
  b = 0xcu;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
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
uint32_t __attribute__((naked)) bb_qmix_integer_accumulator_stereo_output_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused)),
    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_stereo_output_c) ";leal "
                                                   "4(%esp),%esp;popal;popfl;"
                                                   "leal 512(%esp),%esp;ret "
                                                   "$12");
}
#endif
void bb_qmix_integer_accumulator_flush_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x8u);
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000cdca;
  edx = BB_U32(esp + 0x10u);
  a = BB_U32(ebx + 0xcu);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000cdb9;
  edi = BB_U32(ebx + 0x4u);
  ecx = (eax * 4u);
  a = ecx;
  result = (a >> 2) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 1);
  ecx = result;
  esi = (edi + edx * 4u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  goto L_1000cdca;
L_1000cdb9:;
  ecx = (eax * 4u);
  edi = BB_U32(ebx + 0x4u);
  ecx = (ecx >> 2);
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
L_1000cdca:;
  BB_U32(ebx + 0xcu) = 0x0u;
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
bb_qmix_integer_accumulator_flush_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_flush_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_integer_accumulator_silent_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x8u);
  edx = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000d070u, 0, 0x1000cdedu); /* 1000cde8 */
  a = esp;
  b = 0x8u;
  esp += b;
  sp += b / 4u;
  a = eax;
  b = 0x64u;
  flags = sub_flags(flags, a, b, 32);
  eax = 0x1u;
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000cdfc;
  a = eax;
  b = eax;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  eax = result;
L_1000cdfc:;
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
uint32_t __attribute__((naked)) bb_qmix_integer_accumulator_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_silent_c) ";leal "
                                            "4(%esp),%esp;popal;popfl;leal "
                                            "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_accumulator_clear_c(uint32_t *input) {
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
  eax = ecx;
  ecx = BB_U32(eax + 0x8u);
  edi = BB_U32(eax + 0x4u);
  ecx = (ecx << 2);
  eax = (eax ^ eax);
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
uint32_t __attribute__((naked)) bb_qmix_integer_accumulator_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_clear_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_mix_mono8_store_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = BB_U32(esp + 0x18u);
  BB_U32(esp + 0x1cu) = (BB_U32(esp + 0x1cu) << 2);
  args[sp + 7u] = BB_U32(esp + 28u);
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000ce8c;
L_1000ce3d:;
  eax = BYTE(esi);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = edx;
  eax = BYTE(esi + 0x1u);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0x4u) = edx;
  eax = BYTE(esi + 0x2u);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0x8u) = edx;
  eax = BYTE(esi + 0x3u);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0xcu) = edx;
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x4u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000ce3d;
L_1000ce8c:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000ceb6;
  BB_U32(esp + 0x1cu) = ((uint32_t)((int32_t)(BB_U32(esp + 0x1cu)) >> 2));
  args[sp + 7u] = BB_U32(esp + 28u);
L_1000ce9a:;
  eax = BYTE(esi);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = edx;
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x1u);
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
    goto L_1000ce9a;
L_1000ceb6:;
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
bb_qmix_integer_mix_mono8_store_entry(uint32_t arg0 __attribute__((unused)),
                                      uint32_t arg1 __attribute__((unused)),
                                      uint32_t arg2 __attribute__((unused)),
                                      uint32_t arg3 __attribute__((unused)),
                                      uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_mix_mono8_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_mix_mono8_add_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = BB_U32(esp + 0x18u);
  BB_U32(esp + 0x1cu) = (BB_U32(esp + 0x1cu) << 2);
  args[sp + 7u] = BB_U32(esp + 28u);
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000cf2c;
L_1000cedd:;
  eax = BYTE(esi);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = (BB_U32(edi) + edx);
  eax = BYTE(esi + 0x1u);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0x4u) = (BB_U32(edi + 0x4u) + edx);
  eax = BYTE(esi + 0x2u);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0x8u) = (BB_U32(edi + 0x8u) + edx);
  eax = BYTE(esi + 0x3u);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0xcu) = (BB_U32(edi + 0xcu) + edx);
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x4u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000cedd;
L_1000cf2c:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000cf56;
  BB_U32(esp + 0x1cu) = ((uint32_t)((int32_t)(BB_U32(esp + 0x1cu)) >> 2));
  args[sp + 7u] = BB_U32(esp + 28u);
L_1000cf3a:;
  eax = BYTE(esi);
  eax = (eax - 0x80u);
  eax = (eax << 17);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = (BB_U32(edi) + edx);
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x1u);
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
    goto L_1000cf3a;
L_1000cf56:;
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
bb_qmix_integer_mix_mono8_add_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused)),
                                    uint32_t arg3 __attribute__((unused)),
                                    uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_mix_mono8_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_mix_mono16_store_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = BB_U32(esp + 0x18u);
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000cf8f;
L_1000cf78:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = edx;
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x2u);
  edi = (edi + 0x4u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000cf78;
L_1000cf8f:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000cfe7;
  BB_U32(esp + 0x1cu) = (BB_U32(esp + 0x1cu) << 2);
  args[sp + 7u] = BB_U32(esp + 28u);
  ecx = (ecx << 3);
  esi = (esi + ecx);
  edi = (edi + ecx * 2u);
  ecx = (0u - ecx);
L_1000cfa8:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + ecx));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + ecx * 2u) = edx;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + ecx + 0x2u));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + ecx * 2u + 0x4u) = edx;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + ecx + 0x4u));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + ecx * 2u + 0x8u) = edx;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + ecx + 0x6u));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + ecx * 2u + 0xcu) = edx;
  ebx = (ebx + BB_U32(esp + 0x1cu));
  a = ecx;
  b = 0x8u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000cfa8;
L_1000cfe7:;
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
bb_qmix_integer_mix_mono16_store_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused)),
                                       uint32_t arg2 __attribute__((unused)),
                                       uint32_t arg3 __attribute__((unused)),
                                       uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_mix_mono16_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_mix_mono16_add_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = BB_U32(esp + 0x18u);
  BB_U32(esp + 0x1cu) = (BB_U32(esp + 0x1cu) << 2);
  args[sp + 7u] = BB_U32(esp + 28u);
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000d047;
L_1000d00d:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = (BB_U32(edi) + edx);
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + 0x2u));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0x4u) = (BB_U32(edi + 0x4u) + edx);
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + 0x4u));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0x8u) = (BB_U32(edi + 0x8u) + edx);
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi + 0x6u));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi + 0xcu) = (BB_U32(edi + 0xcu) + edx);
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x8u);
  edi = (edi + 0x10u);
  if (--ecx)
    goto L_1000d00d;
L_1000d047:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000d06b;
  BB_U32(esp + 0x1cu) = ((uint32_t)((int32_t)(BB_U32(esp + 0x1cu)) >> 2));
  args[sp + 7u] = BB_U32(esp + 28u);
L_1000d055:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi));
  eax = (eax << 9);
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  BB_U32(edi) = (BB_U32(edi) + edx);
  ebx = (ebx + BB_U32(esp + 0x1cu));
  esi = (esi + 0x2u);
  a = edi;
  b = 0x4u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  if (--ecx)
    goto L_1000d055;
L_1000d06b:;
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
bb_qmix_integer_mix_mono16_add_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused)),
                                     uint32_t arg3 __attribute__((unused)),
                                     uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_mix_mono16_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_accumulator_magnitude_c(uint32_t *input) {
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
  BB_U32(esp) = 0x0u;
  args[sp + 0u] = BB_U32(esp + 0u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x14u);
  ebx = (ebx ^ ebx);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000d097;
L_1000d08a:;
  eax = BB_U32(esi + ecx * 4u);
  edx = (uint32_t)((int32_t)eax >> 31);
  eax = (eax ^ edx);
  eax = (eax - edx);
  ebx = (ebx + eax);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d08a;
L_1000d097:;
  BB_U32(esp + 0x8u) = ebx;
  args[sp + 2u] = BB_U32(esp + 8u);
  eax = BB_U32(esp + 0x8u);
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
uint32_t __attribute__((naked)) bb_qmix_integer_accumulator_magnitude_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_accumulator_magnitude_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_mix_add_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esp + 0x14u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 2)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 2, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000d0e2;
L_1000d0c3:;
  eax = BB_U32(esi);
  BB_U32(edi) = (BB_U32(edi) + eax);
  eax = BB_U32(esi + 0x4u);
  BB_U32(edi + 0x4u) = (BB_U32(edi + 0x4u) + eax);
  eax = BB_U32(esi + 0x8u);
  BB_U32(edi + 0x8u) = (BB_U32(edi + 0x8u) + eax);
  eax = BB_U32(esi + 0xcu);
  BB_U32(edi + 0xcu) = (BB_U32(edi + 0xcu) + eax);
  esi = (esi + 0x10u);
  edi = (edi + 0x10u);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000d0c3;
L_1000d0e2:;
  ecx = BB_U32(esp + 0x14u);
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000d0f8;
L_1000d0eb:;
  eax = BB_U32(esi);
  BB_U32(edi) = (BB_U32(edi) + eax);
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
    goto L_1000d0eb;
L_1000d0f8:;
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
bb_qmix_integer_mix_add_entry(uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_mix_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_integer_clip_stereo16_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esp + 0x14u);
  ecx = (ecx - 1u);
L_1000d10f:;
  eax = BB_U32(esi + ecx * 4u);
  eax = ((uint32_t)((int32_t)(eax) >> 8));
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000d12c;
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000d137;
  WORD(edi + ecx * 4u) = ((uint16_t)(eax));
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d10f;
  goto L_1000d140;
L_1000d12c:;
  WORD(edi + ecx * 4u) = 0x7fffu;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d10f;
  goto L_1000d140;
L_1000d137:;
  WORD(edi + ecx * 4u) = 0x8000u;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d10f;
L_1000d140:;
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
bb_qmix_integer_clip_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_integer_clip_stereo16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_backend_name_c(uint32_t *input) {
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
  eax = 0x10020bf8u;
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
uint32_t __attribute__((naked)) bb_qmix_short_backend_name_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_backend_name_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                    "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_ramp_constructor_c(uint32_t *input) {
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
  args[--sp] = 0x0u;
  esp -= 4u;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_short_ramp);
  esi = ecx;
  args[--sp] = 0x0u;
  esp -= 4u;
  SAMPLE_CALL(0x1000d1a0u, 8, 0x1000d192u); /* 1000d18d */
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
uint32_t __attribute__((naked)) bb_qmix_short_ramp_constructor_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_ramp_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_ramp_set_c(uint32_t *input) {
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
  fp[depth] = F32(esp + 0x4u);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(0x1001a4d8u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  WORD(ebx + 0xau) = ((uint16_t)(eax));
  esi = BB_U32(esp + 0x14u);
  edi = (edi & 0xffff0000u) | (((uint32_t)(((uint16_t)(eax))) & 0xffffu) << 0);
  BB_U32(ebx + 0x4u) = esi;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000d1d6;
  WORD(ebx + 0xcu) = 0x0u;
  WORD(ebx + 0x8u) = ((uint16_t)(edi));
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
L_1000d1d6:;
  eax = (uint32_t)(int32_t)(int16_t)(((uint16_t)(edi)));
  ecx = (uint32_t)(int32_t)(int16_t)(WORD(ebx + 0x8u));
  eax = (eax - ecx);
  edx = (uint32_t)((int32_t)eax >> 31);
  b = esi;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  WORD(ebx + 0xcu) = ((uint16_t)(eax));
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (!(flags & 0x40u))
    goto L_1000d1f6;
  BB_U32(ebx + 0x4u) = 0x0u;
  WORD(ebx + 0x8u) = ((uint16_t)(edi));
L_1000d1f6:;
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
bb_qmix_short_ramp_set_entry(uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_ramp_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "512(%esp),%esp;ret $8");
}
#endif
void bb_qmix_short_ramp_mono8_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(ecx + 0x4u);
  edi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000d23c;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x8u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (flags & 0x40u)
    goto L_1000d2c2;
  ebx = BB_U32(esp + 0x20u);
  esi = BB_U32(esp + 0x1cu);
  ebp = BB_U32(esp + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000d530u, 20, 0x1000d235u); /* 1000d230 */
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
L_1000d23c:;
  esi = BB_U32(esp + 0x1cu);
  ebx = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d278;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xcu)) & 0xffffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x8u)) & 0xffffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ebp = BB_U32(esp + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000d530u, 20, 0x1000d262u); /* 1000d25d */
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xcu)) & 0xffffu) << 0);
  ebp = args[sp++];
  esp += 4u;
  a = ((uint16_t)(eax));
  b = ((uint16_t)(esi));
  wide = (uint64_t)((int64_t)(int16_t)a * (int64_t)(int16_t)b);
  result = (uint16_t)wide;
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  WORD(edi + 0x8u) = (WORD(edi + 0x8u) + ((uint16_t)(eax)));
  a = BB_U32(edi + 0x4u);
  b = esi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(edi + 0x4u) = result;
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
L_1000d278:;
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x8u)) & 0xffffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xcu)) & 0xffffu) << 0);
  ebp = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000d530u, 20, 0x1000d291u); /* 1000d28c */
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xau)) & 0xffffu) << 0);
  ecx = BB_U32(edi + 0x4u);
  ebx = (ebx + ecx);
  esi = (esi - ecx);
  args[--sp] = ebx;
  esp -= 4u;
  a = ecx;
  b = ebp;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  SAMPLE_CALL(0x1000d530u, 20, 0x1000d2adu); /* 1000d2a8 */
  BB_U32(edi + 0x4u) = 0x0u;
  WORD(edi + 0xcu) = 0x0u;
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xau)) & 0xffffu) << 0);
  WORD(edi + 0x8u) = ((uint16_t)(ecx));
L_1000d2c2:;
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
bb_qmix_short_ramp_mono8_entry(uint32_t arg0 __attribute__((unused)),
                               uint32_t arg1 __attribute__((unused)),
                               uint32_t arg2 __attribute__((unused)),
                               uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_ramp_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_short_ramp_mono16_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(ecx + 0x4u);
  edi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_1000d30c;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x8u)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = ((uint16_t)(eax));
  flags = logic_flags(flags, a & b, 16);
  if (flags & 0x40u)
    goto L_1000d394;
  ebx = BB_U32(esp + 0x20u);
  esi = BB_U32(esp + 0x1cu);
  ebp = BB_U32(esp + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x18u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000d5e0u, 20, 0x1000d305u); /* 1000d300 */
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
L_1000d30c:;
  esi = BB_U32(esp + 0x1cu);
  ebx = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  a = esi;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d348;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xcu)) & 0xffffu) << 0);
  args[--sp] = esi;
  esp -= 4u;
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x8u)) & 0xffffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  ebp = BB_U32(esp + 0x24u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000d5e0u, 20, 0x1000d332u); /* 1000d32d */
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xcu)) & 0xffffu) << 0);
  ebp = args[sp++];
  esp += 4u;
  a = ((uint16_t)(eax));
  b = ((uint16_t)(esi));
  wide = (uint64_t)((int64_t)(int16_t)a * (int64_t)(int16_t)b);
  result = (uint16_t)wide;
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  WORD(edi + 0x8u) = (WORD(edi + 0x8u) + ((uint16_t)(eax)));
  a = BB_U32(edi + 0x4u);
  b = esi;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  BB_U32(edi + 0x4u) = result;
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
L_1000d348:;
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(edi + 0x8u)) & 0xffffu) << 0);
  args[--sp] = eax;
  esp -= 4u;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xcu)) & 0xffffu) << 0);
  ebp = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x24u);
  args[--sp] = ebp;
  esp -= 4u;
  SAMPLE_CALL(0x1000d5e0u, 20, 0x1000d361u); /* 1000d35c */
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xau)) & 0xffffu) << 0);
  ecx = BB_U32(edi + 0x4u);
  ebx = (ebx + ecx);
  a = esi;
  b = ecx;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  esi = result;
  args[--sp] = ebx;
  esp -= 4u;
  ecx = (ebp + ecx * 2u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = 0x0u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x28u);
  SAMPLE_CALL(0x1000d5e0u, 20, 0x1000d37fu); /* 1000d37a */
  BB_U32(edi + 0x4u) = 0x0u;
  WORD(edi + 0xcu) = 0x0u;
  ecx = (ecx & 0xffff0000u) | (((uint32_t)(WORD(edi + 0xau)) & 0xffffu) << 0);
  WORD(edi + 0x8u) = ((uint16_t)(ecx));
L_1000d394:;
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
bb_qmix_short_ramp_mono16_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused)),
                                uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_ramp_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $16");
}
#endif
void bb_qmix_short_accumulator_delete_c(uint32_t *input) {
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
  esi = ecx;
  SAMPLE_CALL(0x1000d440u, 0, 0x1000d428u); /* 1000d423 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_1000d438;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10001070u, 0, 0x1000d435u); /* 1000d430 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_1000d438:;
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
bb_qmix_short_accumulator_delete_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_delete_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_accumulator_feed_c(uint32_t *input) {
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
  BB_U32(ecx + 0x8u) = eax;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x1cu), 0, 0x1000d4bcu); /* 1000d4b9 indirect */
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
bb_qmix_short_accumulator_feed_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_feed_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_accumulator_mono8_c(uint32_t *input) {
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
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(esi + 0x8u);
  edi = (edi + BB_U32(esp + 0x28u));
  eax = BB_U32(esi + 0xcu);
  edi = (edi + edi);
  eax = (eax - BB_U32(esp + 0x28u));
  edi = (edi + BB_U32(esi + 0x4u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = BB_U32(esp + 0x24u);
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x24u);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000d55e;
  eax = (esp + 0x10u);
L_1000d55e:;
  ebx = BB_U32(eax);
  ebp = BB_U32(esp + 0x24u);
  ebp = (ebp - ebx);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d583;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000d950u, 0, 0x1000d580u); /* 1000d57b */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000d583:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d5ae;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = ebx;
  a = ((uint16_t)(eax));
  b = WORD(esp + 0x28u);
  wide = (uint64_t)((int64_t)(int16_t)a * (int64_t)(int16_t)b);
  result = (uint16_t)wide;
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  eax = (eax & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(eax)) + WORD(esp + 0x24u))) & 0xffffu) << 0);
  ecx = (edi + ebx * 2u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esp + 0x24u);
  a = eax;
  b = ebx;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x1000d860u, 0, 0x1000d5abu); /* 1000d5a6 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000d5ae:;
  eax = BB_U32(esp + 0x24u);
  eax = (eax + BB_U32(esp + 0x28u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esi + 0xcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esi + 0xcu);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d5c6;
  eax = (esp + 0x10u);
L_1000d5c6:;
  eax = BB_U32(eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0xcu) = eax;
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
bb_qmix_short_accumulator_mono8_entry(uint32_t arg0 __attribute__((unused)),
                                      uint32_t arg1 __attribute__((unused)),
                                      uint32_t arg2 __attribute__((unused)),
                                      uint32_t arg3 __attribute__((unused)),
                                      uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_mono8_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_short_accumulator_mono16_c(uint32_t *input) {
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
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = ebp;
  esp -= 4u;
  edi = BB_U32(esi + 0x8u);
  edi = (edi + BB_U32(esp + 0x28u));
  eax = BB_U32(esi + 0xcu);
  edi = (edi + edi);
  eax = (eax - BB_U32(esp + 0x28u));
  edi = (edi + BB_U32(esi + 0x4u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esp + 0x24u);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x24u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000d60e;
  eax = (esp + 0x10u);
L_1000d60e:;
  ebx = BB_U32(eax);
  ebp = BB_U32(esp + 0x24u);
  ebp = (ebp - ebx);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d633;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebx;
  esp -= 4u;
  ecx = BB_U32(esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(esp + 0x20u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000db50u, 0, 0x1000d630u); /* 1000d62b */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000d633:;
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d65b;
  eax = BB_U32(esp + 0x20u);
  args[--sp] = ebp;
  esp -= 4u;
  ecx = BB_U32(esp + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  a = ((uint16_t)(eax));
  b = ((uint16_t)(ebx));
  wide = (uint64_t)((int64_t)(int16_t)a * (int64_t)(int16_t)b);
  result = (uint16_t)wide;
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = WORD(esp + 0x24u);
  result = (a + b) & 0xffffu;
  flags = add_flags(flags, a, b, 16);
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  edx = (ecx + ebx * 2u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  eax = (edi + ebx * 2u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x1000da80u, 0, 0x1000d658u); /* 1000d653 */
  a = esp;
  b = 0x14u;
  esp += b;
  sp += b / 4u;
L_1000d65b:;
  eax = BB_U32(esp + 0x24u);
  eax = (eax + BB_U32(esp + 0x28u));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = BB_U32(esi + 0xcu);
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  eax = (esi + 0xcu);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000d673;
  eax = (esp + 0x10u);
L_1000d673:;
  eax = BB_U32(eax);
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  BB_U32(esi + 0xcu) = eax;
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
bb_qmix_short_accumulator_mono16_entry(uint32_t arg0 __attribute__((unused)),
                                       uint32_t arg1 __attribute__((unused)),
                                       uint32_t arg2 __attribute__((unused)),
                                       uint32_t arg3 __attribute__((unused)),
                                       uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_mono16_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $20");
}
#endif
void bb_qmix_short_accumulator_zero_extend_c(uint32_t *input) {
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
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx + 0xcu);
  esi = ecx;
  a = edx;
  b = eax;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d6c3;
  ebx = edx;
  edi = BB_U32(esi + 0x8u);
  ebx = (ebx - eax);
  edi = (edi + eax);
  ebx = (ebx + ebx);
  edi = (edi + edi);
  edi = (edi + BB_U32(esi + 0x4u));
  eax = (eax ^ eax);
  ecx = ebx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = ebx;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
  BB_U32(esi + 0xcu) = edx;
L_1000d6c3:;
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
bb_qmix_short_accumulator_zero_extend_entry(uint32_t arg0
                                            __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_zero_extend_c) ";leal "
                                               "4(%esp),%esp;popal;popfl;leal "
                                               "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_accumulator_merge_c(uint32_t *input) {
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
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx + 0x8u);
  args[--sp] = edi;
  esp -= 4u;
  eax = (eax + BB_U32(ecx + 0xcu));
  args[--sp] = ebp;
  esp -= 4u;
  ebx = ecx;
  esi = BB_U32(esp + 0x24u);
  BB_U32(esp + 0x14u) = eax;
  args[sp + 5u] = BB_U32(esp + 20u);
  eax = BB_U32(esi + 0x8u);
  eax = (eax + BB_U32(esi + 0xcu));
  BB_U32(esp + 0x10u) = eax;
  args[sp + 4u] = BB_U32(esp + 16u);
  a = eax;
  b = BB_U32(esp + 0x14u);
  flags = sub_flags(flags, a, b, 32);
  eax = (esp + 0x10u);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_1000d6ff;
  eax = (esp + 0x14u);
L_1000d6ff:;
  ebp = BB_U32(eax);
  a = ebp;
  b = ebp;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d716;
  args[--sp] = ebp;
  esp -= 4u;
  eax = BB_U32(esi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebx + 0x4u);
  args[--sp] = ecx;
  esp -= 4u;
  SAMPLE_CALL(0x1000dc70u, 0, 0x1000d713u); /* 1000d70e */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
L_1000d716:;
  BB_U32(esp + 0x1cu) = 0x0u;
  args[sp + 7u] = BB_U32(esp + 28u);
  eax = BB_U32(esp + 0x10u);
  a = eax;
  b = BB_U32(esp + 0x14u);
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  BB_U32(esp + 0x18u) = eax;
  args[sp + 6u] = BB_U32(esp + 24u);
  eax = (esp + 0x18u);
  if (!(flags & 0x80u))
    goto L_1000d734;
  eax = (esp + 0x1cu);
L_1000d734:;
  eax = BB_U32(eax);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000d768;
  ecx = (ebp * 2u);
  edx = (eax * 2u);
  esi = BB_U32(esi + 0x4u);
  edi = BB_U32(ebx + 0x4u);
  edi = (edi + ecx);
  esi = (esi + ecx);
  ecx = edx;
  ecx = (ecx >> 2);
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
  ebp = (ebp - BB_U32(ebx + 0x8u));
  ebp = (ebp + eax);
  BB_U32(ebx + 0xcu) = ebp;
L_1000d768:;
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
bb_qmix_short_accumulator_merge_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_merge_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_accumulator_stereo_output_c(uint32_t *input) {
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  edi = BB_U32(esp + 0x18u);
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x1000d690u, 4, 0x1000d78fu); /* 1000d78a */
  esi = BB_U32(esp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  ecx = esi;
  SAMPLE_CALL(0x1000d690u, 4, 0x1000d79bu); /* 1000d796 */
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  ecx = BB_U32(ebx + 0x4u);
  edx = BB_U32(esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000de00u, 0, 0x1000d7aeu); /* 1000d7a9 */
  a = esp;
  b = 0x10u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
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
uint32_t __attribute__((naked)) bb_qmix_short_accumulator_stereo_output_entry(
    uint32_t arg0 __attribute__((unused)),
    uint32_t arg1 __attribute__((unused)),
    uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_stereo_output_c) ";leal "
                                                 "4(%esp),%esp;popal;popfl;"
                                                 "leal 512(%esp),%esp;ret $12");
}
#endif
void bb_qmix_short_accumulator_flush_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x8u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edx = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000d806;
  ecx = BB_U32(esp + 0xcu);
  a = BB_U32(edx + 0xcu);
  b = ecx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_1000d7ec;
  edi = BB_U32(edx + 0x4u);
  eax = (eax + eax);
  esi = (edi + ecx * 2u);
  ecx = eax;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = eax;
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
  goto L_1000d806;
L_1000d7ec:;
  esi = (eax * 2u);
  edi = BB_U32(edx + 0x4u);
  eax = (eax ^ eax);
  ecx = esi;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = esi;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
L_1000d806:;
  BB_U32(edx + 0xcu) = 0x0u;
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
bb_qmix_short_accumulator_flush_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_flush_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $4");
}
#endif
void bb_qmix_short_accumulator_silent_c(uint32_t *input) {
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
  eax = BB_U32(ecx + 0x8u);
  edx = BB_U32(ecx + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0xau;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  SAMPLE_CALL(0x1000dd70u, 0, 0x1000d82fu); /* 1000d82a */
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  eax = (eax + 1u);
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
uint32_t __attribute__((naked)) bb_qmix_short_accumulator_silent_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_silent_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                          "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_accumulator_clear_c(uint32_t *input) {
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
  edx = BB_U32(ecx + 0x8u);
  edx = (edx + edx);
  edi = BB_U32(ecx + 0x4u);
  eax = (eax ^ eax);
  ecx = edx;
  ecx = (ecx >> 2);
  while (ecx) {
    BB_U32(edi) = eax;
    edi += (flags & 0x400u) ? 0u - 4u : 4u;
    --ecx;
  }
  ecx = edx;
  a = ecx;
  b = 0x3u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  while (ecx) {
    BYTE(edi) = (uint8_t)eax;
    edi += (flags & 0x400u) ? 0u - 1u : 1u;
    --ecx;
  }
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
uint32_t __attribute__((naked)) bb_qmix_short_accumulator_clear_entry(void) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_clear_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                         "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_mix_mono8_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x18u));
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000d924;
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  mm4 = (uint32_t)(ebx);
  mm6 = (uint32_t)(edx);
  mm4 = packed_unpack(mm4, mm4, 16, 0);
  mm6 = packed_unpack(mm6, mm6, 16, 0);
  mm4 = packed_unpack(mm4, mm4, 32, 0);
  mm6 = packed_unpack(mm6, mm6, 32, 0);
  mm6 = packed_shift_words(mm6, 0x3u);
  mm5 = mm4;
  mm5 = packed_add_saturate(mm5, mm6);
  mm6 = packed_shift_words(mm6, 0x1u);
  eax = 0x1001f430u;
  mm7 = QWORD(eax);
  ecx = (ecx << 4);
  esi = (esi + ecx);
  edi = (edi + ecx * 2u);
  ecx = (0u - ecx);
  mm0 = mm0 ^ mm0;
L_1000d8b8:;
  mm1 = mm1 ^ mm1;
  mm2 = mm2 ^ mm2;
  mm0 = packed_unpack(mm0, BB_U32(esi + ecx), 8, 0);
  mm3 = mm3 ^ mm3;
  mm1 = packed_unpack(mm1, QWORD(esi + ecx), 8, 1);
  mm0 = mm0 ^ mm7;
  mm2 = packed_unpack(mm2, BB_U32(esi + ecx + 0x8u), 8, 0);
  mm1 = mm1 ^ mm7;
  mm3 = packed_unpack(mm3, QWORD(esi + ecx + 0x8u), 8, 1);
  mm0 = packed_multiply_high(mm0, mm4);
  mm1 = packed_multiply_high(mm1, mm4);
  mm2 = mm2 ^ mm7;
  mm2 = packed_multiply_high(mm2, mm5);
  mm3 = mm3 ^ mm7;
  mm3 = packed_multiply_high(mm3, mm5);
  mm0 = packed_shift_words(mm0, 0x1u);
  QWORD(edi + ecx * 2u) = mm0;
  mm1 = packed_shift_words(mm1, 0x1u);
  QWORD(edi + ecx * 2u + 0x8u) = mm1;
  mm2 = packed_shift_words(mm2, 0x1u);
  QWORD(edi + ecx * 2u + 0x10u) = mm2;
  mm3 = packed_shift_words(mm3, 0x1u);
  QWORD(edi + ecx * 2u + 0x18u) = mm3;
  mm4 = packed_add_saturate(mm4, mm6);
  mm5 = packed_add_saturate(mm5, mm6);
  mm0 = mm0 ^ mm0;
  a = ecx;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000d8b8;
  ebx = (uint32_t)(mm4);
  ebx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(ebx)));
L_1000d924:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0xfu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000d94c;
L_1000d92d:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) ^ 0x80u)) & 0xffu) << 0);
  eax = (eax & 0xffff0000u) | (uint16_t)(int16_t)(int8_t)eax;
  eax = (uint32_t)(int32_t)(int16_t)eax;
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  eax = ((uint32_t)((int32_t)(eax) >> 7));
  WORD(edi) = ((uint16_t)(eax));
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  ebx = (ebx + eax);
  esi = (esi + 0x1u);
  a = edi;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000d92d;
L_1000d94c:;
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
bb_qmix_short_mix_mono8_store_entry(uint32_t arg0 __attribute__((unused)),
                                    uint32_t arg1 __attribute__((unused)),
                                    uint32_t arg2 __attribute__((unused)),
                                    uint32_t arg3 __attribute__((unused)),
                                    uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_mix_mono8_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                       "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_mix_mono8_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x18u));
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000da2d;
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  mm4 = (uint32_t)(ebx);
  mm6 = (uint32_t)(edx);
  mm4 = packed_unpack(mm4, mm4, 16, 0);
  mm6 = packed_unpack(mm6, mm6, 16, 0);
  mm4 = packed_unpack(mm4, mm4, 32, 0);
  mm6 = packed_unpack(mm6, mm6, 32, 0);
  mm6 = packed_shift_words(mm6, 0x3u);
  mm5 = mm4;
  mm5 = packed_add_saturate(mm5, mm6);
  mm6 = packed_shift_words(mm6, 0x1u);
  eax = 0x1001f430u;
  mm7 = QWORD(eax);
  ecx = (ecx << 4);
  esi = (esi + ecx);
  edi = (edi + ecx * 2u);
  ecx = (0u - ecx);
  mm0 = mm0 ^ mm0;
  mm1 = mm1 ^ mm1;
  mm2 = mm2 ^ mm2;
L_1000d9ae:;
  mm0 = packed_unpack(mm0, BB_U32(esi + ecx), 8, 0);
  mm3 = mm3 ^ mm3;
  mm1 = packed_unpack(mm1, QWORD(esi + ecx), 8, 1);
  mm0 = mm0 ^ mm7;
  mm2 = packed_unpack(mm2, BB_U32(esi + ecx + 0x8u), 8, 0);
  mm1 = mm1 ^ mm7;
  mm3 = packed_unpack(mm3, QWORD(esi + ecx + 0x8u), 8, 1);
  mm0 = packed_multiply_high(mm0, mm4);
  mm1 = packed_multiply_high(mm1, mm4);
  mm2 = mm2 ^ mm7;
  mm2 = packed_multiply_high(mm2, mm5);
  mm3 = mm3 ^ mm7;
  mm3 = packed_multiply_high(mm3, mm5);
  mm0 = packed_shift_words(mm0, 0x1u);
  mm0 = packed_add_saturate(mm0, QWORD(edi + ecx * 2u));
  mm1 = packed_shift_words(mm1, 0x1u);
  mm1 = packed_add_saturate(mm1, QWORD(edi + ecx * 2u + 0x8u));
  mm2 = packed_shift_words(mm2, 0x1u);
  mm2 = packed_add_saturate(mm2, QWORD(edi + ecx * 2u + 0x10u));
  mm3 = packed_shift_words(mm3, 0x1u);
  mm3 = packed_add_saturate(mm3, QWORD(edi + ecx * 2u + 0x18u));
  mm4 = packed_add_saturate(mm4, mm6);
  QWORD(edi + ecx * 2u) = mm0;
  mm5 = packed_add_saturate(mm5, mm6);
  QWORD(edi + ecx * 2u + 0x8u) = mm1;
  mm0 = mm0 ^ mm0;
  QWORD(edi + ecx * 2u + 0x10u) = mm2;
  mm1 = mm1 ^ mm1;
  QWORD(edi + ecx * 2u + 0x18u) = mm3;
  mm2 = mm2 ^ mm2;
  a = ecx;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000d9ae;
  ebx = (uint32_t)(mm4);
  ebx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(ebx)));
L_1000da2d:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0xfu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000da74;
L_1000da36:;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi)) & 0xffu) << 0);
  eax = (eax & 0xffffff00u) |
        (((uint32_t)((((uint8_t)(eax)) ^ 0x80u)) & 0xffu) << 0);
  eax = (eax & 0xffff0000u) | (uint16_t)(int16_t)(int8_t)eax;
  eax = (uint32_t)(int32_t)(int16_t)eax;
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  eax = ((uint32_t)((int32_t)(eax) >> 7));
  edx = (uint32_t)(int32_t)(int16_t)(WORD(edi));
  eax = (eax + edx);
  a = eax;
  b = 0x7fffu;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000da55;
  eax = 0x7fffu;
  goto L_1000da61;
L_1000da55:;
  a = eax;
  b = 0xffff8000u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000da61;
  eax = 0xffff8000u;
L_1000da61:;
  WORD(edi) = ((uint16_t)(eax));
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  ebx = (ebx + eax);
  esi = (esi + 0x1u);
  a = edi;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000da36;
L_1000da74:;
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
bb_qmix_short_mix_mono8_add_entry(uint32_t arg0 __attribute__((unused)),
                                  uint32_t arg1 __attribute__((unused)),
                                  uint32_t arg2 __attribute__((unused)),
                                  uint32_t arg3 __attribute__((unused)),
                                  uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_mix_mono8_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_mix_mono16_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x18u));
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000db25;
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  mm4 = (uint32_t)(ebx);
  mm6 = (uint32_t)(edx);
  mm4 = packed_unpack(mm4, mm4, 16, 0);
  mm6 = packed_unpack(mm6, mm6, 16, 0);
  mm4 = packed_unpack(mm4, mm4, 32, 0);
  mm6 = packed_unpack(mm6, mm6, 32, 0);
  mm6 = packed_shift_words(mm6, 0x3u);
  mm5 = mm4;
  mm5 = packed_add_saturate(mm5, mm6);
  mm6 = packed_shift_words(mm6, 0x1u);
  ecx = (ecx << 5);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
  mm0 = mm4;
L_1000dad0:;
  mm0 = packed_multiply_high(mm0, QWORD(esi + ecx));
  mm1 = mm4;
  mm1 = packed_multiply_high(mm1, QWORD(esi + ecx + 0x8u));
  mm2 = mm5;
  mm2 = packed_multiply_high(mm2, QWORD(esi + ecx + 0x10u));
  mm3 = mm5;
  mm3 = packed_multiply_high(mm3, QWORD(esi + ecx + 0x18u));
  mm0 = packed_shift_words(mm0, 0x1u);
  mm4 = packed_add_saturate(mm4, mm6);
  mm1 = packed_shift_words(mm1, 0x1u);
  QWORD(edi + ecx) = mm0;
  mm2 = packed_shift_words(mm2, 0x1u);
  QWORD(edi + ecx + 0x8u) = mm1;
  mm3 = packed_shift_words(mm3, 0x1u);
  QWORD(edi + ecx + 0x10u) = mm2;
  mm5 = packed_add_saturate(mm5, mm6);
  QWORD(edi + ecx + 0x18u) = mm3;
  mm0 = mm4;
  a = ecx;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dad0;
  ebx = (uint32_t)(mm4);
  ebx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(ebx)));
L_1000db25:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0xfu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000db49;
L_1000db2e:;
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esi));
  a = (uint32_t)eax;
  b = ebx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  eax = result;
  edx = (uint32_t)(wide >> 32);
  eax = ((uint32_t)((int32_t)(eax) >> 15));
  WORD(edi) = ((uint16_t)(eax));
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  ebx = (ebx + eax);
  esi = (esi + 0x2u);
  a = edi;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edi = result;
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000db2e;
L_1000db49:;
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
bb_qmix_short_mix_mono16_store_entry(uint32_t arg0 __attribute__((unused)),
                                     uint32_t arg1 __attribute__((unused)),
                                     uint32_t arg2 __attribute__((unused)),
                                     uint32_t arg3 __attribute__((unused)),
                                     uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_mix_mono16_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                        "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_mix_mono16_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 24u), 24u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  edi = BB_U32(esp + 0x10u);
  ebx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x18u));
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000dc08;
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x1cu));
  mm4 = (uint32_t)(ebx);
  mm6 = (uint32_t)(edx);
  mm4 = packed_unpack(mm4, mm4, 16, 0);
  mm6 = packed_unpack(mm6, mm6, 16, 0);
  mm4 = packed_unpack(mm4, mm4, 32, 0);
  mm6 = packed_unpack(mm6, mm6, 32, 0);
  mm6 = packed_shift_words(mm6, 0x3u);
  mm5 = mm4;
  mm5 = packed_add_saturate(mm5, mm6);
  mm6 = packed_shift_words(mm6, 0x1u);
  ecx = (ecx << 5);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
  mm0 = mm4;
L_1000dba0:;
  mm0 = packed_multiply_high(mm0, QWORD(esi + ecx));
  mm1 = mm4;
  mm1 = packed_multiply_high(mm1, QWORD(esi + ecx + 0x8u));
  mm2 = mm5;
  mm2 = packed_multiply_high(mm2, QWORD(esi + ecx + 0x10u));
  mm3 = mm5;
  mm3 = packed_multiply_high(mm3, QWORD(esi + ecx + 0x18u));
  mm0 = packed_shift_words(mm0, 0x1u);
  mm0 = packed_add_saturate(mm0, QWORD(edi + ecx));
  mm1 = packed_shift_words(mm1, 0x1u);
  mm1 = packed_add_saturate(mm1, QWORD(edi + ecx + 0x8u));
  mm2 = packed_shift_words(mm2, 0x1u);
  mm2 = packed_add_saturate(mm2, QWORD(edi + ecx + 0x10u));
  mm3 = packed_shift_words(mm3, 0x1u);
  mm3 = packed_add_saturate(mm3, QWORD(edi + ecx + 0x18u));
  QWORD(edi + ecx) = mm0;
  mm4 = packed_add_saturate(mm4, mm6);
  QWORD(edi + ecx + 0x8u) = mm1;
  mm5 = packed_add_saturate(mm5, mm6);
  QWORD(edi + ecx + 0x10u) = mm2;
  mm0 = mm4;
  QWORD(edi + ecx + 0x18u) = mm3;
  a = ecx;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dba0;
  ebx = (uint32_t)(mm4);
  ebx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(ebx)));
L_1000dc08:;
  ecx = BB_U32(esp + 0x20u);
  a = ecx;
  b = 0xfu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000dc62;
  ecx = (ecx << 1);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000dc1c:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + ecx)) & 0xffffu) << 0);
  a = (uint16_t)eax;
  b = ((uint16_t)(ebx));
  wide = (uint64_t)((int64_t)(int16_t)a * (int64_t)(int16_t)b);
  result = (uint16_t)wide;
  eax = (eax & 0xffff0000u) | result;
  edx = (edx & 0xffff0000u) | ((wide >> 16) & 0xffffu);
  a = ((uint16_t)(eax));
  b = ((uint16_t)(edx));
  result = ((a >> 15) | (b << 1)) & 0xffffu;
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = WORD(edi + ecx);
  result = (a + b) & 0xffffu;
  flags = add_flags(flags, a, b, 16);
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  if (flags & 0x800u)
    goto L_1000dc3e;
  WORD(edi + ecx) = ((uint16_t)(eax));
  ebx = (ebx & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(ebx)) + WORD(esp + 0x1cu))) & 0xffffu) << 0);
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dc1c;
  goto L_1000dc62;
L_1000dc3e:;
  if (flags & 1u)
    goto L_1000dc52;
  WORD(edi + ecx) = 0x7fffu;
  ebx = (ebx & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(ebx)) + WORD(esp + 0x1cu))) & 0xffffu) << 0);
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dc1c;
  goto L_1000dc62;
L_1000dc52:;
  WORD(edi + ecx) = 0x8000u;
  ebx = (ebx & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(ebx)) + WORD(esp + 0x1cu))) & 0xffffu) << 0);
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dc1c;
L_1000dc62:;
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
bb_qmix_short_mix_mono16_add_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused)),
                                   uint32_t arg3 __attribute__((unused)),
                                   uint32_t arg4 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_mix_mono16_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_mix_add_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x10u);
  edi = BB_U32(esp + 0xcu);
  ecx = BB_U32(esp + 0x14u);
  a = ecx;
  result = ((uint32_t)((int32_t)a >> 4)) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 4, 2);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000dcce;
  ecx = (ecx << 5);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000dc8e:;
  mm0 = QWORD(esi + ecx);
  mm1 = QWORD(esi + ecx + 0x8u);
  mm2 = QWORD(esi + ecx + 0x10u);
  mm3 = QWORD(esi + ecx + 0x18u);
  mm0 = packed_add_saturate(mm0, QWORD(edi + ecx));
  mm1 = packed_add_saturate(mm1, QWORD(edi + ecx + 0x8u));
  mm2 = packed_add_saturate(mm2, QWORD(edi + ecx + 0x10u));
  mm3 = packed_add_saturate(mm3, QWORD(edi + ecx + 0x18u));
  QWORD(edi + ecx) = mm0;
  QWORD(edi + ecx + 0x8u) = mm1;
  QWORD(edi + ecx + 0x10u) = mm2;
  QWORD(edi + ecx + 0x18u) = mm3;
  a = ecx;
  b = 0x20u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dc8e;
L_1000dcce:;
  ecx = BB_U32(esp + 0x14u);
  a = ecx;
  b = 0xfu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000dd11;
  ecx = (ecx << 1);
  esi = (esi + ecx);
  edi = (edi + ecx);
  ecx = (0u - ecx);
L_1000dce2:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + ecx)) & 0xffffu) << 0);
  a = ((uint16_t)(eax));
  b = WORD(edi + ecx);
  result = (a + b) & 0xffffu;
  flags = add_flags(flags, a, b, 16);
  eax = (eax & 0xffff0000u) | (((uint32_t)(result) & 0xffffu) << 0);
  if (flags & 0x800u)
    goto L_1000dcf7;
  WORD(edi + ecx) = ((uint16_t)(eax));
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dce2;
  goto L_1000dd11;
L_1000dcf7:;
  if (flags & 1u)
    goto L_1000dd06;
  WORD(edi + ecx) = 0x7fffu;
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dce2;
  goto L_1000dd11;
L_1000dd06:;
  WORD(edi + ecx) = 0x8000u;
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dce2;
L_1000dd11:;
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
bb_qmix_short_mix_add_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_mix_add_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_accumulator_magnitude_c(uint32_t *input) {
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
  a = BB_U32(esp + 0xcu);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  if (!(flags & 0x40u))
    goto L_1000dd35;
  eax = (eax & 0xffff0000u) |
        (((uint32_t)((((uint16_t)(eax)) ^ ((uint16_t)(eax)))) & 0xffffu) << 0);
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
L_1000dd35:;
  esi = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x14u);
  eax = (eax ^ eax);
L_1000dd3f:;
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esi));
  esi = (esi + 0x2u);
  ebx = edx;
  edx = ((uint32_t)((int32_t)(edx) >> 15));
  ebx = (ebx ^ edx);
  eax = (eax - edx);
  eax = (eax + ebx);
  a = ecx;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000dd3f;
  WORD(esp + 0xau) = ((uint16_t)(eax));
  args[sp + 2u] = BB_U32(esp + 8u);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esp + 0xau)) & 0xffffu) << 0);
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
bb_qmix_short_accumulator_magnitude_entry(uint32_t arg0 __attribute__((unused)),
                                          uint32_t arg1
                                          __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_accumulator_magnitude_c) ";leal "
                                             "4(%esp),%esp;popal;popfl;leal "
                                             "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_find_signal_c(uint32_t *input) {
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
  ecx = BB_U32(esp + 0xcu);
  a = esp;
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000dd88;
  eax = 0xffffffffu;
  edi = args[sp++];
  esp += 4u;
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
L_1000dd88:;
  edx = (edx & 0xffff0000u) | (((uint32_t)(WORD(esp + 0x14u)) & 0xffffu) << 0);
  eax = BB_U32(esp + 0x10u);
  eax = (eax + ecx * 2u + 0xfffffffeu);
  edi = (edi & 0xffff0000u) | (((uint32_t)(WORD(eax)) & 0xffffu) << 0);
  WORD(eax) = ((uint16_t)(edx));
  esi = BB_U32(esp + 0x10u);
  eax = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x14u));
  edx = eax;
  edx = (0u - edx);
L_1000dda8:;
  a = WORD(esi);
  b = ((uint16_t)(eax));
  flags = sub_flags(flags, a, b, 16);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000ddb7;
  a = WORD(esi);
  b = ((uint16_t)(edx));
  flags = sub_flags(flags, a, b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ddb7;
  esi = (esi + 0x2u);
  goto L_1000dda8;
L_1000ddb7:;
  esi = (esi - BB_U32(esp + 0x10u));
  esi = ((uint32_t)((int32_t)(esi) >> 1));
  BB_U32(esp + 0x8u) = esi;
  args[sp + 2u] = BB_U32(esp + 8u);
  eax = BB_U32(esp + 0x10u);
  WORD(eax + ecx * 2u + 0xfffffffeu) = ((uint16_t)(edi));
  eax = (ecx + 0xffffffffu);
  a = eax;
  b = BB_U32(esp + 0x8u);
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u) && ((!!(flags & 0x80u)) == (!!(flags & 0x800u))))
    goto L_1000ddf4;
  a = WORD(esp + 0x14u);
  b = ((uint16_t)(edi));
  flags = sub_flags(flags, a, b, 16);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_1000ddf8;
  ecx = (uint32_t)(int32_t)(int16_t)(((uint16_t)(edi)));
  edx = (uint32_t)(int32_t)(int16_t)(WORD(esp + 0x14u));
  ecx = (0u - ecx);
  a = ecx;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_1000ddf8;
  eax = 0xffffffffu;
  edi = args[sp++];
  esp += 4u;
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
L_1000ddf4:;
  eax = BB_U32(esp + 0x8u);
L_1000ddf8:;
  edi = args[sp++];
  esp += 4u;
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
bb_qmix_short_find_signal_entry(uint32_t arg0 __attribute__((unused)),
                                uint32_t arg1 __attribute__((unused)),
                                uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_find_signal_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "512(%esp),%esp;ret $0");
}
#endif
void bb_qmix_short_interleave_stereo16_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 20u), 20u);
  uint64_t mm0 __attribute__((unused)) = 0, mm1 __attribute__((unused)) = 0,
               mm2 __attribute__((unused)) = 0, mm3 __attribute__((unused)) = 0,
               mm4 __attribute__((unused)) = 0, mm5 __attribute__((unused)) = 0,
               mm6 __attribute__((unused)) = 0, mm7 __attribute__((unused)) = 0;
  long double call_float __attribute__((unused)) = 0;
  long double fp[8] __attribute__((unused)) = {0};
  unsigned depth __attribute__((unused)) = 0;
  uint16_t fp_status __attribute__((unused)) = 0;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = BB_U32(esp + 0x14u);
  ebx = BB_U32(esp + 0x18u);
  edi = BB_U32(esp + 0x10u);
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  result = (a >> 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 1);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000de61;
  ecx = (ecx << 4);
  esi = (esi + ecx);
  ebx = (ebx + ecx);
  edi = (edi + ecx * 2u);
  ecx = (0u - ecx);
L_1000de26:;
  mm0 = QWORD(esi + ecx);
  mm2 = QWORD(esi + ecx + 0x8u);
  mm1 = mm0;
  mm0 = packed_unpack(mm0, BB_U32(ebx + ecx), 16, 0);
  mm3 = mm2;
  mm1 = packed_unpack(mm1, QWORD(ebx + ecx), 16, 1);
  mm2 = packed_unpack(mm2, BB_U32(ebx + ecx + 0x8u), 16, 0);
  mm3 = packed_unpack(mm3, QWORD(ebx + ecx + 0x8u), 16, 1);
  QWORD(edi + ecx * 2u) = mm0;
  QWORD(edi + ecx * 2u + 0x8u) = mm1;
  QWORD(edi + ecx * 2u + 0x10u) = mm2;
  QWORD(edi + ecx * 2u + 0x18u) = mm3;
  a = ecx;
  b = 0x10u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000de26;
L_1000de61:;
  ecx = BB_U32(esp + 0x1cu);
  a = ecx;
  b = 0x7u;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  ecx = result;
  if (flags & 0x40u)
    goto L_1000de8e;
  ecx = (ecx << 1);
  esi = (esi + ecx);
  ebx = (ebx + ecx);
  edi = (edi + ecx * 2u);
  ecx = (0u - ecx);
L_1000de78:;
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + ecx)) & 0xffffu) << 0);
  edx = (edx & 0xffff0000u) | (((uint32_t)(WORD(ebx + ecx)) & 0xffffu) << 0);
  WORD(edi + ecx * 2u) = ((uint16_t)(eax));
  WORD(edi + ecx * 2u + 0x2u) = ((uint16_t)(edx));
  a = ecx;
  b = 0x2u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  if (!(flags & 0x40u))
    goto L_1000de78;
L_1000de8e:;
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
bb_qmix_short_interleave_stereo16_entry(uint32_t arg0 __attribute__((unused)),
                                        uint32_t arg1 __attribute__((unused)),
                                        uint32_t arg2 __attribute__((unused)),
                                        uint32_t arg3 __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_short_interleave_stereo16_c) ";leal "
                                           "4(%esp),%esp;popal;popfl;leal "
                                           "512(%esp),%esp;ret $0");
}
#endif
