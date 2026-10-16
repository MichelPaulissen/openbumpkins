/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* CRT text parsing and character helpers. The parser converts its digit
 * sequence through the C decimal arithmetic. Character locales use the
 * shared C NLS adapters and the existing platform API boundary. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u * (n + 1u));
}
static void call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 256u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax; input[6] = state.ecx;
  input[5] = state.edx; input[8] = state.flags;
}
static uint16_t type(uint32_t ch) {
  return BB_U16(BB_U32(0x10020260u) + 2u * ch);
}
static unsigned character_bytes(uint32_t ch, uint32_t out) {
  unsigned count = 1;
  if (type((ch >> 8) & 255u) & 0x8000u) {
    BB_U8(out++) = (uint8_t)(ch >> 8); count = 2;
  }
  BB_U8(out++) = (uint8_t)ch; BB_U8(out) = 0;
  return count;
}
/* 0x10015f90..0x10016021: table classification, including EOF, with the
 * original single/multibyte NLS fallback for packed characters. */
void bb_qmix_parse_character_type_c(uint32_t *input) {
  uint32_t ch = arg(input, 0), mask = arg(input, 1);
  if (ch + 1u <= 256u) { input[7] = type(ch) & mask; return; }
  uint32_t out = stack(input) - 16u, buffer = out + 4u;
  unsigned count = character_bytes(ch, buffer);
  uint32_t args[16] = {1, buffer, count, out, 0, 0};
  call(input, 0x100180c0u, args);
  input[7] = input[7] ? BB_U16(out) & mask : 0;
}
/* 0x10016030..0x10016115: C-locale ASCII conversion and the original
 * locale-aware uppercase gate, byte packing and failed-map fallback. */
void bb_qmix_parse_lowercase_c(uint32_t *input) {
  uint32_t ch = arg(input, 0), locale = BB_U32(0x100204a8u);
  if (!locale) {
    input[7] = (int32_t)ch >= 'A' && (int32_t)ch <= 'Z' ? ch + 32u : ch;
    return;
  }
  if ((int32_t)ch < 256) {
    unsigned uppercase;
    if ((int32_t)BB_U32(0x10020254u) > 1) {
      uint32_t args[16] = {ch, 1};
      call(input, 0x10015f90u, args); uppercase = input[7];
    } else uppercase = type(ch) & 1u;
    if (!uppercase) { input[7] = ch; return; }
  }
  uint32_t out = stack(input) - 16u, buffer = out + 4u;
  unsigned count = character_bytes(ch, buffer);
  uint32_t args[16] = {locale, 0x100, buffer, count, out, 3, 0};
  call(input, 0x100181f0u, args);
  if (input[7])
    input[7] = input[7] == 1 ? BB_U8(out) : BB_U8(out) | ((uint32_t)BB_U8(out + 1) << 8);
  else input[7] = ch;
}
static unsigned digit(uint32_t *input, uint8_t ch) {
  if ((int32_t)BB_U32(0x10020254u) <= 1) return type(ch) & 4u;
  uint32_t args[16] = {ch, 4}; call(input, 0x10015f90u, args);
  return input[7];
}
static unsigned exponent_marker(uint8_t ch) {
  return ch == 'e' || ch == 'E' || ch == 'd' || ch == 'D';
}
/* 0x10018660..0x10018c87: decimal text to a private twelve-byte extended
 * number. Modes supply an implied exponent/fraction or allow a signed
 * exponent without E/D. Only four whitespace bytes are skipped. */
void bb_qmix_parse_extended_c(uint32_t *input) {
  uint32_t output = arg(input, 0), end = arg(input, 1), p = arg(input, 2);
  uint32_t rollback = p, digits = stack(input) - 80u, value = stack(input) - 40u;
  uint32_t power = 0, adjustment = 0, count = 0;
  unsigned state = 0, present = 0, fractional = 0, explicit_power = 0;
  unsigned negative_power = 0, sign = 0;
  uint8_t ch;
  while ((ch = BB_U8(p)) == ' ' || ch == '\t' || ch == '\n' || ch == '\r') ++p;
  while (state != 10) {
    ch = BB_U8(p++);
    switch (state) {
    case 0: case 2:
      if (ch >= '1' && ch <= '9') { state = 3; --p; }
      else if (ch == BB_U8(0x10020258u)) state = 5;
      else if (ch == '0') state = 1;
      else if (!state && (ch == '+' || ch == '-')) {
        sign = ch == '-' ? 0x8000u : 0; state = 2;
      } else { if (state == 2) p = rollback; else --p; state = 10; }
      break;
    case 1:
      present = 1;
      if (ch >= '1' && ch <= '9') { state = 3; --p; }
      else if (ch == BB_U8(0x10020258u)) state = 4;
      else if (ch == '0') state = 1;
      else if (ch == '+' || ch == '-') { state = 11; --p; }
      else if (exponent_marker(ch)) state = 6;
      else { state = 10; --p; }
      break;
    case 3:
      present = 1;
      while (digit(input, ch)) {
        if (count < 25) BB_U8(digits + count++) = (uint8_t)(ch - '0');
        else ++adjustment;
        ch = BB_U8(p++);
      }
      if (ch == BB_U8(0x10020258u)) state = 4;
      else if (ch == '+' || ch == '-') { state = 11; --p; }
      else if (exponent_marker(ch)) state = 6;
      else { state = 10; --p; }
      break;
    case 4:
      present = fractional = 1;
      if (!count) while (ch == '0') { --adjustment; ch = BB_U8(p++); }
      while (digit(input, ch)) {
        if (count < 25) {
          BB_U8(digits + count++) = (uint8_t)(ch - '0'); --adjustment;
        }
        ch = BB_U8(p++);
      }
      if (ch == '+' || ch == '-') { state = 11; --p; }
      else if (exponent_marker(ch)) state = 6;
      else { state = 10; --p; }
      break;
    case 5:
      fractional = 1;
      if (digit(input, ch)) { state = 4; --p; }
      else { state = 10; p = rollback; }
      break;
    case 6:
      rollback = p - 2u;
      if (ch >= '1' && ch <= '9') { state = 9; --p; }
      else if (ch == '+' || ch == '-') { negative_power = ch == '-'; state = 7; }
      else if (ch == '0') state = 8;
      else { state = 10; p = rollback; }
      break;
    case 7:
      if (ch >= '1' && ch <= '9') { state = 9; --p; }
      else if (ch == '0') state = 8;
      else { state = 10; p = rollback; }
      break;
    case 8:
      explicit_power = 1;
      while (ch == '0') ch = BB_U8(p++);
      if (ch >= '1' && ch <= '9') { state = 9; --p; }
      else { state = 10; --p; }
      break;
    case 9:
      explicit_power = 1; power = 0;
      while (digit(input, ch)) {
        power = power * 10u + (uint32_t)(int32_t)(int8_t)ch - '0';
        if ((int32_t)power > 5200) { power = 5201; break; }
        ch = BB_U8(p++);
      }
      while (digit(input, ch)) ch = BB_U8(p++);
      state = 10; --p;
      break;
    case 11:
      if (!arg(input, 6)) { state = 10; --p; break; }
      rollback = p - 1u;
      if (ch == '+' || ch == '-') { negative_power = ch == '-'; state = 7; }
      else { state = 10; p = rollback; }
      break;
    }
  }
  BB_U32(end) = p;
  uint32_t status = 0;
  memset((void *)(uintptr_t)value, 0, 12);
  if (!present) status = 4;
  else if (count) {
    /* The old parser drops digit 25 and rounds digit 24 using digit 24
     * itself. Keep that unusual rule, including a possible numeric digit 10. */
    if (count > 24) {
      if ((int8_t)BB_U8(digits + 23) >= 5) ++BB_U8(digits + 23);
      count = 24; ++adjustment;
    }
    while (!BB_U8(digits + count - 1u)) { --count; ++adjustment; }
    uint32_t args[16] = {digits, count, value}; call(input, 0x10018570u, args);
    if (negative_power) power = 0u - power;
    power += adjustment;
    if (!explicit_power) power += arg(input, 4);
    if (!fractional) power -= arg(input, 5);
    if ((int32_t)power > 5200) {
      memset((void *)(uintptr_t)value, 0, 12);
      BB_U32(value + 6) = 0x80000000u; BB_U16(value + 10) = 0x7fff; status = 2;
    } else if ((int32_t)power < -5200) {
      memset((void *)(uintptr_t)value, 0, 12); status = 1;
    } else {
      args[0] = value; args[1] = power; args[2] = arg(input, 3);
      call(input, 0x10019550u, args);
    }
  }
  BB_U16(value + 10) |= sign;
  memcpy((void *)(uintptr_t)output, (const void *)(uintptr_t)value, 12);
  input[7] = status;
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_parse_character_type, (ARG(0), ARG(1)))
ENTRY(bb_qmix_parse_lowercase, (ARG(0)))
ENTRY(bb_qmix_parse_extended, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)))
#undef ENTRY
#undef ARG
