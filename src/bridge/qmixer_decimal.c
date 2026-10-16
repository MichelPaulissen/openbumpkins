/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Decimal arithmetic and generation in C. These private twelve-byte values
 * contain an 80-bit significand followed by sign/exponent. Original powers
 * of ten remain data; no original instruction bodies are used here. */
struct extended { uint16_t word[6]; };
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u * (n + 1u));
}
static struct extended load(uint32_t p) {
  struct extended value;
  memcpy(&value, (const void *)(uintptr_t)p, sizeof(value));
  return value;
}
static void store(uint32_t p, const struct extended *value) {
  memcpy((void *)(uintptr_t)p, value, sizeof(*value));
}
static void left(uint16_t words[6]) {
  unsigned carry = 0;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned next = words[i] >> 15;
    words[i] = (uint16_t)((words[i] << 1) | carry);
    carry = next;
  }
}
static void right(uint16_t words[6]) {
  unsigned carry = 0;
  for (unsigned i = 6; i-- > 0;) {
    unsigned next = (words[i] & 1u) << 15;
    words[i] = (uint16_t)((words[i] >> 1) | carry);
    carry = next;
  }
}
static void infinity(struct extended *value, unsigned sign) {
  memset(value, 0, sizeof(*value));
  value->word[4] = 0x8000;
  value->word[5] = (uint16_t)(sign | 0x7fff);
}
static unsigned zero_mantissa(const struct extended *value) {
  return !(value->word[0] | value->word[1] | value->word[2] |
           value->word[3] | value->word[4]);
}
/* 0x100192a0..0x10019546: truncated limb multiplication, normalization,
 * gradual underflow and guard-word rounding of private extended numbers. */
static void multiply(struct extended *out, const struct extended *rhs) {
  struct extended a = *out, b = *rhs;
  unsigned sign = (a.word[5] ^ b.word[5]) & 0x8000u;
  unsigned ea = a.word[5] & 0x7fffu, eb = b.word[5] & 0x7fffu;
  uint16_t exponent = (uint16_t)(ea + eb);
  if (ea >= 0x7fff || eb >= 0x7fff || exponent > 0xbffd) {
    infinity(out, sign); return;
  }
  if (exponent <= 0x3fbf) { memset(out, 0, sizeof(*out)); return; }
  if (!ea) {
    ++exponent;
    if (zero_mantissa(&a)) { a.word[5] = 0; *out = a; return; }
  }
  if (!eb) {
    ++exponent;
    if (zero_mantissa(&b)) { memset(out, 0, sizeof(*out)); return; }
  }
  uint16_t product[8] = {0};
  /* The CRT retains diagonals with limb sums 4..8. Lower diagonals do not
   * contribute carries; preserving this convention keeps its decimal text. */
  for (unsigned k = 0; k < 5; ++k) {
    for (unsigned i = k, j = 4; i < 5; ++i, --j) {
      uint32_t current = product[k] | ((uint32_t)product[k + 1] << 16);
      uint64_t sum = (uint64_t)current + (uint32_t)a.word[i] * b.word[j];
      product[k] = (uint16_t)sum; product[k + 1] = (uint16_t)(sum >> 16);
      if (sum >> 32) ++product[k + 2];
    }
  }
  exponent = (uint16_t)(exponent - 0x3ffe);
  while ((int16_t)exponent > 0 && !(product[5] & 0x8000)) {
    left(product); --exponent;
  }
  if ((int16_t)exponent <= 0) {
    --exponent;
    uint32_t sticky = product[0] | ((uint32_t)product[1] << 16);
    if ((int16_t)exponent < 0) {
      unsigned count = (uint16_t)-exponent;
      exponent = (uint16_t)(exponent + count);
      while (count--) {
        if (product[0] & 1u) ++sticky;
        right(product);
      }
    }
    if (sticky) product[0] |= 1;
  }
  if (product[0] > 0x8000) {
    unsigned i = 1;
    while (i < 5 && product[i] == 0xffff) product[i++] = 0;
    if (i == 5 && product[5] == 0xffff) {
      product[5] = 0x8000; ++exponent;
    } else ++product[i];
  }
  if (exponent >= 0x7fff) infinity(out, sign);
  else {
    for (unsigned i = 0; i < 5; ++i) out->word[i] = product[i + 1];
    out->word[5] = (uint16_t)(exponent | sign);
  }
}
/* 0x10019550..0x100195cb: table-based decimal scaling in base-eight groups. */
static void scale(struct extended *value, int32_t exponent, unsigned extra) {
  if (!exponent) return;
  uint32_t table = exponent < 0 ? 0x10020818u : 0x100206b8u;
  uint32_t power = exponent < 0 ? 0u - (uint32_t)exponent : (uint32_t)exponent;
  if (!extra) value->word[0] = 0;
  while (power) {
    table += 0x54;
    unsigned digit = power & 7u; power >>= 3;
    if (digit) {
      struct extended factor = load(table + digit * 12u);
      if (factor.word[0] >= 0x8000) {
        uint32_t lower = factor.word[1] | ((uint32_t)factor.word[2] << 16);
        --lower;
        factor.word[1] = (uint16_t)lower; factor.word[2] = (uint16_t)(lower >> 16);
      }
      multiply(value, &factor);
    }
  }
}
/* 0x10018480..0x100184e6: low-word-first 96-bit addition. Preserve sequential
 * reads when the two buffers alias, including carry increments before reads. */
void bb_qmix_decimal_add_c(uint32_t *input) {
  uint32_t out = arg(input, 0), rhs = arg(input, 1);
  uint64_t sum = (uint64_t)BB_U32(out) + BB_U32(rhs);
  BB_U32(out) = (uint32_t)sum;
  if (sum >> 32) {
    sum = (uint64_t)BB_U32(out + 4) + 1u; BB_U32(out + 4) = (uint32_t)sum;
    if (sum >> 32) ++BB_U32(out + 8);
  }
  sum = (uint64_t)BB_U32(out + 4) + BB_U32(rhs + 4);
  BB_U32(out + 4) = (uint32_t)sum;
  if (sum >> 32) ++BB_U32(out + 8);
  sum = (uint64_t)BB_U32(out + 8) + BB_U32(rhs + 8);
  BB_U32(out + 8) = (uint32_t)sum; input[7] = (uint32_t)(sum >> 32);
}
/* 0x100184f0..0x1001852e and 0x10018530..0x10018566: one-bit shifts. */
void bb_qmix_decimal_shift_left_c(uint32_t *input) {
  uint32_t out = arg(input, 0); struct extended value = load(out);
  left(value.word); store(out, &value); input[7] = BB_U32(out + 8);
}
void bb_qmix_decimal_shift_right_c(uint32_t *input) {
  uint32_t out = arg(input, 0); struct extended value = load(out);
  right(value.word); store(out, &value); input[7] = BB_U32(out);
}
void bb_qmix_decimal_multiply_c(uint32_t *input) {
  uint32_t out = arg(input, 0);
  struct extended value = load(out), rhs = load(arg(input, 1));
  multiply(&value, &rhs); store(out, &value);
}
void bb_qmix_decimal_scale_c(uint32_t *input) {
  uint32_t out = arg(input, 0); struct extended value = load(out);
  scale(&value, (int32_t)arg(input, 1), arg(input, 2)); store(out, &value);
}
/* 0x10018570..0x10018658: accumulate numeric digit bytes, then normalize the
 * private 80-bit significand. The parser supplies a nonzero digit sequence. */
void bb_qmix_decimal_accumulate_c(uint32_t *input) {
  uint32_t source = arg(input, 0), count = arg(input, 1), out = arg(input, 2);
  struct extended value = {{0}};
  while (count--) {
    uint32_t carry = (uint32_t)(int32_t)(int8_t)BB_U8(source++);
    for (unsigned i = 0; i < 6; ++i) {
      uint32_t sum = (uint32_t)value.word[i] * 10u + (carry & 0xffffu);
      value.word[i] = (uint16_t)sum; carry = (carry >> 16) + (sum >> 16);
    }
  }
  if (zero_mantissa(&value) && !value.word[5]) { store(out, &value); return; }
  uint16_t exponent = 0x404e;
  while (!(value.word[4] | value.word[5])) {
    for (unsigned i = 0; i < 16; ++i) left(value.word);
    exponent = (uint16_t)(exponent - 16);
  }
  while (!(value.word[4] & 0x8000)) { left(value.word); --exponent; }
  value.word[5] = exponent; store(out, &value);
}
/* 0x10019150..0x100191f2: raw numeric classification with Microsoft codes. */
void bb_qmix_decimal_classify_c(uint32_t *input) {
  uint64_t raw = arg(input, 0) | ((uint64_t)arg(input, 1) << 32);
  uint64_t magnitude = raw & UINT64_C(0x7fffffffffffffff);
  unsigned exponent = (unsigned)(magnitude >> 52), sign = (unsigned)(raw >> 63);
  if (exponent == 0x7ff)
    input[7] = !(magnitude & UINT64_C(0x000fffffffffffff)) ? (sign ? 4u : 0x200u) :
               magnitude & UINT64_C(0x0008000000000000) ? 2u : 1u;
  else if (!magnitude) input[7] = sign ? 0x20 : 0x40;
  else if (!exponent) input[7] = sign ? 0x10 : 0x80;
  else input[7] = sign ? 8 : 0x100;
}
static void generated_zero(uint32_t out) {
  BB_U16(out) = 0; BB_U8(out + 2) = ' '; BB_U8(out + 3) = 1;
  BB_U8(out + 4) = '0'; BB_U8(out + 5) = 0;
}
/* 0x10018db0..0x1001914b: generate up to 21 significant decimal digits. */
void bb_qmix_decimal_generate_c(uint32_t *input) {
  uint32_t out = arg(input, 5), low = arg(input, 0), high = arg(input, 1);
  uint16_t encoded = (uint16_t)arg(input, 2), exponent = encoded & 0x7fff;
  unsigned sign = encoded & 0x8000;
  BB_U8(out + 2) = sign ? '-' : ' ';
  if (!exponent && !(low | high)) { generated_zero(out); input[7] = 1; return; }
  if (exponent == 0x7fff) {
    const char *special;
    if ((high != 0x80000000u || low) && !(high & 0x40000000u)) special = "1#SNAN";
    else if (sign && high == 0xc0000000u && !low) special = "1#IND";
    else if (high == 0x80000000u && !low) special = "1#INF";
    else special = "1#QNAN";
    size_t length = strlen(special);
    BB_U16(out) = 1; BB_U8(out + 3) = (uint8_t)length;
    memcpy((void *)(uintptr_t)(out + 4), special, length + 1);
    input[7] = 0; return;
  }
  struct extended value = {{0, (uint16_t)low, (uint16_t)(low >> 16),
                            (uint16_t)high, (uint16_t)(high >> 16), exponent}};
  uint32_t estimate = 77u * ((exponent >> 8) + 2u * (high >> 24)) +
                      19728u * exponent - 0x134312f4u;
  int32_t position = (int32_t)estimate / 65536;
  if ((int32_t)estimate < 0 && (estimate & 0xffffu)) --position;
  scale(&value, -position, 1);
  if (value.word[5] >= 0x3fff) {
    ++position;
    const struct extended tenth = {{0xcccc,0xcccc,0xcccc,0xcccc,0xcccc,0x3ffb}};
    multiply(&value, &tenth);
  }
  BB_U16(out) = (uint16_t)position;
  int32_t count = (int32_t)arg(input, 3);
  if (arg(input, 4) & 1u) {
    count += position;
    if (count <= 0) { generated_zero(out); input[7] = 1; return; }
  }
  if (count > 21) count = 21;
  int32_t shift = (int32_t)value.word[5] - 0x3ffe;
  value.word[5] = 0;
  for (unsigned i = 0; i < 8; ++i) left(value.word);
  if (shift < 0)
    for (unsigned n = (unsigned)-shift & 255u; n; --n) right(value.word);
  uint32_t start = out + 4, end = start;
  for (int32_t n = 0; n < count + 1; ++n) {
    unsigned carry = 0;
    for (unsigned i = 0; i < 6; ++i) {
      unsigned sum = value.word[i] * 10u + carry;
      value.word[i] = (uint16_t)sum; carry = sum >> 16;
    }
    BB_U8(end++) = (uint8_t)((value.word[5] >> 8) + '0');
    value.word[5] &= 0xff;
  }
  uint8_t guard = BB_U8(end - 1); uint32_t last = end - 2;
  if ((int8_t)guard >= '5') {
    while (last >= start && BB_U8(last) == '9') BB_U8(last--) = '0';
    if (last < start) { ++BB_U16(out); ++last; }
    ++BB_U8(last);
  } else {
    while (last >= start && BB_U8(last) == '0') --last;
    if (last < start) { generated_zero(out); input[7] = 1; return; }
  }
  uint8_t length = (uint8_t)(last - start + 1u);
  BB_U8(out + 3) = length; BB_U8(start + length) = 0; input[7] = 1;
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_decimal_add, (ARG(0), ARG(1)))
ENTRY(bb_qmix_decimal_shift_left, (ARG(0)))
ENTRY(bb_qmix_decimal_shift_right, (ARG(0)))
ENTRY(bb_qmix_decimal_accumulate, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_decimal_generate, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)))
ENTRY(bb_qmix_decimal_classify, (ARG(0), ARG(1)))
ENTRY(bb_qmix_decimal_multiply, (ARG(0), ARG(1)))
ENTRY(bb_qmix_decimal_scale, (ARG(0), ARG(1), ARG(2)))
#undef ENTRY
#undef ARG
