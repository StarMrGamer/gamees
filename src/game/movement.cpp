#include "game/movement.h"

#include "game/collision.h"
#include "game/tuning.h"

#include <cmath>

static Vec3 horizontal(Vec3 v) {
  return {v.x, 0.0f, v.z};
}

static void apply_friction(Vec3& vel, float amount, float dt) {
  Vec3 h = horizontal(vel);
  float speed = vec3_length(h);
  if (speed <= 0.0001f) return;
  float drop = speed * amount * dt;
  float next = speed - drop;
  if (next < 0.0f) next = 0.0f;
  float scale = next / speed;
  vel.x *= scale;
  vel.z *= scale;
}

static void accelerate(Vec3& vel, Vec3 wish, float speed_cap, float accel_speed, float accel, float dt) {
  if (speed_cap <= 0.0f || accel_speed <= 0.0f) return;
  float current = vec3_dot(horizontal(vel), wish);
  float add = speed_cap - current;
  if (add <= 0.0f) return;
  float amount = accel * dt * accel_speed;
  if (amount > add) amount = add;
  vel.x += wish.x * amount;
  vel.z += wish.z * amount;
}

static bool spend_stamina(Player& p) {
  if (p.stamina <= 0) return false;
  p.stamina -= 1;
  if (p.stamina < MAX_STAMINA && p.stamina_recharge_timer <= 0.0f) {
    p.stamina_recharge_timer = STAMINA_RECHARGE_TIME;
  }
  return true;
}

static void update_stamina(Player& p, float dt) {
  if (p.stamina > MAX_STAMINA) p.stamina = MAX_STAMINA;
  if (p.stamina < 0) p.stamina = 0;
  if (!p.on_ground || p.stamina >= MAX_STAMINA) {
    if (p.stamina >= MAX_STAMINA) p.stamina_recharge_timer = 0.0f;
    return;
  }

  if (p.stamina_recharge_timer <= 0.0f) p.stamina_recharge_timer = STAMINA_RECHARGE_TIME;
  p.stamina_recharge_timer -= dt;
  while (p.stamina_recharge_timer <= 0.0f && p.stamina < MAX_STAMINA) {
    p.stamina += 1;
    if (p.stamina < MAX_STAMINA) {
      p.stamina_recharge_timer += STAMINA_RECHARGE_TIME;
    } else {
      p.stamina_recharge_timer = 0.0f;
    }
  }
}

static Vec3 wall_probe_normal(const Map& map, Vec3 pos, bool crouching) {
  constexpr float probe = 0.08f;
  Vec3 p = pos;
  p.x += probe;
  if (map_box_overlap(map, player_aabb(p, crouching))) return {-1.0f, 0.0f, 0.0f};
  p = pos;
  p.x -= probe;
  if (map_box_overlap(map, player_aabb(p, crouching))) return {1.0f, 0.0f, 0.0f};
  p = pos;
  p.z += probe;
  if (map_box_overlap(map, player_aabb(p, crouching))) return {0.0f, 0.0f, -1.0f};
  p = pos;
  p.z -= probe;
  if (map_box_overlap(map, player_aabb(p, crouching))) return {0.0f, 0.0f, 1.0f};
  return {0.0f, 0.0f, 0.0f};
}

void player_move(Player& p, const PlayerInput& in, const Map& map, float dt) {
  p.move_sound = 0;
  p.yaw = in.yaw;
  p.pitch = clampf(in.pitch, -1.5f, 1.5f);

  if (p.dash_cooldown > 0.0f) p.dash_cooldown -= dt;
  if (p.slide_time > 0.0f) p.slide_time -= dt;
  if (p.jump_buffer > 0.0f) p.jump_buffer -= dt;
  if (p.air_jump_buffer > 0.0f) p.air_jump_buffer -= dt;
  if (p.wall_contact_time > 0.0f) p.wall_contact_time -= dt;
  if (p.wall_jump_cooldown > 0.0f) p.wall_jump_cooldown -= dt;
  if (p.dash_air_control_time > 0.0f) p.dash_air_control_time -= dt;
  if (p.wall_contact_time <= 0.0f) p.wall_normal = {0.0f, 0.0f, 0.0f};
  update_stamina(p, dt);
  float class_speed = player_class_speed_scale(p.player_class);
  float ground_max_speed = GROUND_MAX_SPEED * class_speed;
  float ground_accel = GROUND_ACCEL * class_speed;
  float air_accel = AIR_ACCEL * class_speed;
  float air_wish_cap = AIR_WISH_CAP * class_speed;

  bool want_crouch = (in.buttons & BTN_CROUCH) != 0;
  if (want_crouch) {
    p.crouching = true;
  } else if (!map_box_overlap(map, player_aabb(p.pos, false))) {
    p.crouching = false;
  }

  Vec3 fwd = {std::sin(p.yaw), 0.0f, -std::cos(p.yaw)};
  Vec3 right = angles_right(p.yaw);
  Vec3 wish{};
  if (in.buttons & BTN_FORWARD) wish += fwd;
  if (in.buttons & BTN_BACK) wish -= fwd;
  if (in.buttons & BTN_RIGHT) wish += right;
  if (in.buttons & BTN_LEFT) wish -= right;
  wish = vec3_normalize(wish);

  bool jump_down = (in.buttons & BTN_JUMP) != 0;
  if (jump_down && !p.jump_held) p.jump_buffer = JUMP_BUFFER_TIME;
  p.jump_held = jump_down;
  // Double jump has its own buffer fed by BTN_AIRJUMP; by default the client
  // mirrors the jump button onto it, so behaviour is unchanged unless rebound.
  bool air_jump_down = (in.buttons & BTN_AIRJUMP) != 0;
  if (air_jump_down && !p.air_jump_held) p.air_jump_buffer = JUMP_BUFFER_TIME;
  p.air_jump_held = air_jump_down;
  bool dash_down = (in.buttons & BTN_DASH) != 0;
  bool dash_pressed = dash_down && !p.dash_held;
  p.dash_held = dash_down;

  bool queued_jump = p.on_ground && p.jump_buffer > 0.0f;
  bool slide_jump = queued_jump && p.sliding;
  float speed = vec3_length(horizontal(p.vel));
  // Slide re-arms only after leaving the ground or releasing crouch, so holding
  // crouch past the slide's end cannot chain +SLIDE_BOOST forever.
  if (!want_crouch || !p.on_ground) p.slide_suppressed = false;
  if (want_crouch && p.on_ground && speed > SLIDE_TRIGGER_SPEED && !p.sliding &&
      !p.slide_suppressed) {
    p.sliding = true;
    p.slide_suppressed = true;
    p.slide_time = SLIDE_DURATION;
    Vec3 dir = vec3_normalize(horizontal(p.vel));
    p.vel += dir * SLIDE_BOOST;
    if (p.move_sound == 0) p.move_sound = SND_SLIDE;
  }
  if (!want_crouch || p.slide_time <= 0.0f || !p.on_ground) {
    p.sliding = false;
  }

  if (p.on_ground) {
    if (!queued_jump) {
      float friction = GROUND_FRICTION;
      if (p.sliding) {
        float slide_elapsed = SLIDE_DURATION - clampf(p.slide_time, 0.0f, SLIDE_DURATION);
        float slide_t = clampf(slide_elapsed / SLIDE_DURATION, 0.0f, 1.0f);
        friction = lerp(SLIDE_FRICTION, GROUND_FRICTION, slide_t);
      }
      apply_friction(p.vel, friction, dt);
    }
    accelerate(p.vel, wish, ground_max_speed, ground_max_speed, ground_accel, dt);
  } else {
    float air_mult = p.dash_air_control_time > 0.0f ? DASH_JUMP_AIR_CONTROL_MULT : 1.0f;
    accelerate(p.vel, wish, air_wish_cap * air_mult, ground_max_speed, air_accel * air_mult, dt);
  }

  if (dash_pressed && p.dash_cooldown <= 0.0f && spend_stamina(p)) {
    Vec3 dir = vec3_length(wish) > 0.0f ? wish : fwd;
    p.vel += dir * (DASH_IMPULSE * player_class_dash_impulse_scale(p.player_class));
    p.dash_cooldown = DASH_COOLDOWN * player_class_dash_cooldown_scale(p.player_class);
    if (jump_down) p.dash_air_control_time = DASH_JUMP_AIR_CONTROL_TIME;
    if (p.move_sound == 0) p.move_sound = SND_DASH;
  }

  if (p.on_ground && p.jump_buffer > 0.0f) {
    if (slide_jump) {
      Vec3 dir = vec3_normalize(horizontal(p.vel));
      if (vec3_length(dir) > 0.0f) p.vel += dir * SLIDE_JUMP_BOOST;
    }
    p.vel.y = JUMP_VELOCITY;
    p.on_ground = false;
    p.sliding = false;
    p.air_jump_used = false;
    p.jump_buffer = 0.0f;
    p.air_jump_buffer = 0.0f;
    if (p.move_sound == 0) p.move_sound = SND_JUMP;
  }

  if (!p.on_ground) {
    p.vel.y -= GRAVITY * dt;
  } else if (p.vel.y < 0.0f) {
    p.vel.y = 0.0f;
  }

  bool was_airborne = !p.on_ground;
  float fall_speed = -p.vel.y;
  MoveResult mr = move_slide(map, p.pos, p.vel, p.crouching, dt);
  p.pos = mr.pos;
  p.vel = mr.vel;
  p.on_ground = mr.on_ground;
  if (p.on_ground) {
    p.wall_contact_time = 0.0f;
    p.wall_normal = {0.0f, 0.0f, 0.0f};
    p.air_jump_used = false;
    if (was_airborne && fall_speed > LAND_SOUND_MIN_FALL_SPEED && p.move_sound == 0) {
      p.move_sound = SND_LAND;
    }
  } else {
    Vec3 wall = vec3_length(mr.wall_normal) > 0.0f ? mr.wall_normal : wall_probe_normal(map, p.pos, p.crouching);
    if (vec3_length(wall) > 0.0f) {
      p.wall_normal = wall;
      p.wall_contact_time = WALL_CONTACT_GRACE;
    }
    // Wall jump stays on the primary jump button (it is contextual to touching
    // a wall); the plain mid-air double jump lives on the separate airjump
    // buffer so it can be rebound independently.
    if (p.jump_buffer > 0.0f && p.wall_contact_time > 0.0f && p.wall_jump_cooldown <= 0.0f) {
      Vec3 normal = vec3_normalize(p.wall_normal);
      Vec3 h = horizontal(p.vel);
      float into_wall = vec3_dot(h, -normal);
      if (into_wall > 0.0f) h += normal * into_wall;
      p.vel = h + normal * WALL_JUMP_PUSH;
      if (vec3_length(wish) > 0.0f) p.vel += wish * WALL_JUMP_WISH_BOOST;
      p.vel.y = WALL_JUMP_UP_VELOCITY;
      p.jump_buffer = 0.0f;
      p.air_jump_buffer = 0.0f;
      p.wall_jump_cooldown = WALL_JUMP_COOLDOWN;
      p.wall_contact_time = 0.0f;
      p.wall_normal = {0.0f, 0.0f, 0.0f};
      if (p.move_sound == 0) p.move_sound = SND_JUMP;
    } else if (p.air_jump_buffer > 0.0f && !p.air_jump_used && spend_stamina(p)) {
      p.vel.y = DOUBLE_JUMP_VELOCITY;
      p.air_jump_used = true;
      p.air_jump_buffer = 0.0f;
      p.jump_buffer = 0.0f;
      if (p.move_sound == 0) p.move_sound = SND_JUMP;
    }
  }

  // Void layer: below the map the player is returned to the nearest spawn with
  // all momentum cancelled. Shared by prediction and server so both agree.
  if (p.pos.y < map.void_y) {
    int best = -1;
    float best_dist = 1e30f;
    for (int i = 0; i < map.spawn_count; ++i) {
      float dx = map.spawns[i].x - p.pos.x;
      float dz = map.spawns[i].z - p.pos.z;
      float d = dx * dx + dz * dz;
      if (d < best_dist) { best_dist = d; best = i; }
    }
    if (best >= 0) {
      p.pos = map.spawns[best];
      p.vel = {0.0f, 0.0f, 0.0f};
      p.on_ground = false;
      p.sliding = false;
      p.wall_contact_time = 0.0f;
      p.wall_normal = {0.0f, 0.0f, 0.0f};
      p.dash_air_control_time = 0.0f;
      p.move_sound = SND_RESPAWN;
    } else {
      p.pos.y = map.void_y + 5.0f;
      p.vel = {0.0f, 0.0f, 0.0f};
    }
  }
}
