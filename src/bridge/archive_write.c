/* SPDX-License-Identifier: GPL-3.0-only */
#include "archive_models.h"
#include "game_calls.h"
#include <string.h>

static uint32_t create_file(uint32_t name, uint32_t access, uint32_t creation) {
  return BB_GAME_CALL(BB_U32(0x006ca384u), 0, name, access, 1, 0, creation,
                      0x80, 0);
}
static uint32_t seek_file(uint32_t file, uint32_t distance, uint32_t origin) {
  return BB_GAME_CALL(BB_U32(0x006ca4b0u), 0, file, distance, 0, origin);
}
static void close_file(uint32_t file) {
  BB_GAME_CALL(BB_U32(0x006ca37cu), 0, file);
}
static uint32_t debug(uint32_t message) {
  return BB_GAME_CALL(BB_U32(0x006ca374u), 0, message);
}
static void file_time(uint32_t file, uint32_t output) {
  BB_GAME_CALL(BB_U32(0x006ca4bcu), 0, file, 0, 0, output);
}

/* One shared transfer buffer. The boundary's existing-buffer path preserves
 * incoming call state; typed callers receive its four-byte reference. */
uint32_t bb_archive_buffer(void) {
  if (!BB_U32(0x006c1b70u)) {
    BB_U32(0x006c1b70u) = BB_GAME_CALL(0x0046c060u, 0, BB_U32(0x0048c368u));
    if (!BB_U32(0x006c1b70u))
      return debug(0x0048c394u);
  }
  return BB_U32(0x006c1b70u);
}

/* Cache imports before copying. Subtract requested bytes even on short I/O;
 * continue only after a full-buffer read, including a final zero read when
 * the file size is an exact multiple. API/partial-write status is ignored. */
static void copy_file(uint32_t source, uint32_t destination, uint32_t remaining,
                      uint32_t actual, uint32_t written) {
  uint32_t read = BB_U32(0x006ca380u), write = BB_U32(0x006ca4b4u);
  do {
    uint32_t amount = remaining;
    if (amount > BB_U32(0x0048c368u))
      amount = BB_U32(0x0048c368u);
    BB_GAME_CALL(read, 0, source, BB_U32(0x006c1b70u), amount, actual, 0);
    remaining -= amount;
    if (BB_U32(actual))
      BB_GAME_CALL(write, 0, destination, BB_U32(0x006c1b70u), BB_U32(actual),
                   written, 0);
  } while (BB_U32(actual) == BB_U32(0x0048c368u));
}

/* Refresh an existing basename from its stored path. Append does not extend
 * the directory. Deterministic unused header/name padding is retained from
 * the working C implementation rather than the original stack's garbage. */
uint32_t bb_archive_add_c(void *object, const char *source_path,
                          uint32_t scratch) {
  bb_archive_buffer();
  uint32_t self = bb_address_encode(object),
           path = bb_address_encode(source_path);
  const char *tail = strrchr(BB_DATA(path), '\\');
  uint32_t name = tail ? bb_address_encode(tail + 1) : path;
  void *entry = bb_archive_find_name(object, (const char *)(uintptr_t)name);
  if (entry) {
    bb_archive_replace(object, entry);
    return 1;
  }
  uint32_t box = create_file(self, 0xc0000000u, 4);
  if (!box) {
    debug(0x0048c36cu);
    return 0;
  }
  uint32_t header = scratch, record = scratch + 8u, timestamp = scratch + 540u;
  uint32_t actual = scratch + 548u, written = scratch + 552u;
  uint32_t write = BB_U32(0x006ca4b4u);
  if (!seek_file(box, 0, 2)) {
    const unsigned char bytes[8] = {'B', 'O', 'X', 0, 0, 0, 1, 0};
    memcpy(BB_SPAN(header, 8), bytes, 8);
    BB_GAME_CALL(write, 0, box, header, 8, written, 0);
  }
  uint32_t source = create_file(path, 0x80000000u, 3);
  if (!source) {
    debug(0x0048c380u);
    return 0; /* Retain the original open box handle on this path. */
  }
  memset(BB_SPAN(record, 0x214), 0, 0x214);
  strcpy(BB_DATA(record + 0x100u), BB_DATA(path));
  strcpy(BB_DATA(record), BB_DATA(name));
  file_time(source, timestamp);
  BB_GAME_CALL(BB_U32(0x006ca4b8u), 0, timestamp, record + 0x200u);
  uint32_t size = seek_file(source, 0, 2);
  BB_U32(record + 0x210u) = size;
  BB_GAME_CALL(write, 0, box, record, 0x214, written, 0);
  seek_file(source, 0, 0);
  copy_file(source, box, size, actual, written);
  close_file(source);
  close_file(box);
  return 1;
}

/* Changed-size replacement truncates, rescans and re-adds subsequent stored
 * source paths. This private native C array holds logical words only and is
 * never passed to a game callback; callback-visible buffers use scratch. */
static uint32_t replace_record(void *object, void *record, uint32_t scratch,
                               int *invalidated) {
  if (invalidated)
    *invalidated = 0;
  uint32_t self = bb_address_encode(object), entry = bb_address_encode(record);
  bb_archive_buffer();
  uint32_t box = create_file(self, 0xc0000000u, 3);
  if (!box) {
    debug(0x0048c3a4u);
    return 0;
  }
  seek_file(box, BB_U32(entry + 0x214u) - 0x214u, 0);
  uint32_t source = create_file(entry + 0x100u, 0x80000000u, 3);
  if (!source) {
    debug(0x0048c3b8u);
    return 0;
  }
  uint32_t timestamp = scratch, actual = scratch + 8u, written = scratch + 12u;
  file_time(source, timestamp);
  BB_GAME_CALL(BB_U32(0x006ca4b8u), 0, timestamp, entry + 0x200u);
  uint32_t size = seek_file(source, 0, 2);
  seek_file(source, 0, 0);
  int changed = BB_U32(entry + 0x210u) != size;
  BB_U32(entry + 0x210u) = size;
  uint32_t write = BB_U32(0x006ca4b4u);
  BB_GAME_CALL(write, 0, box, entry, 0x214, written, 0);
  copy_file(source, box, size, actual, written);
  if (!changed) {
    close_file(source);
    close_file(box);
    return 1;
  }
  seek_file(box, 0, 1);
  BB_GAME_CALL(BB_U32(0x006ca480u), 0, box);
  close_file(source);
  close_file(box);
  uint32_t paths[4096];
  int count = 0;
  for (uint32_t next = BB_U32(entry + 0x218u); next;
       next = BB_U32(next + 0x218u)) {
    uint32_t path = next + 0x100u;
    uint32_t copy =
        BB_GAME_CALL(0x0046c060u, 0, (uint32_t)strlen(BB_DATA(path)) + 1u);
    paths[count++] = copy;
    strcpy(BB_DATA(copy), BB_DATA(path));
  }
  if (invalidated)
    *invalidated = 1;
  bb_archive_directory(object);
  for (int n = 0; n < count; n++) {
    bb_archive_add(object, (const char *)(uintptr_t)paths[n]);
    BB_GAME_CALL(0x0046c110u, 0, paths[n]);
  }
  return 1;
}

uint32_t bb_archive_replace_c(void *object, void *record, uint32_t scratch) {
  return replace_record(object, record, scratch, NULL);
}

/* A size-changing replacement frees the directory before appending its tail.
 * Keep the original refresh inputs in private C storage, and resolve fresh
 * offsets only when another changed member needs replacement after a rebuild.
 * Ordinary refreshes retain their live record pointers and call sequence. */
struct refresh_record {
  uint32_t reference;
  unsigned char metadata[0x214];
};
struct refresh_work {
  void *object;
  struct refresh_record *records;
  size_t length;
  uint32_t scratch, count, source;
};
static void refresh_records(struct refresh_work *work) {
  int stale = 0, dirty = 0;
  uint32_t saved = work->scratch, current = saved + 8u;
  for (size_t n = 0; n < work->length; n++) {
    uint32_t entry = work->records[n].reference;
    if (stale) {
      entry = work->scratch + 16u;
      memcpy(BB_SPAN(entry, 0x214), work->records[n].metadata, 0x214);
    }
    work->source = create_file(entry + 0x100u, 0x80000000u, 3);
    if (!work->source)
      continue;
    BB_GAME_CALL(BB_U32(0x006ca4a0u), 0, entry + 0x200u, saved);
    file_time(work->source, current);
    if (BB_U64(saved) != BB_U64(current)) {
      uint32_t target = entry;
      if (stale) {
        if (dirty) {
          bb_archive_directory(work->object);
          dirty = 0;
        }
        target =
            bb_address_encode(bb_archive_find_index(work->object, (uint32_t)n));
      }
      int invalidated = 0;
      if (target)
        replace_record(work->object, (void *)(uintptr_t)target, work->scratch,
                       &invalidated);
      /* A failed rescan still counts this attempted timestamp update. */
      ++work->count;
      if (invalidated)
        stale = dirty = 1;
    }
    close_file(work->source);
    work->source = 0;
  }
}
#ifdef BB_MODEL_RUNTIME
/* The native context never enters packed game state. A nested refresh borrows
 * its own context and logical frame; recovery restores the prior context. */
static _Thread_local struct refresh_work *active_refresh;
static void refresh_body(uint32_t *unused) {
  (void)unused;
  refresh_records(active_refresh);
}
#endif
uint32_t bb_archive_refresh_c(void *object, uint32_t scratch) {
  uint32_t first = BB_U32(bb_address_encode(object) + 0x108u);
  size_t length = 0;
  for (uint32_t entry = first; entry; entry = BB_U32(entry + 0x218u)) {
    if (length == SIZE_MAX / sizeof(struct refresh_record))
      abort();
    ++length;
  }
  if (!length)
    return 0;
  struct refresh_record *records = malloc(length * sizeof(*records));
  if (!records)
    abort();
  uint32_t entry = first;
  for (size_t n = 0; n < length; n++) {
    records[n].reference = entry;
    memcpy(records[n].metadata, BB_SPAN(entry, 0x214), 0x214);
    entry = BB_U32(entry + 0x218u);
  }
  struct refresh_work work = {object, records, length, scratch, 0, 0};
#ifdef BB_MODEL_RUNTIME
  struct refresh_work *previous = active_refresh;
  active_refresh = &work;
  uint32_t input[10] = {0}, error;
  int succeeded = bb_model_try_body(refresh_body, input, &error);
  active_refresh = previous;
  free(records);
  if (!succeeded) {
    if (work.source)
      close_file(work.source);
    bb_model_raise(error);
  }
#else
  refresh_records(&work);
  free(records);
#endif
  return work.count;
}
#ifndef BB_MODEL_RUNTIME
uint32_t bb_archive_add(void *object, const char *path) {
  unsigned char scratch[BB_ARCHIVE_ADD_BYTES];
  return bb_archive_add_c(object, path, bb_address_encode(scratch));
}
uint32_t bb_archive_replace(void *object, void *entry) {
  unsigned char scratch[BB_ARCHIVE_REPLACE_BYTES];
  return bb_archive_replace_c(object, entry, bb_address_encode(scratch));
}
uint32_t bb_archive_refresh(void *object) {
  unsigned char scratch[BB_ARCHIVE_REFRESH_BYTES];
  return bb_archive_refresh_c(object, bb_address_encode(scratch));
}
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

void __attribute__((naked)) bb_archive_buffer_entry(void) {
  __asm__ volatile("cmpl $0, 0x006c1b70\n\t"
                   "je 1f\n\t"
                   "ret\n"
                   "1: jmp " BB_ASM(bb_archive_buffer));
}
void __attribute__((naked)) bb_archive_add_entry(void) {
  __asm__ volatile("pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_add) "\n\t"
                                                  "addl $8, %esp\n\t"
                                                  "ret $4");
}
void __attribute__((naked)) bb_archive_replace_entry(void) {
  __asm__ volatile("pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_replace) "\n\t"
                                                      "addl $8, %esp\n\t"
                                                      "ret $4");
}
void __attribute__((naked)) bb_archive_refresh_entry(void) {
  __asm__ volatile("cmpl $0, 0x108(%ecx)\n\t"
                   "jne 1f\n\t"
                   "xorl %eax, %eax\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_refresh) "\n\t"
                                                      "addl $4, %esp\n\t"
                                                      "ret");
}

#endif
