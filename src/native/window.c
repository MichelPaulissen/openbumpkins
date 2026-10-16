/* SPDX-License-Identifier: GPL-3.0-only */
#include "codepage.h"
#include "native.h"
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "../bridge/callback_runtime.h"
#include "../bridge/heap.h"
#endif
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

SDL_Window *bb_window;
WindowProc bb_window_proc;
int bb_dispatching_mouse_motion;
static WindowProc registered_proc;
#ifdef BB_MODEL_RUNTIME
/* The provider borrows dispatch environments, just as native procedure tokens
 * borrow the lifetime of their module. Window records copy their environment
 * so class removal cannot invalidate an existing fallback procedure. */
static struct bb_model_context registered_context, main_context;
#endif
_Static_assert(sizeof(Message) == 28, "Windows MSG stays 28 bytes");
static SDL_Cursor *game_cursor;
struct RegisteredClass {
  char *name;
  WindowProc procedure;
  U16 atom;
#ifdef BB_MODEL_RUNTIME
  struct bb_model_context context;
#endif
};
struct CallbackWindow {
  Handle handle, parent;
  WindowProc procedure;
  unsigned class_index, destroying;
#ifdef BB_MODEL_RUNTIME
  struct bb_model_context context;
#endif
};
static struct RegisteredClass classes[32];
static struct CallbackWindow callback_windows[32];
static U16 next_atom = 1;
static U32 next_window = 2;
static unsigned main_class = UINT32_MAX;
static Message messages[1024];
enum { MESSAGE_MOTION = 1, MESSAGE_SDL_KEY = 2 };
static unsigned char message_flags[1024];
/* Existing game/mixer loops translate the message just returned by Get/Peek.
 * Keep its origin outside the public 28-byte MSG, including filtered peeks.
 * Posted demo keys still need translation; SDL supplies committed text itself.
 */
static _Thread_local struct {
  const Message *output;
  Message value;
  unsigned flags;
} retrieved_message;
static unsigned first_message, message_count;
static int fullscreen_enter[2];
#ifdef __APPLE__
static int fullscreen_f;
#endif
static pthread_mutex_t message_mutex = PTHREAD_MUTEX_INITIALIZER;
static Handle message_window(const Message *message) {
  return (Handle)(uintptr_t)message->window;
}
#ifdef BB_MODEL_RUNTIME
static U32 message_reference(Handle window) { return (U32)(uintptr_t)window; }
#else
static Handle message_reference(Handle window) { return window; }
#endif
static void enqueue(Handle window, U32 message, U32 wparam, I32 lparam,
                    unsigned flags) {
  if (bb_trace && message >= 0x100 && message <= 0x209)
    fprintf(stderr, "Native input: message=%03x wparam=%08x lparam=%08x\n",
            message, wparam, (U32)lparam);
  pthread_mutex_lock(&message_mutex);
  /* Coalesce only consecutive hardware motion. Posted messages and button/key
   * transitions retain their order, including a motion preceding a click. */
  unsigned slot = (first_message + message_count) % 1024;
  if ((flags & MESSAGE_MOTION) && message_count &&
      (message_flags[(slot + 1023) % 1024] & MESSAGE_MOTION))
    slot = (slot + 1023) % 1024;
  else {
    if (message_count == 1024)
      bb_die("native window message queue overflow");
    ++message_count;
  }
  messages[slot] = (Message){
      message_reference(window), message, wparam, lparam, bb_tick(), {0, 0}};
  message_flags[slot] = (unsigned char)flags;
  pthread_mutex_unlock(&message_mutex);
}
static void queue_window(Handle window, U32 message, U32 wparam, I32 lparam) {
  enqueue(window, message, wparam, lparam, 0);
  /* A filtered GetMessage may be waiting while unrelated messages remain.
   * Posts from audio/worker threads must wake that SDL event wait too. */
  if (SDL_WasInit(SDL_INIT_EVENTS)) {
    SDL_Event wake = {0};
    wake.type = SDL_USEREVENT;
    SDL_PushEvent(&wake);
  }
}
static void queue(U32 message, U32 wparam, I32 lparam) {
  queue_window((void *)1, message, wparam, lparam);
}
static unsigned class_index(const char *name) {
  for (unsigned i = 0; i < 32; i++)
    if (classes[i].name &&
        ((uintptr_t)name < 65536 ? classes[i].atom == (uintptr_t)name
                                 : !strcmp(classes[i].name, name)))
      return i;
  return UINT32_MAX;
}
static struct CallbackWindow *callback_window(Handle window) {
  for (unsigned i = 0; i < 32; i++)
    if (callback_windows[i].handle && callback_windows[i].handle == window)
      return callback_windows + i;
  return NULL;
}
static WindowProc window_procedure(Handle window) {
  if (window == (void *)1)
    return bb_window_proc;
  struct CallbackWindow *record = callback_window(window);
  return record ? record->procedure : NULL;
}
/* Procedure tokens stay observable through Get/SetWindowLong. Only calls
 * translate them to native C, including callback windows and subclass chains.
 */
static I32 call_procedure(WindowProc procedure, Handle window, U32 message,
                          U32 wparam, I32 lparam) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_context current, context;
  struct CallbackWindow *record = callback_window(window);
  context = window == (void *)1 ? main_context
            : record            ? record->context
                                : (struct bb_model_context){0};
  if (!context.lookup && !bb_model_capture_context(&context))
    bb_die("window callback without a C dispatch environment");
  int attached = bb_model_capture_context(&current) &&
                 current.lookup == context.lookup &&
                 current.context == context.context;
  uint32_t stack = attached ? 0 : bb_heap_allocate(BB_CALLBACK_STACK_BYTES);
  if (!attached && !stack)
    bb_die("cannot allocate a foreign window callback stack");
  uint32_t arguments[] = {(U32)(uintptr_t)window, message, wparam, (U32)lparam};
  struct bb_call_state state = {0, 0, 0, 0x202};
  uint32_t error;
  int success =
      bb_model_context_invoke(&context, stack, BB_CALLBACK_STACK_BYTES, &state,
                              (U32)(uintptr_t)procedure, arguments, 4, &error);
  if (stack && !bb_heap_release(stack))
    bb_die("window callback stack lost its owner");
  if (!success)
    bb_die("unhandled window callback error %08x", error);
  return (I32)state.eax;
#else
  WindowProc target = (void *)bb_native_entry((uintptr_t)procedure);
  return target(window, message, wparam, lparam);
#endif
}
static U32 virtual_key(const SDL_Keysym *symbol) {
  SDL_Keycode key = symbol->sym;
  if (key >= SDLK_a && key <= SDLK_z)
    return (U32)(key - 'a' + 'A');
  if (key >= SDLK_0 && key <= SDLK_9)
    return (U32)key;
  if (key >= SDLK_F1 && key <= SDLK_F12)
    return 0x70 + (U32)(key - SDLK_F1);
  if (key >= SDLK_F13 && key <= SDLK_F24)
    return 0x7c + (U32)(key - SDLK_F13);
  if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
    static const U32 navigation[] = {0x23, 0x28, 0x22, 0x25, 0x0c,
                                     0x27, 0x24, 0x26, 0x21};
    unsigned index = (unsigned)(key - SDLK_KP_1);
    return symbol->mod & KMOD_NUM ? 0x61 + index : navigation[index];
  }
  /* Windows number-row identities survive layouts such as French, whose
   * unshifted SDL symbols include both accents and punctuation (notably '-').
   */
  if (symbol->scancode >= SDL_SCANCODE_1 && symbol->scancode <= SDL_SCANCODE_9)
    return '1' + (U32)(symbol->scancode - SDL_SCANCODE_1);
  if (symbol->scancode == SDL_SCANCODE_0)
    return '0';
  switch (key) {
  case SDLK_PAUSE:
    return symbol->mod & KMOD_CTRL ? 3 : 0x13;
  case SDLK_CANCEL:
    return 3;
  case SDLK_CLEAR:
    return 0x0c;
  case SDLK_ESCAPE:
    return 27;
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return 13;
  case SDLK_SPACE:
    return 32;
  case SDLK_LEFT:
    return 0x25;
  case SDLK_UP:
    return 0x26;
  case SDLK_RIGHT:
    return 0x27;
  case SDLK_DOWN:
    return 0x28;
  case SDLK_BACKSPACE:
    return 8;
  case SDLK_TAB:
    return 9;
  case SDLK_CAPSLOCK:
    return 0x14;
  case SDLK_PAGEUP:
    return 0x21;
  case SDLK_PAGEDOWN:
    return 0x22;
  case SDLK_END:
    return 0x23;
  case SDLK_HOME:
    return 0x24;
  case SDLK_PRINTSCREEN:
    return 0x2c;
  case SDLK_INSERT:
    return 0x2d;
  case SDLK_DELETE:
    return 0x2e;
  case SDLK_HELP:
    return 0x2f;
  case SDLK_KP_0:
    return symbol->mod & KMOD_NUM ? 0x60 : 0x2d;
  case SDLK_KP_PERIOD:
    return symbol->mod & KMOD_NUM ? 0x6e : 0x2e;
  case SDLK_KP_MULTIPLY:
    return 0x6a;
  case SDLK_KP_PLUS:
    return 0x6b;
  case SDLK_KP_COMMA:
    return 0x6c;
  case SDLK_KP_MINUS:
    return 0x6d;
  case SDLK_KP_DIVIDE:
    return 0x6f;
  case SDLK_NUMLOCKCLEAR:
    return 0x90;
  case SDLK_SCROLLLOCK:
    return 0x91;
  case SDLK_LGUI:
    return 0x5b;
  case SDLK_RGUI:
    return 0x5c;
  case SDLK_APPLICATION:
    return 0x5d;
  case SDLK_SEMICOLON:
  case SDLK_COLON:
    return 0xba;
  case SDLK_EQUALS:
  case SDLK_PLUS:
  case SDLK_KP_EQUALS:
    return 0xbb;
  case SDLK_COMMA:
  case SDLK_LESS:
    return 0xbc;
  case SDLK_MINUS:
  case SDLK_UNDERSCORE:
    return 0xbd;
  case SDLK_PERIOD:
  case SDLK_GREATER:
    return 0xbe;
  case SDLK_SLASH:
  case SDLK_QUESTION:
    return 0xbf;
  case SDLK_BACKQUOTE:
    return 0xc0;
  case SDLK_LEFTBRACKET:
    return 0xdb;
  case SDLK_BACKSLASH:
    return symbol->scancode == SDL_SCANCODE_NONUSBACKSLASH ? 0xe2 : 0xdc;
  case SDLK_RIGHTBRACKET:
    return 0xdd;
  case SDLK_QUOTE:
  case SDLK_QUOTEDBL:
    return 0xde;
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    return 0x10;
  case SDLK_LCTRL:
  case SDLK_RCTRL:
    return 0x11;
  case SDLK_LALT:
  case SDLK_RALT:
    return 0x12;
  default:
    if (symbol->scancode == SDL_SCANCODE_NONUSBACKSLASH)
      return 0xe2;
    return 0;
  }
}
static unsigned char ansi_character(U32 codepoint) {
  /* The native runtime's GetACP is 1252. Keep UTF-8 byte fragments out of
   * the original ANSI window procedure, including composed/IME text. */
  int byte = bb_codepage_encode(1252, codepoint, 0);
  return byte < 0 ? '?' : (unsigned char)byte;
}
static void text_input(const char *text, size_t capacity) {
  size_t length = strnlen(text, capacity);
  for (size_t i = 0; i < length;) {
    unsigned first = (unsigned char)text[i++], extra = 0;
    U32 codepoint = first, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf)
      codepoint = first & 31u, extra = 1, minimum = 0x80;
    else if (first >= 0xe0 && first <= 0xef)
      codepoint = first & 15u, extra = 2, minimum = 0x800;
    else if (first >= 0xf0 && first <= 0xf4)
      codepoint = first & 7u, extra = 3, minimum = 0x10000;
    else if (first >= 0x80)
      codepoint = '?';
    unsigned consumed = 0;
    while (consumed < extra && i < length &&
           ((unsigned char)text[i] & 0xc0u) == 0x80u) {
      codepoint = (codepoint << 6) | ((unsigned char)text[i++] & 63u);
      ++consumed;
    }
    if (consumed != extra || codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
      codepoint = '?';
    queue(0x102, ansi_character(codepoint), 1);
  }
}
static int game_point(int *x, int *y) {
  SDL_Renderer *renderer = SDL_GetRenderer(bb_window);
  int width = 640, height = 480;
  if (renderer)
    SDL_RenderGetLogicalSize(renderer, &width, &height);
  int inside = *x >= 0 && *y >= 0 && *x < width && *y < height;
  *x = *x < 0 ? 0 : *x >= width ? width - 1 : *x;
  *y = *y < 0 ? 0 : *y >= height ? height - 1 : *y;
  return inside;
}
static void toggle_fullscreen(void) {
  U32 mode = SDL_GetWindowFlags(bb_window) & SDL_WINDOW_FULLSCREEN
                 ? 0
                 : SDL_WINDOW_FULLSCREEN_DESKTOP;
  if (SDL_SetWindowFullscreen(bb_window, mode))
    fprintf(stderr, "Cannot toggle fullscreen: %s\n", SDL_GetError());
}
void bb_pump_events(void) {
  if (!bb_window)
    return;
  SDL_Event event;
  while (SDL_PollEvent(&event))
    switch (event.type) {
    case SDL_QUIT:
      queue(0x10, 0, 0);
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
#ifdef __APPLE__
      if (event.key.keysym.sym == SDLK_f) {
        if (event.type == SDL_KEYDOWN && !event.key.repeat)
          fullscreen_f = (event.key.keysym.mod & KMOD_CTRL) &&
                         (event.key.keysym.mod & KMOD_GUI);
        /* Keep repeats and the release with the shortcut, even if either
         * modifier is released before F. Plain F still belongs to the game. */
        if (fullscreen_f) {
          if (event.type == SDL_KEYDOWN && !event.key.repeat)
            toggle_fullscreen();
          if (event.type == SDL_KEYUP)
            fullscreen_f = 0;
          break;
        }
      }
#endif
      if (event.key.keysym.sym == SDLK_RETURN ||
          event.key.keysym.sym == SDLK_KP_ENTER) {
        int *shortcut =
            &fullscreen_enter[event.key.keysym.sym == SDLK_KP_ENTER];
        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
          *shortcut = !!(event.key.keysym.mod & KMOD_ALT);
          if (*shortcut)
            toggle_fullscreen();
        }
        /* Consume the whole shortcut even if Alt is released before Enter.
         * Track the main and keypad Enter independently; plain Enter and F11
         * retain their original game behavior. */
        if (*shortcut) {
          if (event.type == SDL_KEYUP)
            *shortcut = 0;
          break;
        }
      }
      U32 key = virtual_key(&event.key.keysym);
      if (key)
        enqueue((void *)1, event.type == SDL_KEYDOWN ? 0x100 : 0x101, key,
                event.type == SDL_KEYUP ? (I32)0xc0000001
                : event.key.repeat      ? 0x40000001
                                        : 1,
                MESSAGE_SDL_KEY);
      break;
    }
    case SDL_TEXTINPUT:
      text_input(event.text.text, sizeof event.text.text);
      break;
    case SDL_MOUSEMOTION: {
      int x = event.motion.x, y = event.motion.y;
      game_point(&x, &y);
      U32 state = event.motion.state;
      U32 buttons = (state & SDL_BUTTON_LMASK ? 1 : 0) |
                    (state & SDL_BUTTON_RMASK ? 2 : 0) |
                    (state & SDL_BUTTON_MMASK ? 16 : 0);
      enqueue((void *)1, 0x200, buttons,
              (I32)((U32)(U16)x | ((U32)(U16)y << 16)), MESSAGE_MOTION);
      break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      int x = event.button.x, y = event.button.y;
      if (!game_point(&x, &y) && event.type == SDL_MOUSEBUTTONDOWN)
        break;
      U32 code = event.button.button == SDL_BUTTON_LEFT    ? 0x201
                 : event.button.button == SDL_BUTTON_RIGHT ? 0x204
                                                           : 0x207;
      if (event.type == SDL_MOUSEBUTTONUP)
        code++;
      else if (event.button.clicks == 2)
        code += 2;
      queue(code,
            event.type == SDL_MOUSEBUTTONDOWN
                ? (event.button.button == SDL_BUTTON_LEFT ? 1 : 2)
                : 0,
            (I32)((U32)(U16)x | ((U32)(U16)y << 16)));
      break;
    }
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_ENTER ||
          event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        SDL_ShowCursor(SDL_DISABLE);
      if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        queue(6, 1, 0);
        queue(0x1c, 1, 0);
        queue(7, 0, 0);
      }
      if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        queue(6, 0, 0);
        queue(0x1c, 0, 0);
        queue(8, 0, 0);
      }
      if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
        queue(15, 0, 0);
      break;
    default:
      break;
    }
}
static U16 W RegisterClassA(const WindowClass *window_class) {
  if (!window_class || !window_class->class_name ||
      class_index(window_class->class_name) != UINT32_MAX)
    return 0;
  unsigned i = 0;
  while (i < 32 && classes[i].name)
    i++;
  if (i == 32)
    return 0;
  char *name = strdup(window_class->class_name);
  if (!name)
    return 0;
  classes[i] = (struct RegisteredClass){
      .name = name, .procedure = window_class->procedure, .atom = next_atom++};
#ifdef BB_MODEL_RUNTIME
  if (!bb_model_capture_context(&classes[i].context))
    bb_die("window class without a C dispatch environment");
  registered_context = classes[i].context;
#endif
  registered_proc = window_class->procedure;
  return classes[i].atom;
}
static int W UnregisterClassA(const char *name, Handle instance) {
  unsigned i = class_index(name);
  if (i == UINT32_MAX || main_class == i)
    return 0;
  for (unsigned n = 0; n < 32; n++)
    if (callback_windows[n].handle && callback_windows[n].class_index == i)
      return 0;
  free(classes[i].name);
  memset(classes + i, 0, sizeof classes[i]);
  return 1;
}
static Handle W CreateWindowExA(U32 extended, const char *class_name,
                                const char *title, U32 style, int x, int y,
                                int width, int height, Handle parent,
                                Handle menu, Handle instance, void *parameter) {
  unsigned registered = class_index(class_name);
  WindowProc procedure = registered != UINT32_MAX
                             ? classes[registered].procedure
                             : registered_proc;
  /* Audio drivers create zero-sized callback windows. They retain independent
   * HWND/procedure ownership and do not replace the game's visible SDL window.
   */
  if (bb_window || (!width && !height)) {
    unsigned i = 0;
    while (i < 32 && callback_windows[i].handle)
      i++;
    if (i == 32)
      return NULL;
    Handle handle = (void *)(uintptr_t)next_window++;
    callback_windows[i] =
        (struct CallbackWindow){.handle = handle,
                                .parent = style & 0x40000000u ? parent : NULL,
                                .procedure = procedure,
                                .class_index = registered};
#ifdef BB_MODEL_RUNTIME
    callback_windows[i].context = registered != UINT32_MAX
                                      ? classes[registered].context
                                      : registered_context;
#endif
    if (procedure) {
      call_procedure(procedure, handle, 0x81, 0, 0);
      call_procedure(procedure, handle, 1, 0, 0);
    }
    return handle;
  }
#ifdef __APPLE__
  /* Match the game's desktop-fullscreen toggle without an asynchronous
   * macOS Spaces transition. Set this before video initialization. */
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
    bb_die("SDL initialization: %s", SDL_GetError());
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  bb_window = SDL_CreateWindow(title ? title : "Beasts and Bumpkins",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               640, 480, SDL_WINDOW_RESIZABLE);
  if (!bb_window)
    bb_die("SDL window creation: %s", SDL_GetError());
  SDL_StartTextInput();
  /* The game paints its own cursor into its surfaces. Keep the platform
   * cursor empty as well as hidden, including focus/fullscreen transitions
   * that might temporarily expose it. Headless SDL backends need no cursor. */
  const Uint8 blank[8] = {0};
  game_cursor = SDL_CreateCursor(blank, blank, 8, 8, 0, 0);
  if (game_cursor)
    SDL_SetCursor(game_cursor);
  SDL_ShowCursor(SDL_DISABLE);
  main_class = registered;
  fullscreen_enter[0] = fullscreen_enter[1] = 0;
#ifdef __APPLE__
  fullscreen_f = 0;
#endif
  bb_window_proc = procedure;
#ifdef BB_MODEL_RUNTIME
  main_context = registered != UINT32_MAX ? classes[registered].context
                                          : registered_context;
#endif
  if (bb_window_proc) {
    call_procedure(bb_window_proc, (void *)1, 0x81, 0, 0);
    call_procedure(bb_window_proc, (void *)1, 1, 0, 0);
  }
  queue(6, 1, 0);
  queue(0x1c, 1, 0);
  queue(7, 0, 0);
  queue(5, 0, (480 << 16) | 640);
  queue(15, 0, 0);
  fprintf(stderr, "Native SDL window ready: 640x480\n");
  return (void *)1;
}
static int W DestroyWindow(Handle window) {
  static int destroying;
  struct CallbackWindow *record = callback_window(window);
  if (record) {
    if (record->destroying)
      return 0;
    record->destroying = 1;
    if (record->procedure) {
      call_procedure(record->procedure, window, 2, 0, 0);
      call_procedure(record->procedure, window, 0x82, 0, 0);
    }
    memset(record, 0, sizeof *record);
    return 1;
  }
  if (!bb_window || window != (void *)1 || destroying)
    return 0;
  destroying = 1;
  /* The original window procedure posts WM_QUIT from WM_DESTROY. These
   * notifications are synchronous and precede destruction of the SDL window. */
  if (bb_window_proc)
    call_procedure(bb_window_proc, window, 2, 0, 0);
  if (bb_window_proc)
    call_procedure(bb_window_proc, window, 0x82, 0, 0);
  SDL_StopTextInput();
  bb_draw_reset_presentation();
  SDL_DestroyWindow(bb_window);
  if (game_cursor) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    SDL_FreeCursor(game_cursor);
    game_cursor = NULL;
  }
  bb_window = NULL;
  bb_window_proc = NULL;
#ifdef BB_MODEL_RUNTIME
  main_context = (struct bb_model_context){0};
#endif
  main_class = UINT32_MAX;
  destroying = 0;
  return 1;
}
static I32 W DefWindowProcA(Handle window, U32 message, U32 wparam,
                            I32 lparam) {
  if (message == 0x81)
    return 1;
  if (message == 0x10)
    DestroyWindow(window);
  return 0;
}
static I32 W CallWindowProcA(WindowProc procedure, Handle window, U32 message,
                             U32 wparam, I32 lparam) {
  return call_procedure(procedure, window, message, wparam, lparam);
}
static I32 W SetWindowLongA(Handle window, int index, I32 value) {
  if (index == -4) {
    struct CallbackWindow *record = callback_window(window);
    if (record) {
      WindowProc old = record->procedure;
      record->procedure = (WindowProc)(uintptr_t)value;
      return (I32)(uintptr_t)old;
    }
    if (window != (void *)1)
      return 0;
    WindowProc old = bb_window_proc;
    bb_window_proc = (WindowProc)(uintptr_t)value;
    return (I32)(uintptr_t)old;
  }
  return 0;
}
static int W PostMessageA(Handle window, U32 message, U32 wparam, I32 lparam) {
  queue_window(window, message, wparam, lparam);
  return 1;
}
static void W PostQuitMessage(int code) {
  queue_window(NULL, 0x12, (U32)code, 0);
}
static int matches_window(Handle message_window, Handle window) {
  if (!window)
    return 1;
  if (window == (void *)UINT32_MAX)
    return !message_window;
  for (unsigned n = 0; message_window && n < 33; ++n) {
    if (message_window == window)
      return 1;
    struct CallbackWindow *record = callback_window(message_window);
    message_window = record ? record->parent : NULL;
  }
  return 0;
}
static int peek_queued(Message *output, Handle window, U32 minimum, U32 maximum,
                       U32 flags) {
  /* Range endpoints use their low WORD. WM_QUIT bypasses filtering; every
   * skipped message remains queued in its original order, including motion
   * coalescing metadata when a match is removed from the middle of the ring.
   * https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-peekmessagea
   */
  minimum &= 0xffffu;
  maximum &= 0xffffu;
  pthread_mutex_lock(&message_mutex);
  for (unsigned n = 0; n < message_count; ++n) {
    unsigned slot = (first_message + n) % 1024;
    Message *message = messages + slot;
    if (message->message != 0x12 &&
        (!matches_window(message_window(message), window) ||
         ((minimum || maximum) &&
          (message->message < minimum || message->message > maximum))))
      continue;
    *output = *message;
    retrieved_message.output = output;
    retrieved_message.value = *output;
    retrieved_message.flags = message_flags[slot];
    if (flags & 1) {
      if (!n)
        first_message = (first_message + 1) % 1024;
      else
        for (unsigned i = n; i + 1 < message_count; ++i) {
          unsigned destination = (first_message + i) % 1024;
          unsigned source = (destination + 1) % 1024;
          messages[destination] = messages[source];
          message_flags[destination] = message_flags[source];
        }
      --message_count;
    }
    pthread_mutex_unlock(&message_mutex);
    return 1;
  }
  pthread_mutex_unlock(&message_mutex);
  return 0;
}
static int W PeekMessageA(Message *output, Handle window, U32 minimum,
                          U32 maximum, U32 flags) {
  bb_pump_events();
  return peek_queued(output, window, minimum, maximum, flags);
}
static int W GetMessageA(Message *output, Handle window, U32 minimum,
                         U32 maximum) {
  while (!PeekMessageA(output, window, minimum, maximum, 1)) {
    /* A null output waits without removing/reordering an SDL event. In
     * particular, sdl2-compat cannot push a text event back into SDL3. */
    if (!bb_window || !SDL_WaitEvent(NULL))
      bb_die("GetMessage without a window");
  }
  return output->message != 0x12;
}
static U32 key_character(U32 key, SDL_Keymod modifiers) {
  if (key == 8 || key == 9 || key == 13 || key == 27)
    return key;
  if (modifiers & KMOD_CTRL)
    return key >= 'A' && key <= 'Z' ? key - 'A' + 1 : 0;
  int shift = !!(modifiers & KMOD_SHIFT);
  if (key >= 'A' && key <= 'Z')
    return shift != !!(modifiers & KMOD_CAPS) ? key : key + 'a' - 'A';
  if (key >= '0' && key <= '9')
    return shift ? (U32)(unsigned char)")!@#$%^&*("[key - '0'] : key;
  if (key == 32)
    return ' ';
  if (key >= 0x60 && key <= 0x69)
    return key - 0x60 + '0';
  static const struct {
    U32 key;
    char plain, shifted;
  } punctuation[] = {{0x6a, '*', '*'}, {0x6b, '+', '+'},  {0x6c, ',', ','},
                     {0x6d, '-', '-'}, {0x6e, '.', '.'},  {0x6f, '/', '/'},
                     {0xba, ';', ':'}, {0xbb, '=', '+'},  {0xbc, ',', '<'},
                     {0xbd, '-', '_'}, {0xbe, '.', '>'},  {0xbf, '/', '?'},
                     {0xc0, '`', '~'}, {0xdb, '[', '{'},  {0xdc, '\\', '|'},
                     {0xdd, ']', '}'}, {0xde, '\'', '"'}, {0xe2, '\\', '|'}};
  for (unsigned i = 0; i < sizeof punctuation / sizeof *punctuation; ++i)
    if (key == punctuation[i].key)
      return (unsigned char)(shift ? punctuation[i].shifted
                                   : punctuation[i].plain);
  return 0;
}
static int W TranslateMessage(const Message *message) {
  if (message->message != 0x100 && message->message != 0x101 &&
      message->message != 0x104 && message->message != 0x105)
    return 0;
  if (message->message == 0x100 || message->message == 0x104) {
    int sdl_key = retrieved_message.output == message &&
                  (retrieved_message.flags & MESSAGE_SDL_KEY) &&
                  !memcmp(&retrieved_message.value, message, sizeof *message);
    /* Printable hardware text is already committed by SDL_TEXTINPUT. Control
     * characters have no SDL text event. Posted demo keys retain translation.
     */
    U32 character = sdl_key && message->wparam != 8 && message->wparam != 9 &&
                            message->wparam != 13 && message->wparam != 27
                        ? 0
                        : key_character(message->wparam, SDL_GetModState());
    if (character)
      queue_window(message_window(message),
                   message->message == 0x104 ? 0x106 : 0x102, character,
                   message->lparam);
  }
  return 1;
}
static I32 W DispatchMessageA(const Message *message) {
  WindowProc procedure = window_procedure(message_window(message));
  int previous = bb_dispatching_mouse_motion;
  if (message->message == 0x200)
    bb_dispatching_mouse_motion = 1;
  I32 result = procedure ? call_procedure(procedure, message_window(message),
                                          message->message, message->wparam,
                                          message->lparam)
                         : 0;
  bb_dispatching_mouse_motion = previous;
  return result;
}
int bb_native_dispatch_pending(U32 *quit_code) {
  /* Poll SDL once per game iteration. New motion arriving while dispatching
   * belongs to the next frame; posted-message floods cannot starve gameplay. */
  bb_pump_events();
  Message message;
  for (unsigned n = 0; n < 64 && peek_queued(&message, NULL, 0, 0, 1); ++n) {
    if (message.message == 0x12) {
      *quit_code = message.wparam;
      return 0;
    }
    TranslateMessage(&message);
    DispatchMessageA(&message);
  }
  return 1;
}
static int W WaitMessage(void) { return bb_window && SDL_WaitEvent(NULL); }
static Handle W BeginPaint(Handle window, void *paint) {
  memset(paint, 0, 64);
  U32 context = 1;
  memcpy(paint, &context, sizeof context);
  return (void *)1;
}
static int W EndPaint(Handle window, const void *paint) { return 1; }
static int W UpdateWindow(Handle window) {
  WindowProc procedure = window_procedure(window);
  if (procedure)
    call_procedure(procedure, window, 15, 0, 0);
  return 1;
}
static int W SetCursorPos(int x, int y) {
  if (bb_window) {
    SDL_Renderer *renderer = SDL_GetRenderer(bb_window);
    if (renderer)
      SDL_RenderLogicalToWindow(renderer, (float)x, (float)y, &x, &y);
    SDL_WarpMouseInWindow(bb_window, x, y);
  }
  return 1;
}
static int W GetSystemMetrics(int index) {
  switch (index) {
  case 0:
    return 640;
  case 1:
    return 480;
  case 4:
    return 0;
  case 19:
    return 1;
  case 43:
    return 3;
  default:
    return 0;
  }
}
static Handle W LoadIconA(Handle instance, const char *name) {
  return (void *)1;
}
static Handle W LoadCursorA(Handle instance, const char *name) {
  return (void *)1;
}
static int W MessageBoxA(Handle window, const char *text, const char *caption,
                         U32 type) {
  fprintf(stderr, "Game dialog [%s]: %s\n", caption ? caption : "",
          text ? text : "");
  return 1;
}
static Handle W GetActiveWindow(void) { return bb_window ? (void *)1 : NULL; }
static U32 W GetWindowThreadProcessId(Handle window, U32 *id) {
  if (id)
    *id = (U32)getpid();
  return (U32)getpid();
}
static int W EnumWindows(int(W *callback)(Handle, I32), I32 parameter) {
  if (bb_window) {
#ifdef BB_MODEL_RUNTIME
    struct bb_call_state state = {0, 0, 0, 0x202};
    uint32_t arguments[] = {1, (U32)parameter};
    bb_model_invoke(&state, (U32)(uintptr_t)callback, arguments, 2);
    return (int)state.eax;
#else
    return ((int(W *)(Handle, I32))bb_native_entry((uintptr_t)callback))(
        (void *)1, parameter);
#endif
  }
  return 1;
}
static int W LoadStringA(Handle instance, U32 id, char *output, int capacity) {
  U32 size = 0;
  U16 *block = bb_pe_resource(instance, (const char *)(uintptr_t)(id / 16 + 1),
                              (const char *)6, &size);
  if (!block || capacity < 1)
    return 0;
  for (unsigned n = 0; n < (id & 15); n++)
    block += 1 + *block;
  unsigned length = *block++;
  if (length >= (unsigned)capacity)
    length = (unsigned)capacity - 1;
  for (unsigned n = 0; n < length; n++)
    output[n] = (char)block[n];
  output[length] = '\0';
  return (int)length;
}
#ifndef BB_MODEL_RUNTIME
static int wsprintfA(char *output, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int count = vsprintf(output, format, args);
  va_end(args);
  return count;
}
#endif
void *bb_window_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(RegisterClassA),   API_ENTRY(UnregisterClassA),
      API_ENTRY(CreateWindowExA),  API_ENTRY(DestroyWindow),
      API_ENTRY(DefWindowProcA),   API_ENTRY(CallWindowProcA),
      API_ENTRY(SetWindowLongA),   API_ENTRY(PostMessageA),
      API_ENTRY(PostQuitMessage),  API_ENTRY(PeekMessageA),
      API_ENTRY(GetMessageA),      API_ENTRY(TranslateMessage),
      API_ENTRY(DispatchMessageA), API_ENTRY(WaitMessage),
      API_ENTRY(BeginPaint),       API_ENTRY(EndPaint),
      API_ENTRY(UpdateWindow),     API_ENTRY(SetCursorPos),
      API_ENTRY(GetSystemMetrics), API_ENTRY(LoadIconA),
      API_ENTRY(LoadCursorA),      API_ENTRY(MessageBoxA),
      API_ENTRY(GetActiveWindow),  API_ENTRY(GetWindowThreadProcessId),
      API_ENTRY(EnumWindows),      API_ENTRY(LoadStringA),
#ifndef BB_MODEL_RUNTIME
      API_ENTRY(wsprintfA)
#endif
  };
  API_RETURN(table);
}

#ifdef BB_MODEL_RUNTIME
#include "window_models.inc"
#endif
