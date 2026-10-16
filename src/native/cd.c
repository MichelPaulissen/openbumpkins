/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* The mixed-mode original disc has data track 1 and seven CD-DA tracks.
 * bchunk -w preserves the latter as beasts02.wav through beasts08.wav. */
struct Track {
  void *mapping;
  size_t size;
  const int16_t *samples;
  U32 frames;
  uint64_t start;
};
static struct Track tracks[9];
static pthread_mutex_t cd_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t disc_frames, position, play_end;
static int loaded, opened, tmsf, playing;
const char *bb_cd_path;

static U32 le32(const unsigned char *p) {
  return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}
static U16 le16(const unsigned char *p) {
  return (U16)(p[0] | ((U16)p[1] << 8));
}
static void unload_tracks(void) {
  for (unsigned n = 2; n <= 8; n++) {
    if (tracks[n].mapping)
      munmap(tracks[n].mapping, tracks[n].size);
    memset(&tracks[n], 0, sizeof(tracks[n]));
  }
  disc_frames = 0;
}
static int load_track(unsigned number) {
  char *path;
  if (asprintf(&path, "%s/beasts%02u.wav", bb_cd_path, number) < 0)
    return 0;
  char *resolved = bb_host_path(path);
  free(path);
  int fd = open(resolved, O_RDONLY);
  free(resolved);
  if (fd < 0)
    return 0;
  struct stat info;
  if (fstat(fd, &info) || info.st_size < 12 ||
      (uint64_t)info.st_size > SIZE_MAX) {
    close(fd);
    return 0;
  }
  size_t size = (size_t)info.st_size;
  unsigned char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (data == MAP_FAILED)
    return 0;
  struct Track *track = &tracks[number];
  track->mapping = data;
  track->size = size;
  if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4) ||
      le32(data + 4) > size - 8)
    return 0;
  size_t limit = (size_t)le32(data + 4) + 8;
  int format_ok = 0;
  for (size_t offset = 12; offset <= limit && limit - offset >= 8;) {
    U32 amount = le32(data + offset + 4);
    if (amount > limit - offset - 8)
      return 0;
    const unsigned char *chunk = data + offset + 8;
    if (!memcmp(data + offset, "fmt ", 4)) {
      if (amount < 16 || le16(chunk) != 1 || le16(chunk + 2) != 2 ||
          le32(chunk + 4) != 44100 || le32(chunk + 8) != 176400 ||
          le16(chunk + 12) != 4 || le16(chunk + 14) != 16)
        return 0;
      format_ok = 1;
    } else if (!memcmp(data + offset, "data", 4)) {
      if (amount % 4)
        return 0;
      track->samples = (const int16_t *)chunk;
      track->frames = amount / 4;
    }
    size_t advance = (size_t)amount + 8 + (amount & 1);
    if (advance > limit - offset)
      break;
    offset += advance;
  }
  if (!format_ok || !track->samples || !track->frames)
    return 0;
  track->start = disc_frames;
  disc_frames += track->frames;
  return 1;
}
static U32 load_disc(void) {
  if (loaded)
    return disc_frames ? 0 : 306; /* MCIERR_DEVICE_NOT_INSTALLED */
  loaded = 1;
  if (!bb_cd_path)
    return 306;
  for (unsigned n = 2; n <= 8; n++)
    if (!load_track(n)) {
      unload_tracks();
      fprintf(stderr, "CD audio: invalid or missing beasts%02u.wav in %s\n", n,
              bb_cd_path);
      return 306;
    }
  fprintf(stderr, "Native CD audio: seven original tracks, 44100 Hz stereo\n");
  return 0;
}
/* The game uses TMSF track numbers without a sub-track position. Accept the
 * full T:M:S:F form too, keeping CD frames (1/75 second) exact at 44100 Hz. */
static int track_position(const char *text, uint64_t *result) {
  unsigned values[4] = {0};
  const char *p = text;
  for (unsigned n = 0; n < 4; n++) {
    if (!isdigit((unsigned char)*p))
      return 0;
    char *end;
    unsigned long value = strtoul(p, &end, 10);
    if (value > 99)
      return 0;
    values[n] = (unsigned)value;
    p = end;
    if (!*p)
      break;
    if (*p++ != ':' || n == 3)
      return 0;
  }
  unsigned track = values[0];
  if (track < 2 || track > 8 || values[2] >= 60 || values[3] >= 75)
    return 0;
  uint64_t frame =
      ((uint64_t)values[1] * 60 + values[2]) * 44100 + values[3] * 588;
  if (frame > tracks[track].frames)
    return 0;
  *result = tracks[track].start + frame;
  return 1;
}
U32 W bb_cd_command(const char *command, char *result, U32 capacity,
                    Handle window) {
  if (result && capacity)
    *result = '\0';
  if (!command || strlen(command) >= 256)
    return 268; /* MCIERR_PARAM_OVERFLOW */
  char text[256];
  for (size_t n = 0; n <= strlen(command); n++)
    text[n] = (char)tolower((unsigned char)command[n]);
  char *args[8], *save;
  unsigned count = 0;
  for (char *word = strtok_r(text, " \t\r\n", &save); word;
       word = strtok_r(NULL, " \t\r\n", &save)) {
    if (count == 8)
      return 268;
    args[count++] = word;
  }
  if (count < 2 || strcmp(args[1], "cdaudio"))
    return 261; /* MCIERR_UNRECOGNIZED_COMMAND */
  if (bb_trace)
    fprintf(stderr, "CD command: %s\n", command);
  /* Device creation can invoke the SDL callback; do not hold cd_mutex here. */
  if (!strcmp(args[0], "open") && !bb_sound_ready())
    return 306;
  pthread_mutex_lock(&cd_mutex);
  U32 error = 0;
  if (!strcmp(args[0], "open") && count == 2) {
    if (opened)
      error = 265; /* MCIERR_DEVICE_OPEN */
    else if (!(error = load_disc())) {
      opened = 1;
      tmsf = 0;
    }
  } else if (!opened)
    error = 257; /* MCIERR_INVALID_DEVICE_ID */
  else if (!strcmp(args[0], "close") && count == 2) {
    opened = 0;
    /* Closing an active CD device does not stop playback on Windows 2000/XP.
     * The original game sends a separate stop command when needed.
     * https://learn.microsoft.com/en-us/windows/win32/multimedia/close */
  } else if (!strcmp(args[0], "set") && count == 5 &&
             !strcmp(args[2], "time") && !strcmp(args[3], "format") &&
             !strcmp(args[4], "tmsf"))
    tmsf = 1;
  else if (!strcmp(args[0], "stop") && count == 2)
    playing = 0;
  else if (!strcmp(args[0], "play") && (count == 4 || count == 6) &&
           !strcmp(args[2], "from") && (count == 4 || !strcmp(args[4], "to"))) {
    uint64_t start, end = disc_frames;
    if (!tmsf)
      error = 293; /* MCIERR_BAD_TIME_FORMAT */
    else if (!track_position(args[3], &start) ||
             (count == 6 && !track_position(args[5], &end)) || start > end)
      error = 282; /* MCIERR_OUTOFRANGE */
    else {
      position = start;
      play_end = end;
      playing = start < end;
    }
  } else
    error = 261;
  pthread_mutex_unlock(&cd_mutex);
  return error;
}
void bb_cd_mix(int16_t *output, unsigned frames) {
  pthread_mutex_lock(&cd_mutex);
  for (unsigned n = 0; n < frames && playing; n++) {
    if (position >= play_end) {
      playing = 0;
      break;
    }
    unsigned track = 2;
    while (track < 8 && position >= tracks[track + 1].start)
      track++;
    size_t offset = (size_t)(position - tracks[track].start) * 2;
    for (unsigned channel = 0; channel < 2; channel++) {
      int value =
          output[n * 2 + channel] + tracks[track].samples[offset + channel];
      if (value > 32767)
        value = 32767;
      if (value < -32768)
        value = -32768;
      output[n * 2 + channel] = (int16_t)value;
    }
    position++;
  }
  if (position == play_end)
    playing = 0;
  pthread_mutex_unlock(&cd_mutex);
}

#ifdef BB_MODEL_RUNTIME
#include "cd_models.inc"
#endif
