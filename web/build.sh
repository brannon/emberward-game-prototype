#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
EMCC="${EMCC:-emcc}"
version=$("$EMCC" -dumpversion)
if [[ "$version" != "6.0.13" ]]; then
    echo "Expected Emscripten 6.0.13 (SDL3 3.4.2 port), found $version." >&2
    exit 1
fi

mkdir -p web/dist
preload=()
for asset in assets/tiles.bmp assets/characters.bmp assets/coast.bmp \
    assets/grass.png assets/terrain.png assets/objects.png assets/walls.png \
    assets/buildings.png assets/battle.png; do
    preload+=(--preload-file "$asset@/$asset")
done

"$EMCC" main.c game.c render.c platform_web.c -std=c11 -O2 \
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
    --use-port=sdl3 -lm -lidbfs.js \
    -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
    -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=2 \
    -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS,addRunDependency,removeRunDependency \
    "${preload[@]}" --shell-file web/shell.html -o web/dist/index.html
cp web/app.js web/style.css web/dist/
touch web/dist/.nojekyll
