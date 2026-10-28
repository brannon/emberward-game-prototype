#include "app_internal.h"
#include "render.h"

#include <stdio.h>

bool platform_set_save_path(App *app, const char *path)
{
    int length = snprintf(app->save_path, sizeof(app->save_path), "%s", path);
    if (length < 0 || (size_t)length >= sizeof(app->save_path)) {
        fprintf(stderr, "Save file path is too long.\n");
        return false;
    }
    app->path = app->save_path;
    return true;
}

void platform_init(App *app)
{
    if (app->path) return;
    char *pref = SDL_GetPrefPath("Emberward", "Emberward");
    if (pref) {
        int length = snprintf(app->save_path, sizeof(app->save_path), "%sjourney.sav", pref);
        if (length >= 0 && (size_t)length < sizeof(app->save_path)) app->path = app->save_path;
        else fprintf(stderr, "Save directory path is too long.\n");
    } else {
        fprintf(stderr, "Save directory unavailable: %s\n", SDL_GetError());
    }
    SDL_free(pref);
}

SDL_RendererLogicalPresentation platform_presentation(void)
{
    return SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
}

void platform_configure_window(App *app)
{
    if (!SDL_SetWindowMinimumSize(app->window, SCREEN_W, SCREEN_H))
        fprintf(stderr, "Could not set minimum window size: %s\n", SDL_GetError());
    if (!SDL_SetWindowPosition(app->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED))
        fprintf(stderr, "Could not center window: %s\n", SDL_GetError());
    if (!app->smoke && !SDL_SetRenderVSync(app->renderer, 1))
        fprintf(stderr, "VSync unavailable: %s\n", SDL_GetError());
}

void platform_toggle_fullscreen(App *app)
{
    bool fullscreen = (SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(app->window, !fullscreen))
        fprintf(stderr, "Fullscreen toggle failed: %s\n", SDL_GetError());
}

const char *platform_save_unavailable(void)
{
    return NULL;
}

void platform_commit_save(PlatformSaveCallback callback, void *context)
{
    callback(context, true, "JOURNEY SAVED.");
}

void platform_finish_frame(App *app)
{
    if (!app->smoke) SDL_Delay(1);
}

void platform_shutdown(void)
{
}
