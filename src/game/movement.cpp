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

void player_move(Player& p, const PlayerInput& in, const Map& map, float dt) {
  p.yaw = in.yaw;
  p.pitch = clampf(in.pitch, -1.5f, 1.5f);

  if (p.dash_cooldown > 0.0f) p.dash_cooldown -= dt;
  if (p.slide_time > 0.0f) p.slide_time -= dt;
  if (p.jump_buffer > 0.0f) p.jump_buffer -= dt;

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

  bool queued_jump = p.on_ground && p.jump_buffer > 0.0f;
  float speed = vec3_length(horizontal(p.vel));
  if (want_crouch && p.on_ground && speed > SLIDE_TRIGGER_SPEED && !p.sliding) {
    p.sliding = true;
    p.slide_time = SLIDE_DURATION;
    Vec3 dir = vec3_normalize(horizontal(p.vel));
    p.vel += dir * SLIDE_BOOST;
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
    accelerate(p.vel, wish, GROUND_MAX_SPEED, GROUND_MAX_SPEED, GROUND_ACCEL, dt);
  } else {
    accelerate(p.vel, wish, AIR_WISH_CAP, GROUND_MAX_SPEED, AIR_ACCEL, dt);
  }

  if ((in.buttons & BTN_DASH) && p.dash_cooldown <= 0.0f) {
    Vec3 dir = vec3_length(wish) > 0.0f ? wish : fwd;
    p.vel += dir * DASH_IMPULSE;
    p.dash_cooldown = DASH_COOLDOWN;
  }

  if (p.on_ground && p.jump_buffer > 0.0f) {
    p.vel.y = JUMP_VELOCITY;
    p.on_ground = false;
    p.jump_buffer = 0.0f;
  }

  if (!p.on_ground) {
    p.vel.y -= GRAVITY * dt;
  } else if (p.vel.y < 0.0f) {
    p.vel.y = 0.0f;
  }

  MoveResult mr = move_slide(map, p.pos, p.vel, p.crouching, dt);
  p.pos = mr.pos;
  p.vel = mr.vel;
  p.on_ground = mr.on_ground;
}
