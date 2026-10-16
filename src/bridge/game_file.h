/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GAME_FILE_H
#define BB_GAME_FILE_H
#include "game_calls.h"
#include "model_buffer.h"

/* The guest CRT retains ownership of FILE records. Private C temporaries are
 * borrowed only for the nested call; mapped game records keep their identity.
 * Transfer arithmetic intentionally retains the original DWORD wraparound. */
static inline uint32_t bb_game_read(void *data, uint32_t size, uint32_t count,
                                    void *file, unsigned archived) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, data, size * count);
  uint32_t result = archived ? BB_GAME_CALL(0x466e10, BB_U32(0x4842cc),
                                            buffer.reference, size * count)
                             : BB_GAME_CALL(0x46cf40, 0, buffer.reference, size,
                                            count, bb_address_encode(file));
  bb_model_buffer_end(&buffer, 1);
  return result;
}
static inline uint32_t bb_game_write(const void *data, uint32_t size,
                                     uint32_t count, void *file) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, data, size * count);
  uint32_t result = BB_GAME_CALL(0x46cd20, 0, buffer.reference, size, count,
                                 bb_address_encode(file));
  bb_model_buffer_end(&buffer, 0);
  return result;
}
static inline void *bb_game_allocate(uint32_t bytes) {
  return (void *)(uintptr_t)BB_GAME_CALL(0x46c060, 0, bytes);
}
static inline void bb_game_release(uintptr_t pointer) {
  BB_GAME_CALL(0x46c110, 0, bb_address_encode((void *)pointer));
}
static inline void *bb_game_open(const char *name, uintptr_t mode) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, name, strlen(BB_DATA(name)) + 1);
  uint32_t file = BB_GAME_CALL(0x46bdd0, 0, buffer.reference, mode);
  bb_model_buffer_end(&buffer, 0);
  return (void *)(uintptr_t)file;
}
static inline void bb_game_close(void *file) {
  BB_GAME_CALL(0x46bbd0, 0, bb_address_encode(file));
}
#endif
