/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_bootstrap.h"
#include "crt_debug_heap.h"
#include "crt_locale.h"
#include "crt_primitives.h"
#include "game_calls.h"
#ifdef BB_MODEL_RUNTIME
#include "model_buffer.h"
#define FATAL(code) BB_GAME_CALL(0x0046d620u, 0, code)
#else
#define FATAL bb_crt_boot_fatal_entry
#endif
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define WIN BB_STDCALL
#define STR(a) ((const char *)(uintptr_t)(a))
uint32_t bb_crt_boot_byte_type_entry(uint32_t c, uint32_t mask,
                                     uint32_t byte_mask) {
  c = (uint8_t)c;
  return ((uint8_t)byte_mask & BYTE(0x48d419 + c)) ||
                 (mask && (WORD(0x48d15a + c * 2) & mask))
             ? 1
             : 0;
}
uint32_t bb_crt_boot_lead_entry(uint32_t c) {
  return bb_crt_boot_byte_type_entry(c, 0, 4);
}
uint32_t bb_crt_boot_system_codepage_entry(uint32_t cp) {
  BB_U32(0x48d534) = 0;
  if (cp == 0xfffffffe) {
    BB_U32(0x48d534) = 1;
#ifdef BB_MODEL_RUNTIME
    cp = BB_GAME_CALL(BB_U32(0x6ca424), 0);
#else
    cp = ((uint32_t(WIN *)(void))BB_CODE(BB_POINTER(0x6ca424)))();
#endif
  } else if (cp == 0xfffffffd) {
    BB_U32(0x48d534) = 1;
#ifdef BB_MODEL_RUNTIME
    cp = BB_GAME_CALL(BB_U32(0x6ca428), 0);
#else
    cp = ((uint32_t(WIN *)(void))BB_CODE(BB_POINTER(0x6ca428)))();
#endif
  } else if (cp == 0xfffffffc) {
    BB_U32(0x48d534) = 1;
    cp = BB_U32(0x48cfd0);
  }
  return cp;
}
uint32_t bb_crt_boot_codepage_locale_entry(uint32_t cp) {
  switch (cp) {
  case 932:
    return 0x411;
  case 936:
    return 0x804;
  case 949:
    return 0x412;
  case 950:
    return 0x404;
  default:
    return 0;
  }
}
void bb_crt_boot_clear_codepage_entry(void) {
  for (unsigned n = 0; n < 257; ++n)
    BYTE(0x48d418 + n) = 0;
  BB_U32(0x48d51c) = BB_U32(0x48d520) = 0;
  for (unsigned n = 0; n < 6; ++n)
    WORD(0x48d528 + n * 2) = 0;
}
int32_t bb_crt_boot_codepage_entry(int32_t requested) {
  uint32_t cp = bb_crt_boot_system_codepage_entry(requested);
  if (cp == BB_U32(0x48d51c))
    return 0;
  if (!cp) {
    bb_crt_boot_clear_codepage_entry();
    return 0;
  }
  for (unsigned n = 0; n < 5; ++n)
    if (BB_U32(0x48d540 + n * 48) == cp) {
      for (unsigned i = 0; i < 257; ++i)
        BYTE(0x48d418 + i) = 0;
      for (unsigned group = 0; group < 4; ++group) {
        uintptr_t range = 0x48d550 + n * 48 + group * 8;
        while (BYTE(range) && BYTE(range + 1)) {
          for (unsigned c = BYTE(range); c <= BYTE(range + 1); ++c)
            BYTE(0x48d419 + c) |= BYTE(0x48d538 + group);
          range += 2;
        }
      }
      BB_U32(0x48d51c) = cp;
      BB_U32(0x48d520) = bb_crt_boot_codepage_locale_entry(cp);
      for (unsigned i = 0; i < 6; ++i)
        WORD(0x48d528 + i * 2) = WORD(0x48d544 + n * 48 + i * 2);
      return 0;
    }
  uint8_t info[20];
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer cp_info;
  bb_model_buffer_begin(&cp_info, info, sizeof info);
  int32_t result = BB_GAME_CALL(BB_U32(0x6ca42c), 0, cp, cp_info.reference);
  bb_model_buffer_end(&cp_info, 1);
#else
  int32_t result =
      ((int32_t(WIN *)(uint32_t, void *))BB_CODE(BB_POINTER(0x6ca42c)))(cp,
                                                                        info);
#endif
  if (result == 1) {
    for (unsigned n = 0; n < 257; ++n)
      BYTE(0x48d418 + n) = 0;
    if (BB_U32(info) < 2) {
      BB_U32(0x48d51c) = BB_U32(0x48d520) = 0;
    } else {
      uintptr_t range = (uintptr_t)info + 6;
      while (BYTE(range) && BYTE(range + 1)) {
        for (unsigned c = BYTE(range); c <= BYTE(range + 1); ++c)
          BYTE(0x48d419 + c) |= 4;
        range += 2;
      }
      for (unsigned c = 1; c < 255; ++c)
        BYTE(0x48d419 + c) |= 8;
      BB_U32(0x48d51c) = cp;
      BB_U32(0x48d520) = bb_crt_boot_codepage_locale_entry(cp);
    }
    for (unsigned n = 0; n < 6; ++n)
      WORD(0x48d528 + n * 2) = 0;
    return 0;
  }
  if (!BB_U32(0x48d534))
    return -1;
  bb_crt_boot_clear_codepage_entry();
  return 0;
}
void bb_crt_boot_initialize_codepage_entry(void) {
  bb_crt_boot_codepage_entry(-3);
}
void bb_crt_boot_parse_arguments_entry(const uint8_t *command, uint32_t *argv,
                                       uint8_t *text, uint32_t *argc,
                                       uint32_t *bytes) {
  uintptr_t p = (uintptr_t)command, a = (uintptr_t)argv, out = (uintptr_t)text;
  BB_U32(bytes) = 0;
  BB_U32(argc) = 1;
  if (a) {
    BB_U32(a) = bb_address_encode((void *)out);
    a += 4;
  }
  if (BYTE(p) == '"') {
    uintptr_t next;
    while (1) {
      next = p + 1;
      if (BYTE(next) == '"' || !BYTE(next))
        break;
      if (BYTE(0x48d419 + BYTE(next)) & 4) {
        ++BB_U32(bytes);
        if (out) {
          BYTE(out++) = BYTE(next);
          next = p + 2;
        }
      }
      p = next;
      ++BB_U32(bytes);
      if (out)
        BYTE(out++) = BYTE(p);
    }
    ++BB_U32(bytes);
    if (out)
      BYTE(out++) = 0;
    if (BYTE(next) == '"')
      next = p + 2;
    p = next;
  } else {
    uint8_t c;
    do {
      ++BB_U32(bytes);
      if (out)
        BYTE(out++) = BYTE(p);
      c = BYTE(p);
      uintptr_t next = p + 1;
      if (BYTE(0x48d419 + c) & 4) {
        ++BB_U32(bytes);
        if (out)
          BYTE(out++) = BYTE(p + 1);
        next = p + 2;
      }
      p = next;
    } while (c != ' ' && c != 9 && c);
    if (!c)
      --p;
    else if (out)
      BYTE(out - 1) = 0;
  }
  int quoted = 0;
  while (1) {
    if (BYTE(p))
      while (BYTE(p) == ' ' || BYTE(p) == 9)
        ++p;
    if (!BYTE(p))
      break;
    if (a) {
      BB_U32(a) = bb_address_encode((void *)out);
      a += 4;
    }
    ++BB_U32(argc);
    while (1) {
      int copying = 1;
      uint32_t slashes = 0;
      while (BYTE(p) == '\\') {
        ++p;
        ++slashes;
      }
      if (BYTE(p) == '"') {
        if (!(slashes & 1)) {
          if (quoted) {
            copying = BYTE(p + 1) == '"';
            if (copying)
              ++p;
          } else
            copying = 0;
          quoted = !quoted;
        }
        slashes >>= 1;
      }
      while (slashes--) {
        if (out)
          BYTE(out++) = '\\';
        ++BB_U32(bytes);
      }
      if (!BYTE(p) || (!quoted && (BYTE(p) == ' ' || BYTE(p) == 9)))
        break;
      if (copying) {
        if (!out) {
          if (BYTE(0x48d419 + BYTE(p)) & 4) {
            ++p;
            ++BB_U32(bytes);
          }
        } else {
          if (BYTE(0x48d419 + BYTE(p)) & 4) {
            BYTE(out++) = BYTE(p++);
            ++BB_U32(bytes);
          }
          BYTE(out++) = BYTE(p);
        }
        ++BB_U32(bytes);
      }
      ++p;
    }
    if (out)
      BYTE(out++) = 0;
    ++BB_U32(bytes);
  }
  if (a)
    BB_U32(a) = 0;
  ++BB_U32(argc);
}
void *bb_crt_boot_arguments_entry(void) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x6ca440), 0, 0, 0x6c88a0, 260);
#else
  ((uint32_t(WIN *)(void *, char *, uint32_t))BB_CODE(BB_POINTER(0x6ca440)))(
      NULL, (char *)0x6c88a0, 260);
#endif
  BB_U32(0x48cbe0) = 0x6c88a0;
  const uint8_t *command =
      BYTE(BB_U32(0x6c9bbc)) ? BB_POINTER(0x6c9bbc) : (const uint8_t *)0x6c88a0;
  uint32_t count, bytes;
  bb_crt_boot_parse_arguments_entry(command, NULL, NULL, &count, &bytes);
  void *block =
      bb_crt_malloc_debug_entry(count * 4 + bytes, 2, STR(0x483010), 117);
  if (!block)
    FATAL(8);
  bb_crt_boot_parse_arguments_entry(
      command, block, (void *)((uintptr_t)block + count * 4), &count, &bytes);
  BB_U32(0x48cbc4) = count - 1;
  BB_SET_POINTER(0x48cbc8, block);
  return block;
}
void *bb_crt_boot_environment_entry(void) {
  uint32_t count = 0;
  uintptr_t p = BB_U32(0x48cc50);
  while (BYTE(p)) {
    if (BYTE(p) != '=')
      ++count;
    p += bb_crt_string_length_entry((const char *)p) + 1;
  }
  uintptr_t out =
      (uintptr_t)bb_crt_malloc_debug_entry(count * 4 + 4, 2, STR(0x483004), 85);
  BB_U32(0x48cbd0) = bb_address_encode((void *)out);
  if (!out)
    FATAL(9);
  p = BB_U32(0x48cc50);
  while (BYTE(p)) {
    uint32_t size = bb_crt_string_length_entry((const char *)p);
    if (BYTE(p) != '=') {
      BB_SET_POINTER(out,
                     bb_crt_malloc_debug_entry(size + 1, 2, STR(0x483004), 97));
      if (!BB_U32(out))
        FATAL(9);
      bb_crt_string_copy_entry(BB_POINTER(out), (const char *)p);
      out += 4;
    }
    p += size + 1;
  }
  bb_crt_free_debug_entry(BB_POINTER(0x48cc50), 2);
  BB_U32(0x48cc50) = 0;
  BB_U32(out) = 0;
  return (void *)out;
}
void *bb_crt_boot_capture_environment_entry(void) {
  uint16_t *wide = NULL;
  char *ansi = NULL;
  if (!BB_U32(0x48d410)) {
#ifdef BB_MODEL_RUNTIME
    wide = (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca434), 0);
#else
    wide = ((uint16_t *(WIN *)(void))BB_CODE(BB_POINTER(0x6ca434)))();
#endif
    if (!wide) {
#ifdef BB_MODEL_RUNTIME
      ansi = (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca488), 0);
#else
      ansi = ((char *(WIN *)(void))BB_CODE(BB_POINTER(0x6ca488)))();
#endif
      if (!ansi)
        return NULL;
      BB_U32(0x48d410) = 2;
    } else
      BB_U32(0x48d410) = 1;
  }
  if (BB_U32(0x48d410) == 1) {
    if (!wide) {
#ifdef BB_MODEL_RUNTIME
      wide = (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca434), 0);
#else
      wide = ((uint16_t *(WIN *)(void))BB_CODE(BB_POINTER(0x6ca434)))();
#endif
      if (!wide)
        return NULL;
    }
    uintptr_t end = (uintptr_t)wide;
    while (WORD(end)) {
      end += 2;
      if (!WORD(end))
        end += 2;
    }
    int32_t chars = ((int32_t)(end - (uintptr_t)wide) >> 1) + 1;
    int32_t size = bb_crt_locale_wide_bytes(0, 0, wide, chars, NULL, 0, NULL);
    char *out = NULL;
    if (size)
      out = bb_crt_malloc_debug_entry(size, 2, STR(0x48301c), 251);
    if (out) {
      if (!bb_crt_locale_wide_bytes(0, 0, wide, chars, out, size, NULL)) {
        bb_crt_free_debug_entry(out, 2);
        out = NULL;
      }
    }
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(BB_U32(0x6ca484), 0, bb_address_encode(wide));
#else
    ((int32_t(WIN *)(uint16_t *))BB_CODE(BB_POINTER(0x6ca484)))(wide);
#endif
    return out;
  }
  if (BB_U32(0x48d410) == 2) {
    if (!ansi) {
#ifdef BB_MODEL_RUNTIME
      ansi = (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca488), 0);
#else
      ansi = ((char *(WIN *)(void))BB_CODE(BB_POINTER(0x6ca488)))();
#endif
      if (!ansi)
        return NULL;
    }
    uintptr_t end = (uintptr_t)ansi;
    while (BYTE(end)) {
      ++end;
      if (!BYTE(end))
        ++end;
    }
    uint32_t size = end - (uintptr_t)ansi + 1;
    void *out = bb_crt_malloc_debug_entry(size, 2, STR(0x48301c), 294);
    if (out)
      bb_crt_memory_copy_entry(out, ansi, size);
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(BB_U32(0x6ca490), 0, bb_address_encode(ansi));
#else
    ((int32_t(WIN *)(char *))BB_CODE(BB_POINTER(0x6ca490)))(ansi);
#endif
    return out;
  }
  return NULL;
}
