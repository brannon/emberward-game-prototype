#ifndef EMBERWARD_RENDER_H
#define EMBERWARD_RENDER_H

#include "game.h"
#include <SDL3/SDL.h>

#define SCREEN_W 384
#define SCREEN_H 288

bool render_init(SDL_Renderer *renderer);
void render_shutdown(void);
void render_game(SDL_Renderer *renderer, const Game *game, bool muted);

#endif
