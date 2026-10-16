/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RENDER_CALLS_H
#define BB_RENDER_CALLS_H
#include "game_calls.h"
#ifdef BB_MODEL_RUNTIME
#include "render_models.h"
#endif
/* Graphics use the common packed game-call contract. */
#define bb_render_call bb_game_call
#define bb_render_method bb_game_method
#define BB_RENDER_CALL BB_GAME_CALL
#define BB_RENDER_METHOD BB_GAME_METHOD
#endif
