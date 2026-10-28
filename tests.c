#define _POSIX_C_SOURCE 200809L
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void dismiss(Game *g)
{
    for (int i = 0; i < MAX_MESSAGES + 1 && g->screen == DIALOG; ++i)
        game_input(g, ACCEPT);
    CHECK(g->screen != DIALOG);
}

static Game new_game(uint32_t seed)
{
    Game g;
    game_init(&g, seed);
    CHECK(g.screen == TITLE);
    game_input(&g, ACCEPT);
    CHECK(g.screen == DIALOG);
    dismiss(&g);
    CHECK(g.screen == EXPLORE && g.map.area == TOWN);
    return g;
}

static void interact_at(Game *g, int x, int y, int facing)
{
    g->screen = EXPLORE;
    g->x = x; g->y = y; g->facing = facing;
    CHECK(game_walkable(g, x, y));
    game_input(g, ACCEPT);
}

static void rest(Game *g)
{
    game_load_map(g, TOWN);
    interact_at(g, 12, 9, DOWN);
    dismiss(g);
    for (int i = 0; i < PARTY_SIZE; ++i) {
        CHECK(g->heroes[i].hp == g->heroes[i].max_hp);
        CHECK(g->heroes[i].mp == g->heroes[i].max_mp);
    }
}

static int weakest_hero(const Game *g)
{
    int target = 0;
    while (target < PARTY_SIZE - 1 && g->heroes[target].hp == 0) ++target;
    for (int i = 0; i < PARTY_SIZE; ++i)
        if (g->heroes[i].hp > 0 &&
            g->heroes[i].hp * g->heroes[target].max_hp <
            g->heroes[target].hp * g->heroes[i].max_hp) target = i;
    return target;
}

static void auto_battle(Game *g)
{
    int safety = 0;
    while (g->screen == BATTLE && g->battle.phase != BATTLE_WON && safety++ < 2500) {
        Battle *b = &g->battle;
        if (b->phase == RESOLVING) {
            game_update(g, 0.8f);
            for (int i = 0; i < PARTY_SIZE; ++i) {
                CHECK(g->heroes[i].hp >= 0);
                CHECK(g->heroes[i].mp >= 0);
                CHECK(g->heroes[i].hp <= g->heroes[i].max_hp);
            }
        } else if (b->phase == CHOOSE_COMMAND) {
            int actor = b->actor, weak = weakest_hero(g);
            bool injured = g->heroes[weak].hp * 4 < g->heroes[weak].max_hp * 3;
            Command cmd = ATTACK;
            int alive = 0;
            for (int i = 0; i < b->count; ++i)
                if (b->enemies[i].hp > 0) ++alive;
            if (g->heroes[actor].mp >= hero_skill_cost(actor) &&
                ((actor == 0 && alive >= 2) || actor == 1 || actor == 2 || (actor == 3 && injured)))
                cmd = SKILL;
            if (injured && g->potions > 0 && g->heroes[3].mp < 4) cmd = POTION;
            b->cursor = (int)cmd;
            game_input(g, ACCEPT);
        } else {
            Action action = b->actions[b->actor];
            if (action.command == POTION || (action.command == SKILL && b->actor == 3))
                b->target = weakest_hero(g);
            game_input(g, ACCEPT);
        }
    }
    CHECK(safety < 2500);
}

static void battle_and_win(Game *g, bool boss)
{
    game_start_battle(g, boss);
    auto_battle(g);
    CHECK(g->screen == BATTLE && g->battle.phase == BATTLE_WON);
    game_input(g, ACCEPT);
    dismiss(g);
}

static void test_new_game_and_maps(void)
{
    Game g = new_game(42);
    CHECK(g.gold == 60 && g.potions == 5 && g.ethers == 2);
    for (int i = 0; i < PARTY_SIZE; ++i) {
        CHECK(g.heroes[i].level == 1);
        CHECK(g.heroes[i].hp == g.heroes[i].max_hp);
        CHECK(g.heroes[i].mp == g.heroes[i].max_mp);
    }
    for (int area = WORLD; area <= RUINS; ++area) {
        game_load_map(&g, (Area)area);
        CHECK(g.map.width <= MAP_W && g.map.height <= MAP_H);
        CHECK(!game_walkable(&g, -1, 0));
        CHECK(!game_walkable(&g, g.map.width, 0));
        CHECK(!game_walkable(&g, 0, g.map.height));
        for (int y = 0; y < g.map.height; ++y)
            for (int x = 0; x < g.map.width; ++x)
                CHECK(g.map.tiles[y][x] >= TILE_WATER && g.map.tiles[y][x] <= TILE_BOSS);
    }
    g.heroes[0].hp = 0;
    g.heroes[1].mp = 0;
    rest(&g);
}

static void test_exploration(void)
{
    Game g = new_game(51);
    g.x = 2; g.y = 1;
    game_input(&g, UP);
    CHECK(g.x == 2 && g.y == 1 && g.facing == UP);
    game_input(&g, DOWN);
    CHECK(g.y == 2);
    game_input(&g, DOWN);
    CHECK(g.y == 2);
    game_update(&g, 0.2f);
    game_input(&g, DOWN);
    CHECK(g.y == 3);
    g.x = 11; g.y = 16; g.move_cooldown = 0;
    game_input(&g, DOWN);
    CHECK(g.map.area == WORLD && g.x == 9 && g.y == 19);
    game_update(&g, 0.2f);
    game_input(&g, UP);
    CHECK(g.map.area == TOWN && g.x == 11 && g.y == 16);
    game_load_map(&g, WORLD);
    g.x = 30; g.y = 9; g.move_cooldown = 0;
    game_input(&g, UP);
    CHECK(g.map.area == RUINS && g.x == 4 && g.y == 16);
    dismiss(&g);
    game_update(&g, 0.2f);
    game_input(&g, DOWN);
    CHECK(g.map.area == WORLD && g.x == 30 && g.y == 9);
}

static void test_world_terrain(void)
{
    Game g = new_game(51);
    game_load_map(&g, WORLD);
    CHECK(g.map.width == 56 && g.map.height == 42);
    CHECK(g.map.width * g.map.height >= 40 * 30 * 19 / 10);
    CHECK(g.map.width * g.map.height <= 40 * 30 * 21 / 10);
    Map original = g.map;
    uint32_t rng = g.rng;
    game_load_map(&g, WORLD);
    CHECK(memcmp(&g.map, &original, sizeof(original)) == 0);
    CHECK(g.rng == rng);

    static const int dx[] = {0, 0, -1, 1}, dy[] = {-1, 1, 0, 0};
    int counts[TILE_BOSS + 1] = {0}, grouped[TILE_BOSS + 1] = {0};
    int road_ends = 0, road_junctions = 0;
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Tile t = g.map.tiles[y][x];
            ++counts[t];
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1)
                CHECK(t == TILE_WATER);
            int neighbors = 0;
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
                Tile next = g.map.tiles[ny][nx];
                if (next == t || (t == TILE_ROAD &&
                    (next == TILE_TOWN || next == TILE_RUINS))) ++neighbors;
            }
            if (t == TILE_FOREST || t == TILE_MOUNTAIN) {
                CHECK(neighbors > 0);
                if (neighbors >= 2) ++grouped[t];
            }
            if (t == TILE_ROAD) {
                if (neighbors == 1) ++road_ends;
                if (neighbors >= 3) ++road_junctions;
            }
        }
    CHECK(counts[TILE_GRASS] > (MAP_W * MAP_H - counts[TILE_WATER]) / 2);
    CHECK(counts[TILE_FOREST] > 200 && counts[TILE_MOUNTAIN] > 100);
    CHECK(grouped[TILE_FOREST] * 10 >= counts[TILE_FOREST] * 9);
    CHECK(grouped[TILE_MOUNTAIN] * 10 >= counts[TILE_MOUNTAIN] * 9);
    CHECK(road_ends >= 4 && road_junctions >= 3);

    bool seen[MAP_H][MAP_W] = {{false}};
    int qx[MAP_W * MAP_H], qy[MAP_W * MAP_H], head = 0, tail = 1;
    qx[0] = 9; qy[0] = 18; seen[18][9] = true;
    while (head < tail) {
        int x = qx[head], y = qy[head++];
        for (int d = 0; d < 4; ++d) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H || seen[ny][nx])
                continue;
            Tile t = g.map.tiles[ny][nx];
            if (t != TILE_ROAD && t != TILE_TOWN && t != TILE_RUINS) continue;
            seen[ny][nx] = true; qx[tail] = nx; qy[tail++] = ny;
        }
    }
    CHECK(tail == counts[TILE_ROAD] + counts[TILE_TOWN] + counts[TILE_RUINS]);
    CHECK(seen[12][11] && seen[29][26] && seen[18][44]);
    CHECK(g.map.tiles[18][9] == TILE_TOWN && g.map.tiles[8][30] == TILE_RUINS);
}

static void test_npcs_and_items(void)
{
    Game g = new_game(17);
    interact_at(&g, 11, 5, UP);
    CHECK(g.quest_started);
    dismiss(&g);
    interact_at(&g, 5, 9, UP);
    CHECK(g.gold == 48 && g.potions == 6);
    dismiss(&g);
    interact_at(&g, 18, 9, UP);
    CHECK(g.gold == 28 && g.ethers == 3);
    dismiss(&g);
    g.gold = 0;
    interact_at(&g, 5, 9, UP);
    CHECK(g.gold == 0 && g.potions == 6);
    dismiss(&g);
    game_load_map(&g, RUINS);
    interact_at(&g, 4, 3, UP);
    CHECK(g.chests == 1 && g.potions == 9 && g.gold == 40);
    dismiss(&g);
    game_load_map(&g, RUINS);
    CHECK(g.map.tiles[2][4] == TILE_FLOOR);
    interact_at(&g, 24, 13, RIGHT);
    CHECK(g.chests == 3 && g.ethers == 5 && g.gold == 105);
    dismiss(&g);
    g.heroes[0].hp = 1;
    g.heroes[1].hp = 0;
    interact_at(&g, 14, 3, UP);
    dismiss(&g);
    CHECK(g.heroes[0].hp == 17 && g.heroes[1].hp == 0);
    game_input(&g, OPEN_MENU);
    CHECK(g.screen == MENU);
    int before = g.potions;
    game_input(&g, ACCEPT);
    CHECK(g.potions == before - 1 && g.heroes[0].hp == g.heroes[0].max_hp);
    dismiss(&g);
    CHECK(g.screen == MENU);
    g.heroes[2].mp = 0;
    g.menu_cursor = 1;
    before = g.ethers;
    game_input(&g, ACCEPT);
    CHECK(g.ethers == before - 1 && g.heroes[2].mp == 12);
    dismiss(&g);
    game_input(&g, BACK);
    CHECK(g.screen == EXPLORE);
}

static void test_battle_commands(void)
{
    Game g = new_game(777);
    game_load_map(&g, WORLD);
    game_start_battle(&g, false);
    g.heroes[0].mp = 0;
    g.battle.cursor = SKILL;
    game_input(&g, ACCEPT);
    CHECK(g.battle.phase == CHOOSE_COMMAND && g.battle.actor == 0);
    CHECK(strstr(g.battle.log, "NOT ENOUGH") != NULL);
    g.potions = 1;
    g.battle.cursor = POTION;
    game_input(&g, ACCEPT);
    CHECK(g.battle.phase == CHOOSE_TARGET);
    game_input(&g, ACCEPT);
    CHECK(g.battle.actor == 1);
    g.battle.cursor = POTION;
    game_input(&g, ACCEPT);
    CHECK(g.battle.phase == CHOOSE_COMMAND);
    CHECK(strstr(g.battle.log, "UNRESERVED") != NULL);
    game_input(&g, BACK);
    CHECK(g.battle.actor == 0);
    g.battle.cursor = ATTACK;
    game_input(&g, ACCEPT);
    CHECK(g.battle.phase == CHOOSE_TARGET);
    game_input(&g, BACK);
    CHECK(g.battle.actor == 0 && g.battle.phase == CHOOSE_COMMAND);
    game_start_battle(&g, true);
    g.battle.cursor = ESCAPE;
    game_input(&g, ACCEPT);
    CHECK(g.battle.actor == 0 && g.battle.phase == CHOOSE_COMMAND);
    CHECK(strstr(g.battle.log, "ESCAPE") != NULL);
    g.heroes[0].hp = 0;
    game_start_battle(&g, false);
    CHECK(g.battle.actor == 1);
    g.heroes[1].hp = 0; g.heroes[2].hp = 0; g.heroes[3].hp = 0;
    game_start_battle(&g, false);
    CHECK(g.screen == GAME_OVER);
}

static Game cue_battle(int actor, Command command, int target)
{
    Game g = new_game(911);
    game_start_battle(&g, false);
    g.battle.count = MAX_ENEMIES;
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        g.battle.enemies[i] = g.battle.enemies[0];
        g.battle.enemies[i].hp = g.battle.enemies[i].max_hp = 200;
    }
    g.battle.phase = RESOLVING;
    g.battle.order_count = 1;
    g.battle.order[0] = actor;
    if (actor < PARTY_SIZE) g.battle.actions[actor] = (Action){command, target};
    return g;
}

static void test_battle_cues(void)
{
    Game g = cue_battle(0, ATTACK, 1);
    Battle *b = &g.battle;
    CHECK(b->acting == -1 && b->flash_targets == 0);
    game_update(&g, 0.01f);
    CHECK(b->acting == 0 && b->windup > 0 && b->flash <= 0);
    CHECK(b->enemies[1].hp == 200);
    game_input(&g, ACCEPT);
    game_update(&g, 0.09f);
    CHECK(b->acting == 0 && b->windup > 0 && b->enemies[1].hp == 200);
    game_update(&g, 0.10f);
    CHECK(b->acting == -1 && b->windup <= 0);
    CHECK(b->flash > 0 && b->flash_targets == (1u << 1) && !b->healing);
    CHECK(b->enemies[1].hp < 200 && b->enemies[0].hp == 200);
    game_input(&g, ACCEPT);
    game_update(&g, 0.1f);
    CHECK(b->phase == RESOLVING && b->flash > 0);
    game_update(&g, 0.23f);
    CHECK(b->phase == CHOOSE_COMMAND && b->flash <= 0);

    g = cue_battle(0, SKILL, 0);
    b->enemies[0].hp = 0;
    b->enemies[1].hp = 1;
    game_update(&g, 0.01f);
    game_update(&g, 0.19f);
    CHECK(b->flash_targets == ((1u << 1) | (1u << 2)));
    CHECK(b->enemies[1].hp == 0 && b->flash > 0);

    g = cue_battle(0, ATTACK, 0);
    for (int i = 1; i < MAX_ENEMIES; ++i) b->enemies[i].hp = 0;
    b->enemies[0].hp = 1;
    game_update(&g, 0.01f);
    game_update(&g, 0.19f);
    game_input(&g, ACCEPT);
    game_update(&g, 0.10f);
    CHECK(b->phase == RESOLVING && b->enemies[0].hp == 0 && b->flash > 0);
    game_update(&g, 0.23f);
    CHECK(b->phase == BATTLE_WON);

    for (int skill = 0; skill < 2; ++skill) {
        g = cue_battle(3, skill ? SKILL : POTION, 1);
        g.heroes[1].hp = 1;
        game_update(&g, 0.01f);
        CHECK(g.heroes[1].hp == 1 && b->acting == 3);
        game_update(&g, 0.19f);
        CHECK(g.heroes[1].hp > 1 && b->healing);
        CHECK(b->flash_targets == (1u << (PARTY_SIZE + 1)) && b->flash > 0);
    }

    g = cue_battle(PARTY_SIZE, ATTACK, 0);
    for (int i = 1; i < PARTY_SIZE; ++i) g.heroes[i].hp = 0;
    g.heroes[0].hp = 1;
    game_update(&g, 0.01f);
    CHECK(b->acting == PARTY_SIZE && g.heroes[0].hp == 1);
    game_update(&g, 0.19f);
    CHECK(b->flash_targets == (1u << PARTY_SIZE) && !b->healing);
    CHECK(g.heroes[0].hp == 0 && b->flash > 0);
    game_input(&g, ACCEPT);
    game_update(&g, 0.10f);
    CHECK(g.screen == BATTLE);
    game_update(&g, 0.23f);
    CHECK(g.screen == GAME_OVER);

    bool saw_breath = false;
    for (uint32_t seed = 1; seed <= 32; ++seed) {
        g = cue_battle(PARTY_SIZE, ATTACK, 0);
        b->boss = true;
        g.rng = seed;
        game_update(&g, 0.01f);
        game_update(&g, 0.19f);
        if (strstr(b->log, "BREATHES ASH")) {
            CHECK(b->flash_targets == 0xf0u);
            saw_breath = true;
        }
    }
    CHECK(saw_breath);

    g = cue_battle(0, ATTACK, 0);
    g.heroes[0].hp = 0;
    b->order_count = 2;
    b->order[1] = PARTY_SIZE;
    game_update(&g, 0.01f);
    CHECK(b->acting == PARTY_SIZE && b->turn == 2);
    CHECK(b->flash_targets == 0 && !b->healing);
    game_start_battle(&g, false);
    CHECK(b->acting == -1 && b->windup == 0 && b->flash_targets == 0);
}

static void test_battle_scenes(void)
{
    Game g = new_game(31);
    game_load_map(&g, WORLD);
    int found[SCENE_VAULT + 1] = {0};
    for (int y = 0; y < g.map.height; ++y)
        for (int x = 0; x < g.map.width; ++x) {
            if (!game_walkable(&g, x, y)) continue;
            Tile t = g.map.tiles[y][x];
            g.screen = EXPLORE; g.x = x; g.y = y;
            game_start_battle(&g, false);
            BattleScene s = g.battle.scene;
            CHECK(s != SCENE_VAULT);
            if (t == TILE_FOREST) CHECK(s == SCENE_FOREST);
            if (t == TILE_ROAD) CHECK(s == SCENE_ROAD);
            if (t == TILE_GRASS) CHECK(s == SCENE_GRASS || s == SCENE_SHORE);
            ++found[s];
        }
    CHECK(found[SCENE_GRASS] && found[SCENE_FOREST] && found[SCENE_ROAD] && found[SCENE_SHORE]);
    game_load_map(&g, RUINS);
    g.x = 4; g.y = 16;
    game_start_battle(&g, false);
    CHECK(g.battle.scene == SCENE_VAULT);
    game_start_battle(&g, true);
    CHECK(g.battle.scene == SCENE_VAULT);
}

static void test_combat_and_campaign(void)
{
    for (uint32_t seed = 1; seed <= 25; ++seed) {
        Game g = new_game(seed * 101u);
        interact_at(&g, 11, 5, UP);
        dismiss(&g);
        for (int i = 0; i < 10; ++i) {
            game_load_map(&g, WORLD);
            int gold_before = g.gold, xp_before = g.heroes[0].xp;
            battle_and_win(&g, false);
            CHECK(g.gold > gold_before);
            if (g.heroes[0].hp > 0) CHECK(g.heroes[0].xp > xp_before);
            rest(&g);
        }
        CHECK(g.heroes[0].level >= 3);
        game_load_map(&g, RUINS);
        g.x = 24; g.y = 3; g.facing = UP;
        game_input(&g, ACCEPT);
        CHECK(g.screen == BATTLE && g.battle.boss);
        auto_battle(&g);
        CHECK(g.screen == BATTLE && g.battle.phase == BATTLE_WON);
        game_input(&g, ACCEPT);
        dismiss(&g);
        CHECK(g.boss_defeated && g.map.tiles[2][24] == TILE_FLOOR);
        game_load_map(&g, TOWN);
        interact_at(&g, 11, 5, UP);
        dismiss(&g);
        CHECK(g.quest_complete && g.screen == ENDING);
        game_input(&g, ACCEPT);
        CHECK(g.screen == TITLE);
    }
}

static void test_map_reachability(void)
{
    Game g = new_game(11);
    const int starts[3][2] = {{9, 19}, {11, 16}, {4, 16}};
    for (int area = WORLD; area <= RUINS; ++area) {
        game_load_map(&g, (Area)area);
        bool seen[MAP_H][MAP_W] = {{false}};
        int qx[MAP_W * MAP_H], qy[MAP_W * MAP_H], head = 0, tail = 1;
        qx[0] = starts[area][0]; qy[0] = starts[area][1];
        seen[qy[0]][qx[0]] = true;
        static const int dx[] = {0, 0, -1, 1}, dy[] = {-1, 1, 0, 0};
        while (head < tail) {
            int x = qx[head], y = qy[head++];
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d], ny = y + dy[d];
                if (game_walkable(&g, nx, ny) && !seen[ny][nx]) {
                    seen[ny][nx] = true; qx[tail] = nx; qy[tail++] = ny;
                }
            }
        }
        for (int y = 0; y < g.map.height; ++y) {
            for (int x = 0; x < g.map.width; ++x) {
                Tile t = g.map.tiles[y][x];
                if (t == TILE_TOWN || t == TILE_RUINS || t == TILE_EXIT) CHECK(seen[y][x]);
                if (t == TILE_NPC || t == TILE_CHEST || t == TILE_BOSS || t == TILE_FOUNTAIN) {
                    bool reachable = false;
                    for (int d = 0; d < 4; ++d) {
                        int nx = x + dx[d], ny = y + dy[d];
                        if (nx >= 0 && ny >= 0 && nx < g.map.width && ny < g.map.height &&
                            seen[ny][nx]) reachable = true;
                    }
                    CHECK(reachable);
                }
                if (game_walkable(&g, x, y)) CHECK(seen[y][x]);
            }
        }
    }
}

static void walk_to(Game *g, int destination_x, int destination_y)
{
    int parent[MAP_H][MAP_W];
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) parent[y][x] = -1;
    int qx[MAP_W * MAP_H], qy[MAP_W * MAP_H], head = 0, tail = 1;
    qx[0] = g->x; qy[0] = g->y; parent[g->y][g->x] = -2;
    static const int dx[] = {0, 0, -1, 1}, dy[] = {-1, 1, 0, 0};
    while (head < tail && parent[destination_y][destination_x] == -1) {
        int x = qx[head], y = qy[head++];
        for (int d = 0; d < 4; ++d) {
            int nx = x + dx[d], ny = y + dy[d];
            if (!game_walkable(g, nx, ny) || parent[ny][nx] != -1) continue;
            Tile t = g->map.tiles[ny][nx];
            if ((t == TILE_TOWN || t == TILE_RUINS || t == TILE_EXIT) &&
                (nx != destination_x || ny != destination_y)) continue;
            parent[ny][nx] = d;
            qx[tail] = nx; qy[tail++] = ny;
        }
    }
    CHECK(parent[destination_y][destination_x] != -1);
    Input route[MAP_W * MAP_H];
    int length = 0, x = destination_x, y = destination_y;
    while (parent[y][x] != -2) {
        int d = parent[y][x];
        CHECK(d >= 0 && d <= 3);
        route[length++] = (Input)d;
        x -= dx[d]; y -= dy[d];
    }
    for (int i = length - 1; i >= 0; --i) {
        CHECK(g->screen == EXPLORE);
        game_update(g, 0.2f);
        game_input(g, route[i]);
        if (g->screen == BATTLE) {
            auto_battle(g);
            CHECK(g->screen == BATTLE && g->battle.phase == BATTLE_WON);
            game_input(g, ACCEPT);
        }
        dismiss(g);
    }
}

static void test_walkthrough(void)
{
    Game g = new_game(20261027);
    walk_to(&g, 11, 5);
    game_input(&g, UP);
    game_input(&g, ACCEPT);
    dismiss(&g);
    CHECK(g.quest_started);
    for (int i = 0; i < 10; ++i) {
        game_load_map(&g, WORLD);
        battle_and_win(&g, false);
        rest(&g);
    }
    walk_to(&g, 11, 17);
    CHECK(g.map.area == WORLD);
    walk_to(&g, 30, 8);
    CHECK(g.map.area == RUINS);
    walk_to(&g, 4, 3);
    game_input(&g, UP);
    game_input(&g, ACCEPT);
    dismiss(&g);
    CHECK(g.chests == 1);
    walk_to(&g, 24, 13);
    game_input(&g, RIGHT);
    game_input(&g, ACCEPT);
    dismiss(&g);
    CHECK(g.chests == 3);
    walk_to(&g, 14, 3);
    for (int i = 0; i < 5; ++i) {
        game_input(&g, UP);
        game_input(&g, ACCEPT);
        dismiss(&g);
    }
    game_input(&g, OPEN_MENU);
    g.menu_cursor = 1;
    for (int i = 0; i < 3; ++i) { game_input(&g, ACCEPT); dismiss(&g); }
    game_input(&g, BACK);
    walk_to(&g, 24, 3);
    game_input(&g, UP);
    game_input(&g, ACCEPT);
    CHECK(g.screen == BATTLE && g.battle.boss);
    auto_battle(&g);
    CHECK(g.screen == BATTLE && g.battle.phase == BATTLE_WON);
    game_input(&g, ACCEPT);
    dismiss(&g);
    CHECK(g.boss_defeated);
    walk_to(&g, 4, 17);
    CHECK(g.map.area == WORLD);
    walk_to(&g, 9, 18);
    CHECK(g.map.area == TOWN);
    walk_to(&g, 11, 5);
    game_input(&g, UP);
    game_input(&g, ACCEPT);
    dismiss(&g);
    CHECK(g.screen == ENDING && g.quest_complete);
}

static void write_save(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    CHECK(fputs(content, f) >= 0);
    CHECK(fclose(f) == 0);
}

static void test_save_load(void)
{
    char path[] = "/tmp/emberward-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(close(fd) == 0);
    char error[160];
    Game g = new_game(191);
    g.quest_started = true;
    g.chests = 3;
    g.heroes[0].hp = 7;
    CHECK(game_save(&g, path, error, sizeof(error)));
    Game restored;
    game_init(&restored, 7);
    CHECK(game_load(&restored, path, error, sizeof(error)));
    CHECK(restored.screen == EXPLORE && restored.map.area == TOWN);
    CHECK(restored.x == g.x && restored.y == g.y);
    CHECK(restored.chests == 3 && restored.quest_started);
    CHECK(restored.rng == g.rng && restored.heroes[0].hp == 7);
    CHECK(restored.heroes[0].max_hp == 42);
    game_load_map(&g, WORLD);
    g.x = 44; g.y = 18;
    CHECK(game_walkable(&g, g.x, g.y));
    CHECK(game_save(&g, path, error, sizeof(error)));
    CHECK(game_load(&restored, path, error, sizeof(error)));
    CHECK(restored.map.area == WORLD && restored.x == 44 && restored.y == 18);
    CHECK(memcmp(&restored.map, &g.map, sizeof(g.map)) == 0);
    g.x = 31; g.y = 35;
    CHECK(game_walkable(&g, g.x, g.y));
    CHECK(game_save(&g, path, error, sizeof(error)));
    CHECK(game_load(&restored, path, error, sizeof(error)));
    CHECK(restored.x == 31 && restored.y == 35);
    g.screen = BATTLE;
    CHECK(!game_save(&g, path, error, sizeof(error)));
    CHECK(game_load(&restored, path, error, sizeof(error)));
    const char *invalid[] = {
        "", "WRONG 1\n", "EMBERWARD 99\n", "EMBERWARD 1\n0 9 19\n",
        "EMBERWARD 1\n9 11 14 0 60 5 2 0 191 1 0 0\n",
        "EMBERWARD 1\n1 11 14 0 -1 5 2 0 191 1 0 0\n",
        "EMBERWARD 1\n1 11 14 0 60 5 2 0 0 1 0 0\n",
        "EMBERWARD 1\n1 11 14 0 60 5 2 0 191 1 0 0\n1 0 999 8\n",
        "EMBERWARD 1\n1 0 0 0 60 5 2 0 191 1 0 0\n1 0 42 8\n1 0 32 10\n1 0 25 20\n1 0 30 22\n",
        "EMBERWARD 1\n1 11 14 0 60 5 2 0 191 1 0 0\n1 0 0 8\n1 0 0 10\n1 0 0 20\n1 0 0 22\n",
        "EMBERWARD 1\n1 11 14 0 4294967356 5 2 0 191 1 0 0\n1 0 42 8\n1 0 32 10\n1 0 25 20\n1 0 30 22\n",
        "EMBERWARD 1\n1 11 14 0 60 5 2 0 -4294967105 1 0 0\n1 0 42 8\n1 0 32 10\n1 0 25 20\n1 0 30 22\n",
        "EMBERWARD 1\n1 11 14 0 99999999999999999999999999999999999999 5 2 0 191 1 0 0\n",
        "EMBERWARD 1\n1 11 14 0 60 5 2 0 191 1 0 0\n1 0 42 8\n1 0 32 10\n1 0 25 20\n1 0 30 22\nGARBAGE"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        Game before = restored;
        write_save(path, invalid[i]);
        CHECK(!game_load(&restored, path, error, sizeof(error)));
        CHECK(memcmp(&restored, &before, sizeof(before)) == 0);
        CHECK(error[0] != '\0');
    }
    CHECK(unlink(path) == 0);
    CHECK(!game_load(&restored, path, error, sizeof(error)));
    g.screen = EXPLORE;
    CHECK(!game_save(&g, "/nonexistent/emberward/journey.sav", error, sizeof(error)));
}

int main(void)
{
    test_new_game_and_maps();
    test_map_reachability();
    test_world_terrain();
    test_exploration();
    test_npcs_and_items();
    test_battle_commands();
    test_battle_cues();
    test_battle_scenes();
    test_combat_and_campaign();
    test_walkthrough();
    test_save_load();
    printf("PASS: %d checks; 25 seeded campaigns, combat, maps, items, quests, and saves.\n", checks);
    return EXIT_SUCCESS;
}
