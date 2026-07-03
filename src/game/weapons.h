#pragma once

#include "game/game_state.h"
#include "game/map.h"

void weapon_fire(GameState& s, const Map& map, int shooter);
void rockets_tick(GameState& s, const Map& map, float dt);
void explode_rocket(GameState& s, const Map& map, int rocket_index);
int find_player_ray_hit(const GameState& s, Vec3 origin, Vec3 dir, float max_t,
                        int exclude, float* t_out);
void damage_player(GameState& s, int victim, int attacker, float amount, Vec3 knockback);

// Deterministic pellet direction for a shotgun blast, shared by the server sim
// and the client tracer renderer so the visual spread matches the shots fired.
Vec3 shotgun_pellet_dir(Vec3 base, Vec3 right, int pellet);
