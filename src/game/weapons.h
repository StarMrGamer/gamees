#pragma once

#include "game/game_state.h"
#include "game/history.h"
#include "game/map.h"

// `rewind`, when non-null, is a lag-compensation pose snapshot: remote players
// are hit-tested at the positions the shooter saw rather than their live ones.
// Rockets ignore it (projectiles are not rewound). Damage still lands on the
// live player state.
void weapon_fire(GameState& s, const Map& map, int shooter, const PlayerPose* rewind = nullptr);
void rockets_tick(GameState& s, const Map& map, float dt);
void explode_rocket(GameState& s, const Map& map, int rocket_index);
int find_player_ray_hit(const GameState& s, Vec3 origin, Vec3 dir, float max_t,
                        int exclude, float* t_out);
int find_player_ray_hit_poses(const PlayerPose poses[MAX_PLAYERS], Vec3 origin, Vec3 dir,
                              float max_t, int exclude, float* t_out);
void damage_player(GameState& s, int victim, int attacker, float amount, Vec3 knockback);

// Deterministic pellet direction for a shotgun blast, shared by the server sim
// and the client tracer renderer so the visual spread matches the shots fired.
Vec3 shotgun_pellet_dir(Vec3 base, Vec3 right, int pellet);

// Muzzle/eye geometry and crosshair convergence, shared so the client's tracers
// leave the same muzzle and aim at the same point as the authoritative shot.
Vec3 player_eye_pos(const Player& p);
Vec3 weapon_muzzle_pos(const Player& p);
Vec3 weapon_converged_dir(const GameState& s, const Map& map, int shooter,
                          Vec3 eye, Vec3 view_dir, Vec3 muzzle, float range,
                          const PlayerPose* rewind = nullptr);
