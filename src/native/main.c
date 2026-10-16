/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "../bridge/qmixer_vtables.h"
#include "../bridge/startup.h"
#include "native.h"
#ifdef BB_MODEL_RUNTIME
#include "../bridge/allocation_models.h"
#include "../bridge/archive_models.h"
#include "../bridge/bootstrap_models.h"
#include "../bridge/crt_models.h"
#include "../bridge/heap.h"
#include "../bridge/qmixer_models.h"
#include "../bridge/render_models.h"
#include "../bridge/session_models.h"
#include "../bridge/sprite_models.h"
#include "../bridge/startup_models.h"
#include "platform_models.h"
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifndef BB_MODEL_RUNTIME
#include <ucontext.h>
#endif
#include <unistd.h>

int bb_skip_intro, bb_trace, bb_fast;
U32 bb_language;
unsigned bb_stop_after_frames;
const char *bb_capture_path;
#ifdef BB_MODEL_RUNTIME
static const struct bb_model_target *lookup(void *unused, uint32_t id) {
  (void)unused;
  const struct bb_model_target *target = bb_crt_model_find(NULL, id);
  if (!target)
    target = bb_startup_model_find(NULL, id);
  if (!target)
    target = bb_bootstrap_model_find(NULL, id);
  if (!target)
    target = bb_archive_model_find(NULL, id);
  if (!target)
    target = bb_sprite_model_find(NULL, id);
  if (!target)
    target = bb_render_model_find(NULL, id);
  if (!target)
    target = bb_display_model_find(NULL, id);
  if (!target)
    target = bb_session_model_find(NULL, id);
  if (!target)
    target = bb_audio_control_model_find(NULL, id);
  if (!target)
    target = bb_qmix_model_find(NULL, id);
  if (!target)
    target = bb_allocation_model_find(NULL, id);
  return target ? target : bb_platform_model_find(NULL, id);
}
#else
static void crash(int signal, siginfo_t *info, void *context) {
  ucontext_t *u = context;
  fprintf(
      stderr,
      "Native fault %d at %p: eip=%08x esp=%08x eax=%08x ecx=%08x edx=%08x\n",
      signal, info->si_addr, (unsigned)u->uc_mcontext.gregs[REG_EIP],
      (unsigned)u->uc_mcontext.gregs[REG_ESP],
      (unsigned)u->uc_mcontext.gregs[REG_EAX],
      (unsigned)u->uc_mcontext.gregs[REG_ECX],
      (unsigned)u->uc_mcontext.gregs[REG_EDX]);
  _exit(128 + signal);
}
#endif
int main(int argc, char **argv) {
  const char *directory = "work/patched";
  const char *cd_directory = "original/tracks";
  int cd_explicit = 0;
  int language_explicit = 0;
  for (int n = 1; n < argc; n++) {
    if (!strcmp(argv[n], "--game-dir") && n + 1 < argc)
      directory = argv[++n];
    else if (!strcmp(argv[n], "--cd-tracks") && n + 1 < argc) {
      cd_directory = argv[++n];
      cd_explicit = 1;
    } else if (!strcmp(argv[n], "--language") && n + 1 < argc) {
      static const char *const names[] = {"english", "german", "french",
                                          "italian", "spanish"};
      const char *name = argv[++n];
      for (bb_language = 0; bb_language < 5; ++bb_language)
        if (!strcasecmp(name, names[bb_language]))
          break;
      if (bb_language == 5)
        bb_die("unknown language '%s'; choose english, german, french, italian "
               "or spanish",
               name);
      language_explicit = 1;
    } else if (!strcmp(argv[n], "--skip-intro"))
      bb_skip_intro = 1;
    else if (!strcmp(argv[n], "--trace"))
      bb_trace = 1;
    else if (!strcmp(argv[n], "--fast"))
      bb_fast = 1;
    else if (!strcmp(argv[n], "--stop-after-frames") && n + 1 < argc)
      bb_stop_after_frames = (unsigned)strtoul(argv[++n], NULL, 10);
    else if (!strcmp(argv[n], "--capture") && n + 1 < argc)
      bb_capture_path = argv[++n];
    else {
      fprintf(stderr,
              "Usage: %s [--game-dir DIR] [--cd-tracks DIR] [--skip-intro] "
              "[--language NAME] "
              "[--trace] [--fast] "
              "[--stop-after-frames N] [--capture FILE]\n",
              argv[0]);
      return strcmp(argv[n], "--help") != 0;
    }
  }
#ifndef BB_MODEL_RUNTIME
  struct sigaction action = {0};
  action.sa_sigaction = crash;
  action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGILL, &action, NULL);
  sigaction(SIGBUS, &action, NULL);
#endif
  bb_cd_path = realpath(cd_directory, NULL);
  if (cd_explicit && !bb_cd_path)
    bb_die("cannot resolve CD track directory %s", cd_directory);
  if (bb_capture_path && bb_capture_path[0] != '/') {
    char *current = getcwd(NULL, 0), *absolute;
    if (!current || asprintf(&absolute, "%s/%s", current, bb_capture_path) < 0)
      bb_die("cannot resolve capture path");
    free(current);
    bb_capture_path = absolute;
  }
  if (chdir(directory))
    bb_die("cannot enter %s", directory);
  if (!language_explicit)
    bb_language =
        bb_profile_integer("reference", "language", 0, "bumpkins.ini");
  if (bb_language > 4)
    bb_die("bumpkins.ini [reference] language must be between 0 and 4");
  /* X11 avoids mixing host 64-bit GTK decoration modules into this i386
   * process on desktops offering both X11 and Wayland. Explicit overrides
   * still select another SDL backend. */
#if defined(__linux__) && !defined(BB_MODEL_RUNTIME)
  if (getenv("DISPLAY") && !getenv("SDL_VIDEODRIVER"))
    setenv("SDL_VIDEODRIVER", "x11", 0);
#endif
#ifdef BB_MODEL_RUNTIME
  uint32_t stack = bb_heap_allocate(4 * 1024 * 1024);
  if (!stack || !bb_model_attach(lookup, NULL, stack, 4 * 1024 * 1024))
    bb_die("cannot initialize the portable game stack");
#else
  bb_init_teb();
  bb_native_initialize_entries();
#endif
  PeModule *game = bb_pe_load("BEASTS.EXE");
  if (bb_address_encode(game->base) != 0x400000 || game->size < 0x31f000)
    bb_die("unsupported executable layout");
  PeModule *mixer = bb_pe_load("QMIXER95.DLL");
  if (bb_address_encode(mixer->base) != 0x10000000 || mixer->size != 0x25000)
    bb_die("unsupported QMIXER layout");
#ifndef BB_MODEL_RUNTIME
  bb_qmix_rebind_static_vtables();
#endif
  bb_pe_initialize(mixer);
  bb_startup_skip_intro = (uint32_t)bb_skip_intro;
#ifdef BB_MODEL_RUNTIME
  struct bb_call_state state = {0, 0, 0, 0x202};
  bb_model_invoke(&state, bb_address_encode(game->base) + game->entry, NULL, 0);
  if (!bb_model_detach() || !bb_heap_release(stack))
    bb_die("cannot release the portable game stack");
#else
  void (*entry)(void) =
      (void *)bb_native_entry((uintptr_t)game->base + game->entry);
  entry();
#endif
  return 0;
}
