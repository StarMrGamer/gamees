#include "client/gunfeel.h"
#include "core/rng.h"
#include "game/sim.h"
#include "game/tuning.h"
#include "test_harness.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

std::string box_map() {
  return "name gunrange\n"
         "box -30 -1 -30 60 1 60 0.5 0.5 0.5\n"
         "spawn 0 1 0 0\n";
}

// Counts the shots the authoritative simulation actually fires for a player
// holding the trigger, by watching the sound events it emits.
int server_shots(uint8_t weapon, int ticks) {
  auto map = std::make_unique<Map>();
  std::string text = box_map();
  CHECK(map_parse(text.c_str(), map.get()));
  auto st = std::make_unique<GameState>();
  Rng rng{0x1111ull};
  game_init(*st, *map, 999);
  game_player_join(*st, *map, "a");
  st->players[0].weapon = weapon;

  int shots = 0;
  uint32_t seen = 0;
  for (int t = 0; t < ticks; ++t) {
    PlayerInput in[MAX_PLAYERS]{};
    in[0].sequence = static_cast<uint32_t>(t + 1);
    in[0].buttons = BTN_FIRE;
    // weapon_switch 0 leaves the weapon alone, so the one set above sticks.
    game_tick(*st, *map, in, rng);
    for (int e = 0; e < MAX_EVENTS; ++e) {
      const GameEvent& ev = st->events[e];
      if (ev.id == 0 || ev.id <= seen) continue;
      if (ev.type == EV_SOUND && ev.a == gunfeel_weapon_sound(weapon)) ++shots;
    }
    for (int e = 0; e < MAX_EVENTS; ++e) {
      if (st->events[e].id > seen) seen = st->events[e].id;
    }
  }
  return shots;
}

int local_shots(uint8_t weapon, int ticks) {
  GunFeel g;
  gunfeel_reset(g);
  int shots = 0;
  for (int t = 0; t < ticks; ++t) {
    gunfeel_update(g, TICK_DT, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, true);
    if (gunfeel_try_fire(g, weapon, true, true)) ++shots;
  }
  return shots;
}

}  // namespace

// The local gun and the real one have to fire at the same rate. If they drift,
// the player watches a weapon that is not the weapon they are shooting.
TEST(gunfeel_fire_rate_matches_the_server) {
  const uint8_t weapons[4] = {WEAPON_RIFLE, WEAPON_ROCKET, WEAPON_SHOTGUN, WEAPON_LMG};
  for (uint8_t w : weapons) {
    int ticks = TICK_RATE * 5;
    int local = local_shots(w, ticks);
    int server = server_shots(w, ticks);
    CHECK(local > 0);
    // One shot of slack: the two start their cooldowns on slightly different
    // sides of the first tick.
    CHECK(std::abs(local - server) <= 1);
  }
}

// Every weapon's local report has to be the one the server would broadcast,
// or your own gun sounds different from everybody else's.
TEST(gunfeel_sound_matches_the_server) {
  CHECK_EQ_INT(gunfeel_weapon_sound(WEAPON_RIFLE), SND_RIFLE);
  CHECK_EQ_INT(gunfeel_weapon_sound(WEAPON_ROCKET), SND_ROCKET_LAUNCH);
  CHECK_EQ_INT(gunfeel_weapon_sound(WEAPON_SHOTGUN), SND_SHOTGUN);
  CHECK_EQ_INT(gunfeel_weapon_sound(WEAPON_LMG), SND_LMG);
}

TEST(gunfeel_intervals_match_the_tuning) {
  CHECK_NEAR(gunfeel_weapon_interval(WEAPON_RIFLE), RIFLE_INTERVAL, 0.0001f);
  CHECK_NEAR(gunfeel_weapon_interval(WEAPON_ROCKET), ROCKET_INTERVAL, 0.0001f);
  CHECK_NEAR(gunfeel_weapon_interval(WEAPON_SHOTGUN), SHOTGUN_INTERVAL, 0.0001f);
  CHECK_NEAR(gunfeel_weapon_interval(WEAPON_LMG), LMG_INTERVAL, 0.0001f);
}

// The shot has to land on the frame the button went down, not a tick later.
// That immediacy is the entire point of the local path.
TEST(gunfeel_fires_on_the_same_frame_as_the_press) {
  GunFeel g;
  gunfeel_reset(g);
  CHECK(gunfeel_try_fire(g, WEAPON_RIFLE, true, true));
  CHECK_EQ_INT(static_cast<int>(g.shots_fired), 1);
  // And not again until the cooldown has run.
  CHECK(!gunfeel_try_fire(g, WEAPON_RIFLE, true, true));
}

TEST(gunfeel_holds_fire_when_dead_or_unpressed) {
  GunFeel g;
  gunfeel_reset(g);
  CHECK(!gunfeel_try_fire(g, WEAPON_RIFLE, true, false));   // dead
  CHECK(!gunfeel_try_fire(g, WEAPON_RIFLE, false, true));   // not pressed
  CHECK_EQ_INT(static_cast<int>(g.shots_fired), 0);
}

// What matters about the punch is not how fast one shot decays but that a
// sustained burst does not accumulate: if each kick outlives the next shot the
// view climbs away and never comes back. Measured peaks are 2.6 degrees on the
// rifle and 4.1 on the rocket, and every weapon settles to exactly zero.
TEST(gunfeel_view_punch_stays_bounded_through_a_burst) {
  const uint8_t weapons[4] = {WEAPON_RIFLE, WEAPON_ROCKET, WEAPON_SHOTGUN, WEAPON_LMG};
  for (uint8_t w : weapons) {
    GunFeel g;
    gunfeel_reset(g);
    float worst = 0.0f;
    for (int t = 0; t < TICK_RATE * 4; ++t) {
      gunfeel_update(g, TICK_DT, 0.0f, 0.0f, {0, 0, 0}, true);
      gunfeel_try_fire(g, w, true, true);
      float yaw = 0.0f;
      float pitch = 0.0f;
      gunfeel_view_punch(g, &yaw, &pitch);
      float mag = std::sqrt(yaw * yaw + pitch * pitch);
      if (mag > worst) worst = mag;
    }
    CHECK(g.shots_fired > 4);        // it really did fire a burst
    CHECK(worst > 0.005f);           // and it really did kick
    CHECK(worst < 0.14f);            // ~8 degrees; beyond that it is climbing

    // Release, and it must come all the way back rather than parking off-centre.
    for (int t = 0; t < TICK_RATE; ++t) {
      gunfeel_update(g, TICK_DT, 0.0f, 0.0f, {0, 0, 0}, true);
    }
    float yaw = 0.0f;
    float pitch = 0.0f;
    gunfeel_view_punch(g, &yaw, &pitch);
    CHECK(std::fabs(yaw) < 0.0005f);
    CHECK(std::fabs(pitch) < 0.0005f);
  }
}

// A long frame must not fling the spring: a hitch should cost a frame of
// animation, not throw the camera across the room.
TEST(gunfeel_survives_a_frame_hitch) {
  GunFeel g;
  gunfeel_reset(g);
  gunfeel_try_fire(g, WEAPON_ROCKET, true, true);
  for (int t = 0; t < 20; ++t) gunfeel_update(g, 0.5f, 0.0f, 0.0f, {0, 0, 0}, true);
  float yaw = 0.0f;
  float pitch = 0.0f;
  gunfeel_view_punch(g, &yaw, &pitch);
  CHECK(std::isfinite(yaw) && std::isfinite(pitch));
  CHECK(std::fabs(pitch) < 0.3f);
  CHECK(std::fabs(yaw) < 0.3f);
}

// Two guns must not share turn history through a static.
TEST(gunfeel_instances_are_independent) {
  GunFeel a;
  GunFeel b;
  gunfeel_reset(a);
  gunfeel_reset(b);
  for (int t = 0; t < 30; ++t) {
    gunfeel_update(a, TICK_DT, static_cast<float>(t) * 0.2f, 0.0f, {0, 0, 0}, true);
    gunfeel_update(b, TICK_DT, 0.0f, 0.0f, {0, 0, 0}, true);
  }
  // `a` has been whipping the view around and should have sway; `b` has not.
  CHECK(std::fabs(a.sway_yaw) > std::fabs(b.sway_yaw));
  CHECK_NEAR(b.sway_yaw, 0.0f, 0.0001f);
}
