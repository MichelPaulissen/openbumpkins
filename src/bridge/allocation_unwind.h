/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_ALLOCATION_UNWIND_H
#define BUMPKINS_ALLOCATION_UNWIND_H
#include <stdint.h>

#define BB_ALLOCATION_CLEANUPS(X)                                              \
  X(0x466d31u, bb_archive_member_allocation_cleanup, 24)                        \
  X(0x4672dfu, bb_wav_acquire_allocation_cleanup, 532)                          \
  X(0x46955du, bb_instrument_song_allocation_cleanup, 20)
#define BB_ALLOCATION_HANDLERS(X)                                              \
  X(0x466d3eu, bb_archive_member_allocation_handler,                            \
    bb_archive_member_allocation_cleanup)                                      \
  X(0x4672efu, bb_wav_acquire_allocation_handler,                               \
    bb_wav_acquire_allocation_cleanup)                                         \
  X(0x46956au, bb_instrument_song_allocation_handler,                           \
    bb_instrument_song_allocation_cleanup)
#define CLEANUP(address, name, offset) void name##_entry(void);
BB_ALLOCATION_CLEANUPS(CLEANUP)
#undef CLEANUP
#define HANDLER(address, name, cleanup)                                        \
  uint32_t name##_entry(void *, void *, void *, void *);
BB_ALLOCATION_HANDLERS(HANDLER)
#undef HANDLER
#endif
