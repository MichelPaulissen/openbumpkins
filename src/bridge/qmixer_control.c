/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
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
/* C forms of QMIXER channel and voice controls.
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
  uint32_t overflow = right ? (before >> (bits - 1u)) & 1u
                            : ((after >> (bits - 1u)) & 1u) ^ carry;
  return logic_flags(flags, after, bits) | carry | (overflow ? 0x800u : 0u);
}
static void __attribute__((noreturn, unused)) division_fault(void) {
  raise(SIGFPE);
  __builtin_unreachable();
}
#define SAMPLE_CALL(target, consumed)                                          \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                   \
    bb_call_words16(&call, BB_CODE(target), args + sp);                        \
    eax = call.eax;                                                            \
    ecx = call.ecx;                                                            \
    edx = call.edx;                                                            \
    flags = call.flags;                                                        \
    sp += (consumed) / 4u;                                                     \
    esp += (consumed);                                                         \
  } while (0)

static uint32_t
    __attribute__((unused)) borrow_flags(uint32_t flags, uint32_t a, uint32_t b,
                                         unsigned bits) {
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
static void sample_float_to_int(struct bb_call_state *state,
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
/* C forms of QMIXER playback state and property updates. Locals exposed
 * to callbacks retain their original storage at the temporary i386 boundary. */
void bb_qmix_call(struct bb_call_state *state, uintptr_t target,
                  uint32_t *args, uint32_t esp, uint32_t edi, uint32_t esi,
                  uint32_t ebp, uint32_t ebx, uint32_t returned) {
  target = bb_qmix_virtual_method_id(target);
  /* The latency probe recovers from WaveError and returns through its owner
   * frame. Give that frame a real C return address: a logical call token would
   * otherwise resume inside the original initialization instructions. */
  if (target == 0x1000a290u) {
    bb_call_words16(state, (uintptr_t)bb_qmix_waveout_measure_latency_entry, args);
    return;
  }
  void (*body)(uint32_t[9]) = 0;
  unsigned argc = 0, reserved = 0, floating = 0;
  switch (target) {
#define BB_QMIX_CASE(address, method, count, frame, fp)                        \
  case address:                                                                \
    body = method;                                                             \
    argc = count;                                                              \
    reserved = frame;                                                          \
    floating = fp;                                                             \
    break;
    BB_QMIX_METHODS(BB_QMIX_CASE)
#undef BB_QMIX_CASE
  default:
    bb_call_words16(state, BB_CODE(target), args);
    return;
  }
  /* Pushed arguments occupy the original logical frame. Keep floating returns
   * in separate C storage so nested methods cannot overwrite saved registers.
   */
  BB_U32(esp - 4u) = returned;
  memcpy((void *)(uintptr_t)esp, args, argc * 4u);
  long double value;
  uint32_t input[10] = {edi,          esi,
                        ebp,          esp - reserved - 8u,
                        ebx,          state->edx,
                        state->ecx,   state->eax,
                        state->flags, (uintptr_t)&value};
  body(input);
  state->eax = input[7];
  state->ecx = input[6];
  state->edx = input[5];
  state->flags = input[8];
  if (floating)
    __asm__ volatile("fldt %0" ::"m"(value) : "st");
}

#endif
#undef SAMPLE_CALL
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
void bb_qmix_current_voice_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000319b;
  eax = BB_U32(eax + 0x4u);
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_1000319b:;
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
uint32_t __attribute__((naked)) bb_qmix_current_voice_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_current_voice_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_channel_enable_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  esi = BB_U32(esp + 0xcu);
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10005ab0u, 4, 0x100033beu); /* 100033b9 */
  ecx = BB_U32(edi + 0x34u);
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x4u), 4, 0x100033c7u); /* 100033c4 indirect */
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
bb_qmix_channel_enable_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_enable_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_pan_rate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  edi = BB_U32(esp + 0x10u);
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(0x10005b10u, 4, 0x100033dfu); /* 100033da */
  eax = BB_U32(ebx + 0x30u);
  esi = BB_U32(ebx + 0x34u);
  args[--sp] = 0x3e8u;
  esp -= 4u;
  ecx = BB_U32(eax + 0x4u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x100033f5u); /* 100033ef indirect */
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(edi + 0x1cu), 4, 0x100033fdu); /* 100033fa indirect */
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
bb_qmix_pan_rate_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pan_rate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                          "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_channel_volume_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  edi = ecx;
  SAMPLE_CALL(0x10005ad0u, 4, 0x1000341eu); /* 10003419 */
  esi = BB_U32(edi + 0x38u);
L_10003421:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003431;
  ecx = BB_U32(esi + 0x4u);
  SAMPLE_CALL(0x10005d70u, 0, 0x1000342du); /* 10003428 */
  esi = BB_U32(esi);
  goto L_10003421;
L_10003431:;
  esi = BB_U32(edi + 0x4cu);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003446;
L_10003438:;
  ecx = BB_U32(esi + 0x4u);
  SAMPLE_CALL(0x10005d70u, 0, 0x10003440u); /* 1000343b */
  esi = BB_U32(esi);
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003438;
L_10003446:;
  ecx = edi;
  SAMPLE_CALL(0x100036f0u, 0, 0x1000344du); /* 10003448 */
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
bb_qmix_channel_volume_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_volume_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_channel_frequency_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  b = 0x8u;
  flags = sub_flags(flags, a, b, 32);
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  esi = ecx;
  SAMPLE_CALL(0x10005b20u, 4, 0x10003470u); /* 1000346b */
  eax = BB_U32(esi + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100034a3;
  eax = BB_U32(eax + 0x4u);
  eax = BB_U32(eax + 0x38u);
  ecx = BB_U32(eax + 0x4u);
  BB_U32(esp + 0x4u) = ecx;
  BB_U32(esp + 0x8u) = 0x0u;
  fp[depth++] = (long double)(int64_t)(QWORD(esp + 0x4u));
  fp[depth - 1] = fp[depth - 1] * F32(esi + 0xccu);
  {
    struct bb_call_state converted = {eax, ecx, edx, flags};
    sample_float_to_int(&converted, &fp[--depth]);
    eax = converted.eax;
    edx = converted.edx;
    flags = converted.flags;
  }
  args[--sp] = eax;
  esp -= 4u;
  ecx = esi;
  SAMPLE_CALL(0x10005b30u, 4, 0x100034a3u); /* 1000349e */
L_100034a3:;
  esi = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x8u;
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
bb_qmix_channel_frequency_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_frequency_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_channel_position_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = esi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10003690u, 4, 0x100034bdu); /* 100034b8 */
  ecx = esi;
  SAMPLE_CALL(0x100036f0u, 0, 0x100034c4u); /* 100034bf */
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
bb_qmix_channel_position_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_channel_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_source_position_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = esi;
  esp -= 4u;
  esi = (ecx + 0x70u);
  eax = BB_U32(edx);
  BB_U32(esi) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(esi + 0x4u) = eax;
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x6cu) = 0x1u;
  BB_U32(esi + 0x8u) = edx;
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
bb_qmix_source_position_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_source_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_source_velocity_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = esi;
  esp -= 4u;
  esi = (ecx + 0x7cu);
  eax = BB_U32(edx);
  BB_U32(esi) = eax;
  eax = BB_U32(edx + 0x4u);
  BB_U32(esi + 0x4u) = eax;
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x6cu) = 0x1u;
  BB_U32(esi + 0x8u) = edx;
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
bb_qmix_source_velocity_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_source_velocity_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_position_update_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10005ae0u, 4, 0x1000369eu); /* 10003699 */
  edi = BB_U32(esi + 0x38u);
L_100036a1:;
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100036b1;
  ecx = BB_U32(edi + 0x4u);
  SAMPLE_CALL(0x10005d80u, 0, 0x100036adu); /* 100036a8 */
  edi = BB_U32(edi);
  goto L_100036a1;
L_100036b1:;
  edi = BB_U32(esi + 0x4cu);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_100036c6;
L_100036b8:;
  ecx = BB_U32(edi + 0x4u);
  SAMPLE_CALL(0x10005d80u, 0, 0x100036c0u); /* 100036bb */
  edi = BB_U32(edi);
  a = edi;
  b = edi;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_100036b8;
L_100036c6:;
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x14u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0xcu), 4, 0x100036d2u); /* 100036cf indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x18u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x14u), 4, 0x100036deu); /* 100036db indirect */
  ecx = BB_U32(esi + 0x34u);
  eax = BB_U32(esi + 0x1cu);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x18u), 4, 0x100036eau); /* 100036e7 indirect */
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
bb_qmix_position_update_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_position_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_gain_update_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  b = 0x4u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  esi = ecx;
  a = BB_U32(ecx + 0xd4u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000370e;
  ecx = BB_U32(esi + 0x34u);
  args[--sp] = 0x0u;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x10003709u); /* 10003706 indirect */
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
L_1000370e:;
  eax = BB_U32(esi);
  ecx = esi;
  SAMPLE_CALL(BB_U32(eax + 0x64u), 0, 0x10003715u); /* 10003712 indirect */
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000372b;
  eax = BB_U32(eax + 0x18u);
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_1000372b;
  BB_U32(esp + 0x4u) = eax;
  fp[depth++] = (long double)(int32_t)(BB_U32(esp + 0x4u));
  goto L_1000372e;
L_1000372b:;
  fp[depth++] = (long double)(int32_t)(BB_U32(esi + 0x10u));
L_1000372e:;
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  --depth;
  a = BB_U32(esi + 0xd0u);
  b = 0x0u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10003749;
  fp[depth] = F32(esi + 0x9cu);
  ++depth;
  fp[depth - 1] = fp[depth - 1] * F32(esp + 0x4u);
  F32(esp + 0x4u) = (float)(fp[depth - 1]);
  --depth;
L_10003749:;
  esi = BB_U32(esi + 0x34u);
  eax = BB_U32(esp + 0x4u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x10002790u, 0, 0x10003756u); /* 10003751 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x10003761u); /* 1000375e indirect */
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
uint32_t __attribute__((naked)) bb_qmix_gain_update_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_gain_update_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_mark_stop_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx + 0x4cu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_1000388f;
  edx = (edx ^ edx);
L_10003879:;
  ecx = BB_U32(eax + 0x4u);
  a = BB_U32(ecx + 0x4cu);
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (!(flags & 0x40u))
    goto L_10003888;
  eax = BB_U32(eax);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_10003879;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10003888:;
  BB_U32(ecx + 0x4cu) = 0x0u;
L_1000388f:;
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
uint32_t __attribute__((naked)) bb_qmix_mark_stop_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_mark_stop_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_source_position_get_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx + 0x70u);
  a = ecx;
  b = 0x70u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(edx) = eax;
  eax = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = eax;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = ecx;
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
bb_qmix_source_position_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_source_position_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_source_velocity_get_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx + 0x7cu);
  a = ecx;
  b = 0x7cu;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  BB_U32(edx) = eax;
  eax = BB_U32(ecx + 0x4u);
  BB_U32(edx + 0x4u) = eax;
  ecx = BB_U32(ecx + 0x8u);
  BB_U32(edx + 0x8u) = ecx;
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
bb_qmix_source_velocity_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_source_velocity_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_distance_get_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx + 0xa0u);
  BB_U32(edx + 0x4u) = eax;
  ecx = BB_U32(ecx + 0xa4u);
  BB_U32(edx + 0x8u) = ecx;
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
bb_qmix_distance_get_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_distance_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_queued_duration_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  args[--sp] = edi;
  esp -= 4u;
  edi = BB_U32(ecx + 0x2cu);
  esi = BB_U32(ecx + 0x4cu);
L_10003fd8:;
  a = esi;
  b = esi;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10003fef;
  ecx = BB_U32(esi + 0x4u);
  SAMPLE_CALL(0x10005d30u, 0, 0x10003fe4u); /* 10003fdf */
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10003ff4;
  edi = (edi + eax);
  esi = BB_U32(esi);
  goto L_10003fd8;
L_10003fef:;
  eax = edi;
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
L_10003ff4:;
  eax = 0xffffffffu;
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
uint32_t __attribute__((naked)) bb_qmix_queued_duration_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_queued_duration_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_error_get_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(0x1001e200u);
  BB_U32(0x1001e200u) = 0x0u;
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
uint32_t __attribute__((naked)) bb_qmix_error_get_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_error_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_error_set_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(0x1001e200u) = eax;
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
bb_qmix_error_set_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_error_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                           "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_base_constructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0x8u) = 0x1u;
  BB_U32(ecx + 0x10u) = 0x2710u;
  a = edx;
  b = edx;
  result = (a ^ b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  edx = result;
  eax = BB_U32(esp + 0x4u);
  BB_U32(ecx + 0x4u) = edx;
  BB_U32(ecx + 0xcu) = edx;
  BB_U32(ecx + 0x30u) = eax;
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_base);
  BB_U32(ecx + 0x14u) = edx;
  BB_U32(ecx + 0x18u) = edx;
  eax = ecx;
  BB_U32(ecx + 0x1cu) = edx;
  BB_U32(ecx + 0x20u) = 0x21u;
  BB_U32(ecx + 0x24u) = 0xffffffffu;
  BB_U32(ecx + 0x28u) = 0x10000u;
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
bb_qmix_base_constructor_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_base_constructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_default_true_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
bb_qmix_default_true_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_true_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_default_false_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
uint32_t __attribute__((naked)) bb_qmix_default_false_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_false_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_default_zero_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
uint32_t __attribute__((naked)) bb_qmix_default_zero_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_zero_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_deleting_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  SAMPLE_CALL(0x10005a90u, 0, 0x10005a78u); /* 10005a73 */
  a = BYTE(esp + 0x8u);
  b = 0x1u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005a88;
  args[--sp] = esi;
  esp -= 4u;
  SAMPLE_CALL(0x10001070u, 0, 0x10005a85u); /* 10005a80 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
L_10005a88:;
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
bb_qmix_deleting_destructor_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_deleting_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_base_destructor_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx) = BB_QMIX_VTABLE(bb_qmix_vtable_base);
  esp += 4u;
  ++sp;
  SAMPLE_CALL(0x10005b70u, 0, BB_U32(esp - 4u)); /* tail call 10005a96 */
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
uint32_t __attribute__((naked)) bb_qmix_base_destructor_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_base_destructor_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_activate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0x4u) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_activate_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_activate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                          "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_enabled_set_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
bb_qmix_enabled_set_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_enabled_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_default_position_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
bb_qmix_default_position_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_position_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_volume_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0x10u) = eax;
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
bb_qmix_volume_store_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_volume_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_position_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  ecx = (ecx + 0x14u);
  edx = BB_U32(esp + 0x8u);
  eax = BB_U32(edx);
  BB_U32(ecx) = eax;
  esi = BB_U32(edx + 0x4u);
  BB_U32(ecx + 0x4u) = esi;
  esi = 0x168u;
  edx = BB_U32(edx + 0x8u);
  BB_U32(ecx + 0x8u) = edx;
  eax = BB_U32(ecx);
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
  BB_U32(ecx) = edx;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_10005b0c;
  a = edx;
  b = esi;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  edx = result;
  BB_U32(ecx) = edx;
L_10005b0c:;
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
bb_qmix_position_store_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_position_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_pan_rate_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0x20u) = eax;
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
bb_qmix_pan_rate_store_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pan_rate_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_frequency_store_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0x24u) = eax;
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
bb_qmix_frequency_store_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_frequency_store_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_increment_calc_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx + 0x30u);
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  edi = BB_U32(esp + 0x10u);
  ecx = BB_U32(eax + 0x4u);
  ebx = BB_U32(0x100212b4u);
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x10000u;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  SAMPLE_CALL(ebx, 12, 0x10005b4eu); /* 10005b4c indirect */
  BB_U32(esi + 0x28u) = eax;
  eax = BB_U32(esi + 0x24u);
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005b64;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(ebx, 12, 0x10005b61u); /* 10005b5f indirect */
  BB_U32(esi + 0x28u) = eax;
L_10005b64:;
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
bb_qmix_increment_calc_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_increment_calc_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_default_reset_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
uint32_t __attribute__((naked)) bb_qmix_default_reset_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_reset_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_default_set_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
bb_qmix_default_set_entry(uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_set_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                             "64(%esp),%esp;ret $4");
}
#endif
void bb_qmix_default_position_get_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(esp + 0x4u);
  ecx = BB_U32(esp + 0x8u);
  BB_U32(eax) = 0x0u;
  BB_U32(ecx) = 0x0u;
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
bb_qmix_default_position_get_entry(uint32_t arg0 __attribute__((unused)),
                                   uint32_t arg1 __attribute__((unused)),
                                   uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_position_get_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                      "64(%esp),%esp;ret $12");
}
#endif
void bb_qmix_pause_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0xcu) = 0x1u;
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
uint32_t __attribute__((naked)) bb_qmix_pause_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_pause_c) ";leal 4(%esp),%esp;popal;popfl;leal 64(%esp),%esp;ret "
                       "$0");
}
#endif
void bb_qmix_restart_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0xcu) = 0x0u;
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
uint32_t __attribute__((naked)) bb_qmix_restart_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_restart_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                         "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_default_pump_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
uint32_t __attribute__((naked)) bb_qmix_default_pump_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_default_pump_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_deactivate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(ecx);
  esi = ecx;
  SAMPLE_CALL(BB_U32(eax + 0x10u), 0, 0x10005be8u); /* 10005be5 indirect */
  BB_U32(esi + 0x4u) = 0x0u;
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
uint32_t __attribute__((naked)) bb_qmix_deactivate_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_deactivate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                            "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_voice_prepare_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = BB_U32(esp + 0x4u);
  args[--sp] = esi;
  esp -= 4u;
  BB_U32(ecx + 0x40u) = eax;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  edx = BB_U32(esp + 0x14u);
  ecx = BB_U32(esp + 0x10u);
  BB_U32(esi + 0x48u) = edx;
  BB_U32(esi + 0x44u) = ecx;
  ecx = BB_U32(esi + 0x30u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005cb5;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x10005cb5u); /* 10005cb2 indirect */
L_10005cb5:;
  ecx = BB_U32(esi + 0x34u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_10005cc1;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x18u), 0, 0x10005cc1u); /* 10005cbe indirect */
L_10005cc1:;
  eax = (eax ^ eax);
  eax = (eax & 0xffff0000u) | (((uint32_t)(WORD(esi + 0x16u)) & 0xffffu) << 0);
  BB_U32(esi + 0x3cu) = eax;
  eax = BB_U32(esi + 0x18u);
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005cdb;
  ecx = BB_U32(esi + 0x40u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(eax + 0x8u), 4, 0x10005cdbu); /* 10005cd8 indirect */
L_10005cdb:;
  a = BYTE(esi + 0x12u);
  b = 0x10u;
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_10005d07;
  ecx = BB_U32(esi + 0x40u);
  eax = BB_U32(esi + 0x24u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0xcu), 4, 0x10005cedu); /* 10005cea indirect */
  ecx = BB_U32(esi + 0x40u);
  eax = BB_U32(esi + 0x28u);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x14u), 4, 0x10005cf9u); /* 10005cf6 indirect */
  ecx = BB_U32(esi + 0x40u);
  eax = BB_U32(esi + 0x2cu);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(ecx);
  SAMPLE_CALL(BB_U32(edi + 0x18u), 4, 0x10005d05u); /* 10005d02 indirect */
  goto L_10005d23;
L_10005d07:;
  eax = BB_U32(esi + 0x1cu);
  a = eax;
  b = 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_10005d23;
  edi = BB_U32(esi + 0x40u);
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(0x1000ba70u, 0, 0x10005d18u); /* 10005d13 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = edi;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(edi);
  SAMPLE_CALL(BB_U32(eax + 0xcu), 4, 0x10005d23u); /* 10005d20 indirect */
L_10005d23:;
  BB_U32(esi + 0x4cu) = 0x1u;
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
bb_qmix_voice_prepare_entry(uint32_t arg0 __attribute__((unused)),
                            uint32_t arg1 __attribute__((unused)),
                            uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_prepare_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                               "64(%esp),%esp;ret $12");
}
#endif
void bb_qmix_voice_duration_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  edx = (edx & 0xffff0000u) | (((uint32_t)(WORD(ecx + 0x16u)) & 0xffffu) << 0);
  a = ((uint16_t)(edx));
  b = 0xffffu;
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_10005d41;
  eax = 0xffffffffu;
  input[0] = edi;
  input[1] = esi;
  input[2] = ebp;
  input[4] = ebx;
  input[5] = edx;
  input[6] = ecx;
  input[7] = eax;
  input[8] = flags;
  return;
L_10005d41:;
  ecx = BB_U32(ecx + 0xau);
  eax = BB_U32(ecx + 0x40u);
  eax = BB_U32(eax + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x3e8u;
  esp -= 4u;
  eax = BB_U32(ecx + 0x14u);
  ecx = ((uint16_t)(edx));
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  a = eax;
  b = ecx;
  wide = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
  result = (uint32_t)wide;
  flags = (flags & ~0x801u) |
          (((int64_t)wide != (int64_t)(int32_t)result) ? 0x801u : 0u);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  SAMPLE_CALL(BB_U32(0x100212b4u), 12, 0x10005d61u); /* 10005d5b indirect */
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
uint32_t __attribute__((naked)) bb_qmix_voice_duration_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_voice_duration_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_volume_invalidate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  BB_U32(ecx + 0x18u) = 0xffffffffu;
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
uint32_t __attribute__((naked)) bb_qmix_volume_invalidate_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_volume_invalidate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_position_invalidate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  a = BB_U32(ecx + 0x12u);
  b = 0xffffffefu;
  result = (a & b) & 0xffffffffu;
  flags = logic_flags(flags, result, 32);
  BB_U32(ecx + 0x12u) = result;
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
uint32_t __attribute__((naked)) bb_qmix_position_invalidate_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_position_invalidate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                     "64(%esp),%esp;ret $0");
}
#endif
void bb_qmix_session_validate_c(uint32_t *input) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 68u;
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
  eax = 0x5432u;
  a = WORD(ecx + 0x4u);
  b = ((uint16_t)(eax));
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_100062f7;
  a = WORD(ecx + 0x6cu);
  b = ((uint16_t)(eax));
  flags = sub_flags(flags, a, b, 16);
  if (!(flags & 0x40u))
    goto L_100062f7;
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
L_100062f7:;
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
uint32_t __attribute__((naked)) bb_qmix_session_validate_entry(void) {
  __asm__ volatile("leal -64(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_qmix_session_validate_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                  "64(%esp),%esp;ret $0");
}
#endif
