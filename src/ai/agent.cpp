#include "ai/agent.h"

#include "game/collision.h"
#include "game/tuning.h"
#include "game/weapons.h"

#include <cmath>
#include <cstring>

namespace {

// Where to aim on a body. The feet are the worst choice (a shot at the feet
// misses whenever the target steps up), so everything targets the centre.
Vec3 body_center(const Player& p) {
  float h = p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
  return p.pos + Vec3{0.0f, h * 0.5f, 0.0f};
}

bool can_see(const GameState& s, const Map& map, int from, Vec3 to) {
  Vec3 eye = player_eye_pos(s.players[from]);
  Vec3 d = to - eye;
  float dist = vec3_length(d);
  if (dist < 0.01f) return true;
  return ray_map(map, eye, d / dist, dist) >= dist - 0.15f;
}

// The preferred engagement range for the weapon in hand. The shotgun's damage
// falls off past SHOTGUN_FALLOFF_START, so a Scout that fights at rifle range
// is throwing away most of its damage; the rifle conversely has no reason to
// close and every reason not to.
float preferred_range(uint8_t weapon) {
  switch (weapon) {
    case WEAPON_SHOTGUN: return 5.0f;
    case WEAPON_LMG: return 13.0f;
    case WEAPON_ROCKET: return 16.0f;
    default: return 18.0f;
  }
}

float weapon_reach(uint8_t weapon) {
  switch (weapon) {
    case WEAPON_SHOTGUN: return SHOTGUN_FALLOFF_END;
    case WEAPON_LMG: return LMG_RANGE;
    case WEAPON_ROCKET: return 45.0f;
    default: return RIFLE_RANGE;
  }
}

// Is there still floor ahead along `dir`? de_dust2 has 191 places where the
// level's seal is genuinely absent, and a bot that walks off one hands over the
// tempo if not the frag. Probing beats trusting the map.
//
// The distance scales with speed because the bot bunny hops: at 12 m/s a fixed
// one-metre probe is a tenth of a second of warning, which it cannot act on.
bool footing_ahead(const Map& map, Vec3 pos, Vec3 vel, Vec3 dir) {
  float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
  // Half a second of travel. The movement tech pushes the bot past 20 m/s, and
  // the old 3.5 m cap was a sixth of a second of warning at that speed - it
  // fell out of de_dust2 thirty-three times in twelve matches.
  float lookahead = speed * 0.5f;
  if (lookahead < 2.0f) lookahead = 2.0f;
  if (lookahead > 10.0f) lookahead = 10.0f;
  // Sample along the way, not just the far end: a gap narrower than the probe
  // distance would otherwise be stepped straight over.
  const float max_drop = 6.0f;  // survivable; only a real pit should veto
  for (int i = 1; i <= 3; ++i) {
    Vec3 probe = pos + dir * (lookahead * static_cast<float>(i) / 3.0f);
    probe.y += 0.5f;
    if (ray_map(map, probe, {0.0f, -1.0f, 0.0f}, max_drop + 0.5f) > max_drop) return false;
  }
  return true;
}

// Is the way ahead open at chest height? Steering straight at a target on
// de_dust2 walks into a wall and stays there; this is what lets the bot slide
// around one instead.
bool wall_clear(const Map& map, Vec3 pos, Vec3 dir, float reach) {
  Vec3 chest = pos + Vec3{0.0f, PLAYER_HEIGHT * 0.55f, 0.0f};
  return ray_map(map, chest, dir, reach) >= reach;
}

// Picks a heading near `desired` to actually walk.
//
// The two constraints are not equal and treating them as if they were cost 78%
// of the bot's frags. Running into a wall is harmless - the collision code
// slides you along it - so openness is only a preference. Walking off the map
// is not recoverable, so footing is the one hard veto. Requiring both made
// every direction illegal in de_dust2's corridors and the bot simply stopped.
Vec3 steer(const Map& map, Vec3 pos, Vec3 vel, Vec3 desired, float bias) {
  if (vec3_length(desired) < 0.001f) return {0.0f, 0.0f, 0.0f};
  desired = vec3_normalize(desired);
  // Straight ahead is the common case; taking it early skips a fan of twenty
  // raycasts and is worth 3x on evaluation throughput.
  if (footing_ahead(map, pos, vel, desired) && wall_clear(map, pos, desired, 2.0f)) {
    return desired;
  }
  const float fan[] = {0.0f, 0.4f, 0.8f, 1.2f, 1.6f, 2.1f, 2.6f};
  Vec3 best{0.0f, 0.0f, 0.0f};
  float best_score = -1e30f;
  for (float offset : fan) {
    for (int side = 0; side < 2; ++side) {
      if (offset == 0.0f && side == 1) continue;
      float a = offset * (side == 0 ? bias : -bias);
      float c = std::cos(a), sn = std::sin(a);
      Vec3 cand{desired.x * c - desired.z * sn, 0.0f, desired.x * sn + desired.z * c};
      if (!footing_ahead(map, pos, vel, cand)) continue;
      const float probe = 3.0f;
      float open = ray_map(map, pos + Vec3{0.0f, PLAYER_HEIGHT * 0.55f, 0.0f}, cand, probe);
      // Staying on course is worth more than open space, so the bot only
      // swings wide when it is genuinely blocked.
      float score = open + std::cos(a) * 4.0f;
      if (score > best_score) {
        best_score = score;
        best = cand;
      }
    }
  }
  return best;
}

// Follows a navmesh route toward `goal`, returning the heading to walk. Falls
// back to pointing straight at the goal when there is no mesh or no route,
// which is exactly the behaviour this replaced.
//
// The route is replanned on a timer rather than every tick: an A* query is
// ~10 us and at 60 Hz the answer is the same one sixty times over.
Vec3 follow_route(const Map& map, AgentMemory& m, Vec3 from, Vec3 goal, float dt) {
  Vec3 direct = goal - from;
  direct.y = 0.0f;
  if (!map.nav.built) return direct;

  m.repath_timer -= dt;
  bool goal_moved = vec3_length(goal - m.path_goal) > 4.0f;
  if (!m.has_path || m.repath_timer <= 0.0f || goal_moved) {
    m.has_path = nav_find_path(map, from, goal, &m.path);
    m.path_index = 0;
    m.path_goal = goal;
    m.repath_timer = 0.5f;
  }
  if (!m.has_path || m.path.count <= 0) return direct;

  // Drop waypoints already reached. Horizontal distance only: a waypoint on
  // the floor you are standing on is reached even though its recorded height
  // is half a metre off.
  while (m.path_index < m.path.count) {
    Vec3 wp = map.nav.nodes[m.path.nodes[m.path_index]];
    Vec3 d = wp - from;
    d.y = 0.0f;
    if (vec3_length(d) > 2.0f || std::fabs(wp.y - from.y) > 2.5f) break;
    ++m.path_index;
  }
  if (m.path_index >= m.path.count) return direct;
  Vec3 wp = map.nav.nodes[m.path.nodes[m.path_index]];
  Vec3 d = wp - from;
  d.y = 0.0f;
  return d;
}

void aim_at(AgentMemory& m, Vec3 from, Vec3 to, float turn_rate, float dt) {
  Vec3 d = to - from;
  float flat = std::sqrt(d.x * d.x + d.z * d.z);
  float want_yaw = std::atan2(d.x, -d.z);
  float want_pitch = std::atan2(d.y, flat > 0.001f ? flat : 0.001f);
  if (!m.aim_init) {
    m.aim_yaw = want_yaw;
    m.aim_pitch = want_pitch;
    m.aim_init = true;
    return;
  }
  // Sweep toward the target at a bounded rate rather than snapping. The cap is
  // the whole difficulty dial: an uncapped bot is an aimbot and teaches a
  // learned policy nothing except that it cannot win.
  float dyaw = angle_wrap(want_yaw - m.aim_yaw);
  float dpitch = want_pitch - m.aim_pitch;
  float step = turn_rate * dt;
  float mag = std::sqrt(dyaw * dyaw + dpitch * dpitch);
  if (mag > step && mag > 0.0f) {
    dyaw *= step / mag;
    dpitch *= step / mag;
  }
  m.aim_yaw = angle_wrap(m.aim_yaw + dyaw);
  m.aim_pitch = clampf(m.aim_pitch + dpitch, -1.5f, 1.5f);
}

// Turns a world-space heading into the four movement bits, relative to where
// the bot is looking.
uint16_t move_buttons(Vec3 move, float yaw) {
  if (vec3_length(move) < 0.001f) return 0;
  move = vec3_normalize(move);
  Vec3 fwd{std::sin(yaw), 0.0f, -std::cos(yaw)};
  Vec3 right = angles_right(yaw);
  float f = move.x * fwd.x + move.z * fwd.z;
  float r = move.x * right.x + move.z * right.z;
  uint16_t b = 0;
  if (f > 0.35f) b |= BTN_FORWARD;
  if (f < -0.35f) b |= BTN_BACK;
  if (r > 0.35f) b |= BTN_RIGHT;
  if (r < -0.35f) b |= BTN_LEFT;
  return b;
}

// An edge-triggered button: the engine only acts on the press, so holding the
// bit does nothing after the first tick. Returns whether to set it now.
bool press(bool want, bool* held) {
  if (!want) {
    *held = false;
    return false;
  }
  if (*held) {
    *held = false;  // release for one tick so the next one is an edge again
    return false;
  }
  *held = true;
  return true;
}

// Everything the engine offers beyond walking: slide-jumps, dashes, wall jumps,
// double jumps and air strafing.
//
// The naive version of this - jump whenever you are on the ground and moving -
// is actively harmful, and measurably so: it left the bot airborne 90.6% of the
// time at an average 7.7 m/s, *slower* than the bot that just walked, because
// AIR_WISH_CAP is 1.0 and a player in the air can barely accelerate. Airborne
// time is only worth having if it is spent air strafing.
// Air strafing is deliberately absent, and that is a measured decision rather
// than an omission. This engine's air acceleration is capped on the wish
// direction's *projection* onto velocity (AIR_WISH_CAP, 1 m/s), which makes
// the only available gain perpendicular - it curves you. Over 20 s on flat
// ground: walking covers 159.9 m at 8.00 m/s; holding the wish perpendicular
// reaches 11.12 m/s average and 18 m/s peak but covers 42.4 m, because it is
// travelling in a circle; and real half-beat strafing, swept from 20 to 70
// degrees of yaw swing, peaked at 99.3 m - still worse than walking.
//
// There is no compounding here the way there is in the games this technique
// comes from, so a bot that air strafes arrives later than one that holds
// forward. Making it pay would mean changing the movement tuning, which is a
// gameplay decision for humans too, not a bot change.
//
// What does pay in this engine is impulses: dash (+12 m/s), slide (+2) and
// slide-jump (+3).
struct TechResult {
  uint16_t buttons;
};

// `journey` is how far away the ultimate destination is, not the next
// waypoint. Gating on the waypoint distance instead silently disabled air
// strafing everywhere the navmesh was in use, because its waypoints are
// NAV_CELL_XZ apart - three metres, never the twelve the gate wanted.
TechResult movement_tech(const Map& map, const Player& me, AgentMemory& m, Vec3 desired,
                         bool engaging) {
  TechResult out{0};
  Vec3 vel_h{me.vel.x, 0.0f, me.vel.z};
  float speed = vec3_length(vel_h);
  // Keep one charge in reserve while fighting: a dodge you cannot afford is
  // worth more than a metre per second of travel.
  const int reserve = engaging ? 1 : 0;

  if (!me.on_ground) {
    // A wall jump costs no stamina, keeps horizontal speed and adds height.
    // It is strictly the best thing to do while touching a wall.
    if (me.wall_contact_time > 0.0f && me.wall_jump_cooldown <= 0.0f) {
      if (press(true, &m.held_jump)) out.buttons |= BTN_JUMP;
    }
    // A double jump out of a fall: crossing a gap, or breaking someone's aim.
    bool falling = me.vel.y < -1.5f;
    bool want_air_jump = falling && !me.air_jump_used && me.stamina > reserve &&
                         (engaging ? me.health < player_class_max_health(me.player_class) * 0.5f
                                   : !footing_ahead(map, me.pos, me.vel, vec3_normalize(desired)));
    if (want_air_jump && vec3_length(desired) > 0.001f) {
      if (press(true, &m.held_airjump)) out.buttons |= BTN_AIRJUMP;
    }
    return out;
  }

  // On the ground. A slide costs nothing and adds SLIDE_BOOST, and jumping out
  // of one adds SLIDE_JUMP_BOOST on top - together worth 5 m/s, which is most
  // of a second of running. It needs SLIDE_TRIGGER_SPEED to start, which is
  // above GROUND_MAX_SPEED, so a dash is what gets the chain going.
  if (me.sliding) {
    out.buttons |= BTN_CROUCH;
    if (press(true, &m.held_jump)) out.buttons |= BTN_JUMP;
  } else if (speed > SLIDE_TRIGGER_SPEED) {
    out.buttons |= BTN_CROUCH;  // level-triggered, no edge needed
  } else if (speed > GROUND_MAX_SPEED * 0.6f) {
    // Too slow to slide: a plain hop at least preserves what speed there is.
    if (press(true, &m.held_jump)) out.buttons |= BTN_JUMP;
  }

  bool want_dash = me.dash_cooldown <= 0.0f && me.stamina > reserve &&
                   speed < SLIDE_TRIGGER_SPEED + 2.0f && vec3_length(desired) > 0.001f;
  if (want_dash && press(true, &m.held_dash)) out.buttons |= BTN_DASH;
  return out;
}

PlayerInput think_simple(AgentMemory& m, float dt) {
  m.clock += dt;
  PlayerInput in{};
  in.buttons = BTN_FORWARD | BTN_FIRE;
  if (static_cast<int>(m.clock) % 4 == 0) in.buttons |= BTN_JUMP;
  if (static_cast<int>(m.clock) % 7 == 0) in.buttons |= BTN_DASH;
  in.weapon_switch = static_cast<int>(m.clock) % 5 == 0 ? 2 : 1;
  in.yaw = std::sin(m.clock * 0.7f) * PI;
  in.pitch = 0.0f;
  return in;
}

}  // namespace

// Difficulty tiers.
//
// The numbers that matter for fairness, with a human for scale: visual
// reaction time is around 250 ms before the aim has moved at all, a flick is a
// burst not a sustained rate, and nobody has a 360-degree field of view. The
// original bot used 0.10 s, 9.0 rad/s (515 deg/s, sustained) and no FOV at
// all, which is an aimbot that cannot be flanked. It is kept as DEMON because
// it is a useful fixed opponent to measure against, not because it is fair.
AgentConfig agent_config(AgentSkill skill) {
  AgentConfig c;
  c.strafe_min = 0.35f;
  c.strafe_max = 0.95f;
  c.seek_health_below = 0.45f;
  switch (skill) {
    case SKILL_EASY:
      c.turn_rate = 2.2f;     // 126 deg/s
      c.reaction = 0.45f;
      c.aim_error = 0.165f;   // up to ~9 degrees off
      c.aim_drift_rate = 3.0f;
      c.fire_cone = 0.22f;
      c.fov = 1.05f;          // 120 degrees across
      break;
    case SKILL_NORMAL:
      c.turn_rate = 3.6f;     // 206 deg/s
      c.reaction = 0.30f;
      c.aim_error = 0.075f;   // up to ~4 degrees
      c.aim_drift_rate = 3.0f;
      c.fire_cone = 0.12f;
      c.fov = 1.22f;          // 140 degrees across
      break;
    case SKILL_HARD:
      c.turn_rate = 5.5f;     // 315 deg/s
      c.reaction = 0.22f;
      c.aim_error = 0.030f;
      c.aim_drift_rate = 3.0f;
      c.fire_cone = 0.075f;
      c.fov = 1.40f;          // 160 degrees across
      break;
    case SKILL_DEMON:
    default:
      c.turn_rate = 9.0f;
      c.reaction = 0.10f;
      c.aim_error = 0.010f;
      c.aim_drift_rate = 3.0f;
      c.fire_cone = 0.06f;
      c.fov = PI;             // sees everything, in every direction
      break;
  }
  return c;
}

const char* agent_skill_name(AgentSkill skill) {
  switch (skill) {
    case SKILL_EASY: return "easy";
    case SKILL_NORMAL: return "normal";
    case SKILL_HARD: return "hard";
    default: return "demon";
  }
}

bool agent_skill_parse(const char* name, AgentSkill* out) {
  if (!name || !out) return false;
  for (int i = 0; i < SKILL_COUNT; ++i) {
    AgentSkill s = static_cast<AgentSkill>(i);
    if (std::strcmp(name, agent_skill_name(s)) == 0) {
      *out = s;
      return true;
    }
  }
  return false;
}

AgentConfig agent_config_demon() { return agent_config(SKILL_DEMON); }

AgentConfig agent_config_demon_handicapped(float handicap) {
  handicap = clampf(handicap, 0.0f, 1.0f);
  AgentConfig c = agent_config_demon();
  c.turn_rate = lerp(c.turn_rate, 1.6f, handicap);
  c.reaction = lerp(c.reaction, 0.55f, handicap);
  c.aim_error = lerp(c.aim_error, 0.090f, handicap);
  c.fire_cone = lerp(c.fire_cone, 0.16f, handicap);
  c.fov = lerp(c.fov, 1.0f, handicap);
  return c;
}

void agent_reset(AgentMemory& m) {
  uint32_t seen = m.last_event_seen;  // events are global; do not re-read old ones
  m = AgentMemory{};
  m.target = -1;
  m.provoked_by = -1;
  m.strafe_sign = 1.0f;
  m.last_event_seen = seen;
}

PlayerInput agent_think(const GameState& s, const Map& map, int self, AgentKind kind,
                        const AgentConfig& cfg, AgentMemory& mem, Rng& rng, float dt) {
  if (kind == AGENT_SIMPLE) return think_simple(mem, dt);

  PlayerInput in{};
  const Player& me = s.players[self];
  if (!me.active || !me.alive) {
    // Dead: hold still so the respawn does not inherit a wall-hugging heading.
    agent_reset(mem);
    in.yaw = me.yaw;
    in.weapon_switch = 1;
    return in;
  }

  mem.clock += dt;
  Vec3 eye = player_eye_pos(me);

  // ---- pick a target ------------------------------------------------------
  // Visible enemies only, preferring the close and the wounded, with a bias
  // toward whoever is already being fought so the bot does not dither between
  // two equidistant targets and shoot neither.
  // Anything that has shot us recently stays "noticed" for a moment even if it
  // is outside the cone: getting hit tells a human roughly where it came from.
  mem.provoked_time -= dt;
  if (mem.provoked_time <= 0.0f) mem.provoked_by = -1;
  for (int e = 0; e < MAX_EVENTS; ++e) {
    const GameEvent& ev = s.events[e];
    if (ev.id == 0 || ev.id <= mem.last_event_seen) continue;
    if (ev.type == EV_HIT && ev.b == self && ev.a != self) {
      mem.provoked_by = ev.a;
      mem.provoked_time = 1.5f;
    }
  }
  for (int e = 0; e < MAX_EVENTS; ++e) {
    if (s.events[e].id > mem.last_event_seen) mem.last_event_seen = s.events[e].id;
  }

  const Vec3 look = angles_forward(me.yaw, me.pitch);
  int best = -1;
  float best_score = -1e30f;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (i == self) continue;
    const Player& e = s.players[i];
    if (!e.active || !e.alive) continue;
    Vec3 c = body_center(e);
    // Field of view first: it is a dot product, and it throws out most
    // candidates before the raycast that would otherwise cost far more.
    Vec3 to = c - eye;
    float to_len = vec3_length(to);
    if (to_len > 0.01f && i != mem.provoked_by) {
      float cosang = vec3_dot(look, to / to_len);
      if (cosang < std::cos(cfg.fov)) continue;
    }
    if (!can_see(s, map, self, c)) continue;
    float dist = vec3_length(c - eye);
    float score = 100.0f / (dist + 4.0f) + (1.0f - e.health / PLAYER_MAX_HEALTH) * 12.0f;
    if (i == mem.target) score += 14.0f;
    if (score > best_score) {
      best_score = score;
      best = i;
    }
  }

  // With nobody in sight, head for the nearest living enemy. This is the one
  // place the bot uses knowledge it has not earned: on a 167 x 140 m map like
  // de_dust2, random wandering means two bots simply never meet - 30 out of 30
  // matches timed out as draws before this. Note it only navigates on this;
  // aiming and firing still require real line of sight.
  int hunt = -1;
  if (best < 0) {
    float nearest = 1e30f;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      if (i == self) continue;
      const Player& e = s.players[i];
      if (!e.active || !e.alive) continue;
      float d = vec3_length(e.pos - me.pos);
      if (d < nearest) {
        nearest = d;
        hunt = i;
      }
    }
  }

  if (best >= 0) {
    mem.has_path = false;  // fighting moves it; any standing route is stale
    if (best != mem.target) {
      mem.target = best;
      mem.target_lock = 0.0f;
      mem.seen_for = 0.0f;
    }
    mem.target_lock += dt;
    mem.seen_for += dt;
    mem.lost_for = 0.0f;
    mem.last_known = body_center(s.players[best]);
    mem.has_last_known = true;
  } else {
    mem.seen_for = 0.0f;
    mem.lost_for += dt;
    // Chase a lost target for a moment, then give up on it.
    if (mem.lost_for > 3.0f) {
      mem.target = -1;
      mem.has_last_known = false;
    }
  }

  // ---- health ------------------------------------------------------------
  float max_hp = player_class_max_health(me.player_class);
  bool hurt = me.health < max_hp * cfg.seek_health_below;
  int pickup = -1;
  float pickup_dist = 1e30f;
  if (hurt) {
    for (int i = 0; i < s.pickup_count; ++i) {
      if (!s.pickups[i].present) continue;
      float d = vec3_length(s.pickups[i].pos - me.pos);
      if (d < pickup_dist) {
        pickup_dist = d;
        pickup = i;
      }
    }
  }

  // ---- aim ---------------------------------------------------------------
  Vec3 aim_point;
  bool engaging = best >= 0;
  if (engaging) {
    const Player& t = s.players[best];
    aim_point = body_center(t);
    float dist = vec3_length(aim_point - eye);
    // A rocket travels, so it has to be thrown where the target will be. A
    // hitscan shot does not, and leading one is simply a miss.
    if (me.weapon == WEAPON_ROCKET) {
      float flight = dist / ROCKET_SPEED;
      aim_point += Vec3{t.vel.x, t.vel.y * 0.5f, t.vel.z} * flight;
    }
  } else if (mem.has_last_known) {
    aim_point = mem.last_known;
  } else if (hunt >= 0) {
    aim_point = body_center(s.players[hunt]);
  } else {
    mem.wander_timer -= dt;
    if (mem.wander_timer <= 0.0f) {
      mem.wander_yaw = rng_float(rng, -PI, PI);
      mem.wander_timer = rng_float(rng, 1.2f, 2.8f);
    }
    aim_point = me.pos + Vec3{std::sin(mem.wander_yaw), 0.0f, -std::cos(mem.wander_yaw)} * 10.0f;
    aim_point.y = eye.y;
  }
  if (pickup >= 0 && !engaging) aim_point = s.pickups[pickup].pos + Vec3{0.0f, 1.0f, 0.0f};

  aim_at(mem, eye, aim_point, cfg.turn_rate, dt);
  // Let the error wander and pull it gently back toward centre. Independent
  // noise each tick would average away across a burst and barely cost the bot
  // anything; an offset that persists for a few tenths of a second is what
  // actually makes shots miss.
  //
  // The kick has to be scaled by aim_error, not just the drift rate. Scaling it
  // by the rate alone made the amplitude depend on how *fast* the error wanders
  // rather than how *large* it is - which had the tiers backwards, giving
  // `hard` more aim error than `easy`, and made easy, normal and hard measure
  // identical at 0.25-0.28 damage per shot.
  const float pull = cfg.aim_drift_rate * dt;
  const float kick = cfg.aim_error * cfg.aim_drift_rate * dt * 3.0f;
  mem.drift_yaw += rng_float(rng, -kick, kick) - mem.drift_yaw * pull;
  mem.drift_pitch += rng_float(rng, -kick, kick) - mem.drift_pitch * pull;
  mem.drift_yaw = clampf(mem.drift_yaw, -cfg.aim_error, cfg.aim_error);
  mem.drift_pitch = clampf(mem.drift_pitch, -cfg.aim_error, cfg.aim_error);
  in.yaw = angle_wrap(mem.aim_yaw + mem.drift_yaw);
  in.pitch = clampf(mem.aim_pitch + mem.drift_pitch, -1.5f, 1.5f);

  // ---- weapon ------------------------------------------------------------
  // Rockets at middling range against something on the ground, where splash
  // lands even on a near miss. Never up close: ROCKET_SPLASH_RADIUS would take
  // a large bite out of the bot itself.
  uint8_t want_weapon = 1;
  if (engaging) {
    const Player& t = s.players[best];
    float dist = vec3_length(body_center(t) - eye);
    if (dist > 8.0f && dist < 32.0f && t.on_ground && me.player_class != CLASS_SCOUT) {
      want_weapon = 2;
    }
  }
  in.weapon_switch = want_weapon;

  // ---- fire --------------------------------------------------------------
  if (engaging && mem.seen_for >= cfg.reaction) {
    const Player& t = s.players[best];
    Vec3 want = vec3_normalize(aim_point - eye);
    Vec3 have = angles_forward(in.yaw, in.pitch);
    float cone = std::acos(clampf(vec3_dot(want, have), -1.0f, 1.0f));
    float dist = vec3_length(body_center(t) - eye);
    bool in_range = dist <= weapon_reach(me.weapon);
    // Do not splash yourself.
    bool rocket_safe = me.weapon != WEAPON_ROCKET || dist > ROCKET_SPLASH_RADIUS * 1.8f;
    if (cone <= cfg.fire_cone && in_range && rocket_safe) in.buttons |= BTN_FIRE;
  }

  // ---- movement ----------------------------------------------------------
  Vec3 move{0.0f, 0.0f, 0.0f};
  if (pickup >= 0 && (!engaging || pickup_dist < 12.0f)) {
    move = follow_route(map, mem, me.pos, s.pickups[pickup].pos, dt);
  } else if (engaging) {
    const Player& t = s.players[best];
    Vec3 to = t.pos - me.pos;
    to.y = 0.0f;
    float dist = vec3_length(to);
    Vec3 toward = dist > 0.001f ? to / dist : Vec3{0.0f, 0.0f, 0.0f};
    float want = preferred_range(me.weapon);
    // Close, back off, or hold - then circle. Circling is most of what makes a
    // bot hard to hit; standing still and trading is what makes one easy.
    float radial = clampf((dist - want) / 6.0f, -1.0f, 1.0f);
    Vec3 side{-toward.z, 0.0f, toward.x};
    mem.strafe_timer -= dt;
    if (mem.strafe_timer <= 0.0f) {
      mem.strafe_sign = -mem.strafe_sign;
      mem.strafe_timer = rng_float(rng, cfg.strafe_min, cfg.strafe_max);
    }
    move = toward * radial + side * (mem.strafe_sign * 0.9f);
  } else if (mem.has_last_known) {
    move = follow_route(map, mem, me.pos, mem.last_known, dt);
  } else if (hunt >= 0) {
    move = follow_route(map, mem, me.pos, s.players[hunt].pos, dt);
  } else {
    move = Vec3{std::sin(mem.wander_yaw), 0.0f, -std::cos(mem.wander_yaw)};
  }

  // Resolve the heading against the level: nearest open direction that still
  // has floor under it. Without this the bot walks into de_dust2's walls and
  // stays there, which cost 30 out of 30 matches as timeouts.
  if (vec3_length(move) > 0.001f) {
    Vec3 chosen = steer(map, me.pos, me.vel, move, mem.strafe_sign >= 0.0f ? 1.0f : -1.0f);
    if (vec3_length(chosen) < 0.001f) {
      // Boxed in on every heading tried - flip the circling direction so the
      // next tick fans out the other way instead of retrying the same arc.
      mem.strafe_sign = -mem.strafe_sign;
      move = {0.0f, 0.0f, 0.0f};
    } else {
      move = chosen;
    }
  }
  // ---- movement tech ------------------------------------------------------
  in.buttons |= move_buttons(move, in.yaw);
  in.buttons |= movement_tech(map, me, mem, move, engaging).buttons;

  return in;
}
