#include "game/weapons.h"

#include "game/collision.h"
#include "game/sim.h"
#include "game/tuning.h"

#include <cmath>

static Vec3 player_center(const Player& p) {
  float h = p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
  return p.pos + Vec3{0.0f, h * 0.5f, 0.0f};
}

Vec3 player_eye_pos(const Player& p) {
  float eye_h = p.crouching ? CROUCH_EYE_HEIGHT : EYE_HEIGHT;
  return p.pos + Vec3{0.0f, eye_h, 0.0f};
}

Vec3 weapon_muzzle_pos(const Player& p) {
  Vec3 view_dir = vec3_normalize(angles_forward(p.yaw, p.pitch));
  Vec3 right = angles_right(p.yaw);
  return player_eye_pos(p) + view_dir * WEAPON_MUZZLE_FORWARD +
         right * WEAPON_MUZZLE_RIGHT + Vec3{0.0f, -WEAPON_MUZZLE_DOWN, 0.0f};
}

// The shot leaves the muzzle (offset right/down from the eye) but is aimed to
// pass through whatever the eye's centre ray hits, so shots land on the
// crosshair rather than parallel-offset from it. Used for both the authoritative
// hitscan and the client's tracer, so the two always agree.
Vec3 weapon_converged_dir(const GameState& s, const Map& map, int shooter,
                          Vec3 eye, Vec3 view_dir, Vec3 muzzle, float range) {
  float wall_t = ray_map(map, eye, view_dir, range);
  float hit_t = range;
  int hit = find_player_ray_hit(s, eye, view_dir, wall_t, shooter, &hit_t);
  float target_t = hit >= 0 ? hit_t : wall_t;
  Vec3 target = eye + view_dir * target_t;
  Vec3 dir = vec3_normalize(target - muzzle);
  return vec3_length(dir) > 0.0f ? dir : view_dir;
}

Vec3 shotgun_pellet_dir(Vec3 base, Vec3 right, int pellet) {
  static const float offsets[SHOTGUN_PELLETS][2] = {
    {0.0f, 0.0f},
    {-1.0f, 0.0f},
    {1.0f, 0.0f},
    {0.0f, 1.0f},
    {0.0f, -1.0f},
    {-0.72f, 0.72f},
    {0.72f, -0.72f},
  };
  Vec3 up{0.0f, 1.0f, 0.0f};
  int i = pellet % SHOTGUN_PELLETS;
  return vec3_normalize(base + right * (offsets[i][0] * SHOTGUN_SPREAD) +
                        up * (offsets[i][1] * SHOTGUN_SPREAD));
}

static float distance_falloff(float dist, float start, float end, float min_frac) {
  if (min_frac >= 1.0f || end <= start || dist <= start) return 1.0f;
  float t = clampf((dist - start) / (end - start), 0.0f, 1.0f);
  return lerp(1.0f, min_frac, t);
}

static void fire_hitscan(GameState& s, const Map& map, int shooter, Vec3 origin, Vec3 shot_dir,
                         float range, float damage, float knockback,
                         float falloff_start, float falloff_end, float min_frac) {
  float wall_t = ray_map(map, origin, shot_dir, range);
  float hit_t = range;
  int hit = find_player_ray_hit(s, origin, shot_dir, wall_t, shooter, &hit_t);
  if (hit >= 0) {
    float scale = distance_falloff(hit_t, falloff_start, falloff_end, min_frac);
    damage_player(s, hit, shooter, damage * scale, shot_dir * (knockback * scale));
  }
}

int find_player_ray_hit(const GameState& s, Vec3 origin, Vec3 dir, float max_t,
                        int exclude, float* t_out) {
  int best = -1;
  float best_t = max_t;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = s.players[i];
    if (!p.active || !p.alive || i == exclude) continue;
    float t = max_t;
    if (ray_aabb(origin, dir, player_aabb(p.pos, p.crouching), best_t, &t) && t < best_t) {
      best_t = t;
      best = i;
    }
  }
  if (best >= 0 && t_out) *t_out = best_t;
  return best;
}

void damage_player(GameState& s, int victim, int attacker, float amount, Vec3 knockback) {
  if (victim < 0 || victim >= MAX_PLAYERS) return;
  Player& v = s.players[victim];
  if (!v.active || !v.alive) return;
  v.vel += knockback;
  v.health -= amount * player_class_damage_taken_scale(v.player_class);
  if (attacker >= 0 && attacker < MAX_PLAYERS) {
    push_event(s, EV_HIT, static_cast<uint8_t>(attacker), static_cast<uint8_t>(victim), player_center(v));
  }
  if (v.health > 0.0f) {
    push_event(s, EV_SOUND, SND_HURT, static_cast<uint8_t>(victim), player_center(v));
    return;
  }

  v.health = 0.0f;
  v.alive = false;
  v.respawn_timer = PLAYER_RESPAWN_TIME;
  v.vel = {0, 0, 0};

  if (attacker >= 0 && attacker < MAX_PLAYERS && attacker != victim && s.players[attacker].active) {
    s.players[attacker].frags += 1;
  } else {
    v.frags -= 1;
  }
  push_event(s, EV_KILL, static_cast<uint8_t>(attacker < 0 ? victim : attacker),
             static_cast<uint8_t>(victim), player_center(v));
  push_event(s, EV_SOUND, SND_DEATH, static_cast<uint8_t>(victim), player_center(v));
}

void weapon_fire(GameState& s, const Map& map, int shooter) {
  if (shooter < 0 || shooter >= MAX_PLAYERS) return;
  Player& p = s.players[shooter];
  if (!p.active || !p.alive || p.fire_cooldown > 0.0f) return;

  Vec3 dir = vec3_normalize(angles_forward(p.yaw, p.pitch));
  Vec3 eye = player_eye_pos(p);
  Vec3 origin = weapon_muzzle_pos(p);
  float aim_range = p.weapon == WEAPON_SHOTGUN ? SHOTGUN_RANGE :
                    (p.weapon == WEAPON_LMG ? LMG_RANGE : RIFLE_RANGE);
  Vec3 shot_dir = weapon_converged_dir(s, map, shooter, eye, dir, origin, aim_range);

  if (p.weapon == WEAPON_ROCKET) {
    for (int i = 0; i < MAX_ROCKETS; ++i) {
      Rocket& r = s.rockets[i];
      if (r.active) continue;
      r.active = true;
      r.owner = static_cast<uint8_t>(shooter);
      r.pos = origin;
      r.vel = shot_dir * ROCKET_SPEED;
      r.life = ROCKET_LIFETIME;
      p.fire_cooldown = ROCKET_INTERVAL;
      push_event(s, EV_SOUND, SND_ROCKET_LAUNCH, static_cast<uint8_t>(shooter), origin);
      return;
    }
  } else if (p.weapon == WEAPON_SHOTGUN) {
    p.fire_cooldown = SHOTGUN_INTERVAL;
    Vec3 right = angles_right(p.yaw);
    float pellet_damage = SHOTGUN_DAMAGE * player_class_damage_scale(p.player_class);
    for (int pellet = 0; pellet < SHOTGUN_PELLETS; ++pellet) {
      fire_hitscan(s, map, shooter, origin, shotgun_pellet_dir(shot_dir, right, pellet),
                   SHOTGUN_RANGE, pellet_damage, SHOTGUN_KNOCKBACK,
                   SHOTGUN_FALLOFF_START, SHOTGUN_FALLOFF_END, SHOTGUN_MIN_DAMAGE_FRAC);
    }
    push_event(s, EV_SOUND, SND_SHOTGUN, static_cast<uint8_t>(shooter), origin);
  } else if (p.weapon == WEAPON_LMG) {
    p.fire_cooldown = LMG_INTERVAL;
    fire_hitscan(s, map, shooter, origin, shot_dir, LMG_RANGE,
                 LMG_DAMAGE * player_class_damage_scale(p.player_class), LMG_KNOCKBACK,
                 0.0f, 1.0f, 1.0f);
    push_event(s, EV_SOUND, SND_LMG, static_cast<uint8_t>(shooter), origin);
  } else {
    p.fire_cooldown = RIFLE_INTERVAL;
    fire_hitscan(s, map, shooter, origin, shot_dir, RIFLE_RANGE,
                 RIFLE_DAMAGE * player_class_damage_scale(p.player_class), RIFLE_KNOCKBACK,
                 0.0f, 1.0f, 1.0f);
    push_event(s, EV_SOUND, SND_RIFLE, static_cast<uint8_t>(shooter), origin);
  }
}

void explode_rocket(GameState& s, const Map&, int rocket_index) {
  if (rocket_index < 0 || rocket_index >= MAX_ROCKETS) return;
  Rocket& r = s.rockets[rocket_index];
  if (!r.active) return;
  Vec3 pos = r.pos;
  int owner = r.owner;
  float damage_scale = 1.0f;
  if (owner >= 0 && owner < MAX_PLAYERS && s.players[owner].active) {
    damage_scale = player_class_damage_scale(s.players[owner].player_class);
  }
  r.active = false;

  push_event(s, EV_SOUND, SND_EXPLOSION, static_cast<uint8_t>(owner), pos);
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = s.players[i];
    if (!p.active || !p.alive) continue;
    Vec3 center = player_center(p);
    Vec3 delta = center - pos;
    float dist = vec3_length(delta);
    if (dist > ROCKET_SPLASH_RADIUS) continue;
    float frac = 1.0f - dist / ROCKET_SPLASH_RADIUS;
    Vec3 dir = dist > 0.001f ? delta / dist : Vec3{0.0f, 1.0f, 0.0f};
    float damage = ROCKET_DIRECT_DAMAGE * frac * damage_scale;
    Vec3 knockback = dir * (ROCKET_KNOCKBACK * frac);
    if (i == owner) {
      damage *= ROCKET_SELF_DAMAGE_SCALE;
      knockback = dir * (ROCKET_JUMP_KNOCKBACK * frac);
      float min_up = ROCKET_JUMP_MIN_UP * frac;
      if (knockback.y < min_up) knockback.y = min_up;
    }
    damage_player(s, i, owner, damage, knockback);
  }
}

void rockets_tick(GameState& s, const Map& map, float dt) {
  for (int i = 0; i < MAX_ROCKETS; ++i) {
    Rocket& r = s.rockets[i];
    if (!r.active) continue;
    r.life -= dt;
    if (r.life <= 0.0f) {
      explode_rocket(s, map, i);
      continue;
    }

    Vec3 step = r.vel * dt;
    float dist = vec3_length(step);
    Vec3 dir = dist > 0.0001f ? step / dist : Vec3{0, 0, -1};
    float wall_t = ray_map(map, r.pos, dir, dist);
    float player_t = dist;
    int hit = find_player_ray_hit(s, r.pos, dir, wall_t, r.owner, &player_t);
    if (wall_t < dist || hit >= 0) {
      float t = hit >= 0 ? player_t : wall_t;
      r.pos += dir * t;
      explode_rocket(s, map, i);
    } else {
      r.pos += step;
    }
  }
}
