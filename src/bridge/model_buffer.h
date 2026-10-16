/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MODEL_BUFFER_H
#define BB_MODEL_BUFFER_H
#include "address.h"
#include "model_runtime.h"
#include <string.h>

/* Existing mapped arguments retain their references and aliasing. Only private
 * native C buffers need a temporary packed copy for a nested model call. End
 * copied buffers in reverse order, optionally preserving all output bytes. */
struct bb_model_buffer {
  struct bb_model_scratch scratch;
  const void *native;
  size_t bytes;
  uint32_t reference;
  unsigned copied;
};
static inline void bb_model_buffer_begin(struct bb_model_buffer *buffer,
                                         const void *data, size_t bytes) {
  *buffer = (struct bb_model_buffer){.native = data, .bytes = bytes};
  if (bb_address_try_encode(data, &buffer->reference))
    return;
  bb_model_scratch_begin(&buffer->scratch, bytes ? bytes : 1);
  buffer->reference = buffer->scratch.address;
  buffer->copied = 1;
  if (bytes)
    memcpy(BB_SPAN(buffer->reference, bytes), data, bytes);
}
static inline void bb_model_buffer_end(const struct bb_model_buffer *buffer,
                                       unsigned write_back) {
  if (buffer->copied) {
    if (write_back && buffer->bytes)
      memcpy((void *)buffer->native, BB_SPAN(buffer->reference, buffer->bytes),
             buffer->bytes);
    bb_model_scratch_end(&buffer->scratch);
  }
}
#endif
