/* SPDX-License-Identifier: GPL-3.0-only */
#include "indexed_text.h"
#include "game_calls.h"
#include "worker_runtime.h"
void *bb_archive_member(void *, const char *);
uint32_t bb_archive_read(void *, void *, uint32_t);
uint32_t bb_archive_release(void *);

struct text_frame {
  char text[264], label[256];
  uint8_t byte;
};
static void copy_text(uintptr_t out, uintptr_t in) {
  uint32_t size = 1;
  while (BB_U8(in + size - 1))
    ++size;
  while (size >= 4) {
    BB_U32(out) = BB_U32(in);
    out += 4;
    in += 4;
    size -= 4;
  }
  while (size--)
    BB_U8(out++) = BB_U8(in++);
}
/* EOF retains the previous input byte. All callback-visible local storage
 * belongs to the active logical frame on portable hosts. */
static uint32_t load(const char *filename, struct text_frame *frame) {
  if (!BB_U32(0x4885e8)) {
    for (uint32_t p = 0x6034e8; p < 0x6044e8; p += 4)
      BB_U32(p) = 0;
    BB_U32(0x4885e8) = 1;
  } else {
    for (uint32_t p = 0x6034e8; p < 0x6044e8; p += 4)
      if (BB_U32(p)) {
        BB_GAME_CALL(0x46c110u, 0, BB_U32(p));
        BB_U32(p) = 0;
      }
  }
  if (!filename)
    return frame->byte;
  void *member = bb_archive_member((void *)0x5e4198u, filename);
  if (!member)
    return frame->byte;
  uint32_t read;
  do {
    char *out = frame->text;
    do {
      read = bb_archive_read(member, &frame->byte, 1);
    } while ((frame->byte == '\n' || frame->byte == '\r') && read);
    while (frame->byte != '\n' && frame->byte != '\r' && read &&
           frame->byte != ':') {
      *out++ = frame->byte;
      read = bb_archive_read(member, &frame->byte, 1);
    }
    if (frame->byte == ':') {
      *out = 0;
      copy_text((uintptr_t)frame->label, (uintptr_t)frame->text);
      out = frame->text;
      frame->text[0] = 0;
      read = bb_archive_read(member, &frame->byte, 1);
      while (frame->byte != '\n' && frame->byte != '\r' && read) {
        *out++ = frame->byte;
        read = bb_archive_read(member, &frame->byte, 1);
      }
      if (frame->text[0]) {
        *out = 0;
        int32_t index =
            BB_GAME_CALL(0x46d230u, 0, bb_address_encode(frame->label));
        if (index >= 0 && index <= 1024) {
          uint32_t size = (uint32_t)strlen(frame->text) + 1;
          uint32_t value = BB_GAME_CALL(0x46c060u, 0, size);
          BB_U32(0x6034e8u + (uint32_t)index * 4u) = value;
          copy_text(value, (uintptr_t)frame->text);
        }
      }
    }
    frame->text[0] = 0;
  } while (read);
  bb_archive_release(member);
  return frame->byte;
}
#ifdef BB_MODEL_RUNTIME
void bb_indexed_text_load_model(uint32_t *r) {
  uint32_t scratch = r[3] + 4u;
  struct text_frame *frame = BB_SPAN(scratch + 4u, sizeof(*frame));
  frame->byte = BB_U8(scratch + 3u);
  const char *filename =
      (const char *)(uintptr_t)BB_U32(r[3] + BB_INDEXED_TEXT_BYTES + 8u);
  r[7] = load(filename, frame);
  BB_U8(scratch + 3u) = r[7];
}
void bb_indexed_text_load_entry(const char *filename) {
  BB_GAME_CALL(0x42a870u, 0, bb_address_encode(filename));
}
#else
uint32_t bb_indexed_text_load_c(const char *filename, uint32_t incoming) {
  struct text_frame frame;
  frame.byte = incoming;
  return load(filename, &frame);
}
#ifdef _WIN32
#define BB_TEXT_ASM "_bb_indexed_text_load_c"
#else
#define BB_TEXT_ASM "bb_indexed_text_load_c"
#endif
void __attribute__((naked))
bb_indexed_text_load_entry(const char *filename __attribute__((unused))) {
  __asm__ volatile(
      "movzbl -0x211(%esp),%eax;pushl %eax;pushl 8(%esp);call " BB_TEXT_ASM
      ";addl $8,%esp;movb %al,-0x211(%esp);ret");
}
#endif
