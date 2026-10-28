#ifndef EMBERWARD_APP_INTERNAL_H
#define EMBERWARD_APP_INTERNAL_H

#include "game.h"
#include "platform.h"

typedef struct {
    SDL_AudioStream *stream;
    double melody_phase, bass_phase, effect_phase;
    unsigned long sample;
    Sound effect;
    int effect_left, effect_length;
    int track;
    bool muted;
} Audio;

struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    Audio audio;
    Game game;
    char save_path[1024];
    const char *path;
    char *capture;
    Uint64 last;
    int frames, verified_scenes;
    uint32_t previous_frame;
    unsigned held;
    bool smoke, focused, saving;
    SDL_AppResult result;
};

void app_set_focus(App *app, bool focused);
void app_input(App *app, Input input, bool pressed);
/* Saving returns whether the request was accepted, not whether persistence completed. */
bool app_save_or_load(App *app, bool load);

#endif
