#define _POSIX_C_SOURCE 200809L
#include "app_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return EXIT_FAILURE; \
    } \
} while (0)

typedef struct {
    bool pending, success;
    int calls;
} Completion;

static void completed(void *context, bool success, const char *message)
{
    Completion *completion = context;
    if (!completion->pending || strcmp(message, "JOURNEY SAVED.") != 0) abort();
    completion->pending = false;
    completion->success = success;
    ++completion->calls;
}

int main(void)
{
    Completion completion = {.pending = true};
    platform_commit_save(completed, &completion);
    CHECK(completion.calls == 1 && completion.success && !completion.pending);

    char path[] = "/tmp/emberward-app-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(close(fd) == 0);
    App app = {0};
    CHECK(platform_set_save_path(&app, path));
    CHECK(app.path == app.save_path && strcmp(app.path, path) == 0);
    platform_init(&app);
    CHECK(strcmp(app.path, path) == 0);
    game_init(&app.game, 12345);
    game_new(&app.game);
    while (app.game.screen == DIALOG) game_input(&app.game, ACCEPT);

    CHECK(app_save_or_load(&app, false));
    CHECK(!app.saving);
    CHECK(app.game.screen == DIALOG && app.game.return_screen == EXPLORE);
    CHECK(strcmp(app.game.messages[0], "JOURNEY SAVED.") == 0);
    game_input(&app.game, ACCEPT);
    app.game.gold = 999;
    CHECK(app_save_or_load(&app, true));
    CHECK(app.game.gold == 60);
    CHECK(unlink(path) == 0);

    game_input(&app.game, ACCEPT);
    app.saving = true;
    CHECK(!app_save_or_load(&app, false));
    CHECK(!app_save_or_load(&app, true));
    CHECK(app.game.screen == EXPLORE);
    app.saving = false;
    CHECK(platform_set_save_path(&app, "/nonexistent/emberward/journey.sav"));
    CHECK(!app_save_or_load(&app, false));
    CHECK(!app.saving && app.game.screen == DIALOG);
    CHECK(strncmp(app.game.messages[0], "SAVE FAILED.", 12) == 0);
    platform_shutdown();
    puts("PASS: native save callback, synchronous application completion, load, and write failure.");
    return EXIT_SUCCESS;
}
