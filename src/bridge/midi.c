/* SPDX-License-Identifier: GPL-3.0-only */
#include "midi.h"
#include "abi.h"
#include "instruments.h"
#include "interop.h"
#include "legacy_audio_calls.h"
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
/* MIDI parsing, track ownership and traversal share the ordinary C bodies.
 * The call adapter handles packed CRT references and private I/O buffers. */
static int get(void *file) { return LA_FILE_ONE(0x46fb60, file); }
static void read(void *out, uint32_t size, void *file) {
  LA_READ(0x46cf40, out, size, 1, file);
}
static void debug(const char *text) { LA_DEBUG(BB_POINTER(0x6ca374), text); }
#define FORMAT(out, address, ...)                                              \
  LA_FORMAT(0x46bae0, out, (const char *)(uintptr_t)(address), __VA_ARGS__)
static void copy(char *out, const char *in) {
  bb_crt_string_copy_entry(out, in);
}
uint8_t bb_midi_read_byte_entry(void *file) { return (uint8_t)get(file); }
uint16_t bb_midi_read_word_entry(void *file) {
  uint32_t a = (uint8_t)get(file), b = (uint8_t)get(file);
  return (a << 8) + b;
}
uint32_t bb_midi_read_three_entry(void *file) {
  uint32_t a = (uint8_t)get(file), b = (uint8_t)get(file);
  uint32_t c = (uint8_t)get(file);
  return (a << 16) + (b << 8) + c;
}
uint32_t bb_midi_read_dword_entry(void *file) {
  uint32_t a = (uint8_t)get(file), b = (uint8_t)get(file);
  uint32_t c = (uint8_t)get(file), d = (uint8_t)get(file);
  return (a << 24) + (c << 8) + (b << 16) + d;
}
uint32_t bb_midi_read_variable_entry(void *file) {
  uint32_t value = get(file);
  if (value & 128) {
    value &= 127;
    uint8_t byte;
    do {
      byte = get(file);
      value = (value << 7) + (byte & 127);
    } while (byte & 128);
  }
  return value;
}
void *BB_MIDI_M bb_midi_track_construct_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  BB_U32(p + 0x44) = 0;
  BB_U32(p + 0x4c) = 0;
  BB_U32(p + 0x50) = 0;
  BYTE(p + 0x48) = 0;
  return object;
}
/* 0x4680d2 appends to the tail only for nondecreasing times. Otherwise it
 * links after the head, overwriting its next pointer, exactly as the library.
 */
void BB_MIDI_M bb_midi_track_append_entry(void *object, uint32_t time,
                                          uint8_t kind, uint8_t channel,
                                          uint8_t note, uint8_t velocity) {
  uint32_t p = bb_address_encode(object), previous = BB_U32(p + 0x4c);
  if (BB_U32(p + 0x50) && BB_U32(BB_U32(p + 0x50)) <= time)
    previous = BB_U32(p + 0x50);
  uint32_t event = (uintptr_t)LA_ALLOCATE(0x46c060, 12);
  BB_U32(event) = time;
  BYTE(event + 4) = kind;
  BYTE(event + 5) = channel;
  BYTE(event + 6) = note;
  BYTE(event + 7) = velocity;
  BB_U32(event + 8) = 0;
  if (previous)
    BB_U32(previous + 8) = event;
  else
    BB_U32(p + 0x4c) = event;
  BB_U32(p + 0x50) = event;
  ++BB_U32(p + 0x44);
}
void BB_MIDI_M bb_midi_track_program_entry(void *object, uint8_t program) {
  BYTE(bb_address_encode(object) + 0x48) = program;
}
void BB_MIDI_M bb_midi_track_release_entry(void *object) {
  uint32_t event = BB_U32(bb_address_encode(object) + 0x4c);
  while (event) {
    uint32_t next = BB_U32(event + 8);
    LA_RELEASE(0x46c110, (void *)(uintptr_t)event);
    event = next;
  }
  /* The original leaves the head, tail and count unchanged. */
}
void BB_MIDI_M bb_midi_track_begin_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  BB_U32(p + 0x54) = BB_U32(p + 0x4c);
  BB_U32(p + 0x40) = BYTE(p + 0x48) * 0x14cu + BB_U32(0x48c400) + 0x10;
}
void BB_MIDI_M bb_midi_track_rewind_entry(void *object) {
  bb_midi_track_begin_entry(object);
}
void BB_MIDI_M bb_midi_track_event_entry(void *object, const void *event) {
  uint32_t p = bb_address_encode(object), e = bb_address_encode(event);
  uint8_t kind = BYTE(e + 4);
  if (kind == 1 || kind == 2) {
    uint8_t velocity = BYTE(e + 7), note = BYTE(e + 6), channel = BYTE(e + 5);
    void *program = BB_POINTER(p + 0x40);
    if (kind == 1)
      bb_instrument_note_on_entry(program, channel, note, velocity);
    else
      bb_instrument_note_off_entry(program, channel, note, velocity);
  }
}
void BB_MIDI_M bb_midi_track_tick_entry(void *object, uint32_t time) {
  uint32_t p = bb_address_encode(object);
  while (BB_U32(p + 0x54) && BB_U32(BB_U32(p + 0x54)) <= time) {
    bb_midi_track_event_entry(object, (void *)(uintptr_t)BB_U32(p + 0x54));
    BB_U32(p + 0x54) = BB_U32(BB_U32(p + 0x54) + 8);
  }
}
void *BB_MIDI_M bb_midi_construct_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  for (unsigned i = 0; i < 64; ++i)
    bb_midi_track_construct_entry((void *)(uintptr_t)(p + i * 0x58));
  WORD(p + 0x1602) = 0;
  return object;
}
uint32_t BB_MIDI_M bb_midi_load_entry(void *object, const char *filename) {
  void *file = LA_OPEN(0x46bdd0, filename, (const char *)0x48c4bc);
  if (file) {
    bb_midi_read_entry(object, file);
    LA_FILE_ONE(0x46bbd0, file);
  }
  return 1; /* Neither open failure nor a rejected file propagates. */
}
/* 0x468412: chunk lengths gate empty tracks but do not bound event reading;
 * EOT ends each track. Running status and the last note channel span tracks.
 * Program changes are stored once per track rather than as timed events. */
uint32_t BB_MIDI_M bb_midi_read_entry(void *object, void *file) {
  uint32_t p = bb_address_encode(object);
  uint8_t tag[4], payload[4096], running = 0, channel = 0, note = 0,
                                 velocity = 0;
  char text[256];
  read(tag, 4, file);
  uint32_t length = bb_midi_read_dword_entry(file);
  if (tag[0] != 'M' || tag[1] != 'T' || tag[2] != 'h' || tag[3] != 'd')
    return 0;
  WORD(p + 0x1600) = bb_midi_read_word_entry(file);
  WORD(p + 0x1602) = bb_midi_read_word_entry(file);
  WORD(p + 0x1604) = bb_midi_read_word_entry(file);
  FORMAT(text, 0x48c4c0, WORD(p + 0x1600), WORD(p + 0x1602), WORD(p + 0x1604));
  debug(text);
  for (uint32_t i = 0; i < WORD(p + 0x1602); ++i) {
    void *track = (void *)(uintptr_t)(p + i * 0x58);
    if (i == 0)
      copy(track, (const char *)0x48c4ec);
    else
      BYTE((uintptr_t)track) = 0;
    BYTE((uintptr_t)track + 0x48) = 0;
    read(tag, 4, file);
    length = bb_midi_read_dword_entry(file);
    if (tag[0] != 'M' || tag[1] != 'T' || tag[2] != 'r' || tag[3] != 'k')
      return 0;
    FORMAT(text, 0x48c4f4, i, length);
    debug(text);
    uint32_t time = 0;
    if (!length)
      continue;
    unsigned done = 0;
    while (!done) {
      time += bb_midi_read_variable_entry(file);
      uint8_t command = bb_midi_read_byte_entry(file);
      if (command == 0xf0 || command == 0xf7) {
        length = bb_midi_read_variable_entry(file);
        read(payload, length, file);
      } else if (command == 0xff) {
        uint8_t meta = bb_midi_read_byte_entry(file);
        length = bb_midi_read_variable_entry(file);
        if (length)
          read(payload, length, file);
        switch (meta) {
        case 3:
          payload[length] = 0;
#ifdef BB_MODEL_RUNTIME
          la_format_text(text, 0x48c514, (const char *)payload);
#else
          FORMAT(text, 0x48c514, payload);
#endif
          copy(track, (const char *)payload);
          debug(text);
          break;
        case 0x2f:
          done = 1;
          debug((const char *)0x48c524);
          break;
        case 0x51: {
          uint32_t tempo = ((uint32_t)payload[0] << 16) +
                           ((uint32_t)payload[1] << 8) + payload[2];
          BB_U32(p + 0x1608) = tempo;
          BB_U32(p + 0x160c) = tempo / 1000;
          BB_U32(p + 0x1610) = 60000000u / tempo;
          FORMAT(text, 0x48c52c, BB_U32(p + 0x1610));
          debug(text);
          break;
        }
        case 0x58:
          FORMAT(text, 0x48c53c, payload[0], (uint8_t)(1u << (payload[1] & 31)),
                 payload[2]);
          debug(text);
          break;
        case 0x59:
          debug((const char *)0x48c55c);
          break;
        default:
          FORMAT(text, 0x48c568, meta);
          debug(text);
        }
      } else if (command >= 0x80 && command <= 0x9f) {
        channel = command - (command >= 0x90 ? 0x8f : 0x7f);
        note = bb_midi_read_byte_entry(file);
        velocity = bb_midi_read_byte_entry(file);
        bb_midi_track_append_entry(track, time, command >= 0x90 ? 1 : 2,
                                   channel, note, velocity);
        FORMAT(text, command >= 0x90 ? 0x48c584 : 0x48c5b0, time, channel, note,
               velocity);
        running = command;
      } else if (command >= 0xa0 && command <= 0xef) {
        uint8_t value = bb_midi_read_byte_entry(file);
        unsigned kind = command >> 4;
        if (kind == 0xa || kind == 0xb || kind == 0xe)
          bb_midi_read_byte_entry(file);
        if (kind == 0xc) {
          bb_midi_track_program_entry(track, value);
          FORMAT(text, 0x48c5f8, value);
          debug(text);
        } else
          debug((const char *)(uintptr_t)(kind == 0xa   ? 0x48c5dc
                                          : kind == 0xb ? 0x48c5e8
                                          : kind == 0xd ? 0x48c614
                                                        : 0x48c620));
        running = command;
      } else if (command < 0x80) {
        unsigned kind = running >> 4;
        if (kind == 8 || kind == 9) {
          note = command;
          velocity = bb_midi_read_byte_entry(file);
          bb_midi_track_append_entry(track, time, kind == 9 && velocity ? 1 : 2,
                                     channel, note, velocity);
        } else if (kind == 0xa || kind == 0xb || kind == 0xe)
          bb_midi_read_byte_entry(file);
        /* This formatted line is deliberately never emitted by the original. */
        FORMAT(text, 0x48c628, time, note, velocity);
      } else {
        FORMAT(text, 0x48c644, command);
        debug(text);
      }
    }
  }
  return 1;
}
void BB_MIDI_M bb_midi_release_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  for (uint32_t i = 0; i < WORD(p + 0x1602); ++i)
    bb_midi_track_release_entry((void *)(uintptr_t)(p + i * 0x58));
}
