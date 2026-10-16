/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "message_calls.h"
#include "object_initializers.h"
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
/* First C forms of notification queues and faction alerts. Address-derived
 * labels retain provenance for auditing queue scans, drawing and callback
 * ordering.
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
#define NOTIFICATION_CALL(target, consumed)                                    \
  do {                                                                         \
    struct bb_call_state call = {eax, ecx, edx, flags};                        \
    BB_MESSAGE_CALL(&call, target, args + sp, esp, edi, esi, ebp, ebx);        \
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

/* This routine uses only a constant %d format. Keep integer formatting local
 * instead of depending on the executable's generic CRT sprintf entry. */
static void format_decimal(char *buffer, int32_t value) {
  char reverse[10];
  unsigned n = 0;
  uint32_t magnitude = (uint32_t)value;
  if (value < 0) {
    *buffer++ = '-';
    magnitude = 0u - magnitude;
  }
  do {
    reverse[n++] = (char)('0' + magnitude % 10u);
    magnitude /= 10u;
  } while (magnitude);
  while (n)
    *buffer++ = reverse[--n];
  *buffer = 0;
}
/* Drawing exposes four local outputs to the font bounds helper; faction
 * dispatch exposes its ten-byte packet; numeric notices expose a text buffer.
 * Their entry wrappers protect those original stack locations while C runs.
 * Other push/pop bookkeeping lives in the C argument array. */

#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked, thiscall))
bb_notification_clear_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_notification_clear_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_notification_clear_entry(void *object) {
  return BB_GAME_CALL(0x41e090, bb_address_encode(object));
}
#endif
void bb_notification_draw_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 20u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = esp;
  b = 0x10u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = (ecx + 0x14u);
  args[--sp] = ebp;
  esp -= 4u;
  edi = 0x20u;
L_41e0bf:;
  eax = BB_U32(esi + 0xfffffff4u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e139;
  ebp = BB_U32(esi + 0xffffffecu);
  eax = BB_U32(esi + 0xfffffff8u);
  ebp = (ebp + eax);
  ecx = BB_U32(0x6024b8u);
  ebp = (ebp - ecx);
  ebx = BB_U32(esi + 0xfffffffcu);
  edx = BB_U32(esi + 0xfffffff0u);
  eax = BB_U32(0x602484u);
  ebx = (ebx + edx);
  args[--sp] = 0x1u;
  esp -= 4u;
  a = ebx;
  b = eax;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  ebx = result;
  ecx = 0x5c5b70u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_font_draw), 16); /* 0041e0ef */
  ecx = (esp + 0x10u);
  eax = (esp + 0x14u);
  edx = (esp + 0x18u);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = (esp + 0x20u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = 0x1u;
  esp -= 4u;
  ecx = 0x5c5b70u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_font_bounds), 32); /* 0041e112 */
  eax = BB_U32(esp + 0x10u);
  ecx = 0x5d19e0u;
  eax = (eax + 1u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esp + 0x18u);
  eax = (eax + 1u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esp + 0x20u);
  eax = (eax - 1u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esp + 0x28u);
  a = eax;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_render_queue_both_entry),
                    16); /* 0041e134 */
L_41e139:;
  esi = (esi + 0x114u);
  a = edi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  edi = result;
  if (!(flags & 0x40u))
    goto L_41e0bf;
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
uint32_t __attribute__((naked, thiscall))
bb_notification_draw_entry(void *object __attribute__((unused))) {
  __asm__ volatile("leal -16(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_notification_draw_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                              "16(%esp),%esp;ret $0");
}
#else
uint32_t bb_notification_draw_entry(void *object) {
  return BB_GAME_CALL(0x41e0b0, bb_address_encode(object));
}
#endif
void bb_notification_tick_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = esi;
  esp -= 4u;
  ecx = (ecx + 0x8u);
  args[--sp] = edi;
  esp -= 4u;
  esi = 0x20u;
L_41e15a:;
  eax = BB_U32(ecx);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e172;
  eax = (eax - 1u);
  edi = 0x3u;
  edx = (uint32_t)((int32_t)eax >> 31);
  BB_U32(ecx) = eax;
  b = edi;
  wide = ((uint64_t)edx << 32) | eax;
  if (!b || (wide == 0x8000000000000000ull && b == 0xffffffffu))
    division_fault();
  if ((int64_t)wide / (int32_t)b < INT32_MIN ||
      (int64_t)wide / (int32_t)b > INT32_MAX)
    division_fault();
  result = (uint32_t)((int64_t)wide / (int32_t)b);
  edx = (uint32_t)((int64_t)wide % (int32_t)b);
  eax = result;
  a = edx;
  b = edx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41e172;
  BB_U32(ecx + 0xfffffffcu) = (BB_U32(ecx + 0xfffffffcu) - 1u);
L_41e172:;
  a = ecx;
  b = 0x114u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  ecx = result;
  a = esi;
  b = 1u;
  result = (a - b) & 0xffffffffu;
  flags = (flags & 1u) | (sub_flags(flags, a, b, 32) & ~1u);
  esi = result;
  if (!(flags & 0x40u))
    goto L_41e15a;
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
uint32_t __attribute__((naked, thiscall))
bb_notification_tick_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_notification_tick_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_notification_tick_entry(void *object) {
  return BB_GAME_CALL(0x41e150, bb_address_encode(object));
}
#endif
void bb_notification_number_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 260u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[112] = {0}, sp = 96, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  a = esp;
  b = 0x100u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  edi = ecx;
  esi = BB_U32(esp + 0x10cu);
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x128u)) & 0xffu) << 0);
  a = ((uint8_t)(eax));
  b = ((uint8_t)(eax));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_41e1d2;
  eax = BB_U32(esp + 0x110u);
  ecx = (esp + 0x8u);
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = 0x486ebcu;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  /* Only the constant %d format is used; all volatile return values are
   * overwritten. */
  format_decimal(BB_DATA(args[sp]), (int32_t)args[sp + 2]);
  edx = (esp + 0x14u);
  a = esp;
  b = 0xcu;
  esp += b;
  sp += b / 4u;
  eax = BB_U32(esi + 0x124u);
  ecx = BB_U32(esi + 0x120u);
  a = eax;
  b = 0x20u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  ecx = edi;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_notification_enqueue_entry),
                    12); /* 0041e1cd */
L_41e1d2:;
  eax = BB_U32(0x48431cu);
  ecx = esi;
  args[--sp] = eax;
  esp -= 4u;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_entity_sound), 4); /* 0041e1da */
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x100u;
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
uint32_t __attribute__((naked, thiscall))
bb_notification_number_entry(void *object __attribute__((unused)),
                             uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -256(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_notification_number_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                "256(%esp),%esp;ret $8");
}
#else
uint32_t bb_notification_number_entry(void *object, uint32_t a0, uint32_t a1) {
  return BB_GAME_CALL(0x41e180, bb_address_encode(object), a0, a1);
}
#endif
void bb_notification_entity_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  eax = BB_U32((uintptr_t)(args + sp) + 4u);
  args[--sp] = ebx;
  esp -= 4u;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0x128u)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  flags = logic_flags(flags, a & b, 8);
  if (flags & 0x40u)
    goto L_41e21a;
  edx = BB_U32(eax + 0x124u);
  ebx = BB_U32((uintptr_t)(args + sp) + 12u);
  a = edx;
  b = 0x20u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  edx = result;
  eax = BB_U32(eax + 0x120u);
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ebx;
  esp -= 4u;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_notification_enqueue_entry),
                    12); /* 0041e215 */
L_41e21a:;
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
uint32_t __attribute__((naked, thiscall))
bb_notification_entity_entry(void *object __attribute__((unused)),
                             uint32_t arg0 __attribute__((unused)),
                             uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_notification_entity_c) ";leal 4(%esp),%esp;popal;popfl;ret $8");
}
#else
uint32_t bb_notification_entity_entry(void *object, uint32_t a0, uint32_t a1) {
  return BB_GAME_CALL(0x41e1f0, bb_address_encode(object), a0, a1);
}
#endif
void bb_notification_enqueue_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 16u), 16u);
  args[--sp] = ebx;
  esp -= 4u;
  edx = (edx ^ edx);
  args[--sp] = esi;
  esp -= 4u;
  eax = (ecx + 0x8u);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
L_41e229:;
  ebx = BB_U32(eax);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e241;
  eax = (eax + 0x114u);
  edx = (edx + 1u);
  a = edx;
  b = 0x20u;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) != (!!(flags & 0x800u)))
    goto L_41e229;
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
L_41e241:;
  eax = edx;
  eax = (eax << 4);
  eax = (eax + edx);
  ebx = (edx + eax * 4u);
  eax = BB_U32((uintptr_t)(args + sp) + 24u);
  edx = (ecx + ebx * 4u);
  ecx = BB_U32((uintptr_t)(args + sp) + 28u);
  BB_U32(edx + 0x8u) = 0x48u;
  edi = BB_U32((uintptr_t)(args + sp) + 20u);
  BB_U32(edx) = eax;
  BB_U32(edx + 0x4u) = ecx;
  ecx = 0xffffffffu;
  ebx = BB_U32(0x6024b8u);
  BB_U32(edx + 0xcu) = ebx;
  eax = BB_U32(0x602484u);
  BB_U32(edx + 0x10u) = eax;
  eax = (eax - eax);
  while (ecx) {
    a = (uint8_t)eax;
    b = BYTE(edi);
    edi += (flags & 0x400u) ? 0xffffffffu : 1u;
    --ecx;
    flags = sub_flags(flags, a, b, 8);
    if (flags & 0x40u)
      break;
  }
  ecx = ~(ecx);
  edi = (edi - ecx);
  ebp = ecx;
  ecx = (ecx >> 2);
  esi = edi;
  edi = (edx + 0x14u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = ebp;
  ebp = args[sp++];
  esp += 4u;
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
uint32_t __attribute__((naked, thiscall))
bb_notification_enqueue_entry(void *object __attribute__((unused)),
                              uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused)),
                              uint32_t arg2 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_notification_enqueue_c) ";leal 4(%esp),%esp;popal;popfl;ret $12");
}
#else
uint32_t bb_notification_enqueue_entry(void *object, uint32_t a0, uint32_t a1,
                                       uint32_t a2) {
  return BB_GAME_CALL(0x41e220, bb_address_encode(object), a0, a1, a2);
}
#endif
void bb_faction_attack_alert_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 16u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  eax = BB_U32(esp + 0x4u);
  a = esp;
  b = 0xcu;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ecx = (ecx ^ ecx);
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0xd4u)) & 0xffu) << 0);
  eax = ecx;
  ecx = (ecx << 3);
  ecx = (ecx - eax);
  edx = (eax + ecx * 8u);
  eax = (eax + edx * 8u);
  ebx = BB_U32(eax * 2u + 0x663578u);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_4291c7;
  eax = BB_U32(0x4842c4u);
  BB_U32(esi + 0x9dcu) = eax;
L_4291c7:;
  eax = BB_U32(esi + 0x44u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_429227;
  eax = (eax & 0xffffff00u) | (((uint32_t)(BYTE(esi + 0x48u)) & 0xffu) << 0);
  edi = BB_U32(esp + 0x20u);
  a = BYTE(edi + 0xf7u);
  b = ((uint8_t)(eax));
  flags = sub_flags(flags, a, b, 8);
  if (flags & 0x40u)
    goto L_429227;
  ecx = (esp + 0xcu);
  BYTE(esp + 0x10u) = ((uint8_t)(eax));
  eax = BB_U32(edi + 0x8eu);
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x513458u;
  BB_U32(esp + 0x16u) = eax;
  WORD(esp + 0x10u) = 0x3u;
  WORD(esp + 0x12u) = 0xau;
  NOTIFICATION_CALL(BB_MESSAGE_TARGET(bb_command_dispatch), 4); /* 00429203 */
  BYTE(esi + 0x406u) = 0x2u;
  ecx = (ecx & 0xffffff00u) | (((uint32_t)(BYTE(edi + 0xf7u)) & 0xffu) << 0);
  BYTE(esi + 0x400u) = ((uint8_t)(ecx));
  edx = BB_U32(edi + 0x8eu);
  BB_U32(esi + 0x402u) = edx;
L_429227:;
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
uint32_t __attribute__((naked, thiscall))
bb_faction_attack_alert_entry(void *object __attribute__((unused)),
                              uint32_t arg0 __attribute__((unused)),
                              uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("leal -12(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_faction_attack_alert_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "12(%esp),%esp;ret $8");
}
#else
uint32_t bb_faction_attack_alert_entry(void *object, uint32_t a0, uint32_t a1) {
  return BB_GAME_CALL(0x429190, bb_address_encode(object), a0, a1);
}
#endif
