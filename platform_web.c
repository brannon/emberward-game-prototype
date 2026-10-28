#include "app_internal.h"

#include <emscripten.h>
#include <stdio.h>

static App *web_app;
static PlatformSaveCallback save_callback;
static void *save_context;

EM_JS(int, browser_storage_ready, (void), {
    return Module.storageReady ? 1 : 0;
})

EM_JS(void, browser_sync_save, (void), {
    Module.persistSave();
})

EM_JS(void, browser_state, (int screen, int muted, int focused, int saving), {
    Module.updateState(screen, !!muted, !!focused, !!saving);
})

bool platform_set_save_path(App *app, const char *path)
{
    (void)app;
    (void)path;
    fprintf(stderr, "--save-file is native-only; browser saves use IndexedDB.\n");
    return false;
}

void platform_init(App *app)
{
    app->path = "/emberward-saves/journey.sav";
    web_app = app;
}

SDL_RendererLogicalPresentation platform_presentation(void)
{
    return SDL_LOGICAL_PRESENTATION_LETTERBOX;
}

void platform_configure_window(App *app)
{
    (void)app;
}

void platform_toggle_fullscreen(App *app)
{
    /* Fullscreen is handled in the page's user-gesture handler, before SDL input. */
    (void)app;
}

const char *platform_save_unavailable(void)
{
    return browser_storage_ready() ? NULL :
        "BROWSER STORAGE IS UNAVAILABLE.\nEXPORT YOUR JOURNEY TO KEEP IT.";
}

void platform_commit_save(PlatformSaveCallback callback, void *context)
{
    if (save_callback) {
        fprintf(stderr, "Browser save already pending.\n");
        callback(context, false, "BROWSER SAVE IS ALREADY PENDING.");
        return;
    }
    save_callback = callback;
    save_context = context;
    browser_sync_save();
}

void platform_finish_frame(App *app)
{
    browser_state((int)app->game.screen, app->audio.muted, app->focused, app->saving);
}

void platform_shutdown(void)
{
    web_app = NULL;
    save_callback = NULL;
    save_context = NULL;
}

EMSCRIPTEN_KEEPALIVE void web_focus(int focused)
{
    if (web_app) app_set_focus(web_app, focused != 0);
}

EMSCRIPTEN_KEEPALIVE void web_input(int input, int pressed)
{
    if (web_app && input >= UP && input <= OPEN_MENU)
        app_input(web_app, (Input)input, pressed != 0);
}

EMSCRIPTEN_KEEPALIVE void web_save_complete(int success)
{
    if (!save_callback) return;
    PlatformSaveCallback callback = save_callback;
    void *context = save_context;
    save_callback = NULL;
    save_context = NULL;
    callback(context, success != 0, success ? "JOURNEY SAVED IN THIS BROWSER." :
             "BROWSER SAVE FAILED.\nEXPORT YOUR JOURNEY TO KEEP IT.");
}

/* File exchange uses the same validated, versioned format as native saves. */
EMSCRIPTEN_KEEPALIVE int web_command(int command)
{
    App *app = web_app;
    if (!app || app->saving) return 0;
    if (command == 0) return app_save_or_load(app, false);
    if (command == 1) return app_save_or_load(app, true);
    if (command == 2) {
        app->audio.muted = !app->audio.muted;
        return 1;
    }
    Game *g = &app->game;
    if (command != 3 && command != 4) return 0;
    if (g->screen != EXPLORE && g->screen != MENU &&
        !(command == 3 && g->screen == TITLE)) return 0;
    char error[160];
    bool ok = command == 3 ? game_load(g, "/import.sav", error, sizeof(error)) :
                            game_save(g, "/export.sav", error, sizeof(error));
    if (!ok) {
        fprintf(stderr, "Save file exchange failed: %s\n", error);
        game_notify(g, error);
    } else if (command == 3) {
        game_notify(g, "JOURNEY IMPORTED.\nSAVE TO KEEP IT IN THIS BROWSER.");
    }
    return ok;
}
