/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_stream.h"
#include "crt_errors.h"
#include "crt_descriptor.h"
#include "crt_primitives.h"
#include "interop.h"
#include "game_calls.h"
#include <signal.h>
#include <stddef.h>
#define BYTE(a) BB_U8(a)
static void assertion(uintptr_t file, uint32_t line, uintptr_t expression) {
#ifdef BB_MODEL_RUNTIME
  if (BB_GAME_CALL(0x00470ef0u, 0, 2, (uint32_t)file, line, 0,
                    (uint32_t)expression) == 1)
#ifdef _WIN32
    bb_host_debug_break();
#else
    raise(SIGTRAP);
#endif
#else
  if (bb_crt_errors_report_entry(2, (const char *)file, line, NULL,
                                   (const char *)expression) == 1)
    __asm__ volatile("int3");
#endif
}
static uint32_t read_file(uint32_t handle, void *out, uint32_t bytes) {
  return bb_crt_descriptor_read_entry(handle, out, bytes);
}
static uint32_t write_file(uint32_t handle, const void *in, uint32_t bytes) {
  return bb_crt_descriptor_write_entry(handle, in, bytes);
}
static int32_t seek_file(uint32_t handle, uint32_t offset, int32_t origin) {
  return bb_crt_descriptor_seek_entry(handle, offset, origin);
}
static uint32_t handle_flags(uint32_t handle) {
  return BYTE(BB_U32(0x6c8aa0 + ((int32_t)(handle & ~31u) >> 3)) +
              (handle & 31) * 8 + 4);
}
static uint32_t handle_flags_or_bad(uint32_t handle) {
  return handle == UINT32_MAX ? BYTE(0x48d04c) : handle_flags(handle);
}
static void *allocate(uint32_t size, uintptr_t file, uint32_t line) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(0x0046d690u, 0, size, 2, (uint32_t)file, line);
#else
  return ((void *(*)(uint32_t, int32_t, const char *, uint32_t))BB_CODE(0x46d690))(
      size, 2, (const char *)file, line);
#endif
}
static void release(void *p) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x0046e130u, 0, bb_address_encode(p), 2);
#else
  ((void (*)(void *, int32_t))BB_CODE(0x46e130))(p, 2);
#endif
}
void bb_crt_stream_allocate_buffer_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x483438, 0x2e, 0x4824ac);
  ++BB_U32(0x48cf88);
  BB_SET_POINTER(p + 8, allocate(4096, 0x483438, 0x3b));
  if (!BB_U32(p + 8)) {
    BB_U32(p + 12) |= 4;
    BB_U32(p + 8) = bb_address_encode((void *)(p + 20));
    BB_U32(p + 24) = 2;
  } else {
    BB_U32(p + 12) |= 8;
    BB_U32(p + 24) = 4096;
  }
  BB_U32(p) = BB_U32(p + 8);
  BB_U32(p + 4) = 0;
}
void bb_crt_stream_free_buffer_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x482f1c, 0x30, 0x482bf8);
  if ((BB_U32(p + 12) & 0x83) && (BB_U32(p + 12) & 8)) {
    release(BB_POINTER(p + 8));
    BB_U32(p + 12) &= ~0x408u;
    BB_U32(p) = 0;
    BB_U32(p + 8) = BB_U32(p);
    BB_U32(p + 4) = 0;
  }
}
int32_t bb_crt_stream_fill_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x482ff0, 0x69, 0x4824ac);
  if (!(BB_U32(p + 12) & 0x83) || (BB_U32(p + 12) & 0x40))
    return -1;
  if (BB_U32(p + 12) & 2) {
    BB_U32(p + 12) |= 0x20;
    return -1;
  }
  BB_U32(p + 12) |= 1;
  if (!(BB_U32(p + 12) & 0x10c))
    bb_crt_stream_allocate_buffer_entry(stream);
  else
    BB_U32(p) = BB_U32(p + 8);
  BB_U32(p + 4) = read_file(BB_U32(p + 16), BB_POINTER(p + 8), BB_U32(p + 24));
  if (!BB_U32(p + 4) || BB_U32(p + 4) == UINT32_MAX) {
    BB_U32(p + 12) |= BB_U32(p + 4) ? 0x20 : 0x10;
    BB_U32(p + 4) = 0;
    return -1;
  }
  if (!(BB_U32(p + 12) & 0x82) &&
      (handle_flags_or_bad(BB_U32(p + 16)) & 0x82) == 0x82)
    BB_U32(p + 12) |= 0x2000;
  if (BB_U32(p + 24) == 512 && (BB_U32(p + 12) & 8) &&
      !(BB_U32(p + 12) & 0x400))
    BB_U32(p + 24) = 4096;
  --BB_U32(p + 4);
  uintptr_t source = BB_U32(p);
  ++BB_U32(p);
  return BYTE(source);
}
int32_t bb_crt_stream_getc_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x482bf0, 0x29, 0x482bf8);
  --BB_U32(p + 4);
  if (BB_I32(p + 4) < 0)
    return bb_crt_stream_fill_entry(stream);
  uint32_t c = BYTE(BB_U32(p));
  ++BB_U32(p);
  return c;
}
int32_t bb_crt_stream_ungetc_entry(int32_t ch, void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x483544, 0x60, 0x4824ac);
  if (ch == -1 || (!(BB_U32(p + 12) & 1) &&
                   (!(BB_U32(p + 12) & 0x80) || (BB_U32(p + 12) & 2))))
    return -1;
  if (!BB_U32(p + 8))
    bb_crt_stream_allocate_buffer_entry(stream);
  if (BB_U32(p + 8) == BB_U32(p)) {
    if (BB_U32(p + 4))
      return -1;
    ++BB_U32(p);
  }
  --BB_U32(p);
  if (!(BB_U32(p + 12) & 0x40))
    BYTE(BB_U32(p)) = ch;
  else if (BYTE(BB_U32(p)) != (uint8_t)ch) {
    ++BB_U32(p);
    return -1;
  }
  ++BB_U32(p + 4);
  BB_U32(p + 12) &= ~0x10u;
  BB_U32(p + 12) |= 1;
  return (uint32_t)ch & 255;
}
int32_t bb_crt_stream_flush_buffer_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  int32_t result = 0;
  if ((BB_U32(p + 12) & 3) == 2 && (BB_U32(p + 12) & 0x108)) {
    uint32_t bytes = BB_U32(p) - BB_U32(p + 8);
    if ((int32_t)bytes > 0) {
      if (write_file(BB_U32(p + 16), BB_POINTER(p + 8), bytes) == bytes) {
        if (BB_U32(p + 12) & 0x80)
          BB_U32(p + 12) &= ~2u;
      } else {
        BB_U32(p + 12) |= 0x20;
        result = -1;
      }
    }
  }
  BB_U32(p) = BB_U32(p + 8);
  BB_U32(p + 4) = 0;
  return result;
}
int32_t bb_crt_stream_flush_entry(void *stream) {
  if (!stream)
    return bb_crt_stream_flush_table_entry(0);
  if (bb_crt_stream_flush_buffer_entry(stream))
    return -1;
  uintptr_t p = (uintptr_t)stream;
  if ((BB_U32(p + 12) & 0x4000) &&
      bb_crt_descriptor_commit_entry(BB_U32(p + 16)))
    return -1;
  return 0;
}
int32_t bb_crt_stream_flush_table_entry(int32_t all) {
  uint32_t successes = 0;
  int32_t result = 0;
  for (uint32_t n = 0; (int32_t)n < BB_I32(0x6c9bb0); ++n) {
    uintptr_t p = BB_U32(BB_U32(0x6c8ba4) + n * 4);
    if (!p || !(BB_U32(p + 12) & 0x83))
      continue;
    if (all == 1) {
      if (bb_crt_stream_flush_entry((void *)p) != -1)
        ++successes;
    } else if (!all && (BB_U32(p + 12) & 2) &&
               bb_crt_stream_flush_entry((void *)p) == -1)
      result = -1;
  }
  return all == 1 ? (int32_t)successes : result;
}
int32_t bb_crt_stream_flush_all_entry(void) {
  return bb_crt_stream_flush_table_entry(1);
}
int32_t bb_crt_stream_close_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  int32_t result = -1;
  if (BB_U32(p + 12) & 0x40) {
    BB_U32(p + 12) = 0;
    return -1;
  }
  if (!p)
    assertion(0x4824a0, 0x77, 0x4824ac);
  if (BB_U32(p + 12) & 0x83) {
    result = bb_crt_stream_flush_buffer_entry(stream);
    bb_crt_stream_free_buffer_entry(stream);
    if (bb_crt_descriptor_close_entry(BB_U32(p + 16)) < 0)
      result = -1;
    else if (BB_U32(p + 28)) {
      release(BB_POINTER(p + 28));
      BB_U32(p + 28) = 0;
    }
  }
  BB_U32(p + 12) = 0;
  return result;
}
int32_t bb_crt_stream_write_char_entry(int32_t ch, void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x482c48, 0x69, 0x4824ac);
  uint32_t handle = BB_U32(p + 16);
  if (!(BB_U32(p + 12) & 0x82) || (BB_U32(p + 12) & 0x40)) {
    BB_U32(p + 12) |= 0x20;
    return -1;
  }
  if (BB_U32(p + 12) & 1) {
    BB_U32(p + 4) = 0;
    if (!(BB_U32(p + 12) & 0x10)) {
      BB_U32(p + 12) |= 0x20;
      return -1;
    }
    BB_U32(p) = BB_U32(p + 8);
    BB_U32(p + 12) &= ~1u;
  }
  BB_U32(p + 12) |= 2;
  BB_U32(p + 12) &= ~0x10u;
  BB_U32(p + 4) = 0;
  uint32_t written = BB_U32(p + 4), bytes;
  if (!(BB_U32(p + 12) & 0x10c) && ((p != 0x48cd28 && p != 0x48cd48) ||
                                    !bb_crt_descriptor_terminal_entry(handle)))
    bb_crt_stream_allocate_buffer_entry(stream);
  if (!(BB_U32(p + 12) & 0x108)) {
    bytes = 1;
    written = write_file(handle, &ch, 1);
  } else {
    if ((int32_t)(BB_U32(p) - BB_U32(p + 8)) < 0)
      assertion(0x482c48, 0xa0, 0x482c08);
    bytes = BB_U32(p) - BB_U32(p + 8);
    BB_U32(p) = BB_U32(p + 8) + 1;
    BB_U32(p + 4) = BB_U32(p + 24) - 1;
    if ((int32_t)bytes < 1) {
      if (handle_flags_or_bad(handle) & 0x20)
        seek_file(handle, 0, 2);
    } else
      written = write_file(handle, BB_POINTER(p + 8), bytes);
    BYTE(BB_U32(p + 8)) = ch;
  }
  if (written == bytes)
    return (uint32_t)ch & 255;
  BB_U32(p + 12) |= 0x20;
  return -1;
}
uint32_t bb_crt_stream_read_entry(void *destination, uint32_t size,
                                  uint32_t count, void *stream) {
  uintptr_t p = (uintptr_t)stream, out = (uintptr_t)destination;
  uint32_t total = size * count, remaining = total;
  if (!total)
    return 0;
  uint32_t buffer_size = BB_U32(p + 12) & 0x10c ? BB_U32(p + 24) : 4096;
  while (remaining) {
    if ((BB_U32(p + 12) & 0x10c) && BB_U32(p + 4)) {
      uint32_t bytes = BB_U32(p + 4);
      if (remaining <= bytes)
        bytes = remaining;
      bb_crt_memory_copy_entry((void *)out, BB_POINTER(p), bytes);
      remaining -= bytes;
      BB_U32(p + 4) -= bytes;
      BB_U32(p) += bytes;
      out += bytes;
    } else if (remaining < buffer_size) {
      int32_t ch = bb_crt_stream_fill_entry(stream);
      if (ch == -1)
        return (total - remaining) / size;
      BYTE(out++) = ch;
      --remaining;
      buffer_size = BB_U32(p + 24);
    } else {
      uint32_t bytes =
          buffer_size ? remaining - remaining % buffer_size : remaining;
      uint32_t got = read_file(BB_U32(p + 16), (void *)out, bytes);
      if (!got || got == UINT32_MAX) {
        BB_U32(p + 12) |= got ? 0x20 : 0x10;
        return (total - remaining) / size;
      }
      remaining -= got;
      out += got;
    }
  }
  return count;
}
uint32_t bb_crt_stream_write_entry(const void *source, uint32_t size,
                                   uint32_t count, void *stream) {
  uintptr_t p = (uintptr_t)stream, in = (uintptr_t)source;
  uint32_t total = size * count, remaining = total;
  if (!total)
    return 0;
  uint32_t buffer_size = BB_U32(p + 12) & 0x10c ? BB_U32(p + 24) : 4096;
  while (remaining) {
    if ((BB_U32(p + 12) & 0x108) && BB_U32(p + 4)) {
      uint32_t bytes = BB_U32(p + 4);
      if (remaining <= bytes)
        bytes = remaining;
      bb_crt_memory_copy_entry(BB_POINTER(p), (const void *)in, bytes);
      remaining -= bytes;
      BB_U32(p + 4) -= bytes;
      BB_U32(p) += bytes;
      in += bytes;
    } else if (remaining < buffer_size) {
      if (bb_crt_stream_write_char_entry((int8_t)BYTE(in), stream) == -1)
        return (total - remaining) / size;
      ++in;
      --remaining;
      buffer_size = BB_I32(p + 24) < 1 ? 1 : BB_U32(p + 24);
    } else {
      if ((BB_U32(p + 12) & 0x108) && bb_crt_stream_flush_buffer_entry(stream))
        return (total - remaining) / size;
      uint32_t bytes =
          buffer_size ? remaining - remaining % buffer_size : remaining;
      uint32_t written = write_file(BB_U32(p + 16), (const void *)in, bytes);
      if (written == UINT32_MAX) {
        BB_U32(p + 12) |= 0x20;
        return (total - remaining) / size;
      }
      remaining -= written;
      in += written;
      if (written < bytes) {
        BB_U32(p + 12) |= 0x20;
        return (total - remaining) / size;
      }
    }
  }
  return count;
}
int32_t bb_crt_stream_tell_entry(void *stream) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x482548, 0x63, 0x4824ac);
  uint32_t handle = BB_U32(p + 16);
  if (BB_I32(p + 4) < 0)
    BB_U32(p + 4) = 0;
  uint32_t position = seek_file(handle, 0, 1);
  if ((int32_t)position < 0)
    return -1;
  if (!(BB_U32(p + 12) & 0x108))
    return position - BB_U32(p + 4);
  uint32_t offset = BB_U32(p) - BB_U32(p + 8);
  if (BB_U32(p + 12) & 3) {
    if (handle_flags(handle) & 0x80)
      for (uintptr_t b = BB_U32(p + 8); b < BB_U32(p); ++b)
        if (BYTE(b) == '\n')
          ++offset;
  } else if (!(BB_U32(p + 12) & 0x80)) {
    BB_U32(0x48cba8) = 22;
    return -1;
  }
  if (!position)
    return offset;
  if (BB_U32(p + 12) & 1) {
    if (!BB_U32(p + 4))
      offset = 0;
    else {
      uint32_t bytes = BB_U32(p) - BB_U32(p + 8) + BB_U32(p + 4);
      if (handle_flags(handle) & 0x80) {
        if ((uint32_t)seek_file(handle, 0, 2) == position) {
          uintptr_t end = BB_U32(p + 8) + bytes;
          for (uintptr_t b = BB_U32(p + 8); b < end; ++b)
            if (BYTE(b) == '\n')
              ++bytes;
          if (BB_U32(p + 12) & 0x2000)
            ++bytes;
        } else {
          seek_file(handle, position, 0);
          if (bytes <= 512 && (BB_U32(p + 12) & 8) && !(BB_U32(p + 12) & 0x400))
            bytes = 512;
          else
            bytes = BB_U32(p + 24);
          if (handle_flags(handle) & 4)
            ++bytes;
        }
      }
      position -= bytes;
    }
  }
  return position + offset;
}
int32_t bb_crt_stream_seek_entry(void *stream, int32_t offset, int32_t origin) {
  uintptr_t p = (uintptr_t)stream;
  if (!p)
    assertion(0x482550, 0x92, 0x4824ac);
  if (!(BB_U32(p + 12) & 0x83) || (origin != 0 && origin != 1 && origin != 2)) {
    BB_U32(0x48cba8) = 22;
    return -1;
  }
  BB_U32(p + 12) &= ~0x10u;
  if (origin == 1) {
    offset = (uint32_t)offset + (uint32_t)bb_crt_stream_tell_entry(stream);
    origin = 0;
  }
  bb_crt_stream_flush_buffer_entry(stream);
  if (BB_U32(p + 12) & 0x80)
    BB_U32(p + 12) &= ~3u;
  else if ((BB_U32(p + 12) & 1) && (BB_U32(p + 12) & 8) &&
           !(BB_U32(p + 12) & 0x400))
    BB_U32(p + 24) = 512;
  return seek_file(BB_U32(p + 16), offset, origin) == -1 ? -1 : 0;
}
void *bb_crt_stream_acquire_entry(void) {
  uintptr_t p = 0;
  for (uint32_t n = 0; (int32_t)n < BB_I32(0x6c9bb0); ++n) {
    uintptr_t slot = BB_U32(0x6c8ba4) + n * 4;
    if (!BB_U32(slot)) {
      void *memory = allocate(32, 0x482f44, 0x55);
      BB_SET_POINTER(BB_U32(0x6c8ba4) + n * 4, memory);
      p = BB_U32(BB_U32(0x6c8ba4) + n * 4);
      break;
    }
    if (!(BYTE(BB_U32(slot) + 12) & 0x83)) {
      p = BB_U32(slot);
      break;
    }
  }
  if (p) {
    BB_U32(p + 4) = 0;
    BB_U32(p + 12) = BB_U32(p + 4);
    BB_U32(p + 8) = 0;
    BB_U32(p) = BB_U32(p + 8);
    BB_U32(p + 28) = BB_U32(p);
    BB_U32(p + 16) = UINT32_MAX;
  }
  return (void *)p;
}
void *bb_crt_stream_configure_entry(const char *name, const char *mode,
                                    int32_t sharing, void *stream) {
  uintptr_t p = (uintptr_t)stream, m = (uintptr_t)mode;
  uint32_t flags = BB_U32(0x48e1c8), open_flags;
  uint8_t committed = 0, access_hint = 0, parsing = 1;
  if (!name)
    assertion(0x482f28, 0x47, 0x482f30);
  if (!m)
    assertion(0x482f28, 0x48, 0x4824cc);
  if (!p)
    assertion(0x482f28, 0x49, 0x4824ac);
  if (BYTE(m) == 'a') {
    open_flags = 0x109;
    flags |= 2;
  } else if (BYTE(m) == 'r') {
    open_flags = 0;
    flags |= 1;
  } else if (BYTE(m) == 'w') {
    open_flags = 0x301;
    flags |= 2;
  } else
    return NULL;
  while (BYTE(++m) && parsing) {
    switch (BYTE(m)) {
    case '+':
      if (open_flags & 2)
        parsing = 0;
      else {
        open_flags = (open_flags & ~1u) | 2;
        flags = (flags & ~3u) | 0x80;
      }
      break;
    case 'b':
    case 't':
      if (open_flags & 0xc000)
        parsing = 0;
      else
        open_flags |= BYTE(m) == 'b' ? 0x8000 : 0x4000;
      break;
    case 'c':
    case 'n':
      if (committed)
        parsing = 0;
      else {
        committed = 1;
        if (BYTE(m) == 'c')
          flags |= 0x4000;
        else
          flags &= ~0x4000u;
      }
      break;
    case 'R':
    case 'S':
      if (access_hint)
        parsing = 0;
      else {
        access_hint = 1;
        open_flags |= BYTE(m) == 'R' ? 0x10 : 0x20;
      }
      break;
    case 'D':
    case 'T': {
      uint32_t flag = BYTE(m) == 'D' ? 0x40 : 0x1000;
      if (open_flags & flag)
        parsing = 0;
      else
        open_flags |= flag;
      break;
    }
    default:
      parsing = 0;
      break;
    }
  }
  int32_t handle =
      bb_crt_descriptor_open_entry(name, open_flags, sharing, 0x1a4);
  if (handle < 0)
    return NULL;
  ++BB_U32(0x48cf88);
  BB_U32(p + 12) = flags;
  BB_U32(p + 4) = 0;
  BB_U32(p) = 0;
  BB_U32(p + 8) = BB_U32(p);
  BB_U32(p + 28) = BB_U32(p + 8);
  BB_U32(p + 16) = handle;
  return stream;
}
void *bb_crt_stream_shared_open_entry(const char *name, const char *mode,
                                      int32_t sharing) {
  if (!name)
    assertion(0x4824f0, 0x35, 0x4824f8);
  if (!BYTE((uintptr_t)name))
    assertion(0x4824f0, 0x36, 0x4824dc);
  if (!mode)
    assertion(0x4824f0, 0x37, 0x4824cc);
  if (!BYTE((uintptr_t)mode))
    assertion(0x4824f0, 0x38, 0x4824b8);
  void *stream = bb_crt_stream_acquire_entry();
  return stream ? bb_crt_stream_configure_entry(name, mode, sharing, stream)
                : NULL;
}
void *bb_crt_stream_open_entry(const char *name, const char *mode) {
  return bb_crt_stream_shared_open_entry(name, mode, 0x40);
}
