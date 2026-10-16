/* SPDX-License-Identifier: GPL-3.0-only */
#include "archive_models.h"
#include "game_calls.h"
#include <string.h>

/* Construction resets the shared writing buffer without freeing it; the
 * existing file handle remains untouched. */
void *bb_archive_name(void *object, const char *name) {
  strcpy(BB_DATA(bb_address_encode(object)), BB_DATA(bb_address_encode(name)));
  return object;
}
void *bb_archive_construct(void *object, const char *name) {
  uint32_t self = bb_address_encode(object);
  BB_U32(self + 0x100u) = 0;
  BB_U32(self + 0x108u) = 0;
  BB_U32(0x006c1b70u) = 0;
  return bb_archive_name(object, name);
}
static uint32_t archive_create(uint32_t self) {
  return BB_GAME_CALL(BB_U32(0x006ca384u), 0, self, 0x80000000u, 1, 0, 3, 0x80,
                      0);
}
static uint32_t archive_seek_file(uint32_t file, uint32_t offset,
                                  uint32_t origin) {
  return BB_GAME_CALL(BB_U32(0x006ca4b0u), 0, file, offset, 0, origin);
}

/* Unlink each directory record before deleting it. The deletion callback can
 * change the live list head, so read it again after every call. */
void *bb_archive_clear(void *object) {
  uint32_t self = bb_address_encode(object), record;
  while ((record = BB_U32(self + 0x108u))) {
    BB_U32(self + 0x108u) = BB_U32(record + 0x218u);
    BB_GAME_CALL(0x0046c110u, 0, record);
  }
  BB_U32(self + 0x108u) = 0;
  return object;
}
/* Leave the shared buffer reference intact after deletion, as in the original.
 */
uint32_t bb_archive_destroy(void *object) {
  bb_archive_clear(object);
  if (BB_U32(0x006c1b70u))
    return BB_GAME_CALL(0x0046c110u, 0, BB_U32(0x006c1b70u));
  return bb_address_encode(object);
}
uint32_t bb_archive_view_delete(void *object, uint32_t flags) {
  uint32_t self = bb_address_encode(object);
  if (flags & 1u)
    BB_GAME_CALL(0x0046c110u, 0, self);
  return self;
}

/* The scanner ignores the header, I/O status and seek failures and tests only
 * a null handle. Partial later metadata retains the previous record's bytes.
 * External buffers belong to this call's logical frame, including actual. */
uint32_t bb_archive_directory_c(void *object, uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  bb_archive_clear(object);
  uint32_t file = archive_create(self);
  if (!file)
    return 0;
  uint32_t header = scratch, record = scratch + 8u, actual = scratch + 540u;
  uint32_t read = BB_U32(0x006ca380u);
  BB_GAME_CALL(read, 0, file, header, 8, actual, 0);
  uint32_t position = archive_seek_file(file, 0, 1), previous = 0;
  for (;;) {
    BB_GAME_CALL(read, 0, file, record, 0x214, actual, 0);
    uint32_t amount = BB_U32(actual);
    if (!amount)
      break;
    position += amount;
    uint32_t entry = BB_GAME_CALL(0x0046c060u, 0, 0x21c);
    memset(BB_SPAN(entry, 0x21c), 0, 0x21c);
    BB_U32(entry + 0x218u) = 0;
    BB_U32(entry + 0x214u) = position;
    strcpy(BB_DATA(entry), BB_DATA(record));
    strcpy(BB_DATA(entry + 0x100u), BB_DATA(record + 0x100u));
    memcpy(BB_SPAN(entry + 0x200u, 16), BB_SPAN(record + 0x200u, 16), 16);
    uint32_t length = BB_U32(record + 0x210u);
    BB_U32(entry + 0x210u) = length;
    BB_U32(previous ? previous + 0x218u : self + 0x108u) = entry;
    previous = entry;
    position = archive_seek_file(file, length, 1);
  }
  BB_GAME_CALL(BB_U32(0x006ca37cu), 0, file);
  return 1;
}

/* Case comparison remains an explicit CRT boundary so locale tables retain
 * their game-defined behavior when the complete runtime is connected. */
void *bb_archive_find_name(void *object, const char *name) {
  uint32_t entry = BB_U32(bb_address_encode(object) + 0x108u);
  while (entry) {
    if (!BB_GAME_CALL(0x0046c1c0u, 0, bb_address_encode(name), entry))
      return (void *)(uintptr_t)entry;
    entry = BB_U32(entry + 0x218u);
  }
  return NULL;
}
void *bb_archive_find_index(void *object, uint32_t index) {
  uint32_t entry = BB_U32(bb_address_encode(object) + 0x108u), current = 0;
  while (entry) {
    if (current++ == index)
      return (void *)(uintptr_t)entry;
    entry = BB_U32(entry + 0x218u);
  }
  return NULL;
}
uint32_t bb_archive_open(void *object) {
  uint32_t self = bb_address_encode(object);
  if (BB_U32(self + 0x100u))
    return self;
  bb_archive_directory(object);
  uint32_t file = archive_create(self);
  BB_U32(self + 0x104u) = file;
  if (!file)
    return BB_GAME_CALL(BB_U32(0x006ca374u), 0, 0x0048c3ccu);
  BB_U32(self + 0x100u) = 1;
  return self;
}
void *bb_archive_close(void *object) {
  uint32_t self = bb_address_encode(object);
  if (BB_U32(self + 0x100u)) {
    BB_GAME_CALL(BB_U32(0x006ca37cu), 0, BB_U32(self + 0x104u));
    BB_U32(self + 0x100u) = 0;
  }
  return object;
}
/* Name lookup happens even when the archive is closed. */
void *bb_archive_member(void *object, const char *name) {
  uint32_t self = bb_address_encode(object);
  void *entry = bb_archive_find_name(object, name);
  if (!BB_U32(self + 0x100u) || !entry)
    return NULL;
  archive_seek_file(BB_U32(self + 0x104u),
                    BB_U32(bb_address_encode(entry) + 0x214u), 0);
  uint32_t view = BB_GAME_CALL(0x0046c060u, 0, 0x228);
  return view ? bb_archive_view((void *)(uintptr_t)view, object, entry) : NULL;
}
uint32_t bb_archive_release(void *member) {
  uint32_t reference = bb_address_encode(member);
  BB_GAME_CALL(0x0046c110u, 0, reference);
  return reference;
}
/* Unknown origins retain the explicit old local word; origin 2 subtracts
 * distance from the end. Seek failure does not suppress the cursor update. */
uint32_t bb_archive_seek(void *object, uint32_t distance, uint32_t origin,
                         uint32_t previous_slot) {
  uint32_t self = bb_address_encode(object), absolute = previous_slot;
  if (origin == 0)
    absolute = BB_U32(self + 0x218u) + distance;
  else if (origin == 1)
    absolute = BB_U32(self + 0x218u) + BB_U32(self + 0x220u) + distance;
  else if (origin == 2)
    absolute = BB_U32(self + 0x214u) + BB_U32(self + 0x218u) - distance;
  archive_seek_file(BB_U32(BB_U32(self) + 0x104u), absolute, 0);
  BB_U32(self + 0x220u) = absolute - BB_U32(self + 0x218u);
  return BB_U32(self + 0x220u);
}
void *bb_archive_view(void *object, void *archive, const void *record) {
  uint32_t self = bb_address_encode(object), entry = bb_address_encode(record);
  BB_SET_POINTER(self, archive);
  memcpy(BB_SPAN(self + 4u, 0x21c), BB_SPAN(entry, 0x21c), 0x21c);
  BB_U32(self + 0x220u) = 0;
  BB_U32(self + 0x224u) = BB_U32(entry + 0x210u);
  return object;
}
/* Fitting reads advance by the requested count even on short I/O. Clipped
 * reads leave the cursor unchanged. Re-read that cursor after callbacks. */
uint32_t bb_archive_read_c(void *object, void *destination, uint32_t amount,
                           uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  uint32_t cursor = BB_U32(self + 0x220u), limit = BB_U32(self + 0x224u);
  int fits = cursor + amount <= limit;
  if (!fits) {
    amount = limit - cursor;
    if (!amount)
      return 0;
  }
  uint32_t archive = BB_U32(self);
  BB_GAME_CALL(BB_U32(0x006ca380u), 0, BB_U32(archive + 0x104u),
               bb_address_encode(destination), amount, scratch, 0);
  if (fits)
    BB_U32(self + 0x220u) += amount;
  return BB_U32(scratch);
}
#ifndef BB_MODEL_RUNTIME
uint32_t bb_archive_directory(void *object) {
  unsigned char scratch[BB_ARCHIVE_DIRECTORY_BYTES];
  return bb_archive_directory_c(object, bb_address_encode(scratch));
}
uint32_t bb_archive_read(void *object, void *destination, uint32_t amount) {
  uint32_t scratch;
  return bb_archive_read_c(object, destination, amount,
                           bb_address_encode(&scratch));
}
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

void __attribute__((naked)) bb_archive_construct_entry(void) {
  __asm__ volatile("pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_construct) "\n\t"
                                                        "addl $8, %esp\n\t"
                                                        "ret $4");
}
void __attribute__((naked)) bb_archive_name_entry(void) {
  __asm__ volatile("pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_name) "\n\t"
                                                   "addl $8, %esp\n\t"
                                                   "ret $4");
}
void __attribute__((naked)) bb_archive_destroy_entry(void) {
  __asm__ volatile("cmpl $0, 0x108(%ecx)\n\t"
                   "jne 1f\n\t"
                   "cmpl $0, 0x006c1b70\n\t"
                   "jne 1f\n\t"
                   "movl %ecx, %eax\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_destroy) "\n\t"
                                                      "addl $4, %esp\n\t"
                                                      "ret");
}
/* 0x00466dfa..0x00466e0f and 0x004666ca..0x004666df have no work,
 * including on a missing source file during archive refresh. */
void __attribute__((naked)) bb_archive_view_destroy_entry(void) {
  __asm__ volatile("ret");
}
void __attribute__((naked)) bb_archive_missing_entry(void) {
  __asm__ volatile("ret $4");
}
void __attribute__((naked)) bb_archive_view_delete_entry(void) {
  __asm__ volatile(
      "testb $1, 4(%esp)\n\t"
      "jz 1f\n\t"
      "pushl 4(%esp)\n\t"
      "pushl %ecx\n\t"
      "call " BB_ASM(bb_archive_view_delete) "\n\t"
                                             "addl $8, %esp\n\t"
                                             "ret $4\n"
                                             "1: movl %ecx, %eax\n\t"
                                             "ret $4");
}
void __attribute__((naked)) bb_archive_clear_entry(void) {
  __asm__ volatile("cmpl $0, 0x108(%ecx)\n\t"
                   "jne 1f\n\t"
                   "movl $0, 0x108(%ecx)\n\t"
                   "movl %ecx, %eax\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_clear) "\n\t"
                                                    "addl $4, %esp\n\t"
                                                    "ret");
}
void __attribute__((naked)) bb_archive_directory_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_directory) "\n\t"
                                                        "addl $4, %esp\n\t"
                                                        "ret");
}
void __attribute__((naked)) bb_archive_find_name_entry(void) {
  __asm__ volatile("movl 0x108(%ecx), %eax\n\t"
                   "testl %eax, %eax\n\t"
                   "jne 1f\n\t"
                   "ret $4\n"
                   "1: pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_find_name) "\n\t"
                                                        "addl $8, %esp\n\t"
                                                        "ret $4");
}
void __attribute__((naked)) bb_archive_find_index_entry(void) {
  __asm__ volatile("pushl %edx\n\t"
                   "pushl 8(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_find_index) "\n\t"
                                                         "movl (%esp), %ecx\n\t"
                                                         "addl $8, %esp\n\t"
                                                         "popl %edx\n\t"
                                                         "ret $4");
}
void __attribute__((naked)) bb_archive_open_entry(void) {
  __asm__ volatile("movl %ecx, %eax\n\t"
                   "cmpl $0, 0x100(%ecx)\n\t"
                   "je 1f\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_open) "\n\t"
                                                   "movl (%esp), %ecx\n\t"
                                                   "addl $4, %esp\n\t"
                                                   "ret");
}
void __attribute__((naked)) bb_archive_close_entry(void) {
  __asm__ volatile("movl %ecx, %eax\n\t"
                   "cmpl $0, 0x100(%ecx)\n\t"
                   "jne 1f\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_close) "\n\t"
                                                    "addl $4, %esp\n\t"
                                                    "ret");
}
/* The original member-open boundary returns ECX holding its saved FS chain,
 * rather than self. The native bridge supplies an FS-backed Windows TEB. */
void __attribute__((naked)) bb_archive_member_entry(void) {
  __asm__ volatile("pushl %fs:0\n\t"
                   "pushl 8(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_member) "\n\t"
                                                     "addl $8, %esp\n\t"
                                                     "popl %ecx\n\t"
                                                     "ret $4");
}
void __attribute__((naked)) bb_archive_release_entry(void) {
  __asm__ volatile("cmpl $0, 4(%esp)\n\t"
                   "jne 1f\n\t"
                   "ret $4\n"
                   "1: pushl 4(%esp)\n\t"
                   "call " BB_ASM(bb_archive_release) "\n\t"
                                                      "addl $4, %esp\n\t"
                                                      "ret $4");
}
void __attribute__((naked)) bb_archive_seek_entry(void) {
  __asm__ volatile("pushl -8(%esp)\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_seek) "\n\t"
                                                   "movl (%esp), %ecx\n\t"
                                                   "addl $16, %esp\n\t"
                                                   "ret $8");
}
/* Both original boundaries use thiscall and retain self in ECX after normal
 * reads/construction. An empty clipped read also retains the caller's EDX. */
void __attribute__((naked)) bb_archive_view_entry(void) {
  __asm__ volatile("pushl 8(%esp)\n\t"
                   "pushl 8(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_view) "\n\t"
                                                   "movl (%esp), %ecx\n\t"
                                                   "addl $12, %esp\n\t"
                                                   "ret $8");
}
void __attribute__((naked)) bb_archive_read_entry(void) {
  __asm__ volatile("movl 0x220(%ecx), %eax\n\t"
                   "addl 8(%esp), %eax\n\t"
                   "cmpl 0x224(%ecx), %eax\n\t"
                   "jbe 1f\n\t"
                   "movl 0x224(%ecx), %eax\n\t"
                   "subl 0x220(%ecx), %eax\n\t"
                   "jne 1f\n\t"
                   "ret $8\n"
                   "1: pushl 8(%esp)\n\t"
                   "pushl 8(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_archive_read) "\n\t"
                                                   "movl (%esp), %ecx\n\t"
                                                   "addl $12, %esp\n\t"
                                                   "ret $8");
}

#endif
