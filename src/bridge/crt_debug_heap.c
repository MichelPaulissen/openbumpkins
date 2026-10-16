/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_debug_heap.h"
#include "abi.h"
#include "crt_errors.h"
#include "crt_heap.h"
#include "crt_output.h"
#include "crt_primitives.h"
#include "game_calls.h"
#include "interop.h"
#include <signal.h>
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define STR(a) ((const char *)(uintptr_t)(a))
#define WIN BB_STDCALL
#define REPORT bb_crt_errors_report_entry
#define FLAGS BB_U32(0x48cc68)
#define REQUEST BB_U32(0x48cc6c)
#define FIRST BB_U32(0x6c8878)
#define LAST BB_U32(0x6c8870)
#define TOTAL BB_U32(0x6c8874)
#define CURRENT BB_U32(0x6c887c)
#define HIGH BB_U32(0x6c8880)
#ifdef BB_MODEL_RUNTIME
static uint32_t debug_word(uint32_t value) { return value; }
#define ARG(value)                                                             \
  _Generic((value),                                                            \
      char *: bb_address_encode,                                               \
      const char *: bb_address_encode,                                         \
      void *: bb_address_encode,                                               \
      const void *: bb_address_encode,                                         \
      default: debug_word)(value)
#undef REPORT
#define REPORT(t, f, l, m, s)                                                  \
  BB_GAME_CALL(0x00470ef0u, 0, t, bb_address_encode(f), l,                     \
               bb_address_encode(m), bb_address_encode(s))
#define NOTICE1(f, a)                                                          \
  stop_if(BB_GAME_CALL(0x00470ef0u, 0, 0, 0, 0, 0, f, ARG(a)))
#define NOTICE2(f, a, b)                                                       \
  stop_if(BB_GAME_CALL(0x00470ef0u, 0, 0, 0, 0, 0, f, ARG(a), ARG(b)))
#define NOTICE3(f, a, b, c)                                                    \
  stop_if(BB_GAME_CALL(0x00470ef0u, 0, 0, 0, 0, 0, f, ARG(a), ARG(b), ARG(c)))
#define WARNING1(f, a)                                                         \
  stop_if(BB_GAME_CALL(0x00470ef0u, 0, 1, 0, 0, 0, f, ARG(a)))
#define WARNING3(f, a, b, c)                                                   \
  stop_if(BB_GAME_CALL(0x00470ef0u, 0, 1, 0, 0, 0, f, ARG(a), ARG(b), ARG(c)))
#else
#define NOTICE1(format, ...)                                                   \
  stop_if(REPORT(0, NULL, 0, NULL, STR(format), __VA_ARGS__))
#define NOTICE2 NOTICE1
#define NOTICE3 NOTICE1
#define WARNING1(format, ...)                                                  \
  stop_if(REPORT(1, NULL, 0, NULL, STR(format), __VA_ARGS__))
#define WARNING3 WARNING1
#endif
static void breakpoint(void) {
#ifdef BB_MODEL_RUNTIME
  raise(SIGTRAP);
#else
  __asm__ volatile("int3");
#endif
}
static void stop_if(int32_t result) {
  if (result == 1)
    breakpoint();
}
static void assertion(uint32_t line, uint32_t expression) {
  stop_if(REPORT(2, STR(0x48264c), line, NULL, STR(expression)));
}
static void check(uint32_t line) {
  if ((FLAGS & 4) && !bb_crt_debug_check_memory_entry())
    assertion(line, 0x482658);
}
static void fill(void *p, uint32_t value, uint32_t size) {
  bb_crt_memory_fill_entry(p, value, size);
}
static int valid_type(uint32_t type) {
  return (type & 0xffff) == 4 || type == 1 || (type & 0xffff) == 2 || type == 3;
}
static const char *type_name(uint32_t type) {
  return BB_POINTER(0x48cc80 + (type & 0xffff) * 4);
}
static void account(uint32_t size) {
  TOTAL += size;
  CURRENT += size;
  if (HIGH < CURRENT)
    HIGH = CURRENT;
}
static int32_t client(int32_t operation, const void *p, uint32_t size,
                      uint32_t type, uint32_t request, const char *file,
                      int32_t line) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(0x48d6d0), 0, operation, bb_address_encode(p),
                      size, type, request, bb_address_encode(file), line);
#else
  return ((int32_t (*)(int32_t, const void *, uint32_t, uint32_t, uint32_t,
                       const char *, int32_t))BB_CODE(BB_POINTER(0x48d6d0)))(
      operation, p, size, type, request, file, line);
#endif
}
void *bb_crt_debug_allocate_entry(uint32_t size, uint32_t type,
                                  const char *file, int32_t line) {
  check(0x141);
  uint32_t request = REQUEST;
  if (request == BB_U32(0x48cc70))
    breakpoint();
  if (!client(1, NULL, size, type, request, file, line)) {
    if (file)
      NOTICE2(0x482614, file, line);
    else
      NOTICE1(0x486e1c, STR(0x4825f0));
    return NULL;
  }
  int ignored = (type & 0xffff) != 2 && !(FLAGS & 1);
  if (size >= 0xffffffe1u || size + 36 >= 0xffffffe1u) {
    WARNING1(0x4825cc, size);
    return NULL;
  }
  if (!valid_type(type))
    WARNING1(0x486e1c, STR(0x482598));
  uint32_t h = bb_address_encode(bb_crt_heap_allocate_entry(size + 36));
  if (!h)
    return NULL;
  ++REQUEST;
  if (ignored) {
    BB_U32(h) = BB_U32(h + 4) = BB_U32(h + 8) = 0;
    BB_U32(h + 12) = 0xfedcbabc;
    BB_U32(h + 16) = size;
    BB_U32(h + 20) = 3;
    BB_U32(h + 24) = 0;
  } else {
    account(size);
    if (FIRST)
      BB_U32(FIRST + 4) = h;
    else
      LAST = h;
    BB_U32(h) = FIRST;
    BB_U32(h + 4) = 0;
    BB_U32(h + 8) = bb_address_encode(file);
    BB_U32(h + 12) = line;
    BB_U32(h + 16) = size;
    BB_U32(h + 20) = type;
    BB_U32(h + 24) = request;
    FIRST = h;
  }
  fill((void *)(uintptr_t)(h + 28), BYTE(0x48cc74), 4);
  fill((void *)(uintptr_t)(h + 32 + size), BYTE(0x48cc74), 4);
  fill((void *)(uintptr_t)(h + 32), BYTE(0x48cc7c), size);
  return (void *)(uintptr_t)(h + 32);
}
void *bb_crt_malloc_debug_retry_entry(uint32_t size, int32_t retry,
                                      uint32_t type, const char *file,
                                      int32_t line) {
  for (;;) {
    void *p = bb_crt_debug_allocate_entry(size, type, file, line);
    if (p || !retry || !bb_crt_heap_retry_entry(size))
      return p;
  }
}
void *bb_crt_malloc_entry(uint32_t size) {
  return bb_crt_malloc_debug_retry_entry(size, BB_I32(0x48d6cc), 1, NULL, 0);
}
void *bb_crt_malloc_debug_entry(uint32_t size, uint32_t type, const char *file,
                                int32_t line) {
  return bb_crt_malloc_debug_retry_entry(size, BB_I32(0x48d6cc), type, file,
                                         line);
}
void *bb_crt_malloc_retry_entry(uint32_t size, int32_t retry) {
  return bb_crt_malloc_debug_retry_entry(size, retry, 1, NULL, 0);
}
void *bb_crt_calloc_debug_entry(uint32_t count, uint32_t size, uint32_t type,
                                const char *file, int32_t line) {
  uint32_t n = count * size;
  uint8_t *p = bb_crt_malloc_debug_entry(n, type, file, line);
  if (p)
    for (uint32_t out = bb_address_encode(p), end = out + n; out < end; ++out)
      BYTE(out) = 0;
  return p;
}
void *bb_crt_calloc_entry(uint32_t count, uint32_t size) {
  return bb_crt_calloc_debug_entry(count, size, 1, NULL, 0);
}
void *bb_crt_debug_resize_entry(void *p, uint32_t size, uint32_t type,
                                const char *file, int32_t line,
                                int32_t reallocating) {
  if (!p)
    return bb_crt_malloc_debug_entry(size, type, file, line);
  if (reallocating && !size) {
    bb_crt_free_debug_entry(p, type);
    return NULL;
  }
  check(0x239);
  uint32_t request = REQUEST;
  if (request == BB_U32(0x48cc70))
    breakpoint();
  if (!client(2, p, size, type, request, file, line)) {
    if (file)
      NOTICE2(0x4827d4, file, line);
    else
      NOTICE1(0x486e1c, STR(0x4827b0));
    return NULL;
  }
  if (size >= 0xffffffdcu) {
    WARNING1(0x482780, size);
    return NULL;
  }
  if (type != 1 && (type & 0xffff) != 4 && (type & 0xffff) != 2)
    WARNING1(0x486e1c, STR(0x482598));
  if (!bb_crt_debug_valid_heap_entry(p))
    assertion(0x261, 0x48275c);
  uint32_t old = bb_address_encode(p) - 32u;
  int ignored = BB_U32(old + 20) == 3;
  if (ignored) {
    if (BB_U32(old + 12) != 0xfedcbabc || BB_U32(old + 24))
      assertion(0x26b, 0x482714);
  } else {
    if ((BB_U32(old + 20) & 0xffff) == 2 && (type & 0xffff) == 1)
      type = 2;
    if ((BB_U32(old + 20) & 0xffff) != (type & 0xffff))
      assertion(0x272, 0x4826d8);
  }
  uint32_t h = bb_address_encode(
      reallocating
          ? bb_crt_heap_reallocate_entry((void *)(uintptr_t)old, size + 36)
          : bb_crt_heap_expand_entry((void *)(uintptr_t)old, size + 36));
  if (!h)
    return NULL;
  ++REQUEST;
  if (!ignored) {
    TOTAL -= BB_U32(h + 16);
    CURRENT -= BB_U32(h + 16);
    account(size);
  }
  if (BB_U32(h + 16) < size)
    fill((void *)(uintptr_t)(h + 32 + BB_U32(h + 16)), BYTE(0x48cc7c),
         size - BB_U32(h + 16));
  fill((void *)(uintptr_t)(h + 32 + size), BYTE(0x48cc74), 4);
  if (!ignored) {
    BB_U32(h + 8) = bb_address_encode(file);
    BB_U32(h + 12) = line;
    BB_U32(h + 24) = request;
  }
  BB_U32(h + 16) = size;
  if (!reallocating && h != old)
    assertion(0x2a8, 0x4826a4);
  if (h != old && !ignored) {
    if (BB_U32(h))
      BB_U32(BB_U32(h) + 4) = BB_U32(h + 4);
    else {
      if (LAST != old)
        assertion(0x2b7, 0x482688);
      LAST = BB_U32(h + 4);
    }
    if (BB_U32(h + 4))
      BB_U32(BB_U32(h + 4)) = BB_U32(h);
    else {
      if (FIRST != old)
        assertion(0x2c2, 0x48266c);
      FIRST = BB_U32(h);
    }
    if (FIRST)
      BB_U32(FIRST + 4) = h;
    else
      LAST = h;
    BB_U32(h) = FIRST;
    BB_U32(h + 4) = 0;
    FIRST = h;
  }
  return (void *)(uintptr_t)(h + 32);
}
void *bb_crt_reallocate_debug_entry(void *p, uint32_t size, uint32_t type,
                                    const char *file, int32_t line) {
  return bb_crt_debug_resize_entry(p, size, type, file, line, 1);
}
void bb_crt_free_debug_entry(void *p, uint32_t type) {
  check(0x3e1);
  if (!p)
    return;
  if (!client(3, p, 0, type, 0, NULL, 0)) {
    NOTICE1(0x486e1c, STR(0x4828f4));
    return;
  }
  if (!bb_crt_debug_valid_heap_entry(p))
    assertion(0x3f3, 0x48275c);
  uint32_t h = bb_address_encode(p) - 32u;
  if (!valid_type(BB_U32(h + 20)))
    assertion(0x3f9, 0x482514);
  if (!(FLAGS & 4)) {
    if (!bb_crt_debug_check_bytes_entry((void *)(uintptr_t)(h + 28),
                                        BYTE(0x48cc74), 4))
      WARNING3(0x4828c8, type_name(BB_U32(h + 20)), BB_U32(h + 24), p);
    if (!bb_crt_debug_check_bytes_entry(
            (void *)(uintptr_t)(bb_address_encode(p) + BB_U32(h + 16)),
            BYTE(0x48cc74), 4))
      WARNING3(0x48289c, type_name(BB_U32(h + 20)), BB_U32(h + 24), p);
  }
  if (BB_U32(h + 20) == 3) {
    if (BB_U32(h + 12) != 0xfedcbabc || BB_U32(h + 24))
      assertion(0x40e, 0x48285c);
    fill((void *)(uintptr_t)h, BYTE(0x48cc78), BB_U32(h + 16) + 36);
    bb_crt_heap_free_entry((void *)(uintptr_t)h);
    return;
  }
  if (BB_U32(h + 20) == 2 && type == 1)
    type = 2;
  if (BB_U32(h + 20) != type)
    assertion(0x41b, 0x48283c);
  CURRENT -= BB_U32(h + 16);
  if (FLAGS & 2) {
    BB_U32(h + 20) = 0;
    fill(p, BYTE(0x48cc78), BB_U32(h + 16));
    return;
  }
  if (BB_U32(h))
    BB_U32(BB_U32(h) + 4) = BB_U32(h + 4);
  else {
    if (LAST != h)
      assertion(0x42a, 0x482824);
    LAST = BB_U32(h + 4);
  }
  if (BB_U32(h + 4))
    BB_U32(BB_U32(h + 4)) = BB_U32(h);
  else {
    if (FIRST != h)
      assertion(0x434, 0x48280c);
    FIRST = BB_U32(h);
  }
  fill((void *)(uintptr_t)h, BYTE(0x48cc78), BB_U32(h + 16) + 36);
  bb_crt_heap_free_entry((void *)(uintptr_t)h);
}
void bb_crt_free_entry(void *p) { bb_crt_free_debug_entry(p, 1); }
uint32_t bb_crt_size_debug_entry(void *p, uint32_t type) {
  check(0x47c);
  if (!bb_crt_debug_valid_heap_entry(p))
    assertion(0x485, 0x48275c);
  uint32_t h = bb_address_encode(p) - 32u;
  if (!valid_type(BB_U32(h + 20)))
    assertion(0x48b, 0x482514);
  if (BB_U32(h + 20) == 2 && type == 1)
    type = 2;
  if (BB_U32(h + 20) != 3 && BB_U32(h + 20) != type)
    assertion(0x492, 0x48283c);
  return BB_U32(h + 16);
}
int32_t bb_crt_debug_check_bytes_entry(const void *p, uint32_t value,
                                       uint32_t size) {
  int32_t result = 1;
  uint32_t b = bb_address_encode(p);
  while (size--) {
    if (BYTE(b) != (uint8_t)value) {
      NOTICE3(0x482910, (void *)(uintptr_t)b, (uint32_t)BYTE(b),
              (uint32_t)(uint8_t)value);
      result = 0;
    }
    ++b;
  }
  return result;
}
int32_t bb_crt_debug_check_memory_entry(void) {
  if (!(FLAGS & 1))
    return 1;
  int32_t heap = bb_crt_heap_check_entry();
  if (heap != -1 && heap != -2) {
    uint32_t message = 0x4829cc;
    if (heap == -6)
      message = 0x4829f8;
    if (heap == -5)
      message = 0x482a1c;
    if (heap == -4)
      message = 0x482a40;
    if (heap == -3)
      message = 0x482a64;
    NOTICE1(0x486e1c, STR(message));
    return 0;
  }
  int32_t result = 1;
  for (uint32_t h = FIRST; h; h = BB_U32(h)) {
    int valid = 1;
    const char *name =
        valid_type(BB_U32(h + 20)) ? type_name(BB_U32(h + 20)) : STR(0x4829c4);
    if (!bb_crt_debug_check_bytes_entry((void *)(uintptr_t)(h + 28),
                                        BYTE(0x48cc74), 4)) {
      NOTICE3(0x4828c8, name, BB_U32(h + 24), (void *)(uintptr_t)(h + 32));
      valid = 0;
    }
    if (!bb_crt_debug_check_bytes_entry(
            (void *)(uintptr_t)(h + 32 + BB_U32(h + 16)), BYTE(0x48cc74), 4)) {
      NOTICE3(0x48289c, name, BB_U32(h + 24), (void *)(uintptr_t)(h + 32));
      valid = 0;
    }
    if (!BB_U32(h + 20) &&
        !bb_crt_debug_check_bytes_entry((void *)(uintptr_t)(h + 32),
                                        BYTE(0x48cc78), BB_U32(h + 16))) {
      NOTICE1(0x482998, (void *)(uintptr_t)(h + 32));
      valid = 0;
    }
    if (!valid) {
      if (BB_U32(h + 8))
        NOTICE3(0x482978, name, BB_POINTER(h + 8), BB_U32(h + 12));
      NOTICE3(0x48294c, name, (void *)(uintptr_t)(h + 32), BB_U32(h + 16));
      result = 0;
    }
  }
  return result;
}
int32_t bb_crt_debug_set_flags_entry(int32_t flags) {
  int32_t previous = FLAGS;
  if (flags != -1)
    FLAGS = flags;
  return previous;
}
int32_t bb_crt_debug_valid_pointer_entry(const void *p, uint32_t size,
                                         int32_t writing) {
#ifdef BB_MODEL_RUNTIME
  if (!p || BB_GAME_CALL(BB_U32(0x6ca458), 0, bb_address_encode(p), size))
    return 0;
  if (writing && BB_GAME_CALL(BB_U32(0x6ca450), 0, bb_address_encode(p), size))
    return 0;
#else
  if (!p || ((int32_t(WIN *)(const void *, uint32_t))BB_CODE(
                BB_POINTER(0x6ca458)))(p, size))
    return 0;
  if (writing && ((int32_t(WIN *)(const void *, uint32_t))BB_CODE(
                     BB_POINTER(0x6ca450)))(p, size))
    return 0;
#endif
  return 1;
}
int32_t bb_crt_debug_valid_heap_entry(const void *p) {
  if (!p)
    return 0;
  void *h = (void *)(uintptr_t)(bb_address_encode(p) - 32u);
  if (!bb_crt_debug_valid_pointer_entry(h, 32, 1))
    return 0;
  uint32_t region, page;
  const uint8_t *mark = bb_crt_heap_find_entry(h, &region, &page);
  if (mark)
    return BYTE(bb_address_encode(mark)) ? 1 : 0;
  if (BYTE(0x48cbb5) & 0x80)
    return 1;
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(0x6ca448), 0, BB_U32(0x6c8a90), 0,
                      bb_address_encode(h));
#else
  return ((int32_t(WIN *)(void *, uint32_t, const void *))BB_CODE(
      BB_POINTER(0x6ca448)))(BB_POINTER(0x6c8a90), 0, h);
#endif
}
void bb_crt_debug_checkpoint_entry(uint32_t *state) {
  if (!state) {
    NOTICE1(0x486e1c, STR(0x482aac));
    return;
  }
  BB_U32((uintptr_t)state + (0) * 4u) = FIRST;
  for (uint32_t n = 0; n < 5; ++n)
    BB_U32((uintptr_t)state + (n + 1) * 4u) =
        BB_U32((uintptr_t)state + (n + 6) * 4u) = 0;
  for (uint32_t h = FIRST; h; h = BB_U32(h)) {
    uint32_t type = BB_U32(h + 20) & 0xffff;
    if (type < 5) {
      ++BB_U32((uintptr_t)state + (type + 1) * 4u);
      BB_U32((uintptr_t)state + (type + 6) * 4u) += BB_U32(h + 16);
    } else
      NOTICE1(0x482a88, (void *)(uintptr_t)h);
  }
  BB_U32((uintptr_t)state + (11) * 4u) = HIGH;
  BB_U32((uintptr_t)state + (12) * 4u) = TOTAL;
}
void bb_crt_debug_dump_objects_entry(const uint32_t *state) {
  NOTICE1(0x486e1c, STR(0x482ba4));
  uint32_t stop = state ? BB_U32((uintptr_t)state) : 0;
  for (uint32_t h = FIRST; h && h != stop; h = BB_U32(h)) {
    uint32_t type = BB_U32(h + 20) & 0xffff;
    if (type == 3 || !type || (type == 2 && !(FLAGS & 16)))
      continue;
    if (BB_U32(h + 8)) {
      if (bb_crt_debug_valid_pointer_entry(BB_POINTER(h + 8), 1, 0))
        NOTICE2(0x482b84, BB_POINTER(h + 8), BB_U32(h + 12));
      else
        NOTICE1(0x482b90, BB_U32(h + 12));
    }
    NOTICE1(0x482b7c, BB_U32(h + 24));
    if (type == 4) {
      NOTICE3(0x482b48, (void *)(uintptr_t)(h + 32), BB_U32(h + 20) >> 16,
              BB_U32(h + 16));
      if (BB_U32(0x6c9bb8)) {
#ifdef BB_MODEL_RUNTIME
        BB_GAME_CALL(BB_U32(0x6c9bb8), 0, h + 32u, BB_U32(h + 16));
#else
        ((void (*)(void *, uint32_t))BB_CODE(BB_POINTER(0x6c9bb8)))(
            (void *)(uintptr_t)(h + 32), BB_U32(h + 16));

#endif
      } else
        bb_crt_debug_print_data_entry((void *)(uintptr_t)h);
    } else if (BB_U32(h + 20) == 1) {
      NOTICE2(0x482b20, (void *)(uintptr_t)(h + 32), BB_U32(h + 16));
      bb_crt_debug_print_data_entry((void *)(uintptr_t)h);
    } else if (type == 2) {
      NOTICE3(0x482aec, (void *)(uintptr_t)(h + 32), BB_U32(h + 20) >> 16,
              BB_U32(h + 16));
      bb_crt_debug_print_data_entry((void *)(uintptr_t)h);
    }
  }
  NOTICE1(0x486e1c, STR(0x482ad4));
}
/* A zero-length block leaves the old hexadecimal scratch buffer untouched.
 * Keep that state explicit at the binary boundary; reserve stack space before
 * saving registers so the legacy slot survives entry to the C worker. */
void __attribute__((used, noinline)) bb_crt_debug_print_data_c(uint32_t *r) {
#ifdef BB_MODEL_RUNTIME
  uint32_t h = BB_U32(r[3] + 136u), ascii = r[3] + 4u, hex = r[3] + 76u;
  int32_t n = 0;
  for (; n < 16 && n < BB_I32(h + 16u); ++n) {
    uint32_t byte = BYTE(h + 32u + n);
    uint32_t printable = BB_I32(0x48d35c) <= 1
                             ? BB_U16(BB_U32(0x48d150) + byte * 2u) & 0x157u
                             : bb_crt_character_type_entry(byte, 0x157);
    BYTE(ascii + n) = printable ? byte : ' ';
    BB_GAME_CALL(0x0046bae0u, 0, hex + n * 3u, 0x482bc8u, byte);
  }
  BYTE(ascii + n) = 0;
  NOTICE2(0x482bb8, (char *)(uintptr_t)ascii, (char *)(uintptr_t)hex);
#else
  uintptr_t h = BB_U32(r[3] + 136), scratch = r[3] + 76;
  char ascii[20], hex[52];
  for (uint32_t n = 0; n < sizeof(hex); ++n)
    hex[n] = BYTE(scratch + n);
  int32_t n = 0;
  for (; n < 16 && n < BB_I32(h + 16); ++n) {
    uint32_t byte = BYTE(h + 32 + n);
    uint32_t printable =
        BB_I32(0x48d35c) <= 1
            ? *(volatile uint16_t *)((uintptr_t)BB_POINTER(0x48d150) +
                                     byte * 2) &
                  0x157
            : bb_crt_character_type_entry(byte, 0x157);
    ascii[n] = printable ? byte : ' ';
    bb_crt_sprintf_entry(hex + n * 3, STR(0x482bc8), byte);
  }
  ascii[n] = 0;
  NOTICE2(0x482bb8, ascii, hex);
  for (uint32_t i = 0; i < sizeof(hex); ++i)
    BYTE(scratch + i) = hex[i];
#endif
}
#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_ASM(n) "_" #n
#else
#define BB_ASM(n) #n
#endif
void __attribute__((naked, noinline))
bb_crt_debug_print_data_entry(const void *p __attribute__((unused))) {
  __asm__ volatile("subl $128,%esp;pushfl;pushal;cld;pushl %esp;call " BB_ASM(
      bb_crt_debug_print_data_c) ";addl $4,%esp;popal;popfl;addl "
                                 "$128,%esp;ret");
}
#endif
int32_t bb_crt_debug_dump_leaks_entry(void) {
  uint32_t state[13];
  bb_crt_debug_checkpoint_entry(state);
  if (!BB_U32((uintptr_t)state + (5) * 4u) &&
      !BB_U32((uintptr_t)state + (2) * 4u) &&
      (!(FLAGS & 16) || !BB_U32((uintptr_t)state + (3) * 4u)))
    return 0;
  NOTICE1(0x486e1c, STR(0x482bd0));
  bb_crt_debug_dump_objects_entry(NULL);
  return 1;
}
