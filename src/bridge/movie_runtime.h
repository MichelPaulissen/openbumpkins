/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_MOVIE_RUNTIME_H
#define BUMPKINS_MOVIE_RUNTIME_H
#include <stdint.h>
/* Keep compatibility entries independently redirectable during binary bridging.
 */
#define BB_MOVIE_M BB_THISCALL __attribute__((noinline))
#define BB_MOVIE_F BB_FASTCALL __attribute__((noinline))
uint32_t BB_MOVIE_M bb_movie_chunk_entry(void *);
uint32_t BB_MOVIE_F bb_movie_frame_read_entry(void *, uint32_t, uint32_t);
uint32_t BB_MOVIE_F bb_movie_audio_read_entry(void *, uint32_t, uint32_t);
void *__attribute__((noinline)) bb_movie_open_entry(const char *, uint32_t,
                                                    const uint32_t *);
uint32_t __attribute__((noinline)) bb_movie_run_entry(void *);
uint32_t __attribute__((noinline)) bb_movie_play_entry(const char *, uint32_t,
                                                       const uint32_t *);
uint32_t __attribute__((noinline)) bb_movie_worker_entry(void *);
uint32_t BB_MOVIE_M bb_movie_predictor_reset_entry(void *);
void BB_MOVIE_F bb_movie_block_decode_entry(uint32_t, uint32_t *, uint32_t *,
                                            uint32_t *, uint32_t *, uint32_t *,
                                            const unsigned char *, uint32_t *);
uint32_t BB_MOVIE_M bb_movie_timer_start_entry(void *);
void BB_STDCALL bb_movie_timer_entry(uint32_t, uint32_t, void *, uint32_t,
                                     uint32_t);
uint32_t BB_MOVIE_M bb_movie_timer_stop_entry(void *);
uint32_t BB_MOVIE_M bb_movie_sound_setup_entry(void *);
uint32_t BB_MOVIE_M bb_movie_interfaces_release_entry(void *);
uint32_t BB_MOVIE_M bb_movie_sound_start_entry(void *);
uint32_t BB_MOVIE_M bb_movie_sound_release_entry(void *);
void bb_movie_mono_decode_entry(uint32_t, const unsigned char *, uint16_t *,
                                uint32_t, uint32_t *, uint32_t *);
void bb_movie_stereo_decode_entry(const unsigned char *, uint16_t *, uint32_t,
                                  uint32_t *, uint32_t *);
void BB_MOVIE_M bb_movie_present_entry(void *);
void BB_MOVIE_M bb_movie_buffers_swap_entry(void *);
uint32_t BB_MOVIE_M bb_movie_bits_entry(uint32_t);
void BB_MOVIE_M bb_movie_buffers_release_entry(void *);
#endif
