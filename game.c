#include "game.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[] = {"ALDEN", "PIP", "LYRA", "SERA"};
static const char *const jobs[] = {"GUARD", "SCOUT", "MAGE", "MENDER"};
static const char *const enemies[] = {
    "MOSS SLIME", "BRIAR WOLF", "CAVE BAT", "STONEKIN", "WISP", "ASH WYRM"
};
static const int base_hp[] = {42, 32, 25, 30};
static const int base_mp[] = {8, 10, 20, 22};
static const int base_attack[] = {12, 10, 6, 7};
static const int base_defense[] = {6, 4, 3, 4};
static const int base_speed[] = {7, 12, 8, 6};

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }

uint32_t game_random(Game *g, uint32_t limit)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return limit ? x % limit : 0;
}

const char *enemy_name(int kind) { return enemies[kind >= 0 && kind < 6 ? kind : 0]; }
const char *hero_skill(int hero)
{
    static const char *const skills[] = {"CLEAVE", "QUICKSHOT", "EMBER", "MEND"};
    return skills[hero];
}
int hero_skill_cost(int hero) { return hero == 2 ? 5 : 4; }
int hero_next_xp(const Hero *hero) { return hero->level * hero->level * 20; }

static void set_stats(Hero *h, int index, int level)
{
    h->name = names[index];
    h->job = jobs[index];
    h->level = level;
    h->max_hp = base_hp[index] + (level - 1) * (index == 0 ? 9 : 7);
    h->max_mp = base_mp[index] + (level - 1) * (index >= 2 ? 4 : 2);
    h->attack = base_attack[index] + (level - 1) * 3;
    h->defense = base_defense[index] + (level - 1) * 2;
    h->speed = base_speed[index] + level - 1;
}

static void carve(Map *m, int x, int y, int w, int h, Tile tile)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            m->tiles[j][i] = tile;
}

static void terrain_patch(Map *m, int cx, int cy, int rx, int ry, Tile tile)
{
    for (int y = max_int(0, cy - ry); y <= min_int(m->height - 1, cy + ry); ++y)
        for (int x = max_int(0, cx - rx); x <= min_int(m->width - 1, cx + rx); ++x) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry &&
                m->tiles[y][x] != TILE_WATER)
                m->tiles[y][x] = tile;
        }
}

void game_load_map(Game *g, Area area)
{
    Map *m = &g->map;
    memset(m, 0, sizeof(*m));
    m->area = area;
    m->width = area == WORLD ? MAP_W : area == TOWN ? 24 : 28;
    m->height = area == WORLD ? MAP_H : area == TOWN ? 18 : 20;
    if (area == WORLD) {
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                int west = 3 + max_int(0, 12 - y) / 3 + max_int(0, y - 30) / 3;
                int east = 52 - max_int(0, 10 - y) / 2 - max_int(0, y - 28) / 2;
                int north = 3 + max_int(0, 14 - x) / 4 + max_int(0, x - 40) / 4;
                int south = 38 - max_int(0, 12 - x) / 3 - max_int(0, x - 39) / 3;
                m->tiles[y][x] = x < west || x > east || y < north || y > south ?
                    TILE_WATER : TILE_GRASS;
            }
        }
        terrain_patch(m, 11, 9, 5, 3, TILE_FOREST);
        terrain_patch(m, 15, 10, 4, 3, TILE_FOREST);
        terrain_patch(m, 29, 8, 6, 4, TILE_FOREST);
        terrain_patch(m, 33, 10, 5, 4, TILE_FOREST);
        terrain_patch(m, 16, 26, 6, 4, TILE_FOREST);
        terrain_patch(m, 20, 29, 4, 4, TILE_FOREST);
        terrain_patch(m, 39, 24, 6, 5, TILE_FOREST);
        terrain_patch(m, 44, 27, 5, 4, TILE_FOREST);
        terrain_patch(m, 31, 35, 6, 3, TILE_FOREST);

        terrain_patch(m, 20, 6, 4, 2, TILE_MOUNTAIN);
        terrain_patch(m, 23, 8, 3, 3, TILE_MOUNTAIN);
        terrain_patch(m, 44, 7, 5, 2, TILE_MOUNTAIN);
        terrain_patch(m, 47, 9, 3, 3, TILE_MOUNTAIN);
        terrain_patch(m, 33, 24, 3, 4, TILE_MOUNTAIN);
        terrain_patch(m, 31, 27, 3, 3, TILE_MOUNTAIN);

        carve(m, 7, 16, 6, 6, TILE_GRASS);
        carve(m, 9, 18, 22, 1, TILE_ROAD);
        carve(m, 28, 8, 1, 11, TILE_ROAD);
        carve(m, 28, 8, 3, 1, TILE_ROAD);
        carve(m, 15, 12, 1, 7, TILE_ROAD);
        carve(m, 11, 12, 5, 1, TILE_ROAD);
        carve(m, 21, 18, 1, 12, TILE_ROAD);
        carve(m, 21, 29, 6, 1, TILE_ROAD);
        carve(m, 28, 14, 17, 1, TILE_ROAD);
        carve(m, 44, 14, 1, 5, TILE_ROAD);
        m->tiles[18][9] = TILE_TOWN;
        m->tiles[8][30] = TILE_RUINS;
    } else if (area == TOWN) {
        carve(m, 0, 0, 24, 18, TILE_WALL);
        carve(m, 1, 1, 22, 16, TILE_GRASS);
        carve(m, 11, 3, 2, 15, TILE_ROAD);
        carve(m, 2, 10, 20, 2, TILE_ROAD);
        carve(m, 10, 9, 4, 4, TILE_ROAD);
        carve(m, 3, 3, 5, 4, TILE_ROOF);
        carve(m, 16, 3, 5, 4, TILE_ROOF);
        carve(m, 3, 12, 5, 4, TILE_ROOF);
        carve(m, 16, 12, 5, 4, TILE_ROOF);
        m->tiles[8][5] = TILE_NPC;
        m->tiles[8][18] = TILE_NPC;
        m->tiles[4][11] = TILE_NPC;
        m->tiles[13][14] = TILE_NPC;
        carve(m, 11, 10, 2, 2, TILE_FOUNTAIN);
        m->tiles[17][11] = TILE_EXIT;
        m->tiles[17][12] = TILE_EXIT;
    } else {
        carve(m, 0, 0, 28, 20, TILE_WALL);
        carve(m, 1, 1, 7, 6, TILE_FLOOR);
        carve(m, 11, 1, 7, 6, TILE_FLOOR);
        carve(m, 21, 1, 6, 6, TILE_FLOOR);
        carve(m, 1, 10, 7, 8, TILE_FLOOR);
        carve(m, 11, 10, 7, 8, TILE_FLOOR);
        carve(m, 21, 10, 6, 8, TILE_FLOOR);
        carve(m, 4, 4, 21, 1, TILE_FLOOR);
        carve(m, 4, 13, 21, 1, TILE_FLOOR);
        carve(m, 14, 4, 1, 10, TILE_FLOOR);
        carve(m, 4, 4, 1, 10, TILE_FLOOR);
        carve(m, 24, 4, 1, 10, TILE_FLOOR);
        m->tiles[17][4] = TILE_EXIT;
        m->tiles[2][4] = (g->chests & 1u) ? TILE_FLOOR : TILE_CHEST;
        m->tiles[13][25] = (g->chests & 2u) ? TILE_FLOOR : TILE_CHEST;
        m->tiles[2][14] = TILE_FOUNTAIN;
        m->tiles[2][24] = g->boss_defeated ? TILE_FLOOR : TILE_BOSS;
    }
}

void game_init(Game *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0xC0FFEEu;
    g->screen = TITLE;
    game_load_map(g, WORLD);
    g->x = 9;
    g->y = 19;
}

void game_message(Game *g, const char *text)
{
    if (g->message_count < MAX_MESSAGES) {
        snprintf(g->messages[g->message_count], sizeof(g->messages[0]), "%s", text);
        ++g->message_count;
    }
}

static void begin_dialog(Game *g)
{
    g->return_screen = g->screen;
    g->message_count = 0;
    g->message_index = 0;
    g->screen = DIALOG;
    g->sound = SFX_CONFIRM;
}

void game_notify(Game *g, const char *text)
{
    begin_dialog(g);
    game_message(g, text);
}

void game_new(Game *g)
{
    uint32_t seed = g->rng;
    game_init(g, seed);
    for (int i = 0; i < PARTY_SIZE; ++i) {
        set_stats(&g->heroes[i], i, 1);
        g->heroes[i].hp = g->heroes[i].max_hp;
        g->heroes[i].mp = g->heroes[i].max_mp;
    }
    g->gold = 60;
    g->potions = 5;
    g->ethers = 2;
    g->screen = EXPLORE;
    game_load_map(g, TOWN);
    g->x = 11;
    g->y = 14;
    g->facing = UP;
    begin_dialog(g);
    game_message(g, "THE BEACON HAS GONE DARK.\nA COLD MIST CREEPS OVER THE COAST.");
    game_message(g, "FOUR TRAVELERS ARRIVE IN BRINDLE.\nSEEK THE ELDER NORTH OF THE WELL.\nYOUR JOURNEY BEGINS HERE.");
}

bool game_walkable(const Game *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= g->map.width || y >= g->map.height) return false;
    Tile t = g->map.tiles[y][x];
    return t != TILE_WATER && t != TILE_MOUNTAIN && t != TILE_WALL &&
           t != TILE_ROOF && t != TILE_NPC && t != TILE_CHEST &&
           t != TILE_FOUNTAIN && t != TILE_BOSS;
}

static bool party_alive(const Game *g)
{
    for (int i = 0; i < PARTY_SIZE; ++i)
        if (g->heroes[i].hp > 0) return true;
    return false;
}

static void heal_party(Game *g)
{
    for (int i = 0; i < PARTY_SIZE; ++i) {
        g->heroes[i].hp = g->heroes[i].max_hp;
        g->heroes[i].mp = g->heroes[i].max_mp;
    }
}

static void interact(Game *g)
{
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    int x = g->x + dx[g->facing], y = g->y + dy[g->facing];
    if (x < 0 || y < 0 || x >= g->map.width || y >= g->map.height) return;
    Tile t = g->map.tiles[y][x];
    if (t == TILE_BOSS) {
        game_start_battle(g, true);
        return;
    }
    begin_dialog(g);
    if (t == TILE_CHEST) {
        unsigned bit = x == 4 ? 1u : 2u;
        g->chests |= bit;
        g->map.tiles[y][x] = TILE_FLOOR;
        if (bit == 1u) {
            g->potions = min_int(99, g->potions + 3);
            g->gold = min_int(999999, g->gold + 40);
            game_message(g, "FOUND 3 POTIONS AND 40 GOLD!");
        } else {
            g->ethers = min_int(99, g->ethers + 2);
            g->gold = min_int(999999, g->gold + 65);
            game_message(g, "FOUND 2 ETHERS AND 65 GOLD!");
        }
        g->sound = SFX_CHEST;
    } else if (t == TILE_FOUNTAIN) {
        if (g->map.area == TOWN) {
            heal_party(g);
            game_message(g, "THE BEACON WELL RESTORES YOUR PARTY.\nALL HP AND MP RECOVERED.\nF5 TO SAVE YOUR JOURNEY.");
        } else {
            for (int i = 0; i < PARTY_SIZE; ++i)
                if (g->heroes[i].hp > 0)
                    g->heroes[i].hp = min_int(g->heroes[i].max_hp, g->heroes[i].hp + 16);
            game_message(g, "CLEAR WATER SOOTHES YOUR WOUNDS.\nLIVING HEROES RECOVER 16 HP.");
        }
        g->sound = SFX_MAGIC;
    } else if (t == TILE_NPC && y == 4) {
        if (g->boss_defeated) {
            g->quest_complete = true;
            game_message(g, "ELDER: THE STAR LENS! YOU DID IT!\nBRINDLE'S BEACON WILL SHINE AGAIN.");
            game_message(g, "BEYOND THESE SHORES, OTHER LIGHTS\nSTILL WAIT FOR SOMEONE TO FIND THEM.\nTHANK YOU, EMBERWARD.");
            g->return_screen = ENDING;
        } else {
            g->quest_started = true;
            game_message(g, "ELDER: AN ASH WYRM STOLE OUR STAR\nLENS. WITHOUT IT, SHIPS CANNOT FIND\nTHEIR WAY HOME.");
            game_message(g, "FOLLOW THE EAST ROAD, THEN NORTH.\nSEARCH THE SUNKEN VAULT'S FAR HALL.\nBRING THE LENS BACK TO ME.");
            game_message(g, "TRAIN ON THE COAST BEFORE YOU GO.\nTHE WELL RESTORES HP AND MP.\nYOUR MENDER CAN HEAL IN BATTLE.");
        }
    } else if (t == TILE_NPC && x == 5) {
        if (g->gold >= 12 && g->potions < 99) {
            g->gold -= 12;
            ++g->potions;
            game_message(g, "APOTHECARY: ONE POTION, 12 GOLD.\nRESTORES 35 HP. USE IT FROM THE\nCAMP MENU OR DURING BATTLE.");
            g->sound = SFX_CHEST;
        } else {
            game_message(g, "APOTHECARY: A POTION COSTS 12 GOLD.\nCOME BACK WITH COIN AND PACK SPACE.");
        }
    } else if (t == TILE_NPC && x == 18) {
        if (g->gold >= 20 && g->ethers < 99) {
            g->gold -= 20;
            ++g->ethers;
            game_message(g, "HERBALIST: ONE ETHER, 20 GOLD.\nRESTORES 12 MP TO EVERY HERO.\nUSE IT FROM THE CAMP MENU.");
            g->sound = SFX_CHEST;
        } else {
            game_message(g, "HERBALIST: AN ETHER COSTS 20 GOLD.\nCOME BACK WITH COIN AND PACK SPACE.");
        }
    } else if (t == TILE_NPC) {
        game_message(g, "RANGER: CLEAVE AND EMBER HIT ALL FOES.\nQUICKSHOT STRIKES ONE FOE HARD.\nA FALLEN HERO NEEDS THE TOWN WELL.");
        game_message(g, "THE VAULT HAS A HEALING SPRING.\nSTAY NEAR IT WHILE YOU TRAIN.\nLEVEL 3 IS WISE FOR THE ASH WYRM.");
    } else {
        game_message(g, "THE SEA WIND CARRIES A DISTANT SONG.\nKEEP GOING.");
    }
}

static void move_player(Game *g, Input input)
{
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    g->facing = (int)input;
    int x = g->x + dx[input], y = g->y + dy[input];
    if (g->move_cooldown > 0 || !game_walkable(g, x, y)) return;
    g->x = x;
    g->y = y;
    g->move_cooldown = 0.13f;
    g->sound = SFX_STEP;
    ++g->steps;
    Tile t = g->map.tiles[y][x];
    if (g->map.area == WORLD && t == TILE_TOWN) {
        game_load_map(g, TOWN);
        g->x = 11;
        g->y = 16;
        g->steps = 0;
    } else if (g->map.area == WORLD && t == TILE_RUINS) {
        game_load_map(g, RUINS);
        g->x = 4;
        g->y = 16;
        g->steps = 0;
        game_notify(g, "THE SUNKEN VAULT\nSOMETHING STIRS IN THE FAR HALL...");
    } else if (t == TILE_EXIT) {
        Area old = g->map.area;
        game_load_map(g, WORLD);
        g->x = old == TOWN ? 9 : 30;
        g->y = old == TOWN ? 19 : 9;
        g->steps = 0;
    } else if (g->map.area != TOWN && g->steps >= 9) {
        uint32_t chance = g->map.area == RUINS ? 13u : t == TILE_FOREST ? 15u : 8u;
        if (game_random(g, 100) < chance) game_start_battle(g, false);
    }
}

static Enemy make_enemy(int kind)
{
    static const int hp[] = {18, 26, 20, 40, 28, 210};
    static const int atk[] = {7, 10, 9, 13, 12, 16};
    static const int def[] = {1, 2, 1, 5, 2, 5};
    static const int spd[] = {3, 9, 11, 3, 10, 8};
    static const int xp[] = {8, 12, 11, 20, 18, 100};
    static const int gold[] = {5, 8, 7, 14, 12, 180};
    Enemy e = {kind, hp[kind], hp[kind], atk[kind], def[kind], spd[kind], xp[kind], gold[kind]};
    return e;
}

static BattleScene battle_scene(const Game *g)
{
    if (g->map.area != WORLD) return SCENE_VAULT;
    Tile t = g->map.tiles[g->y][g->x];
    if (t == TILE_FOREST) return SCENE_FOREST;
    if (t == TILE_ROAD) return SCENE_ROAD;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            int x = g->x + dx, y = g->y + dy;
            if (x >= 0 && y >= 0 && x < g->map.width && y < g->map.height &&
                g->map.tiles[y][x] == TILE_WATER)
                return SCENE_SHORE;
        }
    return SCENE_GRASS;
}

void game_start_battle(Game *g, bool boss)
{
    Battle *b = &g->battle;
    memset(b, 0, sizeof(*b));
    b->boss = boss;
    b->scene = battle_scene(g);
    b->count = boss ? 1 : 1 + (int)game_random(g, 3);
    for (int i = 0; i < b->count; ++i) {
        int kind = boss ? 5 : g->map.area == RUINS ? 2 + (int)game_random(g, 3) :
                   (int)game_random(g, 2);
        b->enemies[i] = make_enemy(kind);
    }
    b->phase = CHOOSE_COMMAND;
    b->acting = -1;
    while (b->actor < PARTY_SIZE && g->heroes[b->actor].hp == 0) ++b->actor;
    if (b->actor == PARTY_SIZE) {
        g->screen = GAME_OVER;
        return;
    }
    snprintf(b->log, sizeof(b->log), "%s", boss ?
             "ASH WYRM: THIS LIGHT IS MINE!" : "FOES APPROACH! CHOOSE YOUR ACTIONS.");
    g->screen = BATTLE;
    g->steps = 0;
    g->sound = SFX_CONFIRM;
}

static bool enemies_alive(const Battle *b)
{
    for (int i = 0; i < b->count; ++i)
        if (b->enemies[i].hp > 0) return true;
    return false;
}

static int living_enemy(const Battle *b, int target)
{
    if (target >= 0 && target < b->count && b->enemies[target].hp > 0) return target;
    for (int i = 0; i < b->count; ++i)
        if (b->enemies[i].hp > 0) return i;
    return 0;
}

static void queue_turn(Game *g)
{
    Battle *b = &g->battle;
    ++b->actor;
    while (b->actor < PARTY_SIZE && g->heroes[b->actor].hp == 0) ++b->actor;
    b->cursor = 0;
    if (b->actor < PARTY_SIZE) {
        b->phase = CHOOSE_COMMAND;
        return;
    }
    int speeds[PARTY_SIZE + MAX_ENEMIES];
    b->order_count = 0;
    for (int i = 0; i < PARTY_SIZE + b->count; ++i) {
        bool alive = i < PARTY_SIZE ? g->heroes[i].hp > 0 : b->enemies[i - PARTY_SIZE].hp > 0;
        if (!alive) continue;
        int speed = i < PARTY_SIZE ? g->heroes[i].speed : b->enemies[i - PARTY_SIZE].speed;
        int n = b->order_count++;
        speeds[n] = speed + (int)game_random(g, 5);
        b->order[n] = i;
        while (n > 0 && speeds[n] > speeds[n - 1]) {
            int tmp = speeds[n]; speeds[n] = speeds[n - 1]; speeds[n - 1] = tmp;
            tmp = b->order[n]; b->order[n] = b->order[n - 1]; b->order[n - 1] = tmp;
            --n;
        }
    }
    b->turn = 0;
    b->phase = RESOLVING;
    b->timer = 0.2f;
}

static void battle_input(Game *g, Input input)
{
    Battle *b = &g->battle;
    if (b->phase == BATTLE_WON) {
        if (input == ACCEPT || input == BACK) {
            g->screen = EXPLORE;
            if (b->boss) {
                g->boss_defeated = true;
                game_load_map(g, RUINS);
                game_notify(g, "THE ASH FALLS SILENT.\nYOU RECOVER THE STAR LENS!\nRETURN TO THE ELDER IN BRINDLE.");
            }
        }
        return;
    }
    if (b->phase == RESOLVING) {
        if (input == ACCEPT && b->windup <= 0) b->timer = 0;
        return;
    }
    if (b->phase == CHOOSE_TARGET) {
        Action *a = &b->actions[b->actor];
        bool ally = a->command == POTION || (a->command == SKILL && b->actor == 3);
        int count = ally ? PARTY_SIZE : b->count;
        if (input <= RIGHT) {
            int delta = input == UP || input == LEFT ? -1 : 1;
            do { b->target = (b->target + delta + count) % count; }
            while (ally ? g->heroes[b->target].hp == 0 : b->enemies[b->target].hp == 0);
            g->sound = SFX_CONFIRM;
        } else if (input == BACK) {
            b->phase = CHOOSE_COMMAND;
        } else if (input == ACCEPT) {
            a->target = b->target;
            queue_turn(g);
            g->sound = SFX_CONFIRM;
        }
        return;
    }
    if (input == UP || input == LEFT) b->cursor = (b->cursor + 3) % 4;
    else if (input == DOWN || input == RIGHT) b->cursor = (b->cursor + 1) % 4;
    else if (input == BACK) {
        int previous = b->actor - 1;
        while (previous >= 0 && g->heroes[previous].hp == 0) --previous;
        if (previous >= 0) b->actor = previous;
    } else if (input == ACCEPT) {
        Command cmd = (Command)b->cursor;
        if (cmd == SKILL && g->heroes[b->actor].mp < hero_skill_cost(b->actor)) {
            snprintf(b->log, sizeof(b->log), "NOT ENOUGH MP FOR %s.", hero_skill(b->actor));
            g->sound = SFX_HURT;
            return;
        }
        int reserved = 0;
        for (int i = 0; i < b->actor; ++i)
            if (g->heroes[i].hp > 0 && b->actions[i].command == POTION) ++reserved;
        if (cmd == POTION && g->potions <= reserved) {
            snprintf(b->log, sizeof(b->log), "NO UNRESERVED POTIONS LEFT.");
            g->sound = SFX_HURT;
            return;
        }
        if (cmd == ESCAPE && b->boss) {
            snprintf(b->log, sizeof(b->log), "THE WYRM BLOCKS YOUR ESCAPE!");
            g->sound = SFX_HURT;
            return;
        }
        b->actions[b->actor].command = cmd;
        b->actions[b->actor].target = living_enemy(b, 0);
        bool target = cmd == ATTACK || cmd == POTION ||
                      (cmd == SKILL && (b->actor == 1 || b->actor == 3));
        if (target) {
            b->phase = CHOOSE_TARGET;
            b->target = living_enemy(b, 0);
            if (cmd == POTION || (cmd == SKILL && b->actor == 3)) {
                b->target = b->actor;
                for (int i = 0; i < PARTY_SIZE; ++i)
                    if (g->heroes[i].hp > 0 &&
                        g->heroes[i].hp * g->heroes[b->target].max_hp <
                        g->heroes[b->target].hp * g->heroes[i].max_hp) b->target = i;
            }
        } else {
            queue_turn(g);
        }
    }
    g->sound = SFX_CONFIRM;
}

static int damage(Game *g, int attack, int defense)
{
    return max_int(1, attack - defense / 2 + (int)game_random(g, 5) - 2);
}

static void win_battle(Game *g)
{
    Battle *b = &g->battle;
    b->phase = BATTLE_WON;
    b->reward_xp = 0;
    b->reward_gold = 0;
    for (int i = 0; i < b->count; ++i) {
        b->reward_xp += b->enemies[i].xp;
        b->reward_gold += b->enemies[i].gold;
    }
    g->gold = min_int(999999, g->gold + b->reward_gold);
    bool level_up = false;
    for (int i = 0; i < PARTY_SIZE; ++i) {
        Hero *h = &g->heroes[i];
        if (h->hp == 0) continue;
        h->xp = min_int(999999, h->xp + b->reward_xp);
        while (h->level < 20 && h->xp >= hero_next_xp(h)) {
            int hp = h->max_hp, mp = h->max_mp;
            set_stats(h, i, h->level + 1);
            h->hp += h->max_hp - hp;
            h->mp += h->max_mp - mp;
            level_up = true;
        }
    }
    snprintf(b->log, sizeof(b->log), "VICTORY! %d XP EACH / %d GOLD.%s",
             b->reward_xp, b->reward_gold, level_up ? " LEVEL UP!" : "");
    g->sound = SFX_WIN;
}

static void resolve_action(Game *g, int actor)
{
    Battle *b = &g->battle;
    if (actor < PARTY_SIZE) {
        Hero *h = &g->heroes[actor];
        if (h->hp == 0) return;
        Action a = b->actions[actor];
        if (a.command == ESCAPE) {
            if (game_random(g, 100) < 70) {
                g->screen = EXPLORE;
                game_notify(g, "YOU SLIP AWAY INTO THE SHADOWS.");
            } else {
                snprintf(b->log, sizeof(b->log), "%s COULD NOT ESCAPE!", h->name);
            }
            return;
        }
        if (a.command == POTION || (a.command == SKILL && actor == 3)) {
            int target = a.target;
            if (g->heroes[target].hp == 0) target = actor;
            Hero *ally = &g->heroes[target];
            int heal;
            if (a.command == POTION) {
                if (g->potions <= 0) {
                    snprintf(b->log, sizeof(b->log), "%s HAS NO POTIONS LEFT.", h->name);
                    return;
                }
                --g->potions;
                heal = 35;
            } else {
                h->mp -= hero_skill_cost(actor);
                heal = 26 + h->level * 6;
            }
            int amount = min_int(heal, ally->max_hp - ally->hp);
            ally->hp += amount;
            snprintf(b->log, sizeof(b->log), "%s HEALS %s FOR %d HP.", h->name, ally->name, amount);
            g->sound = SFX_MAGIC;
            b->flash_targets = 1u << (PARTY_SIZE + target);
            b->healing = true;
        } else {
            bool skill = a.command == SKILL;
            if (skill) h->mp -= hero_skill_cost(actor);
            bool all = skill && (actor == 0 || actor == 2);
            int target = living_enemy(b, a.target);
            int total = 0;
            for (int i = 0; i < b->count; ++i) {
                if (b->enemies[i].hp == 0 || (!all && i != target)) continue;
                Enemy *e = &b->enemies[i];
                int power = h->attack;
                if (skill) power = actor == 2 ? 17 + h->level * 4 : power + 9;
                int dealt = damage(g, power, actor == 2 && skill ? 0 : e->defense);
                e->hp = max_int(0, e->hp - dealt);
                b->flash_targets |= 1u << i;
                total += dealt;
            }
            snprintf(b->log, sizeof(b->log), "%s %s: %d DAMAGE%s",
                     h->name, skill ? hero_skill(actor) : "ATTACKS", total, all ? " TO FOES!" : "!");
            g->sound = skill ? SFX_MAGIC : SFX_HIT;
        }
    } else {
        Enemy *e = &b->enemies[actor - PARTY_SIZE];
        if (e->hp == 0) return;
        int target;
        do { target = (int)game_random(g, PARTY_SIZE); } while (g->heroes[target].hp == 0);
        bool breath = b->boss && game_random(g, 100) < 28;
        int total = 0;
        for (int i = 0; i < PARTY_SIZE; ++i) {
            if (g->heroes[i].hp == 0 || (!breath && i != target)) continue;
            int dealt = damage(g, breath ? 12 : e->attack, g->heroes[i].defense);
            g->heroes[i].hp = max_int(0, g->heroes[i].hp - dealt);
            b->flash_targets |= 1u << (PARTY_SIZE + i);
            total += dealt;
        }
        if (breath)
            snprintf(b->log, sizeof(b->log), "%s BREATHES ASH: %d DAMAGE TO PARTY!",
                     enemy_name(e->kind), total);
        else
            snprintf(b->log, sizeof(b->log), "%s HITS %s FOR %d.",
                     enemy_name(e->kind), g->heroes[target].name, total);
        g->sound = SFX_HURT;
    }
    b->flash = 0.32f;
}

static void menu_input(Game *g, Input input)
{
    if (input == BACK || input == OPEN_MENU) {
        g->screen = EXPLORE;
        return;
    }
    if (input == UP) g->menu_cursor = (g->menu_cursor + 3) % 4;
    if (input == DOWN) g->menu_cursor = (g->menu_cursor + 1) % 4;
    if (input != ACCEPT) return;
    if (g->menu_cursor == 0) {
        int target = -1;
        for (int i = 0; i < PARTY_SIZE; ++i) {
            Hero *h = &g->heroes[i];
            if (h->hp > 0 && h->hp < h->max_hp &&
                (target < 0 || h->hp * g->heroes[target].max_hp < g->heroes[target].hp * h->max_hp))
                target = i;
        }
        if (g->potions == 0) game_notify(g, "NO POTIONS. VISIT BRINDLE'S APOTHECARY.");
        else if (target < 0) game_notify(g, "NO LIVING HERO NEEDS HEALING.\nFALLEN HEROES NEED THE TOWN WELL.");
        else {
            Hero *h = &g->heroes[target];
            h->hp = min_int(h->max_hp, h->hp + 35);
            --g->potions;
            char text[100];
            snprintf(text, sizeof(text), "%s DRINKS A POTION.\nUP TO 35 HP RESTORED.", h->name);
            game_notify(g, text);
            g->sound = SFX_MAGIC;
        }
    } else if (g->menu_cursor == 1) {
        bool needed = false;
        for (int i = 0; i < PARTY_SIZE; ++i)
            if (g->heroes[i].hp > 0 && g->heroes[i].mp < g->heroes[i].max_mp) needed = true;
        if (g->ethers == 0) game_notify(g, "NO ETHERS. VISIT BRINDLE'S HERBALIST.");
        else if (!needed) game_notify(g, "NO LIVING HERO NEEDS MP.");
        else {
            --g->ethers;
            for (int i = 0; i < PARTY_SIZE; ++i)
                if (g->heroes[i].hp > 0)
                    g->heroes[i].mp = min_int(g->heroes[i].max_mp, g->heroes[i].mp + 12);
            game_notify(g, "THE PARTY SHARES AN ETHER.\nLIVING HEROES RECOVER UP TO 12 MP.");
            g->sound = SFX_MAGIC;
        }
    } else if (g->menu_cursor == 2) {
        game_notify(g, g->boss_defeated ?
                    "STAR LENS RECOVERED!\nRETURN TO THE ELDER IN BRINDLE." :
                    "RESTORE BRINDLE'S BEACON.\nFOLLOW THE EAST ROAD, THEN NORTH.\nDEFEAT THE WYRM IN THE SUNKEN VAULT.");
    } else {
        g->screen = EXPLORE;
    }
}

void game_input(Game *g, Input input)
{
    if (g->screen == TITLE) {
        if (input == UP) g->title_cursor = (g->title_cursor + 2) % 3;
        if (input == DOWN) g->title_cursor = (g->title_cursor + 1) % 3;
        if (input == ACCEPT) {
            if (g->title_cursor == 0) game_new(g);
            else if (g->title_cursor == 2) g->quit = true;
        }
    } else if (g->screen == EXPLORE) {
        if (input <= RIGHT) move_player(g, input);
        else if (input == ACCEPT) interact(g);
        else if (input == OPEN_MENU || input == BACK) {
            g->screen = MENU;
            g->menu_cursor = 0;
            g->sound = SFX_CONFIRM;
        }
    } else if (g->screen == DIALOG) {
        if (input == ACCEPT || input == BACK) {
            if (++g->message_index >= g->message_count) g->screen = g->return_screen;
            g->sound = SFX_CONFIRM;
        }
    } else if (g->screen == MENU) menu_input(g, input);
    else if (g->screen == BATTLE) battle_input(g, input);
    else if (g->screen == GAME_OVER || g->screen == ENDING) {
        if (input == ACCEPT) g->screen = TITLE;
    }
}

void game_update(Game *g, float dt)
{
    g->time += dt;
    if (g->time > 3600) g->time -= 3600;
    if (g->move_cooldown > 0) g->move_cooldown -= dt;
    Battle *b = &g->battle;
    if (b->flash > 0) b->flash -= dt;
    if (g->screen != BATTLE || b->phase != RESOLVING) return;
    if (b->windup > 0) {
        b->windup -= dt;
        if (b->windup <= 0) {
            resolve_action(g, b->acting);
            b->acting = -1;
            b->timer = 0.60f;
        }
        return;
    }
    b->timer -= dt;
    if (b->timer > 0 || b->flash > 0) return;
    if (!party_alive(g)) {
        g->screen = GAME_OVER;
        g->sound = SFX_HURT;
        return;
    }
    if (!enemies_alive(b)) {
        win_battle(g);
        return;
    }
    if (b->turn >= b->order_count) {
        b->actor = 0;
        while (g->heroes[b->actor].hp == 0) ++b->actor;
        b->phase = CHOOSE_COMMAND;
        b->cursor = 0;
        snprintf(b->log, sizeof(b->log), "A NEW ROUND. CHOOSE YOUR ACTIONS.");
        return;
    }
    b->flash_targets = 0;
    b->healing = false;
    while (b->turn < b->order_count) {
        int actor = b->order[b->turn++];
        bool alive = actor < PARTY_SIZE ? g->heroes[actor].hp > 0 :
                     b->enemies[actor - PARTY_SIZE].hp > 0;
        if (!alive) continue;
        b->acting = actor;
        b->windup = 0.18f;
        break;
    }
}

static bool fail(char *error, size_t size, const char *text)
{
    if (size > 0) snprintf(error, size, "%s", text);
    return false;
}

bool game_save(const Game *g, const char *path, char *error, size_t error_size)
{
    if (g->screen != EXPLORE && g->screen != MENU)
        return fail(error, error_size, "SAVE ONLY WHILE EXPLORING OR CAMPING.");
    char temp[1024];
    int n = snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temp)) return fail(error, error_size, "SAVE PATH TOO LONG.");
    FILE *f = fopen(temp, "w");
    if (!f) return fail(error, error_size, strerror(errno));
    bool ok = fprintf(f, "EMBERWARD 1\n%d %d %d %d %d %d %d %u %u %d %d %d\n",
                      (int)g->map.area, g->x, g->y, g->facing, g->gold, g->potions,
                      g->ethers, g->chests, g->rng, g->quest_started ? 1 : 0,
                      g->boss_defeated ? 1 : 0, g->quest_complete ? 1 : 0) > 0;
    for (int i = 0; i < PARTY_SIZE; ++i) {
        const Hero *h = &g->heroes[i];
        if (fprintf(f, "%d %d %d %d\n", h->level, h->xp, h->hp, h->mp) < 0) ok = false;
    }
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(temp);
        return fail(error, error_size, "COULD NOT WRITE SAVE FILE.");
    }
    if (rename(temp, path) != 0) {
        int saved_errno = errno;
        remove(temp);
        return fail(error, error_size, strerror(saved_errno));
    }
    return true;
}

static bool read_number(FILE *f, int64_t minimum, int64_t maximum, int64_t *value)
{
    char token[64], *end;
    if (fscanf(f, "%63s", token) != 1) return false;
    errno = 0;
    long long parsed = strtoll(token, &end, 10);
    if (errno == ERANGE || end == token || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    *value = (int64_t)parsed;
    return true;
}

static bool read_int(FILE *f, int minimum, int maximum, int *value)
{
    int64_t parsed;
    if (!read_number(f, minimum, maximum, &parsed)) return false;
    *value = (int)parsed;
    return true;
}

static bool read_uint(FILE *f, uint32_t minimum, uint32_t maximum, uint32_t *value)
{
    int64_t parsed;
    if (!read_number(f, minimum, maximum, &parsed)) return false;
    *value = (uint32_t)parsed;
    return true;
}

bool game_load(Game *g, const char *path, char *error, size_t error_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return fail(error, error_size, strerror(errno));
    char magic[32];
    int version = 0, area = 0, quest = 0, boss = 0, complete = 0;
    Game next;
    game_init(&next, 1);
    uint32_t chests;
    bool ok = fscanf(f, "%31s", magic) == 1 && strcmp(magic, "EMBERWARD") == 0 &&
              read_int(f, 1, 1, &version) &&
              read_int(f, WORLD, RUINS, &area) &&
              read_int(f, 0, MAP_W - 1, &next.x) &&
              read_int(f, 0, MAP_H - 1, &next.y) &&
              read_int(f, UP, RIGHT, &next.facing) &&
              read_int(f, 0, 999999, &next.gold) &&
              read_int(f, 0, 99, &next.potions) &&
              read_int(f, 0, 99, &next.ethers) &&
              read_uint(f, 0, 3, &chests) &&
              read_uint(f, 1, UINT32_MAX, &next.rng) &&
              read_int(f, 0, 1, &quest) &&
              read_int(f, 0, 1, &boss) &&
              read_int(f, 0, 1, &complete) && (!complete || boss);
    for (int i = 0; i < PARTY_SIZE && ok; ++i) {
        int level, xp, hp, mp;
        ok = read_int(f, 1, 20, &level) && read_int(f, 0, 999999, &xp) &&
             read_int(f, 0, 999, &hp) && read_int(f, 0, 999, &mp);
        if (!ok) break;
        set_stats(&next.heroes[i], i, level);
        Hero *h = &next.heroes[i];
        ok = hp >= 0 && hp <= h->max_hp && mp >= 0 && mp <= h->max_mp &&
             (level == 1 || xp >= (level - 1) * (level - 1) * 20) &&
             (level == 20 || xp < hero_next_xp(h));
        h->xp = xp; h->hp = hp; h->mp = mp;
    }
    int c;
    if (ok) {
        do { c = fgetc(f); } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
        ok = c == EOF && !ferror(f);
    }
    if (fclose(f) != 0) ok = false;
    if (!ok) return fail(error, error_size, "SAVE FILE IS INVALID OR INCOMPATIBLE.");
    next.chests = chests;
    next.quest_started = quest != 0;
    next.boss_defeated = boss != 0;
    next.quest_complete = complete != 0;
    game_load_map(&next, (Area)area);
    if (!game_walkable(&next, next.x, next.y) || !party_alive(&next))
        return fail(error, error_size, "SAVE HAS AN INVALID PARTY OR LOCATION.");
    next.screen = EXPLORE;
    *g = next;
    return true;
}
