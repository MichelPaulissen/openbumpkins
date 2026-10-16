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
/* First C forms of queued messages and panel animation. Address-derived labels
 * retain provenance for auditing list updates, drawing and callback ordering.
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
#define MESSAGE_CALL(target, consumed)                                         \
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

/* Next-message processing exposes its 512-byte comparison string to byte
 * scans. Protect that original local area before C saves registers; retain
 * original pointers and forward DWORD-then-byte copying while bridging. */

#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked, thiscall))
bb_message_construct_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_construct_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_message_construct_entry(void *object) {
  return BB_GAME_CALL(0x41e2a0, bb_address_encode(object));
}
#endif

#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked, thiscall))
bb_message_destructor_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_destructor_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_message_destructor_entry(void *object) {
  return BB_GAME_CALL(0x41e2d0, bb_address_encode(object));
}
#endif

#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked, thiscall))
bb_message_destroy_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_destroy_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_message_destroy_entry(void *object) {
  return BB_GAME_CALL(0x41e2e0, bb_address_encode(object));
}
#endif
void bb_message_append_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  ebx = ecx;
  args[--sp] = 0x20au;
  esp -= 4u;
  MESSAGE_CALL(0x46c060u, 0); /* 0041e31a */
  edi = BB_U32((uintptr_t)(args + sp) + 20u);
  BYTE(eax) = 0x0u;
  a = esp;
  b = 0x4u;
  esp += b;
  sp += b / 4u;
  edx = eax;
  ecx = 0xffffffffu;
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
  eax = ecx;
  ecx = (ecx >> 2);
  esi = edi;
  edi = (edx + 0x1u);
  while (ecx) {
    BB_U32(edi) = BB_U32(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 4u : 4u;
    esi += step;
    edi += step;
    --ecx;
  }
  ecx = eax;
  ecx = (ecx & 0x3u);
  while (ecx) {
    BYTE(edi) = BYTE(esi);
    uint32_t step = (flags & 0x400u) ? 0u - 1u : 1u;
    esi += step;
    edi += step;
    --ecx;
  }
  BB_U32(edx + 0x202u) = 0x0u;
  BB_U32(edx + 0x206u) = 0x0u;
  ecx = BB_U32(ebx);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e3b0;
  eax = BB_U32((uintptr_t)(args + sp) + 20u);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  eax = BB_U32(ecx + 0x206u);
  if (flags & 0x40u)
    goto L_41e38e;
  edi = args[sp++];
  esp += 4u;
  BB_U32(edx + 0x206u) = eax;
  ecx = BB_U32(ebx);
  esi = args[sp++];
  esp += 4u;
  BB_U32(ecx + 0x206u) = edx;
  BB_U32(ebx + 0x8u) = 0x1u;
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
L_41e38e:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e3a4;
  eax = (eax ^ eax);
L_41e394:;
  ecx = BB_U32(ecx + 0x206u);
  ebx = BB_U32(ecx + 0x206u);
  a = ebx;
  b = ebx;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41e394;
L_41e3a4:;
  edi = args[sp++];
  esp += 4u;
  BB_U32(ecx + 0x206u) = edx;
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
L_41e3b0:;
  args[--sp] = edx;
  esp -= 4u;
  ecx = ebx;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_message_current_entry), 4); /* 0041e3b3 */
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
bb_message_append_entry(void *object __attribute__((unused)),
                        uint32_t arg0 __attribute__((unused)),
                        uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_append_c) ";leal 4(%esp),%esp;popal;popfl;ret $8");
}
#else
uint32_t bb_message_append_entry(void *object, uint32_t a0, uint32_t a1) {
  return BB_GAME_CALL(0x41e310, bb_address_encode(object), a0, a1);
}
#endif
void bb_message_current_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 8u), 8u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  esi = ecx;
  ecx = BB_U32((uintptr_t)(args + sp) + 16u);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  BB_U32(esi) = ecx;
  BB_U32(esi + 0xcu) = 0x0u;
  if (flags & 0x40u)
    goto L_41e42b;
  ebx = (esi + 0x3cu);
  edi = (esi + 0x34u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = (esi + 0x38u);
  args[--sp] = eax;
  esp -= 4u;
  edx = (esi + 0x30u);
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(esi + 0x14u);
  args[--sp] = edx;
  esp -= 4u;
  a = eax;
  b = 0xc8u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = 0x1u;
  esp -= 4u;
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  args[--sp] = 0x190u;
  esp -= 4u;
  args[--sp] = 0xc8u;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x5cd7d0u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_font_bounds_wrapped), 36); /* 0041e402 */
  eax = BB_U32(ebx);
  ecx = BB_U32(edi);
  eax = (eax - ecx);
  edi = BB_U32(esi);
  edi = (edi + 1u);
  ecx = 0xffffffffu;
  BB_U32(esi + 0x2cu) = eax;
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
  eax = (ecx + ecx * 4u + 0x2e9u);
  a = eax;
  result = (a << 3) & 0xffffffffu;
  flags = shift_flags(flags, a, result, 32, 3, 0);
  eax = result;
  BB_U32(esi + 0x8u) = eax;
L_41e42b:;
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
bb_message_current_entry(void *object __attribute__((unused)),
                         uint32_t arg0 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_current_c) ";leal 4(%esp),%esp;popal;popfl;ret $4");
}
#else
uint32_t bb_message_current_entry(void *object, uint32_t a0) {
  return BB_GAME_CALL(0x41e3c0, bb_address_encode(object), a0);
}
#endif
void bb_message_draw_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  ebp = ecx;
  ecx = BB_U32(ecx);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e4eb;
  ebx = (ebp + 0x38u);
  edi = (ebp + 0x34u);
  esi = (ebp + 0x30u);
  eax = (ebp + 0x3cu);
  args[--sp] = eax;
  esp -= 4u;
  edx = BB_U32(ebp + 0x18u);
  args[--sp] = ebx;
  esp -= 4u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = edi;
  esp -= 4u;
  a = eax;
  b = 0xc8u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = esi;
  esp -= 4u;
  a = ecx;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  ecx = result;
  args[--sp] = 0x1u;
  esp -= 4u;
  args[--sp] = 0x190u;
  esp -= 4u;
  args[--sp] = edx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  args[--sp] = ecx;
  esp -= 4u;
  ecx = 0x5cd7d0u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_font_bounds_wrapped), 36); /* 0041e47b */
  eax = BB_U32(ebp + 0x3cu);
  eax = (eax + 0x5u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebx);
  eax = (eax + 0x7u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(edi);
  eax = (eax - 0x8u);
  ecx = 0x4eebc8u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi);
  a = eax;
  b = 0x7u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_display_shade), 16); /* 0041e49e */
  args[--sp] = 0x0u;
  esp -= 4u;
  ecx = BB_U32(ebp + 0x18u);
  args[--sp] = 0x1u;
  esp -= 4u;
  eax = BB_U32(ebp + 0x14u);
  args[--sp] = 0x190u;
  esp -= 4u;
  a = eax;
  b = 0xc8u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = ecx;
  esp -= 4u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebp);
  ecx = 0x5cd7d0u;
  a = eax;
  b = 1u;
  result = (a + b) & 0xffffffffu;
  flags = (flags & 1u) | (add_flags(flags, a, b, 32) & ~1u);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_font_draw_wrapped), 24); /* 0041e4c3 */
  eax = BB_U32(ebp + 0x3cu);
  eax = (eax + 0x5u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(ebx);
  eax = (eax + 0x7u);
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(edi);
  eax = (eax - 0x8u);
  ecx = 0x5d19e0u;
  args[--sp] = eax;
  esp -= 4u;
  eax = BB_U32(esi);
  a = eax;
  b = 0x7u;
  result = (a - b) & 0xffffffffu;
  flags = sub_flags(flags, a, b, 32);
  eax = result;
  args[--sp] = eax;
  esp -= 4u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_render_queue_both_entry),
               16); /* 0041e4e6 */
L_41e4eb:;
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
uint32_t __attribute__((naked, thiscall))
bb_message_draw_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_draw_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_message_draw_entry(void *object) {
  return BB_GAME_CALL(0x41e440, bb_address_encode(object));
}
#endif
void bb_message_next_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2],
           esp = input[3] + 516u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[200] = {0}, sp = 176, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  a = esp;
  b = 0x200u;
  esp -= b;
  sp -= b / 4u;
  args[--sp] = ebx;
  esp -= 4u;
  args[--sp] = esi;
  esp -= 4u;
  eax = BB_U32(ecx);
  args[--sp] = edi;
  esp -= 4u;
  args[--sp] = ebp;
  esp -= 4u;
  ebx = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  ebp = BB_U32(ebx + 0x8u);
  if (flags & 0x40u)
    goto L_41e58e;
L_41e50a:;
  edi = BB_U32(ebx);
  ecx = 0xffffffffu;
  edi = (edi + 1u);
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
  eax = ecx;
  ecx = (ecx >> 2);
  esi = edi;
  edi = (esp + 0x10u);
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
  eax = BB_U32(ebx);
  args[--sp] = eax;
  esp -= 4u;
  edi = BB_U32(eax + 0x206u);
  MESSAGE_CALL(0x46c110u, 0); /* 0041e538 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  ecx = ebx;
  args[--sp] = edi;
  esp -= 4u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_message_current_entry), 4); /* 0041e543 */
  ecx = BB_U32(ebx);
  a = ecx;
  b = ecx;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e58e;
  a = ebp;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_41e58e;
  eax = (esp + 0x10u);
  ecx = (ecx + 1u);
L_41e558:;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(eax)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = BYTE(ecx);
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_41e578;
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  result = (a | b) & 0xffu;
  flags = logic_flags(flags, result, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  if (flags & 0x40u)
    goto L_41e574;
  edx = (edx & 0xffffff00u) | (((uint32_t)(BYTE(eax + 0x1u)) & 0xffu) << 0);
  a = ((uint8_t)(edx));
  b = BYTE(ecx + 0x1u);
  flags = sub_flags(flags, a, b, 8);
  if (!(flags & 0x40u))
    goto L_41e578;
  eax = (eax + 0x2u);
  ecx = (ecx + 0x2u);
  a = ((uint8_t)(edx));
  b = ((uint8_t)(edx));
  result = (a | b) & 0xffu;
  flags = logic_flags(flags, result, 8);
  edx = (edx & 0xffffff00u) | (((uint32_t)(result) & 0xffu) << 0);
  if (!(flags & 0x40u))
    goto L_41e558;
L_41e574:;
  eax = (eax ^ eax);
  goto L_41e57d;
L_41e578:;
  a = eax;
  b = eax;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  flags = borrow_flags(flags, a, b, 32);
  eax = result;
  a = eax;
  b = 0xffffffffu;
  result = (a - b - (flags & 1u)) & 0xffffffffu;
  eax = result;
L_41e57d:;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41e58e;
  ebp = BB_U32(ebx + 0x8u);
  eax = BB_U32(ebx);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (!(flags & 0x40u))
    goto L_41e50a;
L_41e58e:;
  ebp = args[sp++];
  esp += 4u;
  edi = args[sp++];
  esp += 4u;
  esi = args[sp++];
  esp += 4u;
  ebx = args[sp++];
  esp += 4u;
  a = esp;
  b = 0x200u;
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
bb_message_next_entry(void *object __attribute__((unused))) {
  __asm__ volatile("leal -512(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_next_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                         "512(%esp),%esp;ret $0");
}
#else
uint32_t bb_message_next_entry(void *object) {
  return BB_GAME_CALL(0x41e4f0, bb_address_encode(object));
}
#endif
void bb_message_tick_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 4u), 4u);
  args[--sp] = esi;
  esp -= 4u;
  args[--sp] = edi;
  esp -= 4u;
  eax = BB_U32(ecx);
  esi = ecx;
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e697;
  eax = BB_U32(esi + 0xcu);
  a = eax;
  b = eax;
  flags = logic_flags(flags, a & b, 32);
  if (flags & 0x40u)
    goto L_41e5c6;
  a = eax;
  b = 0x1u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41e5e7;
  a = eax;
  b = 0x2u;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x40u)
    goto L_41e665;
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
L_41e5c6:;
  eax = BB_U32(0x4842fcu);
  args[--sp] = eax;
  esp -= 4u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_sample_play_entry), 0); /* 0041e5cc */
  BB_U32(esi + 0xcu) = 0x1u;
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  eax = BB_U32(0x484740u);
  BB_U32(esi + 0x4u) = eax;
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
L_41e5e7:;
  eax = BB_U32(0x484448u);
  ecx = BB_U32(esi + 0x2cu);
  eax = (eax - ecx);
  edi = BB_U32(esi + 0x18u);
  eax = (eax - 0x14u);
  BB_U32(esi + 0x10u) = eax;
  eax = (eax - BB_U32(0x484448u));
  eax = (eax + BB_U32(0x5f90e8u));
  BB_U32(esi + 0x10u) = eax;
  ecx = (eax + 0x3u);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((!!(flags & 0x80u)) == (!!(flags & 0x800u)))
    goto L_41e622;
  eax = (eax - edi);
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x3u);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 2));
  eax = (eax + edi);
  eax = (eax - 0x3u);
  goto L_41e639;
L_41e622:;
  ecx = (eax + 0xfffffffdu);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_41e639;
  eax = (eax - edi);
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x3u);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 2));
  eax = (eax + edi);
  eax = (eax + 0x3u);
L_41e639:;
  ecx = BB_U32(esi + 0x4u);
  edx = BB_U32(esi + 0x8u);
  BB_U32(esi + 0x18u) = eax;
  eax = BB_U32(0x484740u);
  eax = (eax - ecx);
  a = eax;
  b = edx;
  flags = sub_flags(flags, a, b, 32);
  if (flags & 0x41u)
    goto L_41e697;
  eax = BB_U32(0x484300u);
  args[--sp] = eax;
  esp -= 4u;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_sample_play_entry), 0); /* 0041e653 */
  a = esp;
  b = 0x4u;
  flags = add_flags(flags, a, b, 32);
  esp += b;
  sp += b / 4u;
  BB_U32(esi + 0xcu) = 0x2u;
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
L_41e665:;
  eax = BB_U32(0x5f90e8u);
  edi = BB_U32(esi + 0x18u);
  eax = (eax + 0x14u);
  BB_U32(esi + 0x10u) = eax;
  ecx = (eax + 0x3u);
  a = ecx;
  b = edi;
  flags = sub_flags(flags, a, b, 32);
  if ((flags & 0x40u) || ((!!(flags & 0x80u)) != (!!(flags & 0x800u))))
    goto L_41e690;
  eax = (eax - edi);
  edx = (uint32_t)((int32_t)eax >> 31);
  edx = (edx & 0x3u);
  eax = (eax + edx);
  eax = ((uint32_t)((int32_t)(eax) >> 2));
  eax = (eax + edi);
  edi = args[sp++];
  esp += 4u;
  a = eax;
  b = 0x3u;
  result = (a + b) & 0xffffffffu;
  flags = add_flags(flags, a, b, 32);
  eax = result;
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
L_41e690:;
  ecx = esi;
  MESSAGE_CALL(BB_MESSAGE_TARGET(bb_message_next_entry), 0); /* 0041e692 */
L_41e697:;
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
bb_message_tick_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_tick_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_message_tick_entry(void *object) {
  return BB_GAME_CALL(0x41e5a0, bb_address_encode(object));
}
#endif
void bb_message_input_c(uint32_t input[9]) {
  uint32_t edi = input[0], esi = input[1], ebp = input[2], esp = input[3] + 4u;
  uint32_t ebx = input[4], edx = input[5], ecx = input[6], eax = input[7],
           flags = input[8];
  uint32_t args[80] = {0}, sp = 64, a __attribute__((unused)),
           b __attribute__((unused)), result __attribute__((unused));
  uint64_t wide __attribute__((unused));
  memcpy(args + sp, BB_SPAN(esp, 12u), 12u);
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
uint32_t __attribute__((naked, thiscall))
bb_message_input_entry(void *object __attribute__((unused)),
                       uint32_t arg0 __attribute__((unused)),
                       uint32_t arg1 __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_input_c) ";leal 4(%esp),%esp;popal;popfl;ret $8");
}
#else
uint32_t bb_message_input_entry(void *object, uint32_t a0, uint32_t a1) {
  return BB_GAME_CALL(0x41e6a0, bb_address_encode(object), a0, a1);
}
#endif

#ifndef BB_MODEL_RUNTIME
uint32_t __attribute__((naked, thiscall))
bb_message_self_entry(void *object __attribute__((unused))) {
  __asm__ volatile("pushfl;pushal;pushl %esp;call " BB_ASM(
      bb_message_self_c) ";leal 4(%esp),%esp;popal;popfl;ret $0");
}
#else
uint32_t bb_message_self_entry(void *object) {
  return BB_GAME_CALL(0x41e6b0, bb_address_encode(object));
}
#endif
