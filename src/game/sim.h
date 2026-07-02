#pragma once

#include "core/rng.h"
#include "game/game_state.h"
#include "game/map.h"

void game_init(GameState& s, const Map& map, int frag_limit);
int game_player_join(GameState& s, const Map& map, const char* name);
void game_player_leave(GameState& s, int player_index);
void game_set_player_class(Player& p, uint8_t player_class);
void game_tick(GameState& s, const Map& map, const PlayerInput inputs[MAX_PLAYERS], Rng& rng);
void push_event(GameState& s, uint8_t type, uint8_t a, uint8_t b, Vec3 pos);
Vec3 pick_spawn(const GameState& s, const Map& map, Rng& rng, float* yaw_out);
