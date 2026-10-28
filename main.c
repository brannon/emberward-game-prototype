#include "app_internal.h"
#include "render.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SAMPLE_RATE 22050
#define AUDIO_BLOCK 512
#define TAU 6.2831853071795864769

static void audio_init(Audio *a, bool muted)
{
    memset(a, 0, sizeof(*a));
    a->muted = muted;
    SDL_AudioSpec desired;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE;
    desired.format = SDL_AUDIO_F32;
    desired.channels = 1;
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, NULL, NULL);
    if (!a->stream)
        fprintf(stderr, "Audio unavailable; continuing without sound: %s\n", SDL_GetError());
    else if (!SDL_ResumeAudioStreamDevice(a->stream)) {
        fprintf(stderr, "Audio playback could not start: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(a->stream);
        a->stream = NULL;
    }
}

static double note_frequency(int note)
{
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

static void audio_pump(Audio *a, const Game *g)
{
    if (!a->stream) return;
    if (g->sound != SFX_NONE) {
        a->effect = g->sound;
        a->effect_length = g->sound == SFX_WIN ? SAMPLE_RATE / 2 :
                           g->sound == SFX_STEP ? SAMPLE_RATE / 40 : SAMPLE_RATE / 6;
        a->effect_left = a->effect_length;
        a->effect_phase = 0;
    }
    if (a->muted) {
        if (!SDL_ClearAudioStream(a->stream))
            fprintf(stderr, "Could not clear audio stream: %s\n", SDL_GetError());
        return;
    }
    int track = g->screen == BATTLE ? 2 : g->screen == TITLE || g->screen == ENDING ? 0 : 1;
    if (track != a->track) { a->track = track; a->sample = 0; }
    static const int melody[3][32] = {
        {64,67,71,76,74,71,67,64,62,66,69,74,72,69,66,62,
         60,64,67,72,71,67,64,60,59,62,66,71,69,66,62,59},
        {64,0,67,69,71,0,69,67,62,0,66,67,69,0,67,66,
         60,0,64,67,72,0,71,67,62,0,66,69,71,0,69,66},
        {64,64,76,71,74,71,67,71,62,62,74,69,72,69,66,69,
         60,60,72,67,71,67,64,67,59,59,71,66,69,66,62,66}
    };
    static const int bass[4] = {40, 38, 36, 35};
    int queued = SDL_GetAudioStreamQueued(a->stream);
    if (queued < 0) {
        fprintf(stderr, "Could not query audio stream: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(a->stream);
        a->stream = NULL;
        return;
    }
    while (queued < AUDIO_BLOCK * (int)sizeof(float) * 2) {
        float samples[AUDIO_BLOCK];
        for (int i = 0; i < AUDIO_BLOCK; ++i) {
            double beat_length = track == 2 ? 0.135 : track == 0 ? 0.24 : 0.21;
            unsigned long note_samples = (unsigned long)((double)SAMPLE_RATE * beat_length);
            int step = (int)((a->sample / note_samples) % 32);
            double position = (double)(a->sample % note_samples) / (double)note_samples;
            int note = melody[track][step];
            double frequency = note ? note_frequency(note) : 0;
            a->melody_phase += frequency / SAMPLE_RATE;
            a->melody_phase -= floor(a->melody_phase);
            a->bass_phase += note_frequency(bass[step / 8]) / SAMPLE_RATE;
            a->bass_phase -= floor(a->bass_phase);
            double envelope = position < 0.04 ? position / 0.04 :
                              position > 0.78 ? (1 - position) / 0.22 : 1;
            double value = note ? (a->melody_phase < 0.25 ? 0.026 : -0.009) * envelope : 0;
            value += sin(a->bass_phase * TAU) * 0.025;
            if (a->effect_left > 0) {
                double progress = 1 - (double)a->effect_left / a->effect_length;
                double effect_hz = 420;
                switch (a->effect) {
                    case SFX_STEP: effect_hz = 90; break;
                    case SFX_CONFIRM: effect_hz = 620 + progress * 300; break;
                    case SFX_HIT: effect_hz = 180 - progress * 130; break;
                    case SFX_MAGIC: effect_hz = 330 + progress * 990; break;
                    case SFX_WIN: effect_hz = note_frequency(72 + (int)(progress * 4) * 4); break;
                    case SFX_HURT: effect_hz = 230 - progress * 170; break;
                    case SFX_CHEST: effect_hz = 660 + progress * 660; break;
                    case SFX_NONE: break;
                }
                a->effect_phase += effect_hz / SAMPLE_RATE;
                a->effect_phase -= floor(a->effect_phase);
                value += (a->effect_phase < 0.5 ? 0.055 : -0.055) * (1 - progress);
                --a->effect_left;
            }
            samples[i] = (float)value;
            ++a->sample;
        }
        if (!SDL_PutAudioStreamData(a->stream, samples, (int)sizeof(samples))) {
            fprintf(stderr, "Audio queue failed: %s\n", SDL_GetError());
            SDL_DestroyAudioStream(a->stream);
            a->stream = NULL;
            break;
        }
        queued += (int)sizeof(samples);
    }
}

static void save_complete(void *context, bool success, const char *message)
{
    App *app = context;
    app->saving = false;
    if (!success) fprintf(stderr, "Save failed: %s\n", message);
    game_notify(&app->game, message);
}

bool app_save_or_load(App *app, bool load)
{
    Game *g = &app->game;
    if (app->saving) return false;
    if (!app->path) {
        game_notify(g, "SAVE DIRECTORY IS UNAVAILABLE.\nSEE THE TERMINAL FOR DETAILS.");
        return false;
    }
    if ((load && g->screen != TITLE && g->screen != EXPLORE && g->screen != MENU) ||
        (!load && g->screen != EXPLORE && g->screen != MENU)) return false;
    const char *unavailable = load ? NULL : platform_save_unavailable();
    if (unavailable) {
        fprintf(stderr, "Save failed: %s\n", unavailable);
        game_notify(g, unavailable);
        return false;
    }
    char error[160];
    bool ok = load ? game_load(g, app->path, error, sizeof(error)) :
                     game_save(g, app->path, error, sizeof(error));
    if (ok) {
        if (!load) {
            app->saving = true;
            platform_commit_save(save_complete, app);
            return true;
        }
        game_notify(g, "JOURNEY RESTORED.\nTHE COAST IS WAITING FOR YOU.");
    } else {
        char message[160];
        snprintf(message, sizeof(message), "%s FAILED.\n%.120s", load ? "LOAD" : "SAVE", error);
        fprintf(stderr, "%s: %s\n", load ? "Load" : "Save", error);
        game_notify(g, message);
    }
    return ok;
}

static bool map_key(SDL_Keycode key, Input *input)
{
    switch (key) {
        case SDLK_UP: case SDLK_W: *input = UP; break;
        case SDLK_DOWN: case SDLK_S: *input = DOWN; break;
        case SDLK_LEFT: case SDLK_A: *input = LEFT; break;
        case SDLK_RIGHT: case SDLK_D: *input = RIGHT; break;
        case SDLK_Z: case SDLK_RETURN: case SDLK_SPACE: *input = ACCEPT; break;
        case SDLK_X: case SDLK_ESCAPE: case SDLK_BACKSPACE: *input = BACK; break;
        case SDLK_TAB: case SDLK_C: *input = OPEN_MENU; break;
        default: return false;
    }
    return true;
}

static void smoke_scene(Game *g, int frame)
{
    if (frame == 10) {
        game_new(g);
        while (g->screen == DIALOG) game_input(g, ACCEPT);
    } else if (frame == 20) {
        game_load_map(g, WORLD); g->x = 17; g->y = 18;
    } else if (frame == 30) {
        game_load_map(g, RUINS); g->x = 14; g->y = 4;
    } else if (frame == 40) {
        g->screen = MENU;
    } else if (frame == 50) {
        game_start_battle(g, false);
        g->battle.count = 3;
        for (int i = 0; i < 3; ++i) {
            g->battle.enemies[i].kind = i;
            g->battle.enemies[i].hp = 20;
            g->battle.enemies[i].max_hp = 20;
        }
    } else if (frame == 60) {
        for (int i = 0; i < 3; ++i) g->battle.enemies[i].kind = i + 2;
        game_input(g, ACCEPT);
    } else if (frame == 70) {
        game_start_battle(g, true);
    } else if (frame == 80) {
        g->screen = EXPLORE;
        game_notify(g, "SMOKE TEST: THE COAST IS WAITING.\nALL SYSTEMS READY.");
    } else if (frame == 90) g->screen = GAME_OVER;
    else if (frame == 100) g->screen = ENDING;
    else if (frame == 110) {
        g->screen = EXPLORE;
        game_load_map(g, WORLD); g->x = 18; g->y = 18;
        game_start_battle(g, false);
        g->battle.count = 3;
        for (int i = 0; i < 3; ++i) {
            g->battle.enemies[i].kind = i == 2 ? 0 : 1;
            g->battle.enemies[i].hp = 25;
            g->battle.enemies[i].max_hp = 25;
        }
    }
}

static bool screenshot(SDL_Renderer *renderer, const char *path)
{
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
    if (!surface) return false;
    bool ok = SDL_SaveBMP(surface, path);
    SDL_DestroySurface(surface);
    return ok;
}

static bool verify_frame(SDL_Renderer *renderer, uint32_t *hash)
{
    SDL_FRect bounds;
    int width, height;
    if (!SDL_GetRenderOutputSize(renderer, &width, &height) ||
        !SDL_GetRenderLogicalPresentationRect(renderer, &bounds) ||
        bounds.x < -0.5f || bounds.y < -0.5f ||
        bounds.x + bounds.w > (float)width + 0.5f ||
        bounds.y + bounds.h > (float)height + 0.5f) return false;
    SDL_Surface *readback = SDL_RenderReadPixels(renderer, NULL);
    if (!readback) return false;
    SDL_Surface *surface = SDL_ConvertSurface(readback, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(readback);
    if (!surface) return false;
    uint32_t colors[32];
    int color_count = 0;
    *hash = 2166136261u;
    {
        const unsigned char *pixels = surface->pixels;
        size_t bytes = (size_t)surface->pitch * (size_t)surface->h;
        for (size_t i = 0; i + 4 <= bytes; i += 4 * 53) {
            uint32_t pixel;
            memcpy(&pixel, pixels + i, sizeof(pixel));
            *hash = (*hash ^ pixel) * 16777619u;
            int j = 0;
            while (j < color_count && colors[j] != pixel) ++j;
            if (j == color_count && color_count < 32) colors[color_count++] = pixel;
        }
    }
    SDL_DestroySurface(surface);
    return color_count >= 12;
}

void app_set_focus(App *app, bool focused)
{
    if (app->focused == focused) return;
    app->focused = focused;
    app->held = 0;
    app->last = SDL_GetPerformanceCounter();
    if (!focused) SDL_ResetKeyboard();
    if (app->audio.stream) {
        bool ok = focused ? SDL_ResumeAudioStreamDevice(app->audio.stream) :
                            SDL_PauseAudioStreamDevice(app->audio.stream);
        if (!ok) fprintf(stderr, "Audio focus change failed: %s\n", SDL_GetError());
        if (!SDL_ClearAudioStream(app->audio.stream))
            fprintf(stderr, "Could not clear audio on focus change: %s\n", SDL_GetError());
    }
}

void app_input(App *app, Input input, bool pressed)
{
    if (input <= RIGHT) {
        unsigned bit = 1u << (unsigned)input;
        if (pressed && app->focused && !app->saving) app->held |= bit;
        else app->held &= ~bit;
    }
    if (pressed && app->focused && !app->saving) game_input(&app->game, input);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    App *app = SDL_calloc(1, sizeof(*app));
    if (!app) {
        fprintf(stderr, "Could not allocate application state: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    *appstate = app;
    app->focused = true;
    app->result = SDL_APP_SUCCESS;
    bool muted = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--smoke-test") == 0) app->smoke = true;
        else if (strcmp(argv[i], "--mute") == 0) muted = true;
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            SDL_free(app->capture);
            app->capture = SDL_strdup(argv[++i]);
            if (!app->capture) {
                fprintf(stderr, "Could not copy screenshot path: %s\n", SDL_GetError());
                return SDL_APP_FAILURE;
            }
        } else if (strcmp(argv[i], "--save-file") == 0 && i + 1 < argc) {
            if (!platform_set_save_path(app, argv[++i])) return SDL_APP_FAILURE;
        } else {
            fprintf(stderr, "Usage: %s [--mute] [--save-file PATH] [--smoke-test] [--screenshot PATH]\n", argv[0]);
            return SDL_APP_FAILURE;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    bool has_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!has_audio) fprintf(stderr, "Audio initialization failed: %s\n", SDL_GetError());
    app->window = SDL_CreateWindow("Emberward - The Starless Coast",
                                         SCREEN_W * 3, SCREEN_H * 3,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app->window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer || !SDL_SetRenderLogicalPresentation(app->renderer, SCREEN_W, SCREEN_H,
                                                           platform_presentation())) {
        fprintf(stderr, "Renderer initialization failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!render_init(app->renderer)) {
        fprintf(stderr, "Art asset initialization failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    platform_configure_window(app);
    app->audio.muted = muted;
    if (has_audio) audio_init(&app->audio, muted);
    platform_init(app);
    game_init(&app->game, app->smoke ? 12345u : (uint32_t)time(NULL));
    app->last = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = appstate;
    if (event->type == SDL_EVENT_QUIT) return app->result;
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) app_set_focus(app, false);
    if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) app_set_focus(app, true);
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && app->focused && !app->saving) {
        SDL_Keycode key = event->key.key;
        if (key == SDLK_F11) {
            platform_toggle_fullscreen(app);
        } else if (key == SDLK_M) {
            app->audio.muted = !app->audio.muted;
        } else if (key == SDLK_F5) app_save_or_load(app, false);
        else if (key == SDLK_F9 ||
                 (app->game.screen == TITLE && app->game.title_cursor == 1 &&
                  (key == SDLK_Z || key == SDLK_RETURN || key == SDLK_SPACE))) {
            app_save_or_load(app, true);
        } else {
            Input input;
            if (map_key(key, &input)) game_input(&app->game, input);
        }
    }
    return app->game.quit ? app->result : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    App *app = appstate;
    Game *game = &app->game;
    SDL_Renderer *renderer = app->renderer;
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = (float)((double)(now - app->last) / (double)SDL_GetPerformanceFrequency());
    app->last = now;
    if (dt > 0.05f) dt = 0.05f;
    if (app->smoke) { dt = 1.0f / 60.0f; smoke_scene(game, app->frames); }
    bool active = (app->focused || app->smoke) && !app->saving;
    if (active) game_update(game, dt);
    if (game->screen == EXPLORE && active && !app->smoke) {
        const bool *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W] || (app->held & (1u << UP))) game_input(game, UP);
        else if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S] || (app->held & (1u << DOWN))) game_input(game, DOWN);
        else if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] || (app->held & (1u << LEFT))) game_input(game, LEFT);
        else if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] || (app->held & (1u << RIGHT))) game_input(game, RIGHT);
    }
    if (active) audio_pump(&app->audio, game);
    game->sound = SFX_NONE;
    render_game(renderer, game, app->audio.muted || !app->audio.stream);
    if (app->smoke && app->frames % 10 == 0) {
        uint32_t hash;
        if (!verify_frame(renderer, &hash) || hash == app->previous_frame) {
            fprintf(stderr, "Smoke test failed: clipped, blank, unchanged, or unreadable scene at frame %d.\n", app->frames);
            app->result = SDL_APP_FAILURE;
            game->quit = true;
        } else {
            app->previous_frame = hash;
            ++app->verified_scenes;
        }
    }
    ++app->frames;
    if (app->capture && ((!app->smoke && app->frames == 1) || (app->smoke && app->frames == 120))) {
        if (!screenshot(renderer, app->capture)) {
            fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
            app->result = SDL_APP_FAILURE;
        }
        if (!app->smoke) game->quit = true;
    }
    SDL_RenderPresent(renderer);
    if (app->smoke && app->frames >= 120) {
        if (app->result == SDL_APP_SUCCESS)
            printf("SDL smoke test passed: %d frames, %d distinct scenes verified by pixel readback.\n",
                   app->frames, app->verified_scenes);
        game->quit = true;
    }
    platform_finish_frame(app);
    return game->quit ? app->result : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;
    App *app = appstate;
    if (!app) return;
    platform_shutdown();
    if (app->audio.stream) SDL_DestroyAudioStream(app->audio.stream);
    render_shutdown();
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
    SDL_free(app->capture);
    SDL_free(app);
}
