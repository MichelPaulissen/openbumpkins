/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "game_file.h"
#endif
#include <stddef.h>
#include <string.h>

/* The game's CRT owns these opaque FILE objects. Keep its I/O calls during
 * reconstruction rather than passing Windows FILE objects to Linux libc. */
typedef uint32_t (*file_io_fn)(void *, uint32_t, uint32_t, void *);
typedef uint32_t(BB_THISCALL * archive_read_fn)(void *, void *,
                                                              uint32_t);

/* 0x0042dde0..0x0042de71: a zero is encoded as 0,count; nonzero bytes remain
 * literal. The original scans up to 255 zeros even across the requested block
 * boundary. Preserve that behavior: adjacent game records share storage and
 * the decoder likewise permits the final run to extend past that boundary. */
uint32_t bb_save_encode(const unsigned char *source, uint32_t size,
                        uint32_t count, void *file) {
  unsigned char *scratch = BB_DATA(0x00600180);
  source = BB_DATA(source);
  uint32_t total = size * count, consumed = 0, length = 0;
  while (consumed < total) {
    unsigned char value = source[consumed];
    if (value) {
      scratch[length++] = value;
      consumed++;
    } else {
      uint32_t run = 0;
      do {
        run++;
      } while (source[consumed + run] == 0 && run < 255);
      scratch[length++] = 0;
      scratch[length++] = (unsigned char)run;
      consumed += run;
    }
  }
#ifdef BB_MODEL_RUNTIME
  bb_game_write(&length, 4, 1, file);
  return bb_game_write(scratch, length, 1, file);
#else
  file_io_fn write = (file_io_fn)BB_CODE((uintptr_t)0x0046cd20);
  write(&length, 4, 1, file);
  return write(scratch, length, 1, file);
#endif
}

/* 0x0042de80..0x0042df33: saves use the CRT, maps use the archive reader.
 * Both paths ignore read status, share the scratch buffer, and decode until
 * the requested output size has been reached, independently of stored length.
 * These routines retain the original format's assumptions about valid input. */
uint32_t bb_save_decode(unsigned char *destination, uint32_t size,
                        uint32_t count, void *file, uint32_t archived) {
  unsigned char *scratch = BB_DATA(0x00600180);
  uint32_t length, result;
#ifdef BB_MODEL_RUNTIME
  bb_game_read(&length, 4, 1, file, archived);
  result = bb_game_read(scratch, length, 1, file, archived);
#else
  if (archived) {
    archive_read_fn read = (archive_read_fn)BB_CODE((uintptr_t)0x00466e10);
    read(BB_POINTER(0x004842cc), &length, 4);
    result = read(BB_POINTER(0x004842cc), scratch, length);
  } else {
    file_io_fn read = (file_io_fn)BB_CODE((uintptr_t)0x0046cf40);
    read(&length, 4, 1, file);
    result = read(scratch, length, 1, file);
  }
#endif
  destination = BB_DATA(destination);
  uint32_t total = size * count, written = 0, input = 0;
  while (written < total) {
    unsigned char value = scratch[input++];
    if (value)
      destination[written++] = value;
    else {
      unsigned char run = scratch[input++];
      memset(destination + written, 0, run);
      written += run;
    }
  }
  return total ? written : result;
}

/* 0x00415d30..0x00415d62: restore an entity reference from the saved pool's
 * base to the current pool. Division is unsigned; a reference inside a record
 * is rounded down to its start, and all arithmetic wraps at 32 bits. */
uint32_t bb_entity_relocate(uint32_t *reference, uint32_t saved_base,
                            uint32_t *remainder) {
  uint32_t offset = BB_U32(reference) - saved_base;
  uint32_t base = BB_U32(0x00553f90);
  BB_U32(remainder) = offset % 0x13a;
  BB_U32(reference) = base + (offset / 0x13a) * 0x13a;
  return base;
}

#if !defined(BB_PORTABLE_RUNTIME)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
/* Retain the no-op path's ECX/EDX and the populated path's EAX/ECX/EDX
 * results. The remaining callee-saved registers follow the C ABI. */
void __attribute__((naked)) bb_entity_relocate_entry(void) {
  __asm__ volatile("movl 4(%esp), %eax\n\t"
                   "movl (%eax), %eax\n\t"
                   "testl %eax, %eax\n\t"
                   "jne 1f\n\t"
                   "ret\n"
                   "1: subl $4, %esp\n\t"
                   "pushl %esp\n\t"
                   "pushl 16(%esp)\n\t"
                   "pushl 16(%esp)\n\t"
                   "call " BB_ASM(bb_entity_relocate) "\n\t"
                                                      "addl $12, %esp\n\t"
                                                      "popl %edx\n\t"
                                                      "movl 4(%esp), %ecx\n\t"
                                                      "movl (%ecx), %ecx\n\t"
                                                      "ret");
}

#endif
