#pragma once

#include "game/game_state.h"
#include "game/map.h"

void weapon_fire(GameState& s, const Map& map, int shooter);
void rockets_tick(GameState& s, const Map& map, float dt);
void explode_rocket(GameState& s, const Map& map, int rocket_index);
int find_player_ray_hit(const GameState& s, Vec3 origin, Vec3 dir, float max_t,
                        int exclude, float* t_out);
void damage_player(GameState& s, int victim, int attacker, float amount, Vec3 knockback);
