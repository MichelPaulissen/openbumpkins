/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_heap.h"
#include "abi.h"
#include "crt_primitives.h"
#include "game_calls.h"
#include "interop.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WIN BB_STDCALL
#define HEAD 0x48d6d8u
static void *heap_allocate(uint32_t size) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca414), 0, BB_U32(0x6c8a90),
                                         0, size);
#else

  return ((void *(WIN *)(void *, uint32_t, uint32_t))BB_CODE(
      BB_POINTER(0x6ca414)))(BB_POINTER(0x6c8a90), 0, size);
#endif
}
static void heap_free(void *p) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x6ca40c), 0, BB_U32(0x6c8a90), 0, bb_address_encode(p));
#else

  ((int32_t(WIN *)(void *, uint32_t, void *))BB_CODE(BB_POINTER(0x6ca40c)))(
      BB_POINTER(0x6c8a90), 0, p);
#endif
}
static void *heap_reallocate(void *p, uint32_t size, uint32_t flags) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca438), 0, BB_U32(0x6c8a90),
                                         flags, bb_address_encode(p), size);
#else

  return ((void *(WIN *)(void *, uint32_t, void *, uint32_t))BB_CODE(
      BB_POINTER(0x6ca438)))(BB_POINTER(0x6c8a90), flags, p, size);
#endif
}
static void *virtual_allocate(void *p, uint32_t size, uint32_t flags) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca408), 0,
                                         bb_address_encode(p), size, flags, 4);
#else

  return ((void *(WIN *)(void *, uint32_t, uint32_t, uint32_t))BB_CODE(
      BB_POINTER(0x6ca408)))(p, size, flags, 4);
#endif
}
static uint32_t virtual_free(void *p, uint32_t size, uint32_t flags) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(0x6ca418), 0, bb_address_encode(p), size, flags);
#else

  return ((uint32_t(WIN *)(void *, uint32_t, uint32_t))BB_CODE(
      BB_POINTER(0x6ca418)))(p, size, flags);
#endif
}
static void fill(void *p, int32_t value, uint32_t size) {
  bb_crt_memory_fill_entry(p, value, size);
}
static void copy(void *out, const void *in, uint32_t size) {
  bb_crt_memory_copy_entry(out, in, size);
}
int32_t bb_crt_heap_permission_entry(void) { return 1; }
int32_t bb_crt_heap_retry_entry(uint32_t size) {
#ifdef BB_MODEL_RUNTIME
  if (BB_U32(0x6c89a4) && BB_GAME_CALL(BB_U32(0x6c89a4), 0, size))
#else
  if (BB_U32(0x6c89a4) &&
      ((int32_t (*)(uint32_t))BB_CODE(BB_POINTER(0x6c89a4)))(size))
#endif
    return 1;
  return 0;
}
void *bb_crt_heap_region_create_entry(void) {
  uint32_t region = HEAD;
  if (BB_U32(0x48dee8)) {
    region = bb_address_encode(heap_allocate(0x814));
    if (!region)
      return NULL;
  }
  void *pages = virtual_allocate(NULL, 0x400000, 0x2000);
  if (pages) {
    if (virtual_allocate(pages, 0x10000, 0x1000)) {
      if (region == HEAD) {
        if (!BB_U32(HEAD))
          BB_U32(HEAD) = HEAD;
        if (!BB_U32(HEAD + 4))
          BB_U32(HEAD + 4) = HEAD;
      } else {
        BB_U32(region) = HEAD;
        BB_U32(region + 4) = BB_U32(HEAD + 4);
        BB_U32(HEAD + 4) = region;
        BB_U32(BB_U32(region + 4)) = region;
      }
      BB_U32(region + 0x810) = bb_address_encode(pages);
      BB_U32(region + 8) = 0;
      BB_U32(region + 12) = 16;
      for (uint32_t n = 0; n < 1024; ++n) {
        BYTE(region + 16 + n) = n < 16 ? 240 : 255;
        BYTE(region + 0x410 + n) = 241;
      }
      fill(pages, 0, 0x10000);
      for (uint32_t page = bb_address_encode(pages);
           page < BB_U32(region + 0x810) + 0x10000; page += 4096) {
        BB_U32(page) = page + 8;
        BB_U32(page + 4) = 240;
        BYTE(page + 248) = 255;
      }
      return (void *)(uintptr_t)region;
    }
    virtual_free(pages, 0, 0x8000);
  }
  if (region != HEAD)
    heap_free((void *)(uintptr_t)region);
  return NULL;
}
int32_t bb_crt_heap_initialize_entry(void) {
#ifdef BB_MODEL_RUNTIME
  BB_U32(0x6c8a90) = BB_GAME_CALL(BB_U32(0x6ca41c), 0, 1, 4096, 0);
#else
  BB_SET_POINTER(0x6c8a90,
                 ((void *(WIN *)(uint32_t, uint32_t, uint32_t))BB_CODE(
                     BB_POINTER(0x6ca41c)))(1, 4096, 0));
#endif
  if (!BB_U32(0x6c8a90))
    return 0;
  if (bb_crt_heap_region_create_entry())
    return 1;
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x6ca420), 0, BB_U32(0x6c8a90));
#else
  ((int32_t(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca420)))(BB_POINTER(0x6c8a90));
#endif
  return 0;
}
void bb_crt_heap_region_release_entry(void *object) {
  uint32_t r = bb_address_encode(object);
  virtual_free(BB_POINTER(r + 0x810), 0, 0x8000);
  if (r == BB_U32(0x48deec))
    BB_U32(0x48deec) = BB_U32(r + 4);
  if (r == HEAD)
    BB_U32(0x48dee8) = 0;
  else {
    BB_U32(BB_U32(r + 4)) = BB_U32(r);
    BB_U32(BB_U32(r) + 4) = BB_U32(r + 4);
    heap_free(object);
  }
}
void bb_crt_heap_decommit_entry(int32_t quota) {
  uint32_t r = BB_U32(HEAD + 4);
  do {
    uint32_t previous = r;
    if (BB_U32(r + 0x810)) {
      uint32_t freed = 0;
      for (int32_t n = 1023; n >= 0; --n) {
        uint32_t count = r + 16 + (uint32_t)n;
        if (BYTE(count) == 240 &&
            virtual_free(
                (void *)(uintptr_t)(BB_U32(r + 0x810) + (uint32_t)n * 4096),
                4096, 0x4000)) {
          BYTE(count) = 255;
          --BB_U32(0x48def0);
          if (BB_U32(r + 12) == UINT32_MAX || n < BB_I32(r + 12))
            BB_U32(r + 12) = n;
          ++freed;
          quota = (uint32_t)quota - 1;
          if (!quota)
            break;
        }
      }
      previous = BB_U32(r + 4);
      if (freed && BYTE(r + 16) == 255) {
        uint32_t n = 1;
        while (n < 1024 && BYTE(r + 16 + n) == 255)
          ++n;
        if (n == 1024)
          bb_crt_heap_region_release_entry((void *)(uintptr_t)r);
      }
    }
    r = previous;
  } while (r != BB_U32(HEAD + 4) && quota > 0);
}
void *bb_crt_heap_find_entry(void *object, uint32_t *region, uint32_t *page) {
  uint32_t p = bb_address_encode(object), r = HEAD;
  for (;;) {
    uint32_t base = BB_U32(r + 0x810);
    if (base && p > base && p < base + 0x400000)
      break;
    r = BB_U32(r);
    if (r == HEAD)
      return NULL;
  }
  BB_U32((uintptr_t)region) = r;
  uint32_t base = p & ~4095u;
  BB_U32((uintptr_t)page) = base;
  return (void *)(uintptr_t)(base + 8 + ((int32_t)(p - (base + 256)) >> 4));
}
void bb_crt_heap_block_free_entry(void *region, void *page, void *mark) {
  uint32_t r = bb_address_encode(region), p = bb_address_encode(page),
           m = bb_address_encode(mark);
  uint32_t index = (int32_t)(p - BB_U32(r + 0x810)) >> 12;
  BYTE(r + 16 + index) += BYTE(m);
  BYTE(m) = 0;
  BYTE(r + 0x410 + index) = 241;
  if (BYTE(r + 16 + index) == 240) {
    ++BB_U32(0x48def0);
    if (BB_U32(0x48def0) == 32)
      bb_crt_heap_decommit_entry(16);
  }
}
static void *allocated(uint32_t page, uint32_t mark, uint32_t units,
                       uint32_t available) {
  if (mark + units < page + 248) {
    BB_U32(page) = mark + units;
    BB_U32(page + 4) = available - units;
  } else {
    BB_U32(page) = page + 8;
    BB_U32(page + 4) = 0;
  }
  BYTE(mark) = units;
  return (void *)(uintptr_t)(page + (mark - page) * 16 + 128);
}
void *bb_crt_heap_page_allocate_entry(void *object, uint32_t total,
                                      uint32_t units) {
  uint32_t page = bb_address_encode(object), hint = BB_U32(page);
  if (BB_U32(page + 4) >= units) {
    BYTE(hint) = units;
    if (hint + units < page + 248) {
      BB_U32(page) += units;
      BB_U32(page + 4) -= units;
    } else {
      BB_U32(page) = page + 8;
      BB_U32(page + 4) = 0;
    }
    return (void *)(uintptr_t)(page + (hint - page) * 16 + 128);
  }
  uint32_t mark = hint;
  if (BYTE(hint + BB_U32(page + 4)))
    mark = hint + BB_U32(page + 4);
  while (mark + units < page + 248) {
    if (BYTE(mark)) {
      mark += BYTE(mark);
      continue;
    }
    uint32_t run = 1;
    uint32_t end = mark + 1;
    while (!BYTE(end)) {
      ++run;
      ++end;
    }
    if (run >= units)
      return allocated(page, mark, units, run);
    if (mark == hint)
      BB_U32(page + 4) = run;
    else {
      total -= run;
      if (total < units)
        return NULL;
    }
    mark = end;
  }
  mark = page + 8;
  while (mark < hint && mark + units <= page + 247) {
    if (BYTE(mark)) {
      mark += BYTE(mark);
      continue;
    }
    uint32_t run = 1;
    uint32_t end = mark + 1;
    while (!BYTE(end)) {
      ++run;
      ++end;
    }
    if (run >= units)
      return allocated(page, mark, units, run);
    total -= run;
    if (total < units)
      return NULL;
    mark = end;
  }
  return NULL;
}
int32_t bb_crt_heap_block_resize_entry(void *region, void *object, void *block,
                                       uint32_t units) {
  uint32_t r = bb_address_encode(region), page = bb_address_encode(object),
           mark = bb_address_encode(block);
  uint32_t old = BYTE(mark);
  uint32_t index = (int32_t)(page - BB_U32(r + 0x810)) >> 12;
  if (units < old) {
    BYTE(mark) = units;
    BYTE(r + 16 + index) = BYTE(r + 16 + index) - (uint8_t)units + old;
    BYTE(r + 0x410 + index) = 241;
    return 1;
  }
  if (units <= old || mark + units > page + 248)
    return 0;
  uint32_t end = mark + units, p = mark + old;
  while (p < end && !BYTE(p))
    ++p;
  if (p != end)
    return 0;
  BYTE(mark) = units;
  if (mark <= BB_U32(page) && BB_U32(page) < end) {
    if (end < page + 248) {
      BB_U32(page) = end;
      uint32_t run = 0;
      while (!BYTE(end++))
        ++run;
      BB_U32(page + 4) = run;
    } else {
      BB_U32(page) = page + 8;
      BB_U32(page + 4) = 0;
    }
  }
  BYTE(r + 16 + index) = BYTE(r + 16 + index) - (uint8_t)units + old;
  return 1;
}
void *bb_crt_heap_block_allocate_entry(uint32_t units) {
  uint32_t r = BB_U32(0x48deec);
  do {
    if (BB_U32(r + 0x810)) {
      for (unsigned pass = 0; pass < 2; ++pass) {
        uint32_t n = pass ? 0 : BB_U32(r + 8);
        while ((int32_t)n < (pass ? BB_I32(r + 8) : 1024)) {
          if (units <= BYTE(r + 16 + n) && BYTE(r + 16 + n) != 255 &&
              units < BYTE(r + 0x410 + n)) {
            void *p = bb_crt_heap_page_allocate_entry(
                (void *)(uintptr_t)(BB_U32(r + 0x810) + n * 4096),
                BYTE(r + 16 + n), units);
            if (p) {
              BB_U32(0x48deec) = r;
              BYTE(r + 16 + n) -= (uint8_t)units;
              BB_U32(r + 8) = n;
              return p;
            }
            BYTE(r + 0x410 + n) = units;
          }
          ++n;
        }
      }
    }
    r = BB_U32(r);
  } while (r != BB_U32(0x48deec));
  r = HEAD;
  while (!BB_U32(r + 0x810) || BB_U32(r + 12) == UINT32_MAX) {
    r = BB_U32(r);
    if (r == HEAD) {
      r = bb_address_encode(bb_crt_heap_region_create_entry());
      if (!r)
        return NULL;
      uint32_t page = BB_U32(r + 0x810);
      BYTE(page + 8) = units;
      BB_U32(0x48deec) = r;
      BB_U32(page) = page + units + 8;
      BB_U32(page + 4) = 240 - units;
      BYTE(r + 16) -= (uint8_t)units;
      return (void *)(uintptr_t)(BB_U32(r + 0x810) + 256);
    }
  }
  uint32_t first = BB_U32(r + 12), end = first + 16;
  if ((int32_t)end > 1023)
    end = 1024;
  uint32_t n = first + 1;
  while ((int32_t)n < (int32_t)end && BYTE(r + 16 + n) == 255)
    ++n;
  uint32_t request = BB_U32(r + 0x810) + BB_U32(r + 12) * 4096;
  if (bb_address_encode(virtual_allocate((void *)(uintptr_t)request,
                                         (n - BB_U32(r + 12)) * 4096,
                                         0x1000)) != request)
    return NULL;
  uint32_t page = BB_U32(r + 0x810) + BB_U32(r + 12) * 4096;
  for (uint32_t i = BB_U32(r + 12); (int32_t)i < (int32_t)n;
       ++i, page += 4096) {
    /* The original passes a zero length here; committed pages are already
     * zeroed. */
    fill((void *)(uintptr_t)page, 4096, 0);
    BB_U32(page) = page + 8;
    BB_U32(page + 4) = 240;
    BYTE(page + 248) = 255;
    BYTE(r + 16 + i) = 240;
    BYTE(r + 0x410 + i) = 241;
  }
  BB_U32(0x48deec) = r;
  while ((int32_t)n < 1024 && BYTE(r + 16 + n) != 255)
    ++n;
  first = BB_U32(r + 12);
  BB_U32(r + 12) = (int32_t)n < 1024 ? n : UINT32_MAX;
  page = BB_U32(r + 0x810) + first * 4096;
  BYTE(page + 8) = units;
  BB_U32(r + 8) = first;
  BYTE(r + 16 + first) -= (uint8_t)units;
  BB_U32(page) = page + units + 8;
  BB_U32(page + 4) -= units;
  return (void *)(uintptr_t)(BB_U32(r + 0x810) + first * 4096 + 256);
}
int32_t bb_crt_heap_pool_check_entry(void) {
  uint32_t r = HEAD;
  uint32_t hints = 0;
  do {
    if (r == BB_U32(0x48deec))
      ++hints;
    if (BB_U32(r + 0x810)) {
      uint32_t uncommitted = 0;
      uint32_t page = BB_U32(r + 0x810);
      for (uint32_t n = 0; n < 1024; ++n, page += 4096) {
        if (BYTE(r + 16 + n) == 255) {
          if (!uncommitted && BB_U32(r + 12) != n)
            return -1;
          ++uncommitted;
          continue;
        }
        if (BB_U32(page) >= page + 248)
          return -2;
        if (BYTE(page + 248) != 255)
          return -3;
        uint32_t offset = 0, hint = 0, total = 0, run = 0;
        while (offset < 240) {
          if (page + offset + 8 == BB_U32(page))
            ++hint;
          uint32_t block = BYTE(page + offset + 8);
          if (!block) {
            ++total;
            ++run;
            ++offset;
          } else {
            if ((int32_t)BYTE(r + 0x410 + n) <= (int32_t)run)
              return -4;
            if (hint == 1) {
              if ((int32_t)run < BB_I32(page + 4))
                return -5;
              hint = 2;
            }
            run = 0;
            uint32_t next = offset + 1;
            while ((int32_t)next < (int32_t)(block + offset)) {
              if (BYTE(page + next + 8))
                return -6;
              ++next;
            }
            offset = next;
          }
        }
        if (BYTE(r + 16 + n) != total)
          return -7;
        if (!hint)
          return -8;
      }
    }
    r = BB_U32(r);
  } while (r != HEAD);
  return hints ? 0 : -9;
}
void *bb_crt_heap_allocate_entry(uint32_t size) {
  uint32_t aligned = (size + 15) & ~15u;
  void *p;
  if (aligned <= BB_U32(0x48def4) &&
      (p = bb_crt_heap_block_allocate_entry((size + 15) >> 4)))
    return p;
  return heap_allocate(aligned);
}
void *bb_crt_heap_malloc_retry_entry(uint32_t size, int32_t retry) {
  if (size > 0xffffffe0u)
    return NULL;
  if (!size)
    size = 1;
  do {
    void *p = size <= 0xffffffe0u ? bb_crt_heap_allocate_entry(size) : NULL;
    if (p)
      return p;
    if (!retry)
      return NULL;
  } while (bb_crt_heap_retry_entry(size));
  return NULL;
}
void *bb_crt_heap_malloc_entry(uint32_t size) {
  return bb_crt_heap_malloc_retry_entry(size, BB_U32(0x48d6cc));
}
void bb_crt_heap_free_entry(void *p) {
  if (!p)
    return;
  uint32_t region, page;
  void *mark = bb_crt_heap_find_entry(p, &region, &page);
  if (!mark)
    heap_free(p);
  else
    bb_crt_heap_block_free_entry((void *)(uintptr_t)region,
                                 (void *)(uintptr_t)page, mark);
}
void *bb_crt_heap_expand_entry(void *p, uint32_t size) {
  if (size > 0xffffffe0u)
    return NULL;
  size = size ? (size + 15) & ~15u : 16;
  uint32_t region, page;
  void *mark = bb_crt_heap_find_entry(p, &region, &page);
  if (!mark)
    return heap_reallocate(p, size, 16);
  return size <= BB_U32(0x48def4) &&
                 bb_crt_heap_block_resize_entry((void *)(uintptr_t)region,
                                                (void *)(uintptr_t)page, mark,
                                                size >> 4)
             ? p
             : NULL;
}
void *bb_crt_heap_reallocate_entry(void *p, uint32_t size) {
  if (!p)
    return bb_crt_heap_malloc_entry(size);
  if (!size) {
    bb_crt_heap_free_entry(p);
    return NULL;
  }
  if (size <= 0xffffffe0u)
    size = (size + 15) & ~15u;
  do {
    void *out = NULL;
    if (size <= 0xffffffe0u) {
      uint32_t region, page;
      void *mark = bb_crt_heap_find_entry(p, &region, &page);
      if (!mark)
        out = heap_reallocate(p, size, 0);
      else {
        if (size < BB_U32(0x48def4)) {
          if (bb_crt_heap_block_resize_entry((void *)(uintptr_t)region,
                                             (void *)(uintptr_t)page, mark,
                                             size >> 4))
            out = p;
          else {
            out = bb_crt_heap_block_allocate_entry(size >> 4);
            if (out) {
              uint32_t old = (uint32_t)BYTE((uintptr_t)mark) << 4;
              copy(out, p, old <= size ? old : size);
              bb_crt_heap_block_free_entry((void *)(uintptr_t)region,
                                           (void *)(uintptr_t)page, mark);
            }
          }
        }
        if (!out && (out = heap_allocate(size))) {
          uint32_t old = (uint32_t)BYTE((uintptr_t)mark) << 4;
          copy(out, p, old <= size ? old : size);
          bb_crt_heap_block_free_entry((void *)(uintptr_t)region,
                                       (void *)(uintptr_t)page, mark);
        }
      }
    }
    if (out)
      return out;
    if (!BB_U32(0x48d6cc))
      return NULL;
  } while (bb_crt_heap_retry_entry(size));
  return NULL;
}
int32_t bb_crt_heap_check_entry(void) {
  int32_t result = bb_crt_heap_pool_check_entry() < 0 ? -4 : -2;
#ifdef BB_MODEL_RUNTIME
  if (!BB_GAME_CALL(BB_U32(0x6ca448), 0, BB_U32(0x6c8a90), 0, 0)) {
    uint32_t error = BB_GAME_CALL(BB_U32(0x6ca43c), 0);
#else
  if (!((uint32_t(WIN *)(void *, uint32_t, const void *))BB_CODE(
          BB_POINTER(0x6ca448)))(BB_POINTER(0x6c8a90), 0, NULL)) {
    uint32_t error = ((uint32_t(WIN *)(void))BB_CODE(BB_POINTER(0x6ca43c)))();
#endif
    if (error == 120) {
      BB_U32(0x48cbac) = 120;
      BB_U32(0x48cba8) = 40;
    } else
      result = -4;
  }
  return result;
}
