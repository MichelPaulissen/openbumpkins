/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "crt_descriptor.h"
#include "crt_errors.h"
#include "crt_bootstrap.h"
#include "crt_primitives.h"
#include "interop.h"
#include "game_calls.h"
#include <signal.h>
#ifdef BB_MODEL_RUNTIME
#include "model_buffer.h"
#endif
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define WIN BB_STDCALL
#ifdef BB_MODEL_RUNTIME
static uint32_t last_error(void) { return BB_GAME_CALL(BB_U32(0x6ca43c), 0); }
static uint32_t file_type(uint32_t handle) {
  return BB_GAME_CALL(BB_U32(0x6ca498), 0, handle);
}
static uint32_t close_handle(uint32_t handle) {
  return BB_GAME_CALL(BB_U32(0x6ca37c), 0, handle);
}
static uint32_t transfer(uint32_t api, uint32_t handle, void *data,
                          uint32_t bytes, uint32_t *amount, unsigned reading) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, data, bytes);
  struct bb_model_scratch count;
  bb_model_scratch_begin(&count, 4);
  BB_U32(count.address) = 0;
  uint32_t result = BB_GAME_CALL(BB_U32(api), 0, handle, buffer.reference,
                                 bytes, count.address, 0);
  BB_U32(amount) = BB_U32(count.address);
  bb_model_scratch_end(&count);
  bb_model_buffer_end(&buffer, reading);
  return result;
}
static uint32_t read_handle(uint32_t handle, void *out, uint32_t bytes,
                            uint32_t *got) {
  return transfer(0x6ca380, handle, out, bytes, got, 1);
}
static uint32_t write_handle(uint32_t handle, const void *in, uint32_t bytes,
                             uint32_t *written) {
  return transfer(0x6ca4b4, handle, (void *)in, bytes, written, 0);
}
static void standard_handle(uint32_t number, uint32_t handle) {
  BB_GAME_CALL(BB_U32(0x6ca400), 0, number, handle);
}
#else
static uint32_t last_error(void) {
  return ((uint32_t(WIN *)(void))BB_CODE(BB_POINTER(0x6ca43c)))();
}
static uint32_t file_type(uint32_t handle) {
  return ((uint32_t(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca498)))(handle);
}
static uint32_t close_handle(uint32_t handle) {
  return ((uint32_t(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca37c)))(handle);
}
static uint32_t read_handle(uint32_t handle, void *out, uint32_t bytes,
                            uint32_t *got) {
  return ((uint32_t(WIN *)(uint32_t, void *, uint32_t, uint32_t *,
                           void *))BB_CODE(BB_POINTER(0x6ca380)))(handle, out, bytes,
                                                         got, NULL);
}
static uint32_t write_handle(uint32_t handle, const void *in, uint32_t bytes,
                             uint32_t *written) {
  return ((uint32_t(WIN *)(uint32_t, const void *, uint32_t, uint32_t *,
                           void *))BB_CODE(BB_POINTER(0x6ca4b4)))(handle, in, bytes,
                                                         written, NULL);
}
static void standard_handle(uint32_t number, uint32_t handle) {
  ((uint32_t(WIN *)(uint32_t, uint32_t))BB_CODE(BB_POINTER(0x6ca400)))(number, handle);
}
#endif
static uintptr_t descriptor(uint32_t fd) {
  return BB_U32(0x6c8aa0 + ((int32_t)(fd & ~31u) >> 3)) + (fd & 31) * 8;
}
static int valid(uint32_t fd) {
  return fd < BB_U32(0x6c8ba0) && (BYTE(descriptor(fd) + 4) & 1);
}
static void invalid(void) {
  BB_U32(0x48cba8) = 9;
  BB_U32(0x48cbac) = 0;
}
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
static void *allocate(uintptr_t file, uint32_t line) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(0x0046d690u, 0, 256, 2, (uint32_t)file, line);
#else
  return ((void *(*)(uint32_t, int32_t, const char *, uint32_t))BB_CODE(0x46d690))(
      256, 2, (const char *)file, line);
#endif
}
static const uint32_t error_pairs[][2] = {
    {1, 22},   {2, 2},    {3, 2},    {4, 24},   {5, 13},   {6, 9},    {7, 12},
    {8, 12},   {9, 12},   {10, 7},   {11, 8},   {12, 22},  {13, 22},  {15, 2},
    {16, 13},  {17, 18},  {18, 2},   {33, 13},  {53, 2},   {65, 13},  {67, 2},
    {80, 17},  {82, 13},  {83, 13},  {87, 22},  {89, 11},  {108, 13}, {109, 32},
    {112, 28}, {114, 9},  {6, 22},   {128, 10}, {129, 10}, {130, 9},  {131, 22},
    {132, 13}, {145, 41}, {158, 13}, {161, 2},  {164, 11}, {167, 13}, {183, 17},
    {206, 2},  {215, 11}, {1816, 12}};
void bb_crt_descriptor_error_entry(uint32_t error) {
  BB_U32(0x48cbac) = error;
  for (unsigned n = 0; n < 45; ++n)
    if (error_pairs[n][0] == error) {
      BB_U32(0x48cba8) = error_pairs[n][1];
      return;
    }
  BB_U32(0x48cba8) = error >= 19 && error <= 36     ? 13
                     : error >= 188 && error <= 202 ? 8
                                                    : 22;
}
int32_t bb_crt_descriptor_terminal_entry(int32_t fd) {
  return (uint32_t)fd < BB_U32(0x6c8ba0) ? BYTE(descriptor(fd) + 4) & 0x40 : 0;
}
uint32_t bb_crt_descriptor_get_handle_entry(int32_t fd) {
  if (valid(fd))
    return BB_U32(descriptor(fd));
  invalid();
  return UINT32_MAX;
}
int32_t bb_crt_descriptor_set_handle_entry(int32_t fd, uint32_t handle) {
  if ((uint32_t)fd >= BB_U32(0x6c8ba0) ||
      BB_U32(descriptor(fd)) != UINT32_MAX) {
    invalid();
    return -1;
  }
  if (BB_U32(0x48cc60) == 1 && (uint32_t)fd < 3)
    standard_handle(0xfffffff6u - (uint32_t)fd, handle);
  BB_U32(descriptor(fd)) = handle;
  return 0;
}
int32_t bb_crt_descriptor_free_handle_entry(int32_t fd) {
  if (!valid(fd) || BB_U32(descriptor(fd)) == UINT32_MAX) {
    invalid();
    return -1;
  }
  if (BB_U32(0x48cc60) == 1 && (uint32_t)fd < 3)
    standard_handle(0xfffffff6u - (uint32_t)fd, 0);
  BB_U32(descriptor(fd)) = UINT32_MAX;
  return 0;
}
int32_t bb_crt_descriptor_close_entry(int32_t fd) {
  if (!valid(fd)) {
    invalid();
    return -1;
  }
  uint32_t error = 0;
  unsigned shared = 0;
  if (fd == 1 || fd == 2) {
    uint32_t stderr_handle = bb_crt_descriptor_get_handle_entry(2);
    uint32_t stdout_handle = bb_crt_descriptor_get_handle_entry(1);
    shared = stderr_handle == stdout_handle;
  }
  if (!shared && !close_handle(bb_crt_descriptor_get_handle_entry(fd)))
    error = last_error();
  bb_crt_descriptor_free_handle_entry(fd);
  if (!error) {
    BYTE(descriptor(fd) + 4) = 0;
    return 0;
  }
  bb_crt_descriptor_error_entry(error);
  return -1;
}
int32_t bb_crt_descriptor_seek_entry(int32_t fd, int32_t offset,
                                     int32_t origin) {
  if (!valid(fd)) {
    invalid();
    return -1;
  }
  uint32_t handle = bb_crt_descriptor_get_handle_entry(fd);
  if (handle == UINT32_MAX) {
    BB_U32(0x48cba8) = 9;
    return -1;
  }
#ifdef BB_MODEL_RUNTIME
  uint32_t result = BB_GAME_CALL(BB_U32(0x6ca4b0), 0, handle, offset, 0, origin);
#else
  uint32_t result =
      ((uint32_t(WIN *)(uint32_t, int32_t, int32_t *, uint32_t))BB_CODE(BB_POINTER(
          0x6ca4b0)))(handle, offset, NULL, origin);
#endif
  uint32_t error = result == UINT32_MAX ? last_error() : 0;
  if (!error)
    BYTE(descriptor(fd) + 4) &= ~2u;
  else {
    bb_crt_descriptor_error_entry(error);
    result = UINT32_MAX;
  }
  return result;
}
int32_t bb_crt_descriptor_commit_entry(int32_t fd) {
  if (valid(fd) && (BYTE(descriptor(fd) + 4) & 1)) {
    uint32_t handle = bb_crt_descriptor_get_handle_entry(fd);
#ifdef BB_MODEL_RUNTIME
    uint32_t result = BB_GAME_CALL(BB_U32(0x6ca3fc), 0, handle);
#else
    uint32_t result = ((uint32_t(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca3fc)))(handle);
#endif
    uint32_t error = result ? 0 : last_error();
    if (!error)
      return 0;
    BB_U32(0x48cbac) = error;
  }
  BB_U32(0x48cba8) = 9;
  return -1;
}
int32_t bb_crt_descriptor_mode_entry(int32_t fd, int32_t mode) {
  if (!valid(fd)) {
    BB_U32(0x48cba8) = 9;
    return -1;
  }
  uint8_t old = BYTE(descriptor(fd) + 4);
  if (mode == 0x8000)
    BYTE(descriptor(fd) + 4) &= ~0x80u;
  else if (mode == 0x4000)
    BYTE(descriptor(fd) + 4) |= 0x80;
  else {
    BB_U32(0x48cba8) = 22;
    return -1;
  }
  return old & 0x80 ? 0x4000 : 0x8000;
}
static void initialize_page(uintptr_t table_slot, uintptr_t start) {
  for (uintptr_t p = start; p < BB_U32(table_slot) + 256; p += 8) {
    BYTE(p + 4) = 0;
    BB_U32(p) = UINT32_MAX;
    BYTE(p + 5) = 10;
  }
}
int32_t bb_crt_descriptor_acquire_entry(void) {
  for (uint32_t n = 0; n < 64; ++n) {
    uintptr_t slot = 0x6c8aa0 + n * 4;
    if (!BB_U32(slot)) {
      void *page = allocate(0x483480, 0x79);
      if (!page)
        return -1;
      BB_SET_POINTER(slot, page);
      BB_U32(0x6c8ba0) += 32;
      initialize_page(slot, (uintptr_t)page);
      return n << 5;
    }
    for (uintptr_t p = BB_U32(slot); p < BB_U32(slot) + 256; p += 8)
      if (!(BYTE(p + 4) & 1)) {
        BB_U32(p) = UINT32_MAX;
        return ((p - BB_U32(slot)) >> 3) + n * 32;
      }
  }
  return -1;
}
int32_t bb_crt_descriptor_initialize_entry(void) {
  void *page = allocate(0x482fa8, 0x83);
  if (!page) {
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(0x0046d620u, 0, 27);
#else
    bb_crt_boot_fatal_entry(27);
#endif
  }
  BB_U32(0x6c8ba0) = 32;
  BB_SET_POINTER(0x6c8aa0, page);
  initialize_page(0x6c8aa0, (uintptr_t)page);
  uint32_t startup[17];
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer startup_buffer;
  bb_model_buffer_begin(&startup_buffer, startup, sizeof startup);
  BB_GAME_CALL(BB_U32(0x6ca45c), 0, startup_buffer.reference);
  bb_model_buffer_end(&startup_buffer, 1);
#else
  ((void(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca45c)))(startup);
#endif
  uintptr_t reserved = startup[13];
  if (WORD((uintptr_t)startup + 50) && reserved) {
    uint32_t count = BB_U32(reserved);
    uintptr_t flags = reserved + 4, handles = flags + count;
    if ((int32_t)count > 2047)
      count = 2048;
    uint32_t n = 1;
    while (BB_I32(0x6c8ba0) < (int32_t)count) {
      page = allocate(0x482fa8, 0xb8);
      if (!page) {
        count = BB_U32(0x6c8ba0);
        break;
      }
      BB_SET_POINTER(0x6c8aa0 + n * 4, page);
      BB_U32(0x6c8ba0) += 32;
      initialize_page(0x6c8aa0 + n * 4, (uintptr_t)page);
      ++n;
    }
    for (uint32_t fd = 0; (int32_t)fd < (int32_t)count;
         ++fd, ++flags, handles += 4)
      if (BB_U32(handles) != UINT32_MAX && (BYTE(flags) & 1) &&
          ((BYTE(flags) & 8) || file_type(BB_U32(handles)))) {
        uintptr_t p = descriptor(fd);
        BB_U32(p) = BB_U32(handles);
        BYTE(p + 4) = BYTE(flags);
      }
  }
  for (uint32_t fd = 0; fd < 3; ++fd) {
    uintptr_t p = BB_U32(0x6c8aa0) + fd * 8;
    if (BB_U32(p) == UINT32_MAX) {
      BYTE(p + 4) = 0x81;
#ifdef BB_MODEL_RUNTIME
      uint32_t handle = BB_GAME_CALL(BB_U32(0x6ca410), 0, 0xfffffff6u - fd);
#else
      uint32_t handle =
          ((uint32_t(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca410)))(0xfffffff6u - fd);
#endif
      uint32_t type;
      if (handle == UINT32_MAX || !(type = file_type(handle)))
        BYTE(p + 4) |= 0x40;
      else {
        BB_U32(p) = handle;
        if ((uint8_t)type == 2)
          BYTE(p + 4) |= 0x40;
        else if ((uint8_t)type == 3)
          BYTE(p + 4) |= 8;
      }
    } else
      BYTE(p + 4) |= 0x80;
  }
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(0x6ca49c), 0, BB_U32(0x6c8ba0));
#else
  return ((uint32_t(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca49c)))(BB_U32(0x6c8ba0));
#endif
}
int32_t bb_crt_descriptor_write_entry(int32_t fd, const void *data,
                                      uint32_t bytes) {
  if (!valid(fd)) {
    invalid();
    return -1;
  }
  uint32_t inserted = 0, total = 0, error = 0, amount;
  if (!bytes)
    return 0;
  if (BYTE(descriptor(fd) + 4) & 0x20)
    bb_crt_descriptor_seek_entry(fd, 0, 2);
  if (!(BYTE(descriptor(fd) + 4) & 0x80)) {
    if (!write_handle(BB_U32(descriptor(fd)), data, bytes, &amount))
      error = last_error();
    else
      total = amount;
  } else {
    char buffer[1028];
    uintptr_t source = (uintptr_t)data;
    while ((uint32_t)(source - (uintptr_t)data) < bytes) {
      uint32_t n = 0;
      while (n < 1024 && (uint32_t)(source - (uintptr_t)data) < bytes) {
        uint8_t c = BYTE(source++);
        if (c == '\n') {
          ++inserted;
          buffer[n++] = '\r';
        }
        buffer[n++] = c;
      }
      if (!write_handle(BB_U32(descriptor(fd)), buffer, n, &amount)) {
        error = last_error();
        break;
      }
      total += amount;
      if ((int32_t)amount < (int32_t)n)
        break;
    }
  }
  if (total)
    return total - inserted;
  if (error) {
    if (error == 5) {
      BB_U32(0x48cba8) = 9;
      BB_U32(0x48cbac) = error;
    } else
      bb_crt_descriptor_error_entry(error);
    return -1;
  }
  if ((BYTE(descriptor(fd) + 4) & 0x40) && BYTE((uintptr_t)data) == 26)
    return 0;
  BB_U32(0x48cba8) = 28;
  BB_U32(0x48cbac) = 0;
  return -1;
}
int32_t bb_crt_descriptor_read_entry(int32_t fd, void *data, uint32_t bytes) {
  if (!valid(fd)) {
    invalid();
    return -1;
  }
  uint32_t count = 0, got;
  uintptr_t destination = (uintptr_t)data, next = destination;
  if (!bytes || (BYTE(descriptor(fd) + 4) & 2))
    return 0;
  if ((BYTE(descriptor(fd) + 4) & 0x48) && BYTE(descriptor(fd) + 5) != 10) {
    BYTE(next++) = BYTE(descriptor(fd) + 5);
    count = 1;
    --bytes;
    BYTE(descriptor(fd) + 5) = 10;
  }
  if (!read_handle(BB_U32(descriptor(fd)), (void *)next, bytes, &got)) {
    uint32_t error = last_error();
    if (error == 5) {
      BB_U32(0x48cba8) = 9;
      BB_U32(0x48cbac) = 5;
    } else if (error == 109)
      return 0;
    else
      bb_crt_descriptor_error_entry(error);
    return -1;
  }
  count += got;
  if (!(BYTE(descriptor(fd) + 4) & 0x80))
    return count;
  if (!got || BYTE(destination) != '\n')
    BYTE(descriptor(fd) + 4) &= ~4u;
  else
    BYTE(descriptor(fd) + 4) |= 4;
  uintptr_t in = destination, out = destination;
  while (in < destination + count) {
    uint8_t c = BYTE(in);
    if (c == 26) {
      if (!(BYTE(descriptor(fd) + 4) & 0x40))
        BYTE(descriptor(fd) + 4) |= 2;
      break;
    }
    if (c != '\r') {
      BYTE(out++) = BYTE(in++);
      continue;
    }
    if (in < destination + count - 1) {
      if (BYTE(in + 1) == '\n') {
        in += 2;
        BYTE(out++) = '\n';
      } else
        BYTE(out++) = BYTE(in++);
      continue;
    }
    ++in;
    uint8_t peek;
    uint32_t error = 0;
    if (!read_handle(BB_U32(descriptor(fd)), &peek, 1, &got))
      error = last_error();
    if (error || !got) {
      BYTE(out++) = '\r';
      continue;
    }
    if (BYTE(descriptor(fd) + 4) & 0x48) {
      if (peek == '\n')
        BYTE(out) = '\n';
      else {
        BYTE(out) = '\r';
        BYTE(descriptor(fd) + 5) = peek;
      }
      ++out;
    } else if (out == destination && peek == '\n')
      BYTE(out++) = '\n';
    else {
      bb_crt_descriptor_seek_entry(fd, -1, 1);
      if (peek != '\n')
        BYTE(out++) = '\r';
    }
  }
  return out - destination;
}
int32_t bb_crt_descriptor_resize_entry(int32_t fd, int32_t size) {
  if (!valid(fd)) {
    BB_U32(0x48cba8) = 9;
    return -1;
  }
  if (size < 0)
    assertion(0x483550, 0x7e, 0x48355c);
  int32_t initial = bb_crt_descriptor_seek_entry(fd, 0, 1);
  if (initial == -1)
    return -1;
  int32_t end = bb_crt_descriptor_seek_entry(fd, 0, 2);
  if (end == -1)
    return -1;
  uint32_t remaining = (uint32_t)size - (uint32_t)end;
  int32_t result = 0;
  if ((int32_t)remaining > 0) {
    uint8_t zeros[4096];
    bb_crt_memory_fill_entry(zeros, 0, 4096);
    int32_t old_mode = bb_crt_descriptor_mode_entry(fd, 0x8000);
    do {
      uint32_t amount = (int32_t)remaining < 4096 ? remaining : 4096;
      int32_t written = bb_crt_descriptor_write_entry(fd, zeros, amount);
      if (written == -1) {
        if (BB_U32(0x48cbac) == 5)
          BB_U32(0x48cba8) = 13;
        result = -1;
        break;
      }
      remaining -= (uint32_t)written;
    } while ((int32_t)remaining > 0);
    bb_crt_descriptor_mode_entry(fd, old_mode);
  } else if ((int32_t)remaining < 0) {
    bb_crt_descriptor_seek_entry(fd, size, 0);
    uint32_t handle = bb_crt_descriptor_get_handle_entry(fd);
#ifdef BB_MODEL_RUNTIME
    if (!BB_GAME_CALL(BB_U32(0x6ca480), 0, handle)) {
#else
    if (!((uint32_t(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca480)))(handle)) {
#endif
      result = -1;
      BB_U32(0x48cba8) = 13;
      BB_U32(0x48cbac) = last_error();
    }
  }
  bb_crt_descriptor_seek_entry(fd, initial, 0);
  return result;
}
int32_t bb_crt_descriptor_open_entry(const char *name, uint32_t flags,
                                     int32_t sharing, uint32_t permissions) {
  uint32_t security[3] = {12, 0, !(flags & 0x80)};
  uint8_t mode = flags & 0x80 ? 0x10 : 0;
  if (!(flags & 0x8000) && ((flags & 0x4000) || BB_U32(0x48e46c) != 0x8000))
    mode |= 0x80;
  uint32_t access, share, creation;
  switch (flags & 3) {
  case 0:
    access = 0x80000000;
    break;
  case 1:
    access = 0x40000000;
    break;
  case 2:
    access = 0xc0000000;
    break;
  default:
    goto invalid_mode;
  }
  switch (sharing) {
  case 0x10:
    share = 0;
    break;
  case 0x20:
    share = 1;
    break;
  case 0x30:
    share = 2;
    break;
  case 0x40:
    share = 3;
    break;
  default:
    goto invalid_mode;
  }
  switch (flags & 0x700) {
  case 0:
  case 0x400:
    creation = 3;
    break;
  case 0x100:
    creation = 4;
    break;
  case 0x200:
  case 0x600:
    creation = 5;
    break;
  case 0x300:
    creation = 2;
    break;
  case 0x500:
  case 0x700:
    creation = 1;
    break;
  default:
    goto invalid_mode;
  }
  uint32_t attributes = 0x80;
  if ((flags & 0x100) && !(~BB_U32(0x48cbb0) & permissions & 0x80))
    attributes = 1;
  if (flags & 0x40) {
    attributes |= 0x4000000;
    access |= 0x10000;
  }
  if (flags & 0x1000)
    attributes |= 0x100;
  if (flags & 0x20)
    attributes |= 0x8000000;
  else if (flags & 0x10)
    attributes |= 0x10000000;
  int32_t fd = bb_crt_descriptor_acquire_entry();
  if (fd == -1) {
    BB_U32(0x48cba8) = 24;
    BB_U32(0x48cbac) = 0;
    return -1;
  }
#ifdef BB_MODEL_RUNTIME
  uint32_t name_reference;
  size_t name_bytes = bb_address_try_encode(name, &name_reference) ? 0
                          : strlen(name) + 1;
  struct bb_model_buffer name_buffer, security_buffer;
  bb_model_buffer_begin(&name_buffer, name, name_bytes);
  bb_model_buffer_begin(&security_buffer, security, sizeof security);
  uint32_t handle = BB_GAME_CALL(BB_U32(0x6ca384), 0, name_buffer.reference,
                                 access, share, security_buffer.reference,
                                 creation, attributes, 0);
  bb_model_buffer_end(&security_buffer, 0);
  bb_model_buffer_end(&name_buffer, 0);
#else
  uint32_t handle =
      ((uint32_t(WIN *)(const char *, uint32_t, uint32_t, void *, uint32_t,
                        uint32_t, uint32_t))BB_CODE(BB_POINTER(0x6ca384)))(
          name, access, share, security, creation, attributes, 0);
#endif
  if (handle == UINT32_MAX) {
    bb_crt_descriptor_error_entry(last_error());
    return -1;
  }
  uint32_t type = file_type(handle);
  if (!type) {
    close_handle(handle);
    bb_crt_descriptor_error_entry(last_error());
    return -1;
  }
  if (type == 2)
    mode |= 0x40;
  else if (type == 3)
    mode |= 8;
  bb_crt_descriptor_set_handle_entry(fd, handle);
  BYTE(descriptor(fd) + 4) = mode | 1;
  if (!(mode & 0x48) && (mode & 0x80) && (flags & 2)) {
    int32_t last = bb_crt_descriptor_seek_entry(fd, -1, 2);
    if (last == -1) {
      if (BB_U32(0x48cbac) != 131) {
        bb_crt_descriptor_close_entry(fd);
        return -1;
      }
    } else {
      uint8_t ch = 0;
      int32_t read = bb_crt_descriptor_read_entry(fd, &ch, 1);
      if (!read && ch == 26 && bb_crt_descriptor_resize_entry(fd, last) == -1) {
        bb_crt_descriptor_close_entry(fd);
        return -1;
      }
      if (bb_crt_descriptor_seek_entry(fd, 0, 0) == -1) {
        bb_crt_descriptor_close_entry(fd);
        return -1;
      }
    }
  }
  if (!(mode & 0x48) && (flags & 8))
    BYTE(descriptor(fd) + 4) |= 0x20;
  return fd;
invalid_mode:
  BB_U32(0x48cba8) = 22;
  BB_U32(0x48cbac) = 0;
  return -1;
}
