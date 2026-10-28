#ifndef EMBERWARD_GAME_H
#define EMBERWARD_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAP_W 56
#define MAP_H 42
#define PARTY_SIZE 4
#define MAX_ENEMIES 3
#define MAX_MESSAGES 12

typedef enum {
    TILE_WATER, TILE_GRASS, TILE_FOREST, TILE_MOUNTAIN, TILE_ROAD,
    TILE_TOWN, TILE_RUINS, TILE_FLOOR, TILE_WALL, TILE_ROOF,
    TILE_EXIT, TILE_CHEST, TILE_FOUNTAIN, TILE_NPC, TILE_BOSS
} Tile;

typedef enum { WORLD, TOWN, RUINS } Area;
typedef enum { TITLE, EXPLORE, DIALOG, MENU, BATTLE, GAME_OVER, ENDING } Screen;
typedef enum { UP, DOWN, LEFT, RIGHT, ACCEPT, BACK, OPEN_MENU } Input;
typedef enum { CHOOSE_COMMAND, CHOOSE_TARGET, RESOLVING, BATTLE_WON } BattlePhase;
typedef enum { ATTACK, SKILL, POTION, ESCAPE } Command;
typedef enum { SCENE_GRASS, SCENE_FOREST, SCENE_ROAD, SCENE_SHORE, SCENE_VAULT } BattleScene;
typedef enum { SFX_NONE, SFX_STEP, SFX_CONFIRM, SFX_HIT, SFX_MAGIC,
               SFX_WIN, SFX_HURT, SFX_CHEST } Sound;

typedef struct {
    const char *name;
    const char *job;
    int level, xp, hp, max_hp, mp, max_mp, attack, defense, speed;
} Hero;

typedef struct {
    int kind, hp, max_hp, attack, defense, speed, xp, gold;
} Enemy;

typedef struct {
    Command command;
    int target;
} Action;

typedef struct {
    Area area;
    int width, height;
    Tile tiles[MAP_H][MAP_W];
} Map;

typedef struct {
    Enemy enemies[MAX_ENEMIES];
    int count, actor, cursor, target;
    BattlePhase phase;
    Action actions[PARTY_SIZE];
    int order[PARTY_SIZE + MAX_ENEMIES];
    int order_count, turn;
    bool boss;
    BattleScene scene;
    float timer, windup, flash;
    int acting;
    unsigned flash_targets;
    bool healing;
    char log[100];
    int reward_xp, reward_gold;
} Battle;

typedef struct {
    Screen screen, return_screen;
    Map map;
    Hero heroes[PARTY_SIZE];
    Battle battle;
    int x, y, facing, steps, gold, potions, ethers;
    int menu_cursor, title_cursor;
    unsigned chests;
    bool quest_started, boss_defeated, quest_complete, quit;
    uint32_t rng;
    float time, move_cooldown;
    char messages[MAX_MESSAGES][160];
    int message_count, message_index;
    Sound sound;
} Game;

void game_init(Game *game, uint32_t seed);
void game_new(Game *game);
void game_input(Game *game, Input input);
void game_update(Game *game, float dt);
void game_load_map(Game *game, Area area);
void game_start_battle(Game *game, bool boss);
void game_message(Game *game, const char *text);
void game_notify(Game *game, const char *text);
bool game_walkable(const Game *game, int x, int y);
bool game_save(const Game *game, const char *path, char *error, size_t error_size);
bool game_load(Game *game, const char *path, char *error, size_t error_size);
const char *enemy_name(int kind);
const char *hero_skill(int hero);
int hero_skill_cost(int hero);
int hero_next_xp(const Hero *hero);
uint32_t game_random(Game *game, uint32_t limit);

#endif
