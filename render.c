#include "render.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char r, g, b; } Color;
typedef struct { char letter; unsigned char rows[7]; } Glyph;

static const Color ink = {12, 17, 35}, navy = {19, 30, 65};
static const Color white = {239, 239, 215}, dim = {141, 160, 177};
static const Color gold = {241, 195, 91}, teal = {86, 207, 176};
static const Color red = {231, 103, 105}, blue = {99, 164, 232};

enum {
    ATLAS_CELL_SIZE = 128,
    TERRAIN_TILE = 64,
    TERRAIN_QUADRANT = TERRAIN_TILE / 2,
    TERRAIN_PERIOD = 4,
    OBJECT_CELL = 80,
    OBJECT_SPRITE = 64,
    WALL_PERIOD = 2,
    BUILDING_CELL = 384,
    BUILDING_BASE = 8
};

/* Shared lighting (see tools/build_terrain.py): shadows fall to the lower right. */
static const float shadow_dx = 0.12f * 16.0f, shadow_dy = 0.1f * 16.0f;
static const Color shadow_color = {8, 16, 10};
static const Uint8 shadow_alpha = 150;

/* Must match tools/build_terrain.py. */
typedef enum { LAYER_WATER, LAYER_ROAD, LAYER_FOREST } TerrainLayer;
typedef enum { EDGE_NONE, EDGE_H, EDGE_V, EDGE_INNER, EDGE_ALL, EDGE_STATES } EdgeState;
typedef enum {
    OBJECT_MOUNTAIN_A, OBJECT_MOUNTAIN_B, OBJECT_VILLAGE, OBJECT_RUIN, OBJECT_FOUNTAIN,
    OBJECT_ELDER, OBJECT_APOTHECARY, OBJECT_HERBALIST, OBJECT_FISHER
} MapObject;
typedef enum {
    BUILDING_COTTAGE, BUILDING_TIMBER, BUILDING_APOTHECARY, BUILDING_INN,
    BUILDING_GATEHOUSE, BUILDING_TOWER_NW, BUILDING_TOWER_NE, BUILDING_TOWER_SW,
    BUILDING_TOWER_SE
} Building;

static SDL_Texture *tile_atlas;
static SDL_Texture *character_atlas;
static SDL_Texture *character_flash_atlas;
static SDL_Texture *coast_background;
static SDL_Texture *battle_atlas;
static SDL_Texture *grass_texture;
static SDL_Texture *terrain_atlas;
static SDL_Texture *object_atlas;
static SDL_Texture *wall_atlas;
static SDL_Texture *building_atlas;

static bool near_magenta(Uint32 pixel, const SDL_PixelFormatDetails *format,
                         SDL_Palette *palette)
{
    Uint8 pixel_red, pixel_green, pixel_blue, alpha;
    SDL_GetRGBA(pixel, format, palette, &pixel_red, &pixel_green, &pixel_blue, &alpha);
    return pixel_red >= 200 && pixel_green <= 60 && pixel_blue >= 200;
}

static bool clear_magenta(SDL_Surface *surface, bool white_mask)
{
    if (!SDL_LockSurface(surface)) return false;
    const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(surface->format);
    if (!format) {
        SDL_UnlockSurface(surface);
        return false;
    }
    SDL_Palette *palette = SDL_GetSurfacePalette(surface);
    for (int y = 0; y < surface->h; ++y) {
        Uint8 *row = (Uint8 *)surface->pixels + (size_t)y * (size_t)surface->pitch;
        for (int x = 0; x < surface->w; ++x) {
            Uint32 pixel;
            memcpy(&pixel, row + (size_t)x * sizeof(pixel), sizeof(pixel));
            Uint8 pixel_red, pixel_green, pixel_blue, alpha;
            SDL_GetRGBA(pixel, format, palette,
                        &pixel_red, &pixel_green, &pixel_blue, &alpha);
            bool transparent = near_magenta(pixel, format, palette);
            if (white_mask && !transparent)
                pixel_red = pixel_green = pixel_blue = 255;
            alpha = transparent ? 0 : 255;
            pixel = SDL_MapRGBA(format, palette,
                                pixel_red, pixel_green, pixel_blue, alpha);
            memcpy(row + (size_t)x * sizeof(pixel), &pixel, sizeof(pixel));
        }
    }
    SDL_UnlockSurface(surface);
    return true;
}

static SDL_Texture *load_texture(SDL_Renderer *renderer, const char *filename,
                                 bool magenta_key, bool white_mask)
{
    const char *base = SDL_GetBasePath();
    if (!base) return NULL;

    char path[1024];
    int length = snprintf(path, sizeof(path), "%sassets/%s", base, filename);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        SDL_SetError("Asset path is too long");
        return NULL;
    }

    size_t name_length = strlen(filename);
    bool png = name_length > 4 && strcmp(filename + name_length - 4, ".png") == 0;
    SDL_Surface *surface = png ? SDL_LoadPNG(path) : SDL_LoadBMP(path);
    if (!surface) return NULL;
    if (magenta_key) {
        SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (!rgba) return NULL;
        if (!clear_magenta(rgba, white_mask)) {
            SDL_DestroySurface(rgba);
            return NULL;
        }
        surface = rgba;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) return NULL;
    if ((magenta_key || png) && !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

bool render_init(SDL_Renderer *renderer)
{
    tile_atlas = load_texture(renderer, "tiles.bmp", false, false);
    character_atlas = load_texture(renderer, "characters.bmp", true, false);
    character_flash_atlas = load_texture(renderer, "characters.bmp", true, true);
    coast_background = load_texture(renderer, "coast.bmp", false, false);
    grass_texture = load_texture(renderer, "grass.png", false, false);
    terrain_atlas = load_texture(renderer, "terrain.png", false, false);
    object_atlas = load_texture(renderer, "objects.png", false, false);
    wall_atlas = load_texture(renderer, "walls.png", false, false);
    building_atlas = load_texture(renderer, "buildings.png", false, false);
    battle_atlas = load_texture(renderer, "battle.png", false, false);
    if (!tile_atlas || !character_atlas || !character_flash_atlas || !coast_background ||
        !grass_texture || !terrain_atlas || !object_atlas || !wall_atlas ||
        !building_atlas || !battle_atlas) {
        render_shutdown();
        return false;
    }
    return true;
}

void render_shutdown(void)
{
    if (tile_atlas) SDL_DestroyTexture(tile_atlas);
    if (character_atlas) SDL_DestroyTexture(character_atlas);
    if (character_flash_atlas) SDL_DestroyTexture(character_flash_atlas);
    if (coast_background) SDL_DestroyTexture(coast_background);
    if (grass_texture) SDL_DestroyTexture(grass_texture);
    if (terrain_atlas) SDL_DestroyTexture(terrain_atlas);
    if (object_atlas) SDL_DestroyTexture(object_atlas);
    if (wall_atlas) SDL_DestroyTexture(wall_atlas);
    if (building_atlas) SDL_DestroyTexture(building_atlas);
    if (battle_atlas) SDL_DestroyTexture(battle_atlas);
    battle_atlas = NULL;
    wall_atlas = NULL;
    building_atlas = NULL;
    grass_texture = NULL;
    terrain_atlas = NULL;
    object_atlas = NULL;
    tile_atlas = NULL;
    character_atlas = NULL;
    character_flash_atlas = NULL;
    coast_background = NULL;
}

static void draw_atlas_cell(SDL_Renderer *renderer, SDL_Texture *atlas, int cell,
                            int x, int y, int width, int height)
{
    SDL_FRect source = {
        (float)((cell % 4) * ATLAS_CELL_SIZE),
        (float)((cell / 4) * ATLAS_CELL_SIZE),
        (float)ATLAS_CELL_SIZE,
        (float)ATLAS_CELL_SIZE
    };
    SDL_FRect destination = {(float)x, (float)y, (float)width, (float)height};
    SDL_RenderTexture(renderer, atlas, &source, &destination);
}

static void draw_coast(SDL_Renderer *renderer, int x, int y, int width, int height,
                       int source_y, int source_height)
{
    SDL_FRect source = {85.0f, (float)source_y, 683.0f, (float)source_height};
    SDL_FRect destination = {(float)x, (float)y, (float)width, (float)height};
    SDL_RenderTexture(renderer, coast_background, &source, &destination);
}

static const Glyph font[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'.',{0,0,0,0,0,12,12}}, {',',{0,0,0,0,4,4,8}},
    {':',{0,12,12,0,12,12,0}}, {'!',{4,4,4,4,4,0,4}},
    {'?',{14,17,1,2,4,0,4}}, {'-',{0,0,0,31,0,0,0}},
    {'/',{1,1,2,4,8,16,16}}, {'\'',{4,4,8,0,0,0,0}},
    {'>',{16,8,4,2,4,8,16}}, {'+',{0,4,4,31,4,4,0}},
    {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
    {'=',{0,31,0,31,0,0,0}}, {'%',{17,2,4,8,17,0,0}}
};

static void rect(SDL_Renderer *r, int x, int y, int w, int h, Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_FRect box = {(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(r, &box);
}

static void outline(SDL_Renderer *r, int x, int y, int w, int h, Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_FRect box = {(float)x, (float)y, (float)w, (float)h};
    SDL_RenderRect(r, &box);
}

static void panel(SDL_Renderer *r, int x, int y, int w, int h)
{
    rect(r, x + 2, y + 3, w, h, ink);
    rect(r, x, y, w, h, navy);
    outline(r, x, y, w, h, dim);
    outline(r, x + 2, y + 2, w - 4, h - 4, (Color){47, 66, 104});
    rect(r, x, y, 3, 3, white);
    rect(r, x + w - 3, y, 3, 3, white);
    rect(r, x, y + h - 3, 3, 3, white);
    rect(r, x + w - 3, y + h - 3, 3, 3, white);
}

static void text(SDL_Renderer *r, int x, int y, const char *s, Color c, int scale)
{
    int start = x;
    for (; *s; ++s) {
        if (*s == '\n') { y += 10 * scale; x = start; continue; }
        char ch = (char)toupper((unsigned char)*s);
        for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) {
            if (font[i].letter != ch) continue;
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (font[i].rows[row] & (1u << (unsigned)(4 - col)))
                        rect(r, x + col * scale, y + row * scale, scale, scale, c);
            break;
        }
        x += 6 * scale;
    }
}

static void centered(SDL_Renderer *r, int y, const char *s, Color c, int scale)
{
    text(r, (SCREEN_W - (int)strlen(s) * 6 * scale) / 2, y, s, c, scale);
}

static void wrapped(SDL_Renderer *r, int x, int y, const char *s, int columns, Color color)
{
    char line[100];
    int used = 0;
    while (*s) {
        if (*s == '\n') {
            line[used] = '\0'; text(r, x, y, line, color, 1);
            used = 0; y += 11; ++s;
            continue;
        }
        if (used == 0 && *s == ' ') { ++s; continue; }
        int word = 0;
        while (s[word] && s[word] != ' ' && s[word] != '\n') ++word;
        if (used && word && used + word > columns) {
            line[used] = '\0'; text(r, x, y, line, color, 1);
            used = 0; y += 11;
            continue;
        }
        line[used++] = *s++;
        if (used >= columns) {
            line[used] = '\0'; text(r, x, y, line, color, 1);
            used = 0; y += 11;
        }
    }
    if (used) { line[used] = '\0'; text(r, x, y, line, color, 1); }
}

static void bar(SDL_Renderer *r, int x, int y, int w, int value, int maximum, Color c)
{
    rect(r, x, y, w, 3, ink);
    if (value > 0 && maximum > 0) rect(r, x, y, (w * value + maximum - 1) / maximum, 3, c);
}

static void pointer(SDL_Renderer *r, int x, int y)
{
    for (int i = 0; i < 4; ++i) rect(r, x + i, y + i, 1, 7 - i * 2, gold);
}

/* Soft elliptical shadow on the ground, shifted to match the map lighting. */
static void ground_shadow(SDL_Renderer *r, float cx, float cy, float width, float height)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, shadow_color.r, shadow_color.g, shadow_color.b,
                           (Uint8)(shadow_alpha * 3 / 4));
    int rows = (int)height;
    for (int i = 0; i < rows; ++i) {
        float t = ((float)i + 0.5f) / (float)rows * 2.0f - 1.0f;
        float half = width * 0.5f * sqrtf(1.0f - t * t);
        SDL_FRect row = {cx + shadow_dx * width / 12.0f - half,
                         cy - height * 0.5f + (float)i, half * 2.0f, 1.0f};
        SDL_RenderFillRect(r, &row);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Downward-pointing selection arrow with a dark outline; (cx, y) is its top centre. */
static void down_arrow(SDL_Renderer *r, int cx, int y)
{
    SDL_Rect parts[7] = {{cx - 1, y, 3, 4}};
    for (int i = 0; i < 6; ++i)
        parts[i + 1] = (SDL_Rect){cx - 5 + i, y + 4 + i, 11 - i * 2, 1};
    for (int i = 0; i < 7; ++i)
        rect(r, parts[i].x - 1, parts[i].y - 1, parts[i].w + 2, parts[i].h + 2, ink);
    for (int i = 0; i < 7; ++i) rect(r, parts[i].x, parts[i].y, parts[i].w, parts[i].h, gold);
    rect(r, cx - 1, y, 1, 4, white);
    rect(r, cx - 4, y + 5, 4, 1, white);
}

static void hero_sprite(SDL_Renderer *r, int x, int y, int hero, int facing, int step, int scale,
                        bool flash)
{
    static const int crop[PARTY_SIZE][4] = {
        {12, 8, 106, 120}, {10, 8, 108, 120},
        {8, 8, 112, 120}, {12, 10, 105, 118}
    };
    int index = hero % PARTY_SIZE;
    SDL_FRect source = {
        (float)(index * ATLAS_CELL_SIZE + crop[index][0]),
        (float)crop[index][1],
        (float)crop[index][2],
        (float)crop[index][3]
    };
    SDL_FRect destination = {(float)x, (float)(y - (step ? scale : 0)),
                             (float)(12 * scale), (float)(16 * scale)};
    /* The hero art faces right; mirror it for left-facing poses. */
    SDL_RenderTextureRotated(r, flash ? character_flash_atlas : character_atlas,
                             &source, &destination, 0.0, NULL,
                             facing == LEFT ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void tile(SDL_Renderer *r, Tile t, int x, int y)
{
    draw_atlas_cell(r, tile_atlas, (int)t, x, y, 16, 16);
}

static Tile map_tile(const Map *m, int x, int y)
{
    return m->tiles[y][x];
}

static bool road_base(Tile t)
{
    return t == TILE_ROAD || t == TILE_EXIT || t == TILE_TOWN || t == TILE_RUINS;
}

static bool road_tile(const Map *m, int x, int y)
{
    Tile t = map_tile(m, x, y);
    if (road_base(t)) return true;
    if (t != TILE_FOUNTAIN && t != TILE_NPC) return false;
    static const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int links = 0;
    for (int i = 0; i < 4; ++i) {
        int nx = x + offsets[i][0], ny = y + offsets[i][1];
        if (nx >= 0 && ny >= 0 && nx < m->width && ny < m->height &&
            road_base(map_tile(m, nx, ny)))
            ++links;
    }
    return links >= 2;
}

static bool terrain_at(const Map *m, int x, int y, TerrainLayer layer)
{
    if (x < 0 || y < 0 || x >= m->width || y >= m->height) return true;
    Tile t = map_tile(m, x, y);
    switch (layer) {
    case LAYER_WATER: return t == TILE_WATER;
    case LAYER_FOREST: return t == TILE_FOREST;
    case LAYER_ROAD: return road_tile(m, x, y);
    }
    return false;
}

static void draw_grass(SDL_Renderer *r, int mx, int my, int x, int y)
{
    SDL_FRect source = {
        (float)((mx % TERRAIN_PERIOD) * TERRAIN_TILE),
        (float)((my % TERRAIN_PERIOD) * TERRAIN_TILE),
        (float)TERRAIN_TILE, (float)TERRAIN_TILE
    };
    SDL_FRect destination = {(float)x, (float)y, 16.0f, 16.0f};
    SDL_RenderTexture(r, grass_texture, &source, &destination);
}

/* Each tile is four quadrants whose edge shape depends on the horizontal,
   vertical and diagonal neighbours toward that quadrant's corner. */
static void draw_terrain_layer(SDL_Renderer *r, const Map *m, TerrainLayer layer,
                               int mx, int my, int x, int y)
{
    if (!terrain_at(m, mx, my, layer)) return;
    int row = (int)layer * TERRAIN_PERIOD * TERRAIN_PERIOD +
              (my % TERRAIN_PERIOD) * TERRAIN_PERIOD + mx % TERRAIN_PERIOD;
    for (int q = 0; q < 4; ++q) {
        int dx = q % 2 ? 1 : -1, dy = q / 2 ? 1 : -1;
        bool h = terrain_at(m, mx + dx, my, layer);
        bool v = terrain_at(m, mx, my + dy, layer);
        bool d = terrain_at(m, mx + dx, my + dy, layer);
        int state = h && v ? (d ? EDGE_ALL : EDGE_INNER) :
                    h ? EDGE_H : v ? EDGE_V : EDGE_NONE;
        SDL_FRect source = {
            (float)((q * EDGE_STATES + state) * TERRAIN_QUADRANT),
            (float)(row * TERRAIN_QUADRANT),
            (float)TERRAIN_QUADRANT, (float)TERRAIN_QUADRANT
        };
        SDL_FRect destination = {(float)(x + (q % 2) * 8), (float)(y + (q / 2) * 8),
                                 8.0f, 8.0f};
        SDL_RenderTexture(r, terrain_atlas, &source, &destination);
    }
}

/* Draws an object `size` px square, bottom-centred on the 16px tile at x,y.
   Sizes must match OBJECT_DRAW in tools/build_terrain.py. */
static void draw_object(SDL_Renderer *r, MapObject object, float x, float y, int size)
{
    SDL_FRect source = {
        (float)(((int)object % 3) * OBJECT_CELL),
        (float)(((int)object / 3) * OBJECT_CELL),
        (float)OBJECT_CELL, (float)OBJECT_CELL
    };
    float scale = (float)size / (float)OBJECT_SPRITE;
    SDL_FRect destination = {
        x + 8.0f - (4.0f + (float)OBJECT_SPRITE / 2.0f) * scale,
        y + 16.0f - (float)(OBJECT_CELL - BUILDING_BASE) * scale,
        (float)OBJECT_CELL * scale, (float)OBJECT_CELL * scale
    };
    SDL_RenderTexture(r, object_atlas, &source, &destination);
}

static void draw_map_object(SDL_Renderer *r, const Map *m, int mx, int my, int x, int y)
{
    switch (map_tile(m, mx, my)) {
    case TILE_MOUNTAIN: {
        unsigned n = (unsigned)(mx * 73 + my * 151 + mx * my * 7);
        draw_object(r, n % 3 == 0 ? OBJECT_MOUNTAIN_B : OBJECT_MOUNTAIN_A,
                    (float)x, (float)(y + 1), 20);
        break;
    }
    case TILE_TOWN: draw_object(r, OBJECT_VILLAGE, (float)x, (float)(y + 1), 22); break;
    case TILE_RUINS: draw_object(r, OBJECT_RUIN, (float)x, (float)(y + 1), 20); break;
    case TILE_FOUNTAIN: {
        /* One well covers each fountain block; draw it from the block's
           bottom-left tile, centred on the block. */
        if ((mx > 0 && map_tile(m, mx - 1, my) == TILE_FOUNTAIN) ||
            (my + 1 < m->height && map_tile(m, mx, my + 1) == TILE_FOUNTAIN))
            break;
        int width = 0, height = 0;
        while (mx + width < m->width && map_tile(m, mx + width, my) == TILE_FOUNTAIN) ++width;
        while (my - height >= 0 && map_tile(m, mx, my - height) == TILE_FOUNTAIN) ++height;
        draw_object(r, OBJECT_FOUNTAIN, (float)x + (float)(width - 1) * 8.0f, (float)y,
                    16 * (width < height ? width : height));
        break;
    }
    case TILE_NPC: {
        /* Mirrors the speaker roles in game.c's town dialogue. */
        MapObject npc = my == 4 ? OBJECT_ELDER : mx == 5 ? OBJECT_APOTHECARY :
                        mx == 18 ? OBJECT_HERBALIST : OBJECT_FISHER;
        draw_object(r, npc, (float)x, (float)(y - 1), 16);
        break;
    }
    default: break;
    }
}

static bool wall_at(const Map *m, int x, int y)
{
    return x >= 0 && y >= 0 && x < m->width && y < m->height &&
           map_tile(m, x, y) == TILE_WALL;
}

static void draw_wall(SDL_Renderer *r, const Map *m, int mx, int my, float x, float y)
{
    int mask = (wall_at(m, mx, my - 1) ? 1 : 0) | (wall_at(m, mx + 1, my) ? 2 : 0) |
               (wall_at(m, mx, my + 1) ? 4 : 0) | (wall_at(m, mx - 1, my) ? 8 : 0);
    SDL_FRect source = {
        (float)(mask * TERRAIN_TILE),
        (float)(((my % WALL_PERIOD) * WALL_PERIOD + mx % WALL_PERIOD) * TERRAIN_TILE),
        (float)TERRAIN_TILE, (float)TERRAIN_TILE
    };
    SDL_FRect destination = {x, y, 16.0f, 16.0f};
    SDL_RenderTexture(r, wall_atlas, &source, &destination);
}

/* Building cells are 96x96 logical pixels, anchored at their bottom centre. */
static void draw_building(SDL_Renderer *r, Building building, int center_x, int bottom_y)
{
    SDL_FRect source = {
        (float)(((int)building % 3) * BUILDING_CELL),
        (float)(((int)building / 3) * BUILDING_CELL),
        (float)BUILDING_CELL, (float)BUILDING_CELL
    };
    float size = (float)BUILDING_CELL / 4.0f;
    SDL_FRect destination = {(float)center_x - size / 2.0f,
                             (float)bottom_y + (float)BUILDING_BASE / 4.0f - size, size, size};
    SDL_RenderTexture(r, building_atlas, &source, &destination);
}

static void draw_town_structure(SDL_Renderer *r, const Map *m, int mx, int my, int x, int y)
{
    Tile t = map_tile(m, mx, my);
    bool left_same = mx > 0 && map_tile(m, mx - 1, my) == t;
    bool below_same = my + 1 < m->height && map_tile(m, mx, my + 1) == t;
    if (t == TILE_ROOF && !left_same && !below_same) {
        int width = 0, height = 0;
        while (mx + width < m->width && map_tile(m, mx + width, my) == TILE_ROOF) ++width;
        while (my - height >= 0 && map_tile(m, mx, my - height) == TILE_ROOF) ++height;
        int top = my - height + 1;
        Building house = top < m->height / 2 ?
            (mx < m->width / 2 ? BUILDING_APOTHECARY : BUILDING_TIMBER) :
            (mx < m->width / 2 ? BUILDING_INN : BUILDING_COTTAGE);
        draw_building(r, house, x + width * 8, y + 16);
    } else if (t == TILE_EXIT && !left_same) {
        int width = 0;
        while (mx + width < m->width && map_tile(m, mx + width, my) == TILE_EXIT) ++width;
        draw_building(r, BUILDING_GATEHOUSE, x + width * 8, y + 16);
    } else if (t == TILE_WALL) {
        /* Corner towers; north towers show their doors on the town side. */
        bool n = wall_at(m, mx, my - 1), s = wall_at(m, mx, my + 1);
        bool e = wall_at(m, mx + 1, my), w = wall_at(m, mx - 1, my);
        if ((n || s) && (e || w) && !(n && s) && !(e && w)) {
            Building tower = s ? (e ? BUILDING_TOWER_NW : BUILDING_TOWER_NE) :
                                 (e ? BUILDING_TOWER_SW : BUILDING_TOWER_SE);
            draw_building(r, tower, x + 8, y + 16);
        }
    }
}

static void draw_player(SDL_Renderer *r, const Game *g, int cx, int cy)
{
    int px = (g->x - cx) * 16, py = (g->y - cy) * 16 + 24;
    /* Footprint shadow matching the villagers'. */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, shadow_color.r, shadow_color.g, shadow_color.b, shadow_alpha);
    SDL_FRect feet[] = {
        {(float)px + 4.0f + shadow_dx, (float)py + 13.5f, 8.0f, 2.0f},
        {(float)px + 5.5f + shadow_dx, (float)py + 13.0f, 5.0f, 0.5f},
        {(float)px + 5.5f + shadow_dx, (float)py + 15.5f, 5.0f, 0.5f}
    };
    SDL_RenderFillRects(r, feet, 3);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    int step = g->move_cooldown > 0.02f ? (g->steps % 2 ? 1 : -1) : 0;
    hero_sprite(r, px + 2, py - 1, 0, g->facing, step, 1, false);
}

static void draw_map(SDL_Renderer *r, const Game *g, int cx, int cy)
{
    const Map *m = &g->map;
    if (m->area == RUINS) {
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 24; ++x)
                tile(r, map_tile(m, x + cx, y + cy), x * 16, y * 16 + 24);
        draw_player(r, g, cx, cy);
        return;
    }
    for (int y = 0; y < 14; ++y)
        for (int x = 0; x < 24; ++x)
            draw_grass(r, x + cx, y + cy, x * 16, y * 16 + 24);
    static const TerrainLayer layers[] = {LAYER_WATER, LAYER_ROAD, LAYER_FOREST};
    for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i)
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 24; ++x)
                draw_terrain_layer(r, m, layers[i], x + cx, y + cy, x * 16, y * 16 + 24);
    /* Wall shadows: each tile's silhouette, darkened and offset. Wall tiles
       never overlap, so neither do their shadows. */
    SDL_SetTextureColorMod(wall_atlas, shadow_color.r, shadow_color.g, shadow_color.b);
    SDL_SetTextureAlphaMod(wall_atlas, shadow_alpha);
    for (int y = -1; y < 14; ++y)
        for (int x = -1; x < 24; ++x)
            if (wall_at(m, x + cx, y + cy))
                draw_wall(r, m, x + cx, y + cy, (float)(x * 16) + shadow_dx,
                          (float)(y * 16 + 24) + shadow_dy);
    SDL_SetTextureColorMod(wall_atlas, 255, 255, 255);
    SDL_SetTextureAlphaMod(wall_atlas, 255);
    for (int y = 0; y < 14; ++y)
        for (int x = 0; x < 24; ++x)
            if (map_tile(m, x + cx, y + cy) == TILE_WALL)
                draw_wall(r, m, x + cx, y + cy, (float)(x * 16), (float)(y * 16 + 24));
    /* Sprites taller than a tile are drawn bottom-up by row so nearer rows
       overlap farther ones, including the player. Wide buildings are found
       from their bottom-left tile, which may lie left of the view. */
    int last_row = cy + 18 < m->height ? cy + 18 : m->height;
    int first_col = cx - 6 > 0 ? cx - 6 : 0;
    int last_col = cx + 25 < m->width ? cx + 25 : m->width;
    for (int my = cy; my < last_row; ++my) {
        for (int mx = first_col; mx < last_col; ++mx) {
            int x = (mx - cx) * 16, y = (my - cy) * 16 + 24;
            if (mx >= cx && mx < cx + 24) draw_map_object(r, m, mx, my, x, y);
            if (m->area == TOWN) draw_town_structure(r, m, mx, my, x, y);
        }
        if (my == g->y) draw_player(r, g, cx, cy);
    }
}

static void map_camera(const Game *g, int *cx, int *cy)
{
    *cx = g->x - 12;
    *cy = g->y - 7;
    if (*cx > g->map.width - 24) *cx = g->map.width - 24;
    if (*cy > g->map.height - 14) *cy = g->map.height - 14;
    if (*cx < 0) *cx = 0;
    if (*cy < 0) *cy = 0;
}

static void draw_world(SDL_Renderer *r, const Game *g)
{
    int cx, cy;
    map_camera(g, &cx, &cy);
    draw_map(r, g, cx, cy);
    rect(r, 0, 0, SCREEN_W, 24, ink);
    text(r, 8, 8, g->map.area == WORLD ? "THE BRINDLE COAST" :
         g->map.area == TOWN ? "BRINDLE VILLAGE" : "THE SUNKEN VAULT", gold, 1);
    text(r, 240, 8, "Z TALK   X CAMP", dim, 1);
    if (g->map.area == WORLD) {
        const char *label = NULL;
        if (abs(g->x - 9) < 4 && abs(g->y - 18) < 3) label = "BRINDLE";
        if (abs(g->x - 30) < 4 && abs(g->y - 8) < 3) label = "SUNKEN VAULT";
        if (label) {
            int w = (int)strlen(label) * 6 + 12;
            panel(r, (SCREEN_W - w) / 2, 227, w, 17);
            text(r, (SCREEN_W - w) / 2 + 6, 232, label, gold, 1);
        }
    }
    panel(r, 0, 248, SCREEN_W, 40);
    for (int i = 0; i < PARTY_SIZE; ++i) {
        const Hero *h = &g->heroes[i];
        int x = 8 + i * 96;
        char buf[32];
        text(r, x, 255, h->name, h->hp == 0 ? red : white, 1);
        snprintf(buf, sizeof(buf), "%d/%d", h->hp, h->max_hp);
        text(r, x, 266, buf, h->hp <= h->max_hp / 4 ? red : teal, 1);
        bar(r, x, 278, 78, h->hp, h->max_hp, teal);
        if (i < 3) rect(r, x + 84, 254, 1, 28, (Color){47, 66, 104});
    }
}

static void draw_menu(SDL_Renderer *r, const Game *g)
{
    panel(r, 8, 10, 368, 269);
    text(r, 20, 21, "CAMP / THE EMBERWARD", gold, 1);
    char buf[80];
    snprintf(buf, sizeof(buf), "%d GOLD", g->gold);
    text(r, 262, 21, buf, gold, 1);
    for (int i = 0; i < PARTY_SIZE; ++i) {
        const Hero *h = &g->heroes[i];
        int y = 43 + i * 44;
        hero_sprite(r, 22, y + 5, i, DOWN, 0, 1, false);
        snprintf(buf, sizeof(buf), "%s LV%d %s", h->name, h->level, h->job);
        text(r, 44, y, buf, h->hp ? white : red, 1);
        snprintf(buf, sizeof(buf), "HP %d/%d  MP %d/%d", h->hp, h->max_hp, h->mp, h->max_mp);
        text(r, 44, y + 11, buf, dim, 1);
        bar(r, 44, y + 22, 72, h->hp, h->max_hp, teal);
        bar(r, 123, y + 22, 58, h->mp, h->max_mp, blue);
        snprintf(buf, sizeof(buf), "ATK %d DEF %d XP %d", h->attack, h->defense, h->xp);
        text(r, 44, y + 29, buf, dim, 1);
    }
    rect(r, 226, 41, 1, 174, (Color){47, 66, 104});
    snprintf(buf, sizeof(buf), "POTION  X%d", g->potions);
    text(r, 247, 49, buf, g->menu_cursor == 0 ? gold : white, 1);
    snprintf(buf, sizeof(buf), "ETHER   X%d", g->ethers);
    text(r, 247, 74, buf, g->menu_cursor == 1 ? gold : white, 1);
    text(r, 247, 99, "QUEST LOG", g->menu_cursor == 2 ? gold : white, 1);
    text(r, 247, 124, "BREAK CAMP", g->menu_cursor == 3 ? gold : white, 1);
    pointer(r, 236, 49 + g->menu_cursor * 25);
    text(r, 240, 153, "POTION: +35 HP\nETHER: +12 MP\nFOR THE PARTY", dim, 1);
    text(r, 240, 193, "F5 SAVE\nF9 LOAD", teal, 1);
    rect(r, 20, 226, 342, 1, (Color){47, 66, 104});
    wrapped(r, 20, 236, g->boss_defeated ?
            "THE STAR LENS IS SAFE. RETURN TO BRINDLE'S ELDER." :
            "A LIGHT LOST AT SEA. SEEK THE STAR LENS IN THE SUNKEN VAULT, NORTHEAST OF BRINDLE.", 55, gold);
    text(r, 20, 265, "Z SELECT / X BACK", dim, 1);
}

static void monster(SDL_Renderer *r, int kind, int x, int y, int scale, bool flash)
{
    SDL_Texture *atlas = flash ? character_flash_atlas : character_atlas;
    draw_atlas_cell(r, atlas, 4 + kind, x, y, 16 * scale, 16 * scale);
}

static void battle_background(SDL_Renderer *r, const Game *g)
{
    /* battle.png stacks one 3x-resolution 384x192 backdrop per BattleScene. */
    SDL_FRect source = {0.0f, (float)((int)g->battle.scene * 576), 1152.0f, 576.0f};
    SDL_FRect destination = {0.0f, 0.0f, (float)SCREEN_W, 192.0f};
    SDL_RenderTexture(r, battle_atlas, &source, &destination);
}

static void draw_battle(SDL_Renderer *r, const Game *g)
{
    const Battle *b = &g->battle;
    int hop = b->windup > 0 ? (int)(sinf(b->windup / 0.18f * 3.14159265f) * 8) : 0;
    bool pulse = b->flash > 0 && (int)(b->flash * 30) % 2 != 0;
    battle_background(r, g);
    for (int i = 0; i < b->count; ++i) {
        const Enemy *e = &b->enemies[i];
        bool hit = b->flash > 0 && (b->flash_targets & (1u << i)) != 0;
        if (e->hp <= 0 && !hit) continue;
        int scale = b->boss ? 5 : 3;
        int x = b->boss ? 86 : 20 + i * 78;
        int bob = e->kind == 2 || e->kind == 4 ? (int)(sinf(g->time * 3 + (float)i) * 3) : 0;
        int y = b->boss ? 75 : 106 + (i % 2 ? -14 : 4) + bob;
        ground_shadow(r, (float)(x + 8 * scale), (float)(y + 16 * scale - bob - 2),
                      (float)(12 * scale), (float)(3 * scale));
        if (b->windup > 0 && b->acting == PARTY_SIZE + i) {
            y -= hop;
            down_arrow(r, x + scale * 8, y - 14);
        }
        if (hit) x += pulse ? 2 : -2;
        monster(r, e->kind, x, y, scale, hit && pulse);
        if (b->phase == CHOOSE_TARGET) {
            Action a = b->actions[b->actor];
            bool ally = a.command == POTION || (a.command == SKILL && b->actor == 3);
            if (!ally && b->target == i) {
                int hover = (int)(g->time * 4) % 2;
                down_arrow(r, x + scale * 8, y - 14 + hover);
                int label_y = y + scale * 16 + (b->boss ? 12 : 5);
                for (int o = 0; o < 4; ++o)
                    text(r, x - 2 + (o == 0) - (o == 1), label_y + (o == 2) - (o == 3),
                         enemy_name(e->kind), ink, 1);
                text(r, x - 2, label_y, enemy_name(e->kind), gold, 1);
            }
        }
        if (b->boss) bar(r, x, y + 85, 80, e->hp, e->max_hp, red);
    }
    for (int i = 0; i < PARTY_SIZE; ++i) {
        const int scale = 3, width = 12 * scale, height = 16 * scale;
        int x = 266 + (i % 2) * 62;
        int y = 60 + (i / 2) * 62 + (i % 2) * 12;
        bool hit = b->flash > 0 && (b->flash_targets & (1u << (PARTY_SIZE + i))) != 0;
        if (hit && !b->healing) x += pulse ? 2 : -2;
        if (g->heroes[i].hp > 0 || hit) {
            ground_shadow(r, (float)(x + width / 2), (float)(y + height - scale),
                          (float)(11 * scale), (float)(3 * scale));
            if (b->windup > 0 && b->acting == i) {
                y -= hop;
                pointer(r, x - 9, y + (height - 7) / 2);
            }
            hero_sprite(r, x, y, i, LEFT, 0, scale, hit && pulse);
            if (hit && b->healing)
                text(r, x + width + 6, y + (height - 7) / 2, "+", teal, 1);
        }
        else {
            rect(r, x, y + height - 4 * scale, width, 4 * scale, dim);
            text(r, x + (width - 5) / 2, y + (height - 7) / 2, "X", red, 1);
        }
        if ((b->phase == CHOOSE_COMMAND || b->phase == CHOOSE_TARGET) && b->actor == i)
            pointer(r, x - 9, y + (height - 7) / 2);
        if (b->phase == CHOOSE_TARGET &&
            (b->actions[b->actor].command == POTION ||
             (b->actions[b->actor].command == SKILL && b->actor == 3)) && b->target == i)
            text(r, x + width + 6, y + (height - 7) / 2, "+", teal, 1);
    }
    panel(r, 6, 6, 372, 40);
    wrapped(r, 16, 15, b->log, 58, b->phase == BATTLE_WON ? gold : white);
    panel(r, 6, 190, 133, 92);
    panel(r, 143, 190, 235, 92);
    if (b->phase == CHOOSE_COMMAND || b->phase == CHOOSE_TARGET) {
        const Hero *h = &g->heroes[b->actor];
        char buf[40];
        snprintf(buf, sizeof(buf), "%s'S TURN", h->name);
        text(r, 16, 199, buf, gold, 1);
        const char *commands[] = {"ATTACK", hero_skill(b->actor), "POTION", "RUN"};
        for (int i = 0; i < 4; ++i) {
            text(r, 29, 214 + i * 14, commands[i], b->cursor == i ? gold : white, 1);
            if (i == 1) {
                snprintf(buf, sizeof(buf), "%dMP", hero_skill_cost(b->actor));
                text(r, 104, 228, buf, blue, 1);
            }
        }
        pointer(r, 17, 214 + b->cursor * 14);
        if (b->phase == CHOOSE_TARGET) {
            rect(r, 10, 212, 125, 65, navy);
            text(r, 17, 218, "CHOOSE TARGET", gold, 1);
            text(r, 17, 235, "ARROWS SELECT\nZ CONFIRM\nX CANCEL", white, 1);
        }
    } else {
        text(r, 18, 204, b->phase == BATTLE_WON ? "VICTORY" : "IN BATTLE", gold, 1);
        text(r, 18, 225, b->phase == BATTLE_WON ? "Z CONTINUE" : "Z SPEED UP", dim, 1);
        if (b->phase == BATTLE_WON) {
            char buf[32];
            snprintf(buf, sizeof(buf), "+%d GOLD", b->reward_gold);
            text(r, 18, 248, buf, teal, 1);
        }
    }
    text(r, 154, 198, "HERO     HP       MP    LV", dim, 1);
    for (int i = 0; i < PARTY_SIZE; ++i) {
        const Hero *h = &g->heroes[i];
        int y = 214 + i * 16;
        char buf[60];
        snprintf(buf, sizeof(buf), "%-6s %3d/%-3d %2d/%-2d %2d",
                 h->name, h->hp, h->max_hp, h->mp, h->max_mp, h->level);
        text(r, 154, y, buf, h->hp == 0 ? red : h->hp <= h->max_hp / 4 ? gold : white, 1);
        bar(r, 196, y + 9, 43, h->hp, h->max_hp, h->hp <= h->max_hp / 4 ? red : teal);
        bar(r, 250, y + 9, 28, h->mp, h->max_mp, blue);
    }
}

static void draw_title(SDL_Renderer *r, const Game *g)
{
    draw_coast(r, 0, 0, SCREEN_W, SCREEN_H, 0, 512);
    text(r, 25, 45, "EMBERWARD", ink, 3);
    text(r, 23, 43, "EMBERWARD", gold, 3);
    text(r, 26, 76, "THE STARLESS COAST", white, 1);
    text(r, 26, 96, "A SMALL 8-BIT ADVENTURE", dim, 1);
    panel(r, 25, 154, 184, 83);
    const char *items[] = {"NEW JOURNEY", "CONTINUE", "QUIT"};
    for (int i = 0; i < 3; ++i)
        text(r, 49, 168 + i * 23, items[i], g->title_cursor == i ? gold : white, 1);
    pointer(r, 36, 168 + g->title_cursor * 23);
    for (int i = 0; i < PARTY_SIZE; ++i)
        hero_sprite(r, 251 + i * 25, 213 + (i % 2 ? -3 : 0), i, DOWN, 0, 1, false);
    centered(r, 255, "ARROWS MOVE / Z CONFIRM / X BACK", dim, 1);
    centered(r, 272, "ORIGINAL ART, ORIGINAL ADVENTURE", (Color){93, 116, 140}, 1);
}

static void draw_ending(SDL_Renderer *r, const Game *g, bool won)
{
    draw_title(r, g);
    panel(r, 14, 115, 356, 130);
    centered(r, 130, won ? "THE BEACON SHINES" : "THE EMBERS FADE", gold, 2);
    centered(r, 159, won ? "THE SHIPS RETURN. THE COAST REMEMBERS." :
             "YOUR JOURNEY NEED NOT END HERE.", white, 1);
    centered(r, 178, won ? "FOUR STRANGERS BECAME A LIGHT IN THE DARK." :
             "LOAD YOUR SAVE FROM THE TITLE SCREEN.", dim, 1);
    if (won) {
        for (int i = 0; i < PARTY_SIZE; ++i)
            hero_sprite(r, 145 + i * 25, 196, i, DOWN, 0, 1, false);
    }
    centered(r, 227, "Z RETURN TO TITLE", teal, 1);
}

void render_game(SDL_Renderer *r, const Game *g, bool muted)
{
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    rect(r, 0, 0, SCREEN_W, SCREEN_H, ink);
    if (g->screen == TITLE || (g->screen == DIALOG && g->return_screen == TITLE))
        draw_title(r, g);
    else if (g->screen == BATTLE) draw_battle(r, g);
    else if (g->screen == GAME_OVER || g->screen == ENDING)
        draw_ending(r, g, g->screen == ENDING);
    else {
        draw_world(r, g);
        if (g->screen == MENU || (g->screen == DIALOG && g->return_screen == MENU)) draw_menu(r, g);
    }
    if (g->screen == DIALOG) {
        panel(r, 8, 184, 368, 60);
        wrapped(r, 20, 195, g->messages[g->message_index], 56, white);
        if ((int)(g->time * 2) % 2 == 0) down_arrow(r, 356, 228);
    }
    if (muted) text(r, 346, 1, "MUTE", dim, 1);
}
