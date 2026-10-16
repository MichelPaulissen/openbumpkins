/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_models.h"
#include "capture_video.h"
#include "interop.h"
#include "movie_codec.h"
#include "movie_color.h"
#include "movie_dispatch.h"
#include "movie_iqt.h"
#include "movie_pixels.h"
#include "movie_present.h"
#include "movie_runtime.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
extern void bb_movie_iqt_lz_c(uint32_t *);
extern void bb_movie_iqt_indices_c(uint32_t *);
extern void bb_movie_iqt_blocks_c(uint32_t *);
extern void bb_movie_rle_coefficients_c(uint32_t *);
extern void bb_movie_vlc_coefficients_c(uint32_t *);
extern void bb_movie_packet_block_c(uint32_t *);
extern void bb_movie_block_32_c(uint32_t *);
extern void bb_movie_block_16_c(uint32_t *);
extern void bb_movie_idct_column_c(uint32_t *);
extern void bb_movie_idct_row_c(uint32_t *);
extern void bb_movie_color_row_c(uint32_t *);
extern uint32_t bb_movie_audio_read_c(unsigned char *, void *, uint32_t,
                                      uint32_t);
extern void bb_screenshot_c(unsigned char *, void *, const char *);
extern void bb_video_c(unsigned char *, void *, const char *);
static void audio_read(uint32_t *r) {
  r[7] = bb_movie_audio_read_c(BB_DATA(r[3] + 4u), (void *)(uintptr_t)r[6],
                               r[5], BB_U32(r[3] + 0x8008u + 8u));
}
static void screenshot(uint32_t *r) {
  bb_screenshot_c(BB_DATA(r[3] + 4u), (void *)(uintptr_t)r[6],
                  (const char *)(uintptr_t)BB_U32(r[3] + 0x53cu + 8u));
}
static void video(uint32_t *r) {
  bb_video_c(BB_DATA(r[3] + 4u), (void *)(uintptr_t)r[6],
             (const char *)(uintptr_t)BB_U32(r[3] + 0x1a0u + 8u));
}
static void model_bb_movie_iqt_delta_entry(uint32_t *r) {
  r[7] = bb_movie_iqt_delta_entry((void *)(uintptr_t)r[6],
                                  (const void *)(uintptr_t)r[5]);
}
static void model_bb_movie_iqt_signed_entry(uint32_t *r) {
  bb_movie_iqt_signed_entry((void *)(uintptr_t)r[6],
                            (const void *)(uintptr_t)r[5], A(0), A(1));
}
static void model_bb_movie_iqt_four_color_entry(uint32_t *r) {
  bb_movie_iqt_four_color_entry((const void *)(uintptr_t)r[6],
                                (void *)(uintptr_t)r[5], A(0));
}
static void model_bb_movie_iqt_init_entry(uint32_t *r) {
  r[7] = bb_movie_iqt_init_entry((void *)(uintptr_t)r[6],
                                 (const void *)(uintptr_t)r[5]);
}
static void model_bb_movie_iqt_gamma_entry(uint32_t *r) {
  bb_movie_iqt_gamma_entry(r[6]);
}
static void model_bb_movie_iqt_palette_entry(uint32_t *r) {
  bb_movie_iqt_palette_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_iqt_expand_entry(uint32_t *r) {
  bb_movie_iqt_expand_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_iqt_rotate_entry(uint32_t *r) {
  r[7] = bb_movie_iqt_rotate_entry((void *)(uintptr_t)A(0),
                                   (const void *)(uintptr_t)A(1), A(2));
}
static void model_bb_movie_chroma_convert_entry(uint32_t *r) {
  bb_movie_chroma_convert_entry((void *)(uintptr_t)r[6], r[5]);
}
static void model_bb_movie_packets_32_entry(uint32_t *r) {
  bb_movie_packets_32_entry(
      (void *)(uintptr_t)r[6], (const unsigned char *)(uintptr_t)r[5],
      (void *)(uintptr_t)A(0), (const void *)(uintptr_t)A(1));
}
static void model_bb_movie_frame_dispatch_entry(uint32_t *r) {
  bb_movie_frame_dispatch_entry((void *)(uintptr_t)r[6],
                                (const uint32_t *)(uintptr_t)r[5]);
}
static void model_bb_movie_bit_reset_entry(uint32_t *r) {
  bb_movie_bit_reset_entry((const void *)(uintptr_t)A(0));
}
static void model_bb_movie_color_block_entry(uint32_t *r) {
  bb_movie_color_block_entry(r[6], r[5], A(0), A(1), A(2), A(3),
                             (uint32_t *)(uintptr_t)A(4), A(5));
}
static void model_bb_movie_frame_setup_entry(uint32_t *r) {
  bb_movie_frame_setup_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_color_tables_entry(uint32_t *r) {
  bb_movie_color_tables_entry(r[6], r[5], A(0), A(1), A(2), A(3));
}
static void model_bb_movie_color_pack_entry(uint32_t *r) {
  r[7] = bb_movie_color_pack_entry(r[6], r[5], A(0), A(1), A(2), A(3), A(4),
                                   A(5), A(6), A(7));
}
static void model_bb_movie_color_rgba_entry(uint32_t *r) {
  bb_movie_color_rgba_entry(r[6]);
}
static void model_bb_movie_present_16_entry(uint32_t *r) {
  r[7] = bb_movie_present_16_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_present_32_entry(uint32_t *r) {
  r[7] = bb_movie_present_32_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_chunk_entry(uint32_t *r) {
  r[7] = bb_movie_chunk_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_frame_read_entry(uint32_t *r) {
  r[7] = bb_movie_frame_read_entry((void *)(uintptr_t)r[6], r[5], A(0));
}
static void model_bb_movie_open_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_movie_open_entry(
      (const char *)(uintptr_t)A(0), A(1), (const uint32_t *)(uintptr_t)A(2)));
}
static void model_bb_movie_run_entry(uint32_t *r) {
  r[7] = bb_movie_run_entry((void *)(uintptr_t)A(0));
}
static void model_bb_movie_play_entry(uint32_t *r) {
  r[7] = bb_movie_play_entry((const char *)(uintptr_t)A(0), A(1),
                             (const uint32_t *)(uintptr_t)A(2));
}
static void model_bb_movie_predictor_reset_entry(uint32_t *r) {
  r[7] = bb_movie_predictor_reset_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_block_decode_entry(uint32_t *r) {
  bb_movie_block_decode_entry(
      r[6], (uint32_t *)(uintptr_t)r[5], (uint32_t *)(uintptr_t)A(0),
      (uint32_t *)(uintptr_t)A(1), (uint32_t *)(uintptr_t)A(2),
      (uint32_t *)(uintptr_t)A(3), (const unsigned char *)(uintptr_t)A(4),
      (uint32_t *)(uintptr_t)A(5));
}
static void model_bb_movie_timer_start_entry(uint32_t *r) {
  r[7] = bb_movie_timer_start_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_timer_stop_entry(uint32_t *r) {
  r[7] = bb_movie_timer_stop_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_sound_setup_entry(uint32_t *r) {
  r[7] = bb_movie_sound_setup_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_interfaces_release_entry(uint32_t *r) {
  r[7] = bb_movie_interfaces_release_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_sound_start_entry(uint32_t *r) {
  r[7] = bb_movie_sound_start_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_sound_release_entry(uint32_t *r) {
  r[7] = bb_movie_sound_release_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_mono_decode_entry(uint32_t *r) {
  bb_movie_mono_decode_entry(
      A(0), (const unsigned char *)(uintptr_t)A(1), (uint16_t *)(uintptr_t)A(2),
      A(3), (uint32_t *)(uintptr_t)A(4), (uint32_t *)(uintptr_t)A(5));
}
static void model_bb_movie_stereo_decode_entry(uint32_t *r) {
  bb_movie_stereo_decode_entry(
      (const unsigned char *)(uintptr_t)A(0), (uint16_t *)(uintptr_t)A(1), A(2),
      (uint32_t *)(uintptr_t)A(3), (uint32_t *)(uintptr_t)A(4));
}
static void model_bb_movie_present_entry(uint32_t *r) {
  bb_movie_present_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_buffers_swap_entry(uint32_t *r) {
  bb_movie_buffers_swap_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_bits_entry(uint32_t *r) {
  r[7] = bb_movie_bits_entry(r[6]);
}
static void model_bb_movie_buffers_release_entry(uint32_t *r) {
  bb_movie_buffers_release_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_movie_worker_entry(uint32_t *r) {
  r[7] = bb_movie_worker_entry((void *)(uintptr_t)A(0));
}
static void model_bb_movie_timer_entry(uint32_t *r) {
  bb_movie_timer_entry(A(0), A(1), (void *)(uintptr_t)A(2), A(3), A(4));
}
static void model_bb_numbered_file_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_numbered_file_entry((const char *)(uintptr_t)A(0),
                                                  (const char *)(uintptr_t)A(1),
                                                  (char *)(uintptr_t)A(2)));
}
static const struct bb_model_target targets[] = {
    {0x4624d0, model_bb_movie_iqt_delta_entry, 0, 0, 0},
    {0x46297a, model_bb_movie_iqt_signed_entry, 2, 0, 0},
    {0x462a2f, model_bb_movie_iqt_four_color_entry, 1, 0, 0},
    {0x462c25, model_bb_movie_iqt_init_entry, 0, 0, 0},
    {0x462f8a, model_bb_movie_iqt_gamma_entry, 0, 0, 0},
    {0x463060, model_bb_movie_iqt_palette_entry, 0, 0, 0},
    {0x463174, model_bb_movie_iqt_expand_entry, 0, 0, 0},
    {0x4632a0, bb_movie_iqt_lz_c, 3, 0, 0},
    {0x4633c3, bb_movie_iqt_indices_c, 4, 0, 0},
    {0x463464, bb_movie_iqt_blocks_c, 9, 0, 0},
    {0x463518, model_bb_movie_iqt_rotate_entry, 3, 0, 0},
    {0x45dd95, bb_movie_rle_coefficients_c, 0, 0, 0},
    {0x45df35, bb_movie_vlc_coefficients_c, 0, 0, 0},
    {0x45e9e9, bb_movie_packet_block_c, 3, 0, 0},
    {0x45fbdb, bb_movie_block_32_c, 2, 0, 0},
    {0x460774, bb_movie_block_16_c, 2, 0, 0},
    {0x45d460, model_bb_movie_chroma_convert_entry, 0, 0, 0},
    {0x45e24a, bb_movie_idct_column_c, 0, 0, 0},
    {0x45e3b1, bb_movie_idct_row_c, 0, 0, 0},
    {0x45e4c3, bb_movie_color_row_c, 0, 0, 0},
    {0x45c170, model_bb_movie_packets_32_entry, 2, 0, 0},
    {0x45d150, model_bb_movie_frame_dispatch_entry, 0, 0, 0},
    {0x45dd60, model_bb_movie_bit_reset_entry, 1, 0, 0},
    {0x45c380, model_bb_movie_color_block_entry, 6, 0, 0},
    {0x45c560, model_bb_movie_frame_setup_entry, 0, 0, 0},
    {0x45c9b0, model_bb_movie_color_tables_entry, 4, 0, 0},
    {0x45cca0, model_bb_movie_color_pack_entry, 8, 0, 0},
    {0x45ced0, model_bb_movie_color_rgba_entry, 0, 0, 0},
    {0x45bad0, model_bb_movie_present_16_entry, 0, 0, 0},
    {0x45be30, model_bb_movie_present_32_entry, 0, 0, 0},
    {0x45a2e0, model_bb_movie_chunk_entry, 0, 0, 0},
    {0x45a4e0, model_bb_movie_frame_read_entry, 1, 0, 0},
    {0x45a5a0, audio_read, 1, 32776, 0},
    {0x45a740, model_bb_movie_open_entry, 3, 0, 0},
    {0x45a980, model_bb_movie_run_entry, 1, 0, 0},
    {0x45ab50, model_bb_movie_play_entry, 3, 0, 0},
    {0x45ad20, model_bb_movie_predictor_reset_entry, 0, 0, 0},
    {0x45ad40, model_bb_movie_block_decode_entry, 6, 0, 0},
    {0x45afc0, model_bb_movie_timer_start_entry, 0, 0, 0},
    {0x45b310, model_bb_movie_timer_stop_entry, 0, 0, 0},
    {0x45b3a0, model_bb_movie_sound_setup_entry, 0, 0, 0},
    {0x45b620, model_bb_movie_interfaces_release_entry, 0, 0, 0},
    {0x45b680, model_bb_movie_sound_start_entry, 0, 0, 0},
    {0x45b880, model_bb_movie_sound_release_entry, 0, 0, 0},
    {0x45b8b0, model_bb_movie_mono_decode_entry, 6, 0, 0},
    {0x45b953, model_bb_movie_stereo_decode_entry, 5, 0, 0},
    {0x45c150, model_bb_movie_present_entry, 0, 0, 0},
    {0x45c540, model_bb_movie_buffers_swap_entry, 0, 0, 0},
    {0x45d060, model_bb_movie_bits_entry, 0, 0, 0},
    {0x45d080, model_bb_movie_buffers_release_entry, 0, 0, 0},
    {0x45ac60, model_bb_movie_worker_entry, 1, 0, 0},
    {0x45b060, model_bb_movie_timer_entry, 5, 0, 0},
    {0x431af0, screenshot, 1, 1340, 0},
    {0x431d00, model_bb_numbered_file_entry, 3, 0, 0},
    {0x432df0, video, 1, 416, 0},
};
const struct bb_model_target *bb_movie_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
