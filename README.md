# Emberward: The Starless Coast

A complete, small NES-inspired role-playing game in C11 and SDL3. Explore a
tile-based coastline, meet the villagers of Brindle, lead a party of four through
turn-based encounters, and recover the stolen Star Lens from the Ash Wyrm.

This is an original adventure, not a port or reproduction of any game.
All names, maps, bitmap font, and synthesized music are original. The coastline,
terrain, characters, and creatures use original generated pixel-art assets in
`assets/`. No ROMs, SDL_image, or SDL_ttf are needed; SDL3 loads the included BMP
and PNG files directly.

## Build and play

Requires a C11 compiler, Make, `pkg-config`, and **SDL3 3.4.0 or newer**
(for built-in PNG loading).
The game links directly to SDL3; it does not use SDL2 or `sdl2-compat`.

macOS:

```sh
brew install sdl3 pkg-config
make
./emberward
```

Debian / Ubuntu:

```sh
sudo apt install build-essential pkg-config libsdl3-dev
make
./emberward
```

On older distributions without `libsdl3-dev`, install SDL3's development package
from [the official SDL releases](https://github.com/libsdl-org/SDL/releases).
Verify discovery with `pkg-config --modversion sdl3`.

The window starts at 3x the native 384x288 resolution. Resize it or toggle
fullscreen; rendering keeps its aspect ratio and uses nearest-neighbor integer
scaling.

## Build and play on the web

The same C sources build for browsers using SDL3's Emscripten backend.
Both native and browser targets use SDL's application callbacks; the browser
runs on animation frames, without Asyncify, pthreads, or special HTTP headers.
The native build links `platform_native.c`; the browser build links
`platform_web.c`. Both implement `platform.h`, so the shared application has no
platform preprocessor branches. Save completion uses the same callback contract:
native calls it synchronously after the file write; web calls it after the
IndexedDB flush (including immediate errors). The application marks saves pending
before invoking the platform and the web bridge cancels pending callbacks on shutdown.

Install [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
**6.0.13**, then activate it in your shell:

```sh
./emsdk install 6.0.13
./emsdk activate 6.0.13
source ./emsdk_env.sh
cd /path/to/emberward
make web
python3 -m http.server 8000 --bind 127.0.0.1 --directory web
```

Open <http://127.0.0.1:8000/dist/> and choose **Play**. Do not open the HTML
directly with `file://`. The build pins the compiler and its bundled **SDL3 3.4.2**
port; native SDL development libraries are not required for `make web`.
Node/npm is only needed for browser tests, not to host the game.
`web/dist/` contains the complete static site (about 12 MB uncompressed);
only runtime artwork is packaged, not the source art. All URLs are relative,
including on project sites such as `https://owner.github.io/repository/`.

The page provides Save, Load, Mute, Fullscreen, Export/Import, and touch/mouse
controls. Keyboard shortcuts work when the canvas has focus; Tab navigates the
page controls instead of opening camp (use C). Arrow/Space scrolling and F5
reload are prevented only in the focused game. Fullscreen is optional, with
an explicit error if the browser denies it. The game and sound pause on
window/tab focus loss and resume without catching up elapsed time or held input.
Browser rendering uses aspect-preserving fitted scaling so even low-DPI viewports
smaller than 384x288 show the whole scene; native rendering retains integer scaling.

### Browser saves

The browser restores an IndexedDB-backed IDBFS directory before enabling Play.
Save uses the native versioned format and atomic temporary-file/rename logic,
then flushes to IndexedDB. **Wait for "Journey saved in this browser" before
closing or reloading.** A write/restore failure is visible, never reported as a
successful durable save. A failed initial restore disables persistent saving
to avoid overwriting data that could not be read; playing and exporting still
work. Export is available while exploring/camping, even if persistence fails.

Browser saves are specific to this browser and origin; they are not cloud saves.
Private browsing, clearing site data, and storage eviction can erase them.
Keep an exported backup. Import validates and loads a `.sav` file (maximum 4 KiB)
without replacing the browser slot until you explicitly Save. Invalid files
leave the current journey intact. Native saves and browser exports are compatible.
There is no autosave or guaranteed unload-time write.

### GitHub Pages

In the repository's **Settings > Pages**, choose **GitHub Actions** as the source.
The [native/web workflow](.github/workflows/web.yml) tests the native build,
compiles with a pinned SDK, runs WASM game-rule and browser integration tests,
and deploys only `web/dist/` after successful pushes to `main` or manual runs
on `main`. Adjust the workflow's branch filters if your default branch differs.
Pull requests run the checks without deploying. A server/backend, custom
headers, and cross-origin isolation are not needed.

### Browser validation

Requires Node 24+ and Python 3 for the local test server:

```sh
make web
npm --prefix web ci
cd web && npx playwright install --with-deps chromium firefox webkit && cd ..
make web-test
```

Tests cover Chromium, Firefox, desktop WebKit, and an iPhone-sized WebKit
profile: 12-scene pixel-readback smoke tests, save/reload/load, file exchange,
malformed saves, blocked IndexedDB, delayed/failed writes and retry, held-input
release, focus pause, responsive layout, audio activation, and fullscreen errors.
`http://localhost:8000/dist/?smoke` also offers the visual smoke test.
Playwright's WebKit/iPhone profile is an approximation, not physical-device
certification. Before release, also check real Safari/iOS, Android Chrome, and
your deployed Pages URL (audio, touch, fullscreen availability, private-mode
storage, and reload persistence). The pinned SDL audio backend currently uses
ScriptProcessorNode, which may log a browser deprecation warning; no AudioWorklet
or SharedArrayBuffer is required.

Local validation (2026-10-28): native and WASM game rules and Chromium/desktop
WebKit/iPhone-sized WebKit integration tests pass. Firefox automation on this
Mac fails at browser launch ("Could not find profile folder"), before loading
the game; it remains enabled in the Linux CI suite. Physical iOS/Android and
the public Pages deployment still need release-device checks.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys / WASD | Walk or select |
| Z / Enter / Space | Talk, inspect, confirm |
| X / Escape / Backspace | Back / open camp |
| Tab / C | Open or close camp |
| F5 | Save while exploring or camping |
| F9 | Load from the title, exploration, or camp |
| M | Mute / unmute |
| F11 | Fullscreen |

Face someone or something, then confirm to interact. Stepping on a village,
vault entrance, or exit changes maps. Close the window or choose Quit on the
title screen to exit.

## Your journey

- Speak to the elder north of Brindle's well.
- The well fully restores **and revives** the party for free. Face it and press Z.
- The west apothecary sells a potion for 12 gold per interaction; the east
  herbalist sells an ether for 20 gold. Talk again to buy another.
- Potions heal 35 HP. In camp they automatically select the most injured living
  hero; in battle you choose a target. Camp ethers restore 12 MP to every living
  hero. Fallen heroes need the town well.
- Follow the road east from Brindle, then north to the Sunken Vault.
- The 56x42 coast is mostly grassland, with connected forest groves and mountain
  ranges. Side trails lead to the northwest woods, southern forest clearing,
  and eastern meadow; the village and vault remain on the main road.
- Train on the coast and open the vault's two treasure chests. Level 3 is a good
  target for the boss. The spring in the vault restores 16 HP to living heroes.
- Defeat the Ash Wyrm in the northeast chamber and return to the elder to finish.

### Combat

Choose an action for each living hero, then the round plays out in speed order.
Battle heroes use a consistent enlarged scale in a staggered two-by-two formation;
exploration sprites keep their smaller map size.
Acting heroes and enemies briefly hop before their action; affected targets then
flash and shake. Healing targets flash with a green plus instead of shaking.
Finishing blows remain visible through the hit flash.
Press X to revise a previous hero's choice before the round starts. Targets that
fall before an attack automatically redirect to a living foe; heals redirect to
the caster if their target falls.

| Hero | Role | Skill |
| --- | --- | --- |
| Alden | Guard | Cleave: physical damage to all enemies, 4 MP |
| Pip | Scout | Quickshot: strong single-target attack, 4 MP |
| Lyra | Mage | Ember: fire damage to all enemies, 5 MP |
| Sera | Mender | Mend: heal one living hero, 4 MP |

Living heroes each receive the full encounter XP reward. Level-ups improve
stats and restore the gained HP/MP capacity. Run attempts have a 70% success rate;
you cannot flee the boss. Hold nothing during round resolution; Z can advance
the next action sooner without skipping the action and target cues.

## Saves

Continue on the title screen loads the single save slot. Saves are written to
SDL's per-user application data directory, not the source folder:

- macOS: `~/Library/Application Support/Emberward/Emberward/journey.sav`
- Linux: typically `~/.local/share/Emberward/Emberward/journey.sav`

Override the location with `./emberward --save-file /path/to/journey.sav`.
Saving replaces the slot using a temporary file and rename. Invalid or
incompatible saves are rejected without changing the current game. Loading
intentionally replaces the current journey with the saved state. There is no
autosave; save before entering the vault.

## Verify

```sh
make test    # Game logic (25 seeded campaigns) and native application save tests
make smoke   # 120-frame headless SDL rendering/audio smoke test
```

Useful options:

```sh
./emberward --mute
./emberward --screenshot title.bmp
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
  ./emberward --smoke-test --screenshot battle.bmp
```

The screenshot option captures a BMP and exits. With `--smoke-test`, it captures
the final battle scene after exercising every screen. If audio is unavailable,
the game reports the error and remains playable without sound.

## Source layout

- `game.c` / `game.h`: deterministic game rules, maps, combat, and save/load.
- `render.c` / `render.h`: generated art atlases, font, and interfaces.
- `assets/`: environment tiles, party/enemy sprites, and title/battle coastline.
  Outdoor maps are layered: a seamless world-space grass base, then water,
  road, and forest overlays drawn as neighbor-aware quadrant autotiles
  (`terrain.png`), then mountains, landmarks, and villagers (`objects.png`).
  The town wall is a connected rampart autotile (`walls.png`), and houses,
  the gatehouse, and four corner-specific towers are whole-building sprites
  (`buildings.png`) depth-sorted with the player. All sprites share one
  upper-left light: baked drop shadows fall to the lower right, and wall and
  player shadows are drawn at runtime with the same offset and strength.
  Battles use a backdrop matching where the fight starts (`battle.png`):
  grass, forest, road, shoreline (grass next to water), or the vault.
- `tools/build_terrain.py`: bakes `grass.png`, `terrain.png`, `objects.png`,
  `walls.png`, `buildings.png`, and `battle.png`
  from the generated sheets in `assets/source/`. It is only needed when
  changing terrain art and requires Python with Pillow and numpy:
  `python3 tools/build_terrain.py`.
- `main.c`: shared SDL lifecycle, input, synthesized music/effects, and save coordination.
- `app_internal.h`: private application state and operations used by platform bridges.
- `platform.h`: build-selected platform contract, including save completion callbacks.
- `platform_native.c`: native window policy, save paths, synchronous save completion, pacing.
- `platform_web.c`: Emscripten bridge, browser window policy, asynchronous save completion.
- `tests.c`: maps, collisions, transitions, combat, campaign, and save tests.
- `tests_app.c`: synchronous native save callbacks, shared application save/load, and failures.
- `web/`: maintained web shell, responsive controls, browser storage, pinned build,
  and cross-browser integration tests. `web/dist/` is generated and ignored.
- `web-prototype/`: retained original feasibility snapshot; its build command
  now forwards to the maintained web build.

The game is a compact adventure rather than a full commercial RPG: one town,
one dungeon, six enemy types, four fixed classes, and a complete ending.

## Implementation Notes

This game prototype was 100% vibe-coded using **GitHub Copilot in VS Code**. I used **Copilot Fusion (Standard)** exclusively. All images were generated using the new image generation feature in GitHub Copilot.

Work was done over a handful of sessions, with the following AIC costs:

| Session Task | AIC Cost |
| --- | --- |
| Initial working game without images | 95.2 |
| Main session to generate and refine images | 693.5 |
| Expand and cleanup up the terrain map | 19.6 |
| Fix battle character sizing to be consistent | 23.1 |
| Fix archer bow rendering | 0.7 |
| Web port to Emscripten/WASM | 183.3 |
| Total | 1015.4 |
