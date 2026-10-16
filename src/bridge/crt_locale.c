/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_locale.h"
#include "abi.h"
#include "crt_bootstrap.h"
#include "crt_debug_heap.h"
#include "crt_errors.h"
#include "crt_primitives.h"
#include "game_calls.h"
#include "interop.h"
#include <signal.h>
#ifdef BB_MODEL_RUNTIME
#include "crt_locale_services.inc"
#endif
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define WIN BB_STDCALL
#define STR(a) ((const char *)(uintptr_t)(a))
#ifndef BB_MODEL_RUNTIME
int32_t bb_crt_locale_wide_bytes(uint32_t cp, uint32_t flags,
                                 const uint16_t *src, int32_t count, char *out,
                                 int32_t cap, uint32_t *used) {
  return ((int32_t(WIN *)(uint32_t, uint32_t, const uint16_t *, int32_t, char *,
                          int32_t, const char *,
                          uint32_t *))BB_CODE(BB_POINTER(0x6ca430)))(
      cp, flags, src, count, out, cap, NULL, used);
}
static int32_t byte_wide(uint32_t cp, uint32_t flags, const char *src,
                         int32_t count, uint16_t *out, int32_t cap) {
  return ((int32_t(WIN *)(uint32_t, uint32_t, const char *, int32_t, uint16_t *,
                          int32_t))BB_CODE(BB_POINTER(0x6ca48c)))(
      cp, flags, src, count, out, cap);
}
#endif
static void assertion(uint32_t file, uint32_t line, uint32_t text) {
#ifdef BB_MODEL_RUNTIME
  if (BB_GAME_CALL(0x00470ef0u, 0, 2, file, line, 0, text) == 1)
#ifdef _WIN32
    bb_host_debug_break();
#else
    raise(SIGTRAP);
#endif
#else
  if (bb_crt_errors_report_entry(2, STR(file), line, NULL, STR(text)) == 1)
    __asm__ volatile("int3");
#endif
}
static int32_t encoding_error(void) {
  BB_U32(0x48cba8) = 42;
  return -1;
}
int32_t bb_crt_locale_mbtowc_entry(uint16_t *out, const char *src,
                                   uint32_t size) {
  if (BB_U32(0x48d35c) != 1 && BB_U32(0x48d35c) != 2)
    assertion(0x483514, 77, 0x483520);
  if (!src || !size)
    return 0;
  if (!BYTE(src)) {
    if (out)
      WORD(out) = 0;
    return 0;
  }
  if (!BB_U32(0x48cfc0)) {
    if (out)
      WORD(out) = BYTE(src);
    return 1;
  }
  if (!(WORD((uintptr_t)BB_POINTER(0x48d150) + BYTE(src) * 2) & 0x8000)) {
    if (!byte_wide(BB_U32(0x48cfd0), 9, src, 1, out, out != NULL))
      return encoding_error();
    return 1;
  }
  if (BB_I32(0x48d35c) < 2 || (int32_t)size < BB_I32(0x48d35c) ||
      !byte_wide(BB_U32(0x48cfd0), 9, src, BB_U32(0x48d35c), out,
                 out != NULL)) {
    if (size < BB_U32(0x48d35c) || !BYTE(src + 1))
      return encoding_error();
  }
  return BB_U32(0x48d35c);
}
uint32_t bb_crt_locale_wcstombs_entry(char *out, const uint16_t *src,
                                      uint32_t cap) {
  uint32_t count = 0, used = 0;
  if (out && !cap)
    return 0;
  if (!src)
    assertion(0x483588, 122, 0x483594);
  if (!out) {
    if (!BB_U32(0x48cfc0))
      return bb_crt_wide_length_entry(src);
    int32_t n = bb_crt_locale_wide_bytes(BB_U32(0x48cfd0), 0x220, src, -1, NULL,
                                         0, &used);
    return !n || used ? (uint32_t)encoding_error() : (uint32_t)n - 1;
  }
  if (!BB_U32(0x48cfc0)) {
    while (count < cap) {
      uint16_t c = WORD(src);
      if (c > 255)
        return encoding_error();
      BYTE(out + count) = c;
      if (!WORD(src))
        return count;
      src++;
      count++;
    }
    return count;
  }
  if (BB_U32(0x48d35c) == 1) {
    if (cap)
      cap = bb_crt_wide_count_entry(src, cap);
    count = bb_crt_locale_wide_bytes(BB_U32(0x48cfd0), 0x220, src, cap, out,
                                     cap, &used);
    if (!count || used)
      return encoding_error();
    return BYTE(out + count - 1) ? count : count - 1;
  }
  count = bb_crt_locale_wide_bytes(BB_U32(0x48cfd0), 0x220, src, -1, out, cap,
                                   &used);
  if (count && !used)
    return count - 1;
#ifdef BB_MODEL_RUNTIME
  if (used || BB_GAME_CALL(BB_U32(0x6ca43c), 0) != 122)
#else
  if (used || ((uint32_t(WIN *)(void))BB_CODE(BB_POINTER(0x6ca43c)))() != 122)
#endif
    return encoding_error();
  while (count < cap) {
    char bytes[4];
    int32_t n = bb_crt_locale_wide_bytes(BB_U32(0x48cfd0), 0, src, 1, bytes,
                                         BB_U32(0x48d35c), &used);
    if (!n || used)
      return encoding_error();
    if (cap < (uint32_t)n + count)
      return count;
    for (int32_t i = 0; i < n; ++i) {
      BYTE(out + count) = BYTE(bytes + i);
      if (!BYTE(out + count))
        return count;
      ++count;
    }
    ++src;
  }
  return count;
}
#ifndef BB_MODEL_RUNTIME
static int32_t map_ansi(uint32_t locale, uint32_t flags, const char *src,
                        int32_t count, char *out, int32_t cap) {
  return ((int32_t(WIN *)(uint32_t, uint32_t, const char *, int32_t, char *,
                          int32_t))BB_CODE(BB_POINTER(0x6ca3f8)))(
      locale, flags, src, count, out, cap);
}
static int32_t map_wide(uint32_t locale, uint32_t flags, const uint16_t *src,
                        int32_t count, void *out, int32_t cap) {
  return ((int32_t(WIN *)(uint32_t, uint32_t, const uint16_t *, int32_t, void *,
                          int32_t))BB_CODE(BB_POINTER(0x6ca3f4)))(
      locale, flags, src, count, out, cap);
}
static int32_t type_ansi(uint32_t locale, uint32_t type, const char *src,
                         int32_t count, uint16_t *out) {
  return ((int32_t(WIN *)(uint32_t, uint32_t, const char *, int32_t,
                          uint16_t *))BB_CODE(BB_POINTER(0x6ca3f0)))(
      locale, type, src, count, out);
}
static int32_t type_wide(uint32_t type, const uint16_t *src, int32_t count,
                         uint16_t *out) {
  return ((int32_t(WIN *)(uint32_t, const uint16_t *, int32_t, uint16_t *))
              BB_CODE(BB_POINTER(0x6ca3ec)))(type, src, count, out);
}
static int32_t compare_ansi(uint32_t locale, uint32_t flags, const char *a,
                            int32_t na, const char *b, int32_t nb) {
  return ((int32_t(WIN *)(uint32_t, uint32_t, const char *, int32_t,
                          const char *, int32_t))BB_CODE(BB_POINTER(0x6ca3e0)))(
      locale, flags, a, na, b, nb);
}
static int32_t compare_wide(uint32_t locale, uint32_t flags, const uint16_t *a,
                            int32_t na, const uint16_t *b, int32_t nb) {
  return ((int32_t(WIN *)(
      uint32_t, uint32_t, const uint16_t *, int32_t, const uint16_t *,
      int32_t))BB_CODE(BB_POINTER(0x6ca3dc)))(locale, flags, a, na, b, nb);
}
#endif
static int32_t map_string(uint32_t locale, uint32_t flags, const char *src,
                          int32_t count, char *out, int32_t cap, uint32_t cp,
                          int32_t last) {
  if (!BB_U32(0x48e204)) {
    last = map_ansi(0, 0x100, STR(0x483498), 1, NULL, 0);
    if (!last) {
      last = map_wide(0, 0x100, (void *)0x48349c, 1, NULL, 0);
      if (!last)
        return 0;
      BB_U32(0x48e204) = 1;
    } else
      BB_U32(0x48e204) = 2;
  }
  if (count > 0) {
    last = bb_crt_string_count_entry(src, count);
    count = last;
  }
  if (BB_U32(0x48e204) == 2)
    return map_ansi(locale, flags, src, count, out, cap);
  if (BB_U32(0x48e204) != 1)
    return last;
  if (!cp)
    cp = BB_U32(0x48cfd0);
  int32_t size = byte_wide(cp, 9, src, count, NULL, 0);
  if (!size)
    return 0;
  uint16_t *wide =
      bb_crt_malloc_debug_entry((uint32_t)size * 2, 2, STR(0x48348c), 365);
  if (!wide)
    return 0;
  uint16_t *mapped = NULL;
  int32_t result = 0;
  if (byte_wide(cp, 1, src, count, wide, size)) {
    int32_t target = map_wide(locale, flags, wide, size, NULL, 0);
    if (target) {
      if (flags & 0x400) {
        if (!cap)
          result = target;
        else if (cap >= target && map_wide(locale, flags, wide, size, out, cap))
          result = target;
      } else {
        mapped = bb_crt_malloc_debug_entry((uint32_t)target * 2, 2,
                                           STR(0x48348c), 401);
        if (mapped && map_wide(locale, flags, wide, size, mapped, target))
          result = bb_crt_locale_wide_bytes(cp, 0x220, mapped, target,
                                            cap ? out : NULL, cap, NULL);
      }
    }
  }
  bb_crt_free_debug_entry(wide, 2);
  bb_crt_free_debug_entry(mapped, 2);
  return result;
}
static int32_t character_type(uint32_t type, const char *src, int32_t count,
                              uint16_t *out, uint32_t cp, uint32_t locale,
                              int32_t last) {
  if (!BB_U32(0x48e464)) {
    uint16_t probe[2];
    last = type_ansi(0, 1, STR(0x483498), 1, probe);
    if (!last) {
      last = type_wide(1, (void *)0x48349c, 1, probe);
      if (!last)
        return 0;
      BB_U32(0x48e464) = 1;
    } else
      BB_U32(0x48e464) = 2;
  }
  if (BB_U32(0x48e464) == 2) {
    if (!locale)
      locale = BB_U32(0x48cfc0);
    return type_ansi(locale, type, src, count, out);
  }
  if (BB_U32(0x48e464) != 1)
    return last;
  int32_t result = 0;
  uint16_t *wide = NULL;
  if (!cp)
    cp = BB_U32(0x48cfd0);
  int32_t size = byte_wide(cp, 9, src, count, NULL, 0);
  if (size) {
    wide = bb_crt_calloc_debug_entry(2, size, 2, STR(0x483508), 260);
    if (wide) {
      int32_t converted = byte_wide(cp, 1, src, count, wide, size);
      if (converted)
        result = type_wide(type, wide, converted, out);
    }
  }
  bb_crt_free_debug_entry(wide, 2);
  return result;
}
static int32_t compare_strings(uint32_t locale, uint32_t flags, const char *a,
                               int32_t na, const char *b, int32_t nb,
                               uint32_t cp, int32_t last) {
  if (!BB_U32(0x48e730)) {
    last = compare_ansi(0, 0, STR(0x483498), 1, STR(0x483498), 1);
    if (!last) {
      last = compare_wide(0, 0, (void *)0x48349c, 1, (void *)0x48349c, 1);
      if (!last)
        return 0;
      BB_U32(0x48e730) = 1;
    } else
      BB_U32(0x48e730) = 2;
  }
  if (na > 0) {
    last = bb_crt_string_count_alt_entry(a, na);
    na = last;
  }
  if (nb > 0) {
    last = bb_crt_string_count_alt_entry(b, nb);
    nb = last;
  }
  if (BB_U32(0x48e730) == 2)
    return compare_ansi(locale, flags, a, na, b, nb);
  if (BB_U32(0x48e730) != 1)
    return last;
  if (!cp)
    cp = BB_U32(0x48cfd0);
  if (!na || !nb) {
    if (na == nb)
      return 2;
    if (nb > 1)
      return 1;
    if (na > 1)
      return 3;
    uint8_t info[20];
#ifdef BB_MODEL_RUNTIME
    struct bb_model_buffer cp_info;
    bb_model_buffer_begin(&cp_info, info, sizeof info);
    int32_t got = BB_GAME_CALL(BB_U32(0x6ca42c), 0, cp, cp_info.reference);
    bb_model_buffer_end(&cp_info, 1);
    if (!got)
      return 0;
#else
    if (!((int32_t(WIN *)(uint32_t, void *))BB_CODE(BB_POINTER(0x6ca42c)))(
            cp, info))
      return 0;
#endif
    if (!((!na && nb == 1) || (na == 1 && !nb)))
      assertion(0x4835d8, 354, 0x4835e4);
    if (na > 0 || nb > 0) {
      const char *src = na > 0 ? a : b;
      int32_t unequal = na > 0 ? 3 : 1;
      if (BB_U32(info) < 2)
        return unequal;
      uintptr_t range = (uintptr_t)info + 6;
      while (BYTE(range) && BYTE(range + 1)) {
        if (BYTE(range) <= BYTE(src) && BYTE(src) <= BYTE(range + 1))
          return 2;
        range += 2;
      }
      return unequal;
    }
  }
  int32_t result = 0;
  uint16_t *first = NULL, *second = NULL;
  int32_t sa = byte_wide(cp, 9, a, na, NULL, 0);
  if (!sa)
    return 0;
  first = bb_crt_malloc_debug_entry((uint32_t)sa * 2, 2, STR(0x4835d8), 394);
  if (!first)
    return 0;
  if (byte_wide(cp, 1, a, na, first, sa)) {
    int32_t sb = byte_wide(cp, 9, b, nb, NULL, 0);
    if (sb) {
      second =
          bb_crt_malloc_debug_entry((uint32_t)sb * 2, 2, STR(0x4835d8), 409);
      if (second && byte_wide(cp, 1, b, nb, second, sb))
        result = compare_wide(locale, flags, first, sa, second, sb);
    }
  }
  bb_crt_free_debug_entry(first, 2);
  bb_crt_free_debug_entry(second, 2);
  return result;
}
/* Preserve the incoming register value used by the original private cache's
 * unspecified-selection path, without reading an uninitialized C variable. */
void __attribute__((used, noinline)) bb_crt_locale_map_c(uint32_t *r) {
  uintptr_t a = r[3] + 8;
  r[7] = map_string(BB_U32(a), BB_U32(a + 4), BB_POINTER(a + 8), BB_I32(a + 12),
                    BB_POINTER(a + 16), BB_I32(a + 20), BB_U32(a + 24), r[7]);
}
void __attribute__((used, noinline)) bb_crt_locale_type_c(uint32_t *r) {
  uintptr_t a = r[3] + 8;
  r[7] =
      character_type(BB_U32(a), BB_POINTER(a + 4), BB_I32(a + 8),
                     BB_POINTER(a + 12), BB_U32(a + 16), BB_U32(a + 20), r[7]);
}
void __attribute__((used, noinline)) bb_crt_locale_compare_c(uint32_t *r) {
  uintptr_t a = r[3] + 8;
  r[7] = compare_strings(BB_U32(a), BB_U32(a + 4), BB_POINTER(a + 8),
                         BB_I32(a + 12), BB_POINTER(a + 16), BB_I32(a + 20),
                         BB_U32(a + 24), r[7]);
}
#ifdef BB_MODEL_RUNTIME
int32_t bb_crt_locale_map_entry(uint32_t locale, uint32_t flags,
                                const char *src, int32_t count, char *out,
                                int32_t cap, uint32_t cp) {
  return map_string(locale, flags, src, count, out, cap, cp, 0);
}
int32_t bb_crt_locale_type_entry(uint32_t type, const char *src, int32_t count,
                                 uint16_t *out, uint32_t cp, uint32_t locale) {
  return character_type(type, src, count, out, cp, locale, 0);
}
int32_t bb_crt_locale_compare_entry(uint32_t locale, uint32_t flags,
                                    const char *a, int32_t na, const char *b,
                                    int32_t nb, uint32_t cp) {
  return compare_strings(locale, flags, a, na, b, nb, cp, 0);
}
#else
#ifdef _WIN32
#define BB_ASM(n) "_" #n
#else
#define BB_ASM(n) #n
#endif
#define WRAP(name, signature)                                                  \
  int32_t __attribute__((naked, noinline)) name##_entry signature {            \
    __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(              \
        name##_c) ";addl $4,%esp;popal;popfl;ret");                            \
  }
#define UNUSED __attribute__((unused))
WRAP(bb_crt_locale_map,
     (uint32_t a UNUSED, uint32_t b UNUSED, const char *c UNUSED,
      int32_t d UNUSED, char *e UNUSED, int32_t f UNUSED, uint32_t g UNUSED))
WRAP(bb_crt_locale_type,
     (uint32_t a UNUSED, const char *b UNUSED, int32_t c UNUSED,
      uint16_t *d UNUSED, uint32_t e UNUSED, uint32_t f UNUSED))
WRAP(bb_crt_locale_compare,
     (uint32_t a UNUSED, uint32_t b UNUSED, const char *c UNUSED,
      int32_t d UNUSED, const char *e UNUSED, int32_t f UNUSED,
      uint32_t g UNUSED))

#endif

int32_t bb_crt_locale_collate_entry(const char *a, const char *b,
                                    uint32_t count) {
  if (!count)
    return 0;
  int32_t result = bb_crt_locale_compare_entry(BB_U32(0x48d520), 1, a, count, b,
                                               count, BB_U32(0x48d51c));
  return result ? (int32_t)((uint32_t)result - 2) : INT32_MAX;
}
char *bb_crt_locale_getenv_entry(const char *name) {
  uintptr_t table = BB_U32(0x48cbd0);
  if (!table && BB_U32(0x48cbd8)) {
    if (bb_crt_locale_convert_environment_entry())
      return NULL;
    table = BB_U32(0x48cbd0);
  }
  BB_U32(0x48cbd0) = table;
  if (table && name) {
    uint32_t length = bb_crt_string_length_entry(name);
    for (; BB_U32(table); table += 4) {
      const char *entry = BB_POINTER(table);
      if (length < bb_crt_string_length_entry(entry) &&
          BYTE((uintptr_t)BB_POINTER(table) + length) == '=' &&
          !bb_crt_locale_collate_entry(BB_POINTER(table), name, length))
        return (char *)BB_POINTER(table) + length + 1;
    }
  }
  return NULL;
}
int32_t bb_crt_locale_convert_environment_entry(void) {
  for (uintptr_t table = BB_U32(0x48cbd8); BB_U32(table); table += 4) {
    int32_t size =
        bb_crt_locale_wide_bytes(1, 0, BB_POINTER(table), -1, NULL, 0, NULL);
    if (!size)
      return -1;
    char *option = bb_crt_malloc_debug_entry(size, 2, STR(0x4835b8), 61);
    if (!option)
      return -1;
    if (!bb_crt_locale_wide_bytes(1, 0, BB_POINTER(table), -1, option, size,
                                  NULL))
      return -1;
    bb_crt_locale_setenv_entry(option, 0);
  }
  return 0;
}
int32_t bb_crt_locale_findenv_entry(const char *name, uint32_t length) {
  uintptr_t table = BB_U32(0x48cbd0);
  while (BB_U32(table)) {
    if (!bb_crt_locale_collate_entry(name, BB_POINTER(table), length) &&
        (BYTE((uintptr_t)BB_POINTER(table) + length) == '=' ||
         !BYTE((uintptr_t)BB_POINTER(table) + length)))
      return (int32_t)(table - BB_U32(0x48cbd0)) >> 2;
    table += 4;
  }
  return (int32_t)(0u - (uint32_t)((int32_t)(table - BB_U32(0x48cbd0)) >> 2));
}
void *bb_crt_locale_copy_environment_entry(const void *environment) {
  if (!environment)
    return NULL;
  uintptr_t source = (uintptr_t)environment;
  uint32_t count = 0;
  for (uintptr_t table = source; BB_U32(table); table += 4)
    ++count;
  uint32_t *copy =
      bb_crt_malloc_debug_entry(count * 4 + 4, 2, STR(0x483624), 326);
  if (!copy)
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x0046d620u, 0, 9);
#else
    bb_crt_boot_fatal_entry(9);
#endif
  uintptr_t dest = (uintptr_t)copy;
  for (; BB_U32(source); source += 4, dest += 4) {
    uint32_t size = bb_crt_string_length_entry(BB_POINTER(source)) + 1;
    BB_U32(dest) = bb_address_encode(
        bb_crt_malloc_debug_entry(size, 2, STR(0x483624), 335));
    if (BB_U32(dest))
      bb_crt_string_copy_entry(BB_POINTER(dest), BB_POINTER(source));
  }
  BB_U32(dest) = 0;
  return copy;
}
char *bb_crt_locale_find_character_entry(const char *src, uint32_t ch) {
  if (!BB_U32(0x48d51c))
    return bb_crt_string_find_entry(src, ch);
  while (BYTE(src)) {
    uint32_t c = BYTE(src);
    if (BYTE(0x48d419 + c) & 4) {
      if (!BYTE(src + 1))
        return NULL;
      if (((c << 8) | BYTE(src + 1)) == ch)
        return (char *)src;
      src += 2;
    } else {
      if (c == ch)
        return (char *)src;
      ++src;
    }
  }
  return ch ? NULL : (char *)src;
}
int32_t bb_crt_locale_setenv_entry(char *option, int32_t primary) {
  if (!option)
    return -1;
  char *equals = bb_crt_locale_find_character_entry(option, '=');
  if (!equals || equals == option)
    return -1;
  int deleting = !BYTE(equals + 1);
  if (BB_U32(0x48cbd4) == BB_U32(0x48cbd0))
    BB_U32(0x48cbd0) = bb_address_encode(
        bb_crt_locale_copy_environment_entry(BB_POINTER(0x48cbd0)));
  if (!BB_U32(0x48cbd0)) {
    if (!primary || !BB_U32(0x48cbd8)) {
      if (deleting)
        return 0;
      BB_U32(0x48cbd0) = bb_address_encode(
          bb_crt_malloc_debug_entry(4, 2, STR(0x483624), 135));
      if (!BB_U32(0x48cbd0))
        return -1;
      BB_U32(BB_U32(0x48cbd0)) = 0;
      if (!BB_U32(0x48cbd8)) {
        BB_U32(0x48cbd8) = bb_address_encode(
            bb_crt_malloc_debug_entry(4, 2, STR(0x483624), 142));
        if (!BB_U32(0x48cbd8))
          return -1;
        BB_U32(BB_U32(0x48cbd8)) = 0;
      }
    } else if (bb_crt_locale_convert_environment_entry())
      return -1;
  }
  uintptr_t table = BB_U32(0x48cbd0);
  int32_t index = bb_crt_locale_findenv_entry(option, (uintptr_t)equals -
                                                          (uintptr_t)option);
  if (index < 0 || !BB_U32(table)) {
    if (deleting)
      return 0;
    if (index < 0)
      index = (int32_t)(0u - (uint32_t)index);
    table = bb_address_encode(bb_crt_reallocate_debug_entry(
        (void *)table, (uint32_t)index * 4 + 8, 2, STR(0x483624), 206));
    if (!table)
      return -1;
    BB_U32(table + (uint32_t)index * 4) = bb_address_encode(option);
    BB_U32(table + (uint32_t)index * 4 + 4) = 0;
    BB_U32(0x48cbd0) = table;
  } else if (deleting) {
    bb_crt_free_debug_entry(BB_POINTER(table + (uint32_t)index * 4), 2);
    while (BB_U32(table + (uint32_t)index * 4)) {
      BB_U32(table + (uint32_t)index * 4) =
          BB_U32(table + (uint32_t)index * 4 + 4);
      index = (int32_t)((uint32_t)index + 1);
    }
    void *resized = bb_crt_reallocate_debug_entry(
        (void *)table, (uint32_t)index * 4, 2, STR(0x483624), 185);
    if (resized)
      BB_U32(0x48cbd0) = bb_address_encode(resized);
  } else
    BB_U32(table + (uint32_t)index * 4) = bb_address_encode(option);
  if (primary) {
    char *name = bb_crt_malloc_debug_entry(
        bb_crt_string_length_entry(option) + 2, 2, STR(0x483624), 229);
    if (name) {
      bb_crt_string_copy_entry(name, option);
      uint32_t length = (uintptr_t)equals - (uintptr_t)option;
      BYTE(name + length) = 0;
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(BB_U32(0x6ca3d8), 0, bb_address_encode(name),
                   deleting ? 0 : bb_address_encode(name) + length + 1);
#else
      ((int32_t(WIN *)(const char *, const char *))BB_CODE(
          BB_POINTER(0x6ca3d8)))(name, deleting ? NULL : name + length + 1);
#endif
      bb_crt_free_debug_entry(name, 2);
    }
  }
  return 0;
}
