#pragma once

constexpr int   TICK_RATE = 60;
constexpr float TICK_DT = 1.0f / 60.0f;
constexpr int   MAX_PLAYERS = 8;
constexpr int   MAX_ROCKETS = 64;
constexpr int   MAX_PICKUPS = 16;
constexpr int   MAX_EVENTS = 64;
constexpr float GRAVITY = 20.0f;
constexpr float GROUND_MAX_SPEED = 8.0f;
constexpr float GROUND_ACCEL = 60.0f;
constexpr float GROUND_FRICTION = 6.0f;
constexpr float AIR_ACCEL = 25.0f;
constexpr float AIR_WISH_CAP = 1.0f;
constexpr float JUMP_VELOCITY = 7.0f;
constexpr float DOUBLE_JUMP_VELOCITY = 7.2f;
constexpr float JUMP_BUFFER_TIME = 0.1f;
constexpr float DASH_IMPULSE = 12.0f;
constexpr float DASH_COOLDOWN = 0.25f;
constexpr float DASH_JUMP_AIR_CONTROL_MULT = 1.3f;
constexpr float DASH_JUMP_AIR_CONTROL_TIME = 0.25f;
constexpr int   MAX_STAMINA = 3;
constexpr float STAMINA_RECHARGE_TIME = 1.25f;
constexpr float SLIDE_TRIGGER_SPEED = 9.0f;
constexpr float SLIDE_BOOST = 2.0f;
constexpr float SLIDE_JUMP_BOOST = 3.0f;
constexpr float SLIDE_FRICTION = 0.5f;
constexpr float SLIDE_DURATION = 1.0f;
constexpr float LAND_SOUND_MIN_FALL_SPEED = 5.0f;
constexpr float STEP_HEIGHT = 0.4f;
// Noclip is a map-inspection tool, so it flies briskly - about twice ground
// speed - and ignores gravity, geometry and the void plane entirely.
constexpr float NOCLIP_SPEED = 16.0f;
constexpr float WALL_CONTACT_GRACE = 0.12f;
constexpr float WALL_JUMP_COOLDOWN = 0.25f;
constexpr float WALL_JUMP_UP_VELOCITY = 7.6f;
constexpr float WALL_JUMP_PUSH = 8.5f;
constexpr float WALL_JUMP_WISH_BOOST = 2.0f;
constexpr float PLAYER_HALF_W = 0.3f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float PLAYER_CROUCH_HEIGHT = 1.2f;
constexpr float EYE_HEIGHT = 1.62f;
constexpr float CROUCH_EYE_HEIGHT = 1.0f;
constexpr float PLAYER_MAX_HEALTH = 100.0f;
constexpr float RIFLE_DAMAGE = 9.0f;
constexpr float RIFLE_INTERVAL = 0.12f;
constexpr float RIFLE_KNOCKBACK = 0.5f;
constexpr float RIFLE_RANGE = 200.0f;
constexpr int   SHOTGUN_PELLETS = 7;
constexpr float SHOTGUN_DAMAGE = 8.0f;
constexpr float SHOTGUN_INTERVAL = 0.65f;
constexpr float SHOTGUN_KNOCKBACK = 0.35f;
constexpr float SHOTGUN_RANGE = 45.0f;
constexpr float SHOTGUN_SPREAD = 0.085f;
// Pellets do full damage inside FALLOFF_START and decay linearly to
// MIN_DAMAGE_FRAC of base by FALLOFF_END, so the shotgun rewards closing in.
constexpr float SHOTGUN_FALLOFF_START = 7.0f;
constexpr float SHOTGUN_FALLOFF_END = 28.0f;
constexpr float SHOTGUN_MIN_DAMAGE_FRAC = 0.30f;
// The sniper trades everything for one decisive shot. Its sustained damage is
// mid-pack - 62/s against the rifle's 75 and the shotgun's 86 up close - and
// all of its power is in the burst, which is the right shape for the class.
//
// 90 is chosen for the margins it leaves rather than the number itself. After
// each class's damage-taken scale a single hit does 94.5 to a Scout (85 hp) and
// 97.2 to another Sniper (80 hp), killing both outright; 90 to a Ranger, who
// lives on 10; and 82.8 to a Tank, who lives on 42. An earlier 80 left the
// Scout alive on exactly 1 hp, which is a coin-flip rather than a decision -
// any later tweak to a scale would have silently flipped it.
constexpr float SNIPER_DAMAGE = 90.0f;
constexpr float SNIPER_INTERVAL = 1.45f;
constexpr float SNIPER_KNOCKBACK = 1.6f;
constexpr float SNIPER_RANGE = 300.0f;
// Zoom is purely a client-side view change - the simulation never sees it - so
// it costs nothing on the wire and cannot desync.
constexpr float SNIPER_ZOOM_FOV = 22.0f;
constexpr float SNIPER_ZOOM_TIME = 0.12f;
// Aim scales with the zoom so the same mouse travel covers the same distance on
// screen; without this a scoped sniper is unusable.
constexpr float SNIPER_ZOOM_SENSITIVITY = 0.34f;
constexpr float LMG_DAMAGE = 2.5f;
constexpr float LMG_INTERVAL = 0.055f;
constexpr float LMG_KNOCKBACK = 0.12f;
constexpr float LMG_RANGE = 160.0f;
constexpr float WEAPON_MUZZLE_FORWARD = 0.65f;
constexpr float WEAPON_MUZZLE_RIGHT = 0.34f;
constexpr float WEAPON_MUZZLE_DOWN = 0.20f;
constexpr float ROCKET_SPEED = 25.0f;
constexpr float ROCKET_DIRECT_DAMAGE = 80.0f;
constexpr float ROCKET_SPLASH_RADIUS = 3.5f;
constexpr float ROCKET_INTERVAL = 0.8f;
constexpr float ROCKET_KNOCKBACK = 14.0f;
constexpr float ROCKET_SELF_DAMAGE_SCALE = 0.45f;
constexpr float ROCKET_JUMP_KNOCKBACK = 24.0f;
constexpr float ROCKET_JUMP_MIN_UP = 12.0f;
constexpr float ROCKET_LIFETIME = 10.0f;
constexpr float HEALTH_PACK_AMOUNT = 25.0f;
constexpr float HEALTH_RESPAWN_TIME = 15.0f;
constexpr float PLAYER_RESPAWN_TIME = 2.0f;
constexpr int   DEFAULT_FRAG_LIMIT = 20;
constexpr float MATCH_RESTART_TIME = 10.0f;
constexpr float CLIENT_TIMEOUT = 5.0f;
constexpr int   SERVER_INPUT_QUEUE = 8;
constexpr float INPUT_STALE_TIME = 0.25f;
// Lag compensation: how far back the server may rewind remote players' hit
// boxes to match the shooter's view. One second covers high-ping LANs/VPNs;
// History keeps 1.5 s so there is always headroom.
constexpr int   LAG_COMP_MAX_REWIND = 60;
