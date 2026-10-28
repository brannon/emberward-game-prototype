#ifndef EMBERWARD_PLATFORM_H
#define EMBERWARD_PLATFORM_H

#include <SDL3/SDL.h>

typedef struct App App;
typedef void (*PlatformSaveCallback)(void *context, bool success, const char *message);

bool platform_set_save_path(App *app, const char *path);
void platform_init(App *app);
SDL_RendererLogicalPresentation platform_presentation(void);
void platform_configure_window(App *app);
void platform_toggle_fullscreen(App *app);
const char *platform_save_unavailable(void);
/* Complete exactly once, possibly before returning. Message is valid during the callback.
   Shutdown cancels outstanding completion before its context can be destroyed. */
void platform_commit_save(PlatformSaveCallback callback, void *context);
void platform_finish_frame(App *app);
void platform_shutdown(void);

#endif
