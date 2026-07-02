#include "game/weapons.h"

#include "game/collision.h"
#include "game/sim.h"
#include "game/tuning.h"

#include <cmath>

static Vec3 player_center(const Player& p) {
  float h = p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
  return p.pos + Vec3{0.0f, h * 0.5f, 0.0f};
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
  v.health -= amount;
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
  float eye_h = p.crouching ? CROUCH_EYE_HEIGHT : EYE_HEIGHT;
  Vec3 origin = p.pos + Vec3{0.0f, eye_h, 0.0f};

  if (p.weapon == WEAPON_ROCKET) {
    for (int i = 0; i < MAX_ROCKETS; ++i) {
      Rocket& r = s.rockets[i];
      if (r.active) continue;
      r.active = true;
      r.owner = static_cast<uint8_t>(shooter);
      r.pos = origin + dir * 0.65f;
      r.vel = dir * ROCKET_SPEED;
      r.life = ROCKET_LIFETIME;
      p.fire_cooldown = ROCKET_INTERVAL;
      push_event(s, EV_SOUND, SND_ROCKET_LAUNCH, static_cast<uint8_t>(shooter), origin);
      return;
    }
  } else {
    p.fire_cooldown = RIFLE_INTERVAL;
    float wall_t = ray_map(map, origin, dir, RIFLE_RANGE);
    float hit_t = RIFLE_RANGE;
    int hit = find_player_ray_hit(s, origin, dir, wall_t, shooter, &hit_t);
    if (hit >= 0) {
      damage_player(s, hit, shooter, RIFLE_DAMAGE, dir * RIFLE_KNOCKBACK);
    }
    push_event(s, EV_SOUND, SND_RIFLE, static_cast<uint8_t>(shooter), origin);
  }
}

void explode_rocket(GameState& s, const Map&, int rocket_index) {
  if (rocket_index < 0 || rocket_index >= MAX_ROCKETS) return;
  Rocket& r = s.rockets[rocket_index];
  if (!r.active) return;
  Vec3 pos = r.pos;
  int owner = r.owner;
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
    float damage = ROCKET_DIRECT_DAMAGE * frac;
    Vec3 knockback = dir * (ROCKET_KNOCKBACK * frac);
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
    int hit = find_player_ray_hit(s, r.pos, dir, wall_t, -1, &player_t);
    if (wall_t < dist || hit >= 0) {
      float t = hit >= 0 ? player_t : wall_t;
      r.pos += dir * t;
      explode_rocket(s, map, i);
    } else {
      r.pos += step;
    }
  }
}
