CC ?= cc
PKG_CONFIG ?= pkg-config
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags sdl3)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDLIBS += $(shell $(PKG_CONFIG) --libs sdl3) -lm

.PHONY: all run test smoke web web-test clean
all: emberward

ASSETS = assets/tiles.bmp assets/characters.bmp assets/coast.bmp assets/grass.png assets/terrain.png assets/objects.png assets/walls.png assets/buildings.png assets/battle.png

emberward: main.c game.c render.c platform_native.c game.h render.h app_internal.h platform.h $(ASSETS)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c game.c render.c platform_native.c -o $@ $(LDFLAGS) $(LDLIBS)

test-game: tests.c game.c game.h
	$(CC) $(CFLAGS) tests.c game.c -o $@ $(LDFLAGS)

test-app: tests_app.c main.c game.c render.c platform_native.c app_internal.h platform.h game.h render.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSDL_MAIN_NOIMPL tests_app.c main.c game.c render.c platform_native.c -o $@ $(LDFLAGS) $(LDLIBS)

test: test-game test-app
	./test-game
	./test-app

smoke: emberward
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./emberward --smoke-test

run: emberward
	./emberward

web:
	bash web/build.sh

web-test:
	npm --prefix web test

clean:
	rm -f emberward test-game test-app
