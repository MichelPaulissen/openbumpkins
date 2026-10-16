/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_numeric.h"
#include "crt_primitives.h"
#include "interop.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
struct extended {
  uint16_t word[6];
};
static struct extended load(uintptr_t p) {
  struct extended v;
  for (unsigned n = 0; n < 6; ++n)
    v.word[n] = WORD(p + n * 2);
  return v;
}
static void store(uintptr_t p, const struct extended *v) {
  for (unsigned n = 0; n < 6; ++n)
    WORD(p + n * 2) = v->word[n];
}
static void left(uint16_t v[6]) {
  unsigned carry = 0;
  for (unsigned n = 0; n < 6; ++n) {
    unsigned next = v[n] >> 15;
    v[n] = (uint16_t)((v[n] << 1) | carry);
    carry = next;
  }
}
static void right(uint16_t v[6]) {
  unsigned carry = 0;
  for (unsigned n = 6; n-- > 0;) {
    unsigned next = (v[n] & 1) << 15;
    v[n] = (uint16_t)((v[n] >> 1) | carry);
    carry = next;
  }
}
static void infinity(struct extended *v, unsigned sign) {
  *v = (struct extended){{0, 0, 0, 0, 0x8000, (uint16_t)(sign | 0x7fff)}};
}
static int zero_mantissa(const struct extended *v) {
  return !(v->word[0] | v->word[1] | v->word[2] | v->word[3] | v->word[4]);
}
static uint32_t *word_pointer(const void *p, int32_t index) {
  return (uint32_t *)((uintptr_t)p + (uintptr_t)(intptr_t)index * 4u);
}
uint32_t bb_crt_numeric_tail_zero_entry(const void *p, int32_t bit) {
  int32_t index = bit / 32;
  unsigned shift = (31 - (uint32_t)(bit % 32)) & 31;
  uint32_t v = BB_U32(word_pointer(p, index)) & ~(UINT32_MAX << shift);
  while (!v) {
    if (++index > 2)
      return 1;
    v = BB_U32(word_pointer(p, index));
  }
  return 0;
}
uint32_t bb_crt_numeric_add_word_entry(uint32_t a, uint32_t b, uint32_t *out) {
  uint32_t sum = a + b;
  BB_U32(out) = sum;
  return sum < a || sum < b;
}
uint32_t bb_crt_numeric_increment_entry(void *p, int32_t bit) {
  int32_t index = bit / 32;
  unsigned shift = (31 - (uint32_t)(bit % 32)) & 31;
  uint32_t carry = bb_crt_numeric_add_word_entry(
      BB_U32(word_pointer(p, index)), 1u << shift, word_pointer(p, index));
  while (--index >= 0 && carry)
    carry = bb_crt_numeric_add_word_entry(BB_U32(word_pointer(p, index)), 1,
                                          word_pointer(p, index));
  return carry;
}
uint32_t bb_crt_numeric_round_entry(void *p, int32_t precision) {
  int32_t index = precision / 32;
  unsigned shift = (31 - (uint32_t)(precision % 32)) & 31;
  uint32_t carry = 0;
  if ((BB_U32(word_pointer(p, index)) & (1u << shift)) &&
      !bb_crt_numeric_tail_zero_entry(p, (uint32_t)precision + 1))
    carry = bb_crt_numeric_increment_entry(p, (uint32_t)precision - 1);
  BB_U32(word_pointer(p, index)) &= UINT32_MAX << shift;
  while (++index < 3)
    BB_U32(word_pointer(p, index)) = 0;
  return carry;
}
void bb_crt_numeric_copy_entry(void *out, const void *in) {
  for (unsigned n = 0; n < 3; ++n)
    BB_U32((uintptr_t)out + n * 4) = BB_U32((uintptr_t)in + n * 4);
}
void bb_crt_numeric_clear_entry(void *p) {
  for (unsigned n = 0; n < 3; ++n)
    BB_U32((uintptr_t)p + n * 4) = 0;
}
uint32_t bb_crt_numeric_zero_entry(const void *p) {
  for (unsigned n = 0; n < 3; ++n)
    if (BB_U32((uintptr_t)p + n * 4))
      return 0;
  return 1;
}
void bb_crt_numeric_shift_entry(void *p, int32_t count) {
  int32_t whole = count / 32;
  unsigned partial = (uint32_t)(count % 32) & 31;
  uint32_t carry = 0;
  for (unsigned n = 0; n < 3; ++n) {
    uint32_t v = BB_U32(word_pointer(p, n));
    BB_U32(word_pointer(p, n)) = (v >> partial) | carry;
    carry = (v & ~(UINT32_MAX << partial)) << ((32 - partial) & 31);
  }
  for (int32_t n = 2; n >= 0; --n)
    BB_U32(word_pointer(p, n)) =
        n < whole ? 0 : BB_U32(word_pointer(p, n - whole));
}
static int32_t binary_parameter(const int32_t *format, unsigned n) {
  return BB_I32((uintptr_t)format + n * 4u);
}
uint32_t bb_crt_numeric_binary_entry(const void *source, void *out,
                                     const int32_t *format) {
  uintptr_t s = (uintptr_t)source;
  uint16_t encoded = WORD(s + 10);
  int32_t exponent = (encoded & 0x7fff) - 0x3fff;
  uint32_t words[3] = {BB_U32(s + 6), BB_U32(s + 2), (uint32_t)WORD(s) << 16},
           status = 0;
  int32_t encoded_exponent;
  if (exponent == -0x3fff) {
    encoded_exponent = 0;
    if (!bb_crt_numeric_zero_entry(words)) {
      bb_crt_numeric_clear_entry(words);
      status = 2;
    }
  } else {
    uint32_t original[3];
    bb_crt_numeric_copy_entry(original, words);
    if (bb_crt_numeric_round_entry(words, binary_parameter(format, 2)))
      ++exponent;
    if (exponent < binary_parameter(format, 1) - binary_parameter(format, 2)) {
      bb_crt_numeric_clear_entry(words);
      encoded_exponent = 0;
      status = 2;
    } else if (exponent <= binary_parameter(format, 1)) {
      bb_crt_numeric_copy_entry(words, original);
      bb_crt_numeric_shift_entry(words, binary_parameter(format, 1) - exponent);
      bb_crt_numeric_round_entry(words, binary_parameter(format, 2));
      bb_crt_numeric_shift_entry(words, binary_parameter(format, 3) + 1);
      encoded_exponent = 0;
      status = 2;
    } else if (exponent >= binary_parameter(format, 0)) {
      bb_crt_numeric_clear_entry(words);
      words[0] |= 0x80000000;
      bb_crt_numeric_shift_entry(words, binary_parameter(format, 3));
      encoded_exponent = (uint32_t)binary_parameter(format, 5) +
                         binary_parameter(format, 0);
      status = 1;
    } else {
      encoded_exponent = (uint32_t)exponent + binary_parameter(format, 5);
      words[0] &= 0x7fffffff;
      bb_crt_numeric_shift_entry(words, binary_parameter(format, 3));
    }
  }
  words[0] |=
      ((uint32_t)encoded_exponent <<
       ((31 - (uint32_t)binary_parameter(format, 3)) & 31)) |
      (encoded & 0x8000 ? 0x80000000 : 0);
  if (binary_parameter(format, 4) == 64) {
    BB_U32((uintptr_t)out + 4) = words[0];
    BB_U32(out) = words[1];
  } else if (binary_parameter(format, 4) == 32)
    BB_U32(out) = words[0];
  return status;
}
uint32_t bb_crt_numeric_double_entry(const void *p, void *out) {
  return bb_crt_numeric_binary_entry(p, out, (const int32_t *)0x48e1d0);
}
uint32_t bb_crt_numeric_float_entry(const void *p, void *out) {
  return bb_crt_numeric_binary_entry(p, out, (const int32_t *)0x48e1e8);
}
void bb_crt_numeric_add_entry(void *out, const void *in) {
  uintptr_t a = (uintptr_t)out, b = (uintptr_t)in;
  uint32_t carry =
      bb_crt_numeric_add_word_entry(BB_U32(a), BB_U32(b), (uint32_t *)a);
  if (carry &&
      bb_crt_numeric_add_word_entry(BB_U32(a + 4), 1, (uint32_t *)(a + 4)))
    ++BB_U32(a + 8);
  if (bb_crt_numeric_add_word_entry(BB_U32(a + 4), BB_U32(b + 4),
                                    (uint32_t *)(a + 4)))
    ++BB_U32(a + 8);
  bb_crt_numeric_add_word_entry(BB_U32(a + 8), BB_U32(b + 8),
                                (uint32_t *)(a + 8));
}
void bb_crt_numeric_left_entry(void *p) {
  uintptr_t a = (uintptr_t)p;
  uint32_t c0 = BB_U32(a) >> 31, c1 = BB_U32(a + 4) >> 31;
  BB_U32(a) <<= 1;
  BB_U32(a + 4) = (BB_U32(a + 4) << 1) | c0;
  BB_U32(a + 8) = (BB_U32(a + 8) << 1) | c1;
}
void bb_crt_numeric_right_entry(void *p) {
  uintptr_t a = (uintptr_t)p;
  uint32_t c2 = BB_U32(a + 8) << 31, c1 = BB_U32(a + 4) << 31;
  BB_U32(a + 8) >>= 1;
  BB_U32(a + 4) = (BB_U32(a + 4) >> 1) | c2;
  BB_U32(a) = (BB_U32(a) >> 1) | c1;
}
void bb_crt_numeric_accumulate_entry(const int8_t *digits, uint32_t count,
                                     void *out) {
  uintptr_t p = (uintptr_t)out, cursor = (uintptr_t)digits;
  bb_crt_numeric_clear_entry(out);
  while (count--) {
    uint32_t old[3] = {BB_U32(p), BB_U32(p + 4), BB_U32(p + 8)};
    bb_crt_numeric_left_entry(out);
    bb_crt_numeric_left_entry(out);
    bb_crt_numeric_add_entry(out, old);
    bb_crt_numeric_left_entry(out);
    uint32_t digit[3] = {(uint32_t)(int32_t)(int8_t)BYTE(cursor++), 0, 0};
    bb_crt_numeric_add_entry(out, digit);
  }
  uint16_t exponent = 0x404e;
  /* The original requires a nonzero sequence; zero input keeps normalizing. */
  while (!BB_U32(p + 8)) {
    BB_U32(p + 8) = BB_U32(p + 4) >> 16;
    BB_U32(p + 4) = (BB_U32(p + 4) << 16) | (BB_U32(p) >> 16);
    BB_U32(p) <<= 16;
    exponent -= 16;
  }
  while (!(BYTE(p + 9) & 0x80)) {
    bb_crt_numeric_left_entry(out);
    --exponent;
  }
  WORD(p + 10) = exponent;
}
static void multiply(struct extended *out, const struct extended *rhs,
                     uint32_t *sticky) {
  struct extended a = *out, b = *rhs;
  unsigned sign = (a.word[5] ^ b.word[5]) & 0x8000u;
  unsigned ea = a.word[5] & 0x7fffu, eb = b.word[5] & 0x7fffu;
  uint16_t exponent = (uint16_t)(ea + eb);
  if (ea >= 0x7fff || eb >= 0x7fff || exponent > 0xbffd) {
    infinity(out, sign);
    return;
  }
  if (exponent <= 0x3fbf) {
    *out = (struct extended){{0}};
    return;
  }
  if (!ea) {
    ++exponent;
    if (zero_mantissa(&a)) {
      a.word[5] = 0;
      *out = a;
      return;
    }
  }
  if (!eb) {
    ++exponent;
    if (zero_mantissa(&b)) {
      *out = (struct extended){{0}};
      return;
    }
  }
  uint16_t product[8] = {0};
  /* The CRT retains diagonals with limb sums 4..8. Lower diagonals do not
   * contribute carries; preserving this convention keeps its decimal text. */
  for (unsigned k = 0; k < 5; ++k) {
    for (unsigned i = k, j = 4; i < 5; ++i, --j) {
      uint32_t current = product[k] | ((uint32_t)product[k + 1] << 16);
      uint64_t sum = (uint64_t)current + (uint32_t)a.word[i] * b.word[j];
      product[k] = (uint16_t)sum;
      product[k + 1] = (uint16_t)(sum >> 16);
      if (sum >> 32)
        ++product[k + 2];
    }
  }
  exponent = (uint16_t)(exponent - 0x3ffe);
  while ((int16_t)exponent > 0 && !(product[5] & 0x8000)) {
    left(product);
    --exponent;
  }
  if ((int16_t)exponent <= 0) {
    --exponent;
    if ((int16_t)exponent < 0) {
      unsigned count = (uint16_t)-exponent;
      exponent = (uint16_t)(exponent + count);
      while (count--) {
        if (product[0] & 1u)
          ++*sticky;
        right(product);
      }
    }
    if (*sticky)
      product[0] |= 1;
  }
  if (product[0] > 0x8000) {
    unsigned i = 1;
    while (i < 5 && product[i] == 0xffff)
      product[i++] = 0;
    if (i == 5 && product[5] == 0xffff) {
      product[5] = 0x8000;
      ++exponent;
    } else
      ++product[i];
  }
  if (exponent >= 0x7fff)
    infinity(out, sign);
  else {
    for (unsigned i = 0; i < 5; ++i)
      out->word[i] = product[i + 1];
    out->word[5] = (uint16_t)(exponent | sign);
  }
}
/* Debug CRT underflow uses a previous stack-slot counter. Keep the seed
 * explicit while binary callers remain, without uninitialized C arithmetic. */
void __attribute__((used, noinline)) bb_crt_numeric_multiply_c(uint32_t *r) {
  uintptr_t args = r[3] + 136, slot = r[3] + 124;
  uint32_t sticky = BB_U32(slot);
  uintptr_t out = BB_U32(args);
  struct extended a = load(out), b = load(BB_U32(args + 4));
  multiply(&a, &b, &sticky);
  store(out, &a);
  BB_U32(slot) = sticky;
}
#ifdef _WIN32
#define BB_ASM(n) "_" #n
#else
#define BB_ASM(n) #n
#endif
#ifdef BB_MODEL_RUNTIME
void bb_crt_numeric_multiply_entry(void *out, const void *rhs) {
  struct extended a = load((uintptr_t)out), b = load((uintptr_t)rhs);
  /* Internal typed calls have an initialized C sticky accumulator. The packed
   * entry retains its explicit legacy frame slot for translated callers. */
  uint32_t sticky = 0;
  multiply(&a, &b, &sticky);
  store((uintptr_t)out, &a);
}
#else
void __attribute__((naked, noinline))
bb_crt_numeric_multiply_entry(void *out __attribute__((unused)),
                              const void *rhs __attribute__((unused))) {
  __asm__ volatile("subl $128,%esp;pushfl;pushal;cld;pushl %esp;call " BB_ASM(
      bb_crt_numeric_multiply_c) ";addl $4,%esp;popal;popfl;addl "
                                 "$128,%esp;ret");
}
#endif
void bb_crt_numeric_scale_entry(void *out, int32_t exponent, int32_t extra) {
  if (!exponent)
    return;
  uintptr_t table = exponent < 0 ? 0x48e570 : 0x48e410;
  uint32_t power = exponent < 0 ? 0u - (uint32_t)exponent : (uint32_t)exponent;
  if (!extra)
    WORD(out) = 0;
  while (power) {
    table += 0x54;
    unsigned digit = power & 7;
    power = (uint32_t)((int32_t)power >> 3);
    if (digit) {
      uintptr_t factor = table + digit * 12;
      struct extended copy;
      if (WORD(factor) >= 0x8000) {
        copy = load(factor);
        uint32_t lower = copy.word[1] | (uint32_t)copy.word[2] << 16;
        --lower;
        copy.word[1] = lower;
        copy.word[2] = lower >> 16;
        bb_crt_numeric_multiply_entry(out, &copy);
      } else
        bb_crt_numeric_multiply_entry(out, (const void *)factor);
    }
  }
}
static unsigned digit(uint8_t c) {
  return BB_I32(0x48d35c) <= 1
             ? WORD((uintptr_t)BB_POINTER(0x48d150) + c * 2) & 4
             : bb_crt_character_type_entry(c, 4);
}
static int exponent_marker(uint8_t c) {
  return c == 'e' || c == 'E' || c == 'd' || c == 'D';
}
uint32_t bb_crt_numeric_parse_c(void *output, const char **end,
                                    const char *source, int32_t mult12,
                                    int32_t implied_power,
                                    int32_t implied_fraction,
                                    int32_t implicit_exponent,
                                    unsigned packed_end) {
  uintptr_t p = (uintptr_t)source, rollback = p;
  uint8_t digits_buffer[25];
  struct extended result;
  uintptr_t digits = (uintptr_t)digits_buffer, value = (uintptr_t)&result;
  uint32_t power = 0, adjustment = 0, count = 0;
  unsigned state = 0, present = 0, fractional = 0, explicit_power = 0;
  unsigned negative_power = 0, sign = 0;
  uint8_t ch;
  while ((ch = BYTE(p)) == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
    ++p;
  while (state != 10) {
    ch = BYTE(p++);
    switch (state) {
    case 0:
    case 2:
      if (ch >= '1' && ch <= '9') {
        state = 3;
        --p;
      } else if (ch == BYTE(0x48d360u))
        state = 5;
      else if (ch == '0')
        state = 1;
      else if (!state && (ch == '+' || ch == '-')) {
        sign = ch == '-' ? 0x8000u : 0;
        state = 2;
      } else {
        if (state == 2)
          p = rollback;
        else
          --p;
        state = 10;
      }
      break;
    case 1:
      present = 1;
      if (ch >= '1' && ch <= '9') {
        state = 3;
        --p;
      } else if (ch == BYTE(0x48d360u))
        state = 4;
      else if (ch == '0')
        state = 1;
      else if (ch == '+' || ch == '-') {
        state = 11;
        --p;
      } else if (exponent_marker(ch))
        state = 6;
      else {
        state = 10;
        --p;
      }
      break;
    case 3:
      present = 1;
      while (digit(ch)) {
        if (count < 25)
          BYTE(digits + count++) = (uint8_t)(ch - '0');
        else
          ++adjustment;
        ch = BYTE(p++);
      }
      if (ch == BYTE(0x48d360u))
        state = 4;
      else if (ch == '+' || ch == '-') {
        state = 11;
        --p;
      } else if (exponent_marker(ch))
        state = 6;
      else {
        state = 10;
        --p;
      }
      break;
    case 4:
      present = fractional = 1;
      if (!count)
        while (ch == '0') {
          --adjustment;
          ch = BYTE(p++);
        }
      while (digit(ch)) {
        if (count < 25) {
          BYTE(digits + count++) = (uint8_t)(ch - '0');
          --adjustment;
        }
        ch = BYTE(p++);
      }
      if (ch == '+' || ch == '-') {
        state = 11;
        --p;
      } else if (exponent_marker(ch))
        state = 6;
      else {
        state = 10;
        --p;
      }
      break;
    case 5:
      fractional = 1;
      if (digit(ch)) {
        state = 4;
        --p;
      } else {
        state = 10;
        p = rollback;
      }
      break;
    case 6:
      rollback = p - 2u;
      if (ch >= '1' && ch <= '9') {
        state = 9;
        --p;
      } else if (ch == '+' || ch == '-') {
        negative_power = ch == '-';
        state = 7;
      } else if (ch == '0')
        state = 8;
      else {
        state = 10;
        p = rollback;
      }
      break;
    case 7:
      if (ch >= '1' && ch <= '9') {
        state = 9;
        --p;
      } else if (ch == '0')
        state = 8;
      else {
        state = 10;
        p = rollback;
      }
      break;
    case 8:
      explicit_power = 1;
      while (ch == '0')
        ch = BYTE(p++);
      if (ch >= '1' && ch <= '9') {
        state = 9;
        --p;
      } else {
        state = 10;
        --p;
      }
      break;
    case 9:
      explicit_power = 1;
      power = 0;
      while (digit(ch)) {
        power = power * 10u + (uint32_t)(int32_t)(int8_t)ch - '0';
        if ((int32_t)power > 5200) {
          power = 5201;
          break;
        }
        ch = BYTE(p++);
      }
      while (digit(ch))
        ch = BYTE(p++);
      state = 10;
      --p;
      break;
    case 11:
      if (!implicit_exponent) {
        state = 10;
        --p;
        break;
      }
      rollback = p - 1u;
      if (ch == '+' || ch == '-') {
        negative_power = ch == '-';
        state = 7;
      } else {
        state = 10;
        p = rollback;
      }
      break;
    }
  }
  if (packed_end)
    BB_U32(end) = bb_address_encode((void *)p);
  else
    *end = (const char *)p;
  uint32_t status = 0;
  result = (struct extended){{0}};
  if (!present)
    status = 4;
  else if (count) {
    /* The old parser drops digit 25 and rounds digit 24 using digit 24
     * itself. Keep that unusual rule, including a possible numeric digit 10. */
    if (count > 24) {
      if ((int8_t)BYTE(digits + 23) >= 5)
        ++BYTE(digits + 23);
      count = 24;
      ++adjustment;
    }
    while (!BYTE(digits + count - 1u)) {
      --count;
      ++adjustment;
    }
    bb_crt_numeric_accumulate_entry((const int8_t *)digits, count, &result);
    if (negative_power)
      power = 0u - power;
    power += adjustment;
    if (!explicit_power)
      power += (uint32_t)implied_power;
    if (!fractional)
      power -= (uint32_t)implied_fraction;
    if ((int32_t)power > 5200) {
      result = (struct extended){{0}};
      BB_U32(value + 6) = 0x80000000u;
      WORD(value + 10) = 0x7fff;
      status = 2;
    } else if ((int32_t)power < -5200) {
      result = (struct extended){{0}};
      status = 1;
    } else {
      bb_crt_numeric_scale_entry(&result, power, mult12);
    }
  }
  WORD(value + 10) |= sign;
  store((uintptr_t)output, &result);
  return status;
}
uint32_t bb_crt_numeric_parse_entry(void *output, const char **end,
                                    const char *source, int32_t mult12,
                                    int32_t implied_power,
                                    int32_t implied_fraction,
                                    int32_t implicit_exponent) {
  return bb_crt_numeric_parse_c(output, end, source, mult12, implied_power,
                                implied_fraction, implicit_exponent, 0);
}
uint32_t bb_crt_numeric_parse_double_entry(void *out, const char *text) {
  struct extended value;
  const char *end;
  bb_crt_numeric_parse_entry(&value, &end, text, 0, 0, 0, 0);
  return bb_crt_numeric_double_entry(&value, out);
}
uint32_t bb_crt_numeric_parse_float_entry(void *out, const char *text) {
  struct extended value;
  const char *end;
  bb_crt_numeric_parse_entry(&value, &end, text, 0, 0, 0, 0);
  return bb_crt_numeric_float_entry(&value, out);
}
void bb_crt_numeric_assign_entry(int32_t wide, void *out, const char *text) {
  uint32_t value[2];
  if (wide) {
    bb_crt_numeric_parse_double_entry(value, text);
    BB_U32(out) = value[0];
    BB_U32((uintptr_t)out + 4) = value[1];
  } else {
    bb_crt_numeric_parse_float_entry(value, text);
    BB_U32(out) = value[0];
  }
}
void bb_crt_numeric_pack_entry(void *out, const void *number) {
  uintptr_t p = (uintptr_t)number;
  uint64_t raw = BB_U32(p) | (uint64_t)BB_U32(p + 4) << 32,
           mantissa = (raw & UINT64_C(0xfffffffffffff)) << 11;
  unsigned exponent = (raw >> 52) & 0x7ff;
  uint16_t encoded;
  if (!exponent && !mantissa)
    encoded = 0;
  else {
    encoded = exponent == 0x7ff ? 0x7fff
              : exponent        ? exponent + 0x3c00
                                : 0x3c01;
    if (exponent)
      mantissa |= UINT64_C(0x8000000000000000);
    while (!(mantissa & UINT64_C(0x8000000000000000))) {
      --encoded;
      mantissa <<= 1;
    }
    encoded |= (raw >> 48) & 0x8000;
  }
  BB_U32((uintptr_t)out + 4) = mantissa >> 32;
  BB_U32(out) = mantissa;
  WORD((uintptr_t)out + 8) = encoded;
}
uintptr_t bb_crt_numeric_round_text_entry(char *out, int32_t count,
                                         void *record) {
  uintptr_t source = BB_U32((uintptr_t)record + 12), p = (uintptr_t)out;
  BYTE(p++) = '0';
  for (; count > 0; count--) {
    uint8_t c = BYTE(source);
    BYTE(p++) = c ? BYTE(source++) : '0';
  }
  BYTE(p) = 0;
  if (count >= 0 && (int8_t)BYTE(source) >= '5') {
    do {
      --p;
      if (BYTE(p) != '9')
        break;
      BYTE(p) = '0';
    } while (1);
    ++BYTE(p);
  }
  if (BYTE(out) == '1') {
    ++BB_U32((uintptr_t)record + 4);
    return (uintptr_t)record;
  }
  return (uintptr_t)bb_crt_string_copy_entry(out, out + 1);
}
static void generated_zero(uintptr_t out) {
  WORD(out) = 0;
  BYTE(out + 2) = ' ';
  BYTE(out + 3) = 1;
  BYTE(out + 4) = '0';
  BYTE(out + 5) = 0;
}
uint32_t bb_crt_numeric_generate_entry(uint32_t low, uint32_t high,
                                       uint32_t raw_exponent, int32_t requested,
                                       uint32_t mode, void *destination) {
  uintptr_t out = (uintptr_t)destination;
  uint16_t encoded = raw_exponent, exponent = encoded & 0x7fff;
  unsigned sign = encoded & 0x8000;
  BYTE(out + 2) = sign ? '-' : ' ';
  if (!exponent && !(low | high)) {
    generated_zero(out);
    return 1;
  }
  if (exponent == 0x7fff) {
    const char *special;
    if ((high != 0x80000000u || low) && !(high & 0x40000000u))
      special = (const char *)0x483580;
    else if (sign && high == 0xc0000000u && !low)
      special = (const char *)0x483578;
    else if (high == 0x80000000u && !low)
      special = (const char *)0x483570;
    else
      special = (const char *)0x483568;
    uint32_t length =
        special == (const char *)0x483580 || special == (const char *)0x483568
            ? 6
            : 5;
    WORD(out) = 1;
    bb_crt_string_copy_entry((char *)(out + 4), special);
    BYTE(out + 3) = (uint8_t)length;
    return 0;
  }
  struct extended value = {{0, (uint16_t)low, (uint16_t)(low >> 16),
                            (uint16_t)high, (uint16_t)(high >> 16), exponent}};
  uint32_t estimate = 77u * ((exponent >> 8) + 2u * (high >> 24)) +
                      19728u * exponent - 0x134312f4u;
  int32_t position = (int32_t)estimate / 65536;
  if ((int32_t)estimate < 0 && (estimate & 0xffffu))
    --position;
  bb_crt_numeric_scale_entry(&value, -position, 1);
  if (value.word[5] >= 0x3fff) {
    ++position;
    const struct extended tenth = {
        {0xcccc, 0xcccc, 0xcccc, 0xcccc, 0xcccc, 0x3ffb}};
    bb_crt_numeric_multiply_entry(&value, &tenth);
  }
  WORD(out) = (uint16_t)position;
  int32_t count = requested;
  if (mode & 1u) {
    count = (int32_t)((uint32_t)count + (uint32_t)position);
    if (count <= 0) {
      generated_zero(out);
      return 1;
    }
  }
  if (count > 21)
    count = 21;
  int32_t shift = (int32_t)value.word[5] - 0x3ffe;
  value.word[5] = 0;
  for (unsigned i = 0; i < 8; ++i)
    left(value.word);
  if (shift < 0)
    for (unsigned n = (unsigned)-shift & 255u; n; --n)
      right(value.word);
  uintptr_t start = out + 4, end = start;
  for (int32_t n = 0; n < count + 1; ++n) {
    unsigned carry = 0;
    for (unsigned i = 0; i < 6; ++i) {
      unsigned sum = value.word[i] * 10u + carry;
      value.word[i] = (uint16_t)sum;
      carry = sum >> 16;
    }
    BYTE(end++) = (uint8_t)((value.word[5] >> 8) + '0');
    value.word[5] &= 0xff;
  }
  uint8_t guard = BYTE(end - 1);
  uintptr_t last = end - 2;
  if ((int8_t)guard >= '5') {
    while (last >= start && BYTE(last) == '9')
      BYTE(last--) = '0';
    if (last < start) {
      ++WORD(out);
      ++last;
    }
    ++BYTE(last);
  } else {
    while (last >= start && BYTE(last) == '0')
      --last;
    if (last < start) {
      generated_zero(out);
      return 1;
    }
  }
  uint8_t length = (uint8_t)(last - start + 1u);
  BYTE(out + 3) = length;
  BYTE(start + length) = 0;
  return 1;
}
void *bb_crt_numeric_decimal_entry(uint32_t low, uint32_t high) {
  uint32_t number[2] = {low, high};
  uint8_t value[12];
  bb_crt_numeric_pack_entry(value, number);
  BB_U32(0x6c89d0) =
      bb_crt_numeric_generate_entry(BB_U32(value), BB_U32(value + 4),
                                    WORD(value + 8), 17, 0, (void *)0x6c89a8);
  BB_U32(0x6c89c8) = (int32_t)(int8_t)BYTE(0x6c89aa);
  BB_U32(0x6c89cc) = (int32_t)(int16_t)WORD(0x6c89a8);
  BB_U32(0x6c89d4) = 0x6c89ac;
  return (void *)0x6c89c8;
}
