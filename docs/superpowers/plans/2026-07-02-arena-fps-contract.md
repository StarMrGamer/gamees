# Arena FPS — Architecture Contract

This document is the single source of truth for cross-module names, types,
signatures, constants, and conventions. Implementation tasks MUST use these
exact names. Anything not listed here is module-internal and free to design,
but nothing may *contradict* this contract.

## Global conventions

- Language: **C++20**. No exceptions thrown by our code, no RTTI reliance:
  error handling via `bool`/return values and `fatal_error()`. STL containers
  allowed (`std::vector`, `std::string`, `std::atomic`) but plain structs +
  fixed arrays preferred in `game/`.
- No namespaces. Free functions with module prefixes (`net_`, `map_`, `game_`,
  `audio_`, …). Structs `PascalCase`, functions/variables `snake_case`,
  constants/macros `UPPER_SNAKE`.
- Headers use `#pragma once`. Include order: own header, project, system.
- **Units:** meters, seconds, radians. **Coordinates:** +Y up, right-handed;
  at `yaw = 0` forward is `(0, 0, -1)`; yaw increases turning right (toward +X);
  positive pitch looks up.
  - `Vec3 angles_forward(float yaw, float pitch)` returns
    `{ sinf(yaw)*cosf(pitch), sinf(pitch), -cosf(yaw)*cosf(pitch) }`
  - `Vec3 angles_right(float yaw)` returns `{ cosf(yaw), 0, sinf(yaw) }`
- Player position `Player.pos` is the **center of the feet** (bottom-center of
  the hull AABB).
- No git operations anywhere (user preference — no commits, no branches).

## Build system

- `CMakeLists.txt` at repo root. Project `arena`, C++20 required.
- Targets: **`arena`** (the game, all `src/` except tests) and
  **`arena_tests`** (all `tests/*.cpp` + everything in `src/` except
  `src/main.cpp`).
- SDL3: `find_package(SDL3 QUIET)`; if not found,
  `FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.0.tar.gz)`
  (URL mode, not GIT_REPOSITORY — no git binary required).
  Link `SDL3::SDL3`.
- Windows: `if(WIN32) target_link_libraries(arena PRIVATE ws2_32)` (both targets).
- Standard commands (used in all plan steps):
  - Configure: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  - Build: `cmake --build build`
  - Tests: `./build/arena_tests`
- `cmake` may be a locally downloaded binary at `tools/cmake/bin/cmake`
  (Task 1.1 handles this); steps write plain `cmake` and Task 1.1 puts it on
  `PATH` via `export PATH="$PWD/tools/cmake/bin:$PATH"` when needed.

## File map

```
CMakeLists.txt
maps/arena.txt
src/
  main.cpp                 — SDL_main; arg parse; dispatch to menu/client/host/dedicated/bot
  core/math.h              — Vec2/Vec3/Mat4 + ops (header-only)
  core/log.h  log.cpp      — log_info/warn/error, fatal_error
  core/rng.h               — Rng xorshift64 (header-only)
  platform/socket.h  socket.cpp — UDP wrapper (BSD/Winsock2)
  net/protocol.h  protocol.cpp  — NetWriter/NetReader, packet types, header
  render/gl_loader.h  gl_loader.cpp — GL types/enums/function pointers (self-contained)
  render/shader.h  shader.cpp
  render/mesh.h  mesh.cpp       — Vertex, Mesh, MeshBuilder
  render/renderer.h  renderer.cpp — Camera, main pipeline, world+box drawing
  render/particles.h  particles.cpp
  render/hud.h  hud.cpp         — stb_easy_font text + rects, ortho pass
  audio/mixer.h  mixer.cpp
  audio/synth.h  synth.cpp
  game/tuning.h            — every gameplay constant (header-only)
  game/game_state.h        — entities, GameState, PlayerInput, events
  game/map.h  map.cpp      — Map, parsing/loading
  game/collision.h  collision.cpp — AABB tests, ray casts, move-and-slide
  game/movement.h  movement.cpp   — player_move (Quake physics + dash + slide)
  game/weapons.h  weapons.cpp     — fire logic, rockets, splash
  game/sim.h  sim.cpp      — game_init/game_tick/join/leave/events
  game/snapshot.h  snapshot.cpp   — GameState <-> packet serialization + interpolation
  client/client.h  client.cpp     — connection state machine, snapshot buffer
  client/game_client.h  game_client.cpp — playable app loop (window, input, render, audio glue)
  client/menu.h  menu.cpp
  client/bot.cpp  bot.h    — headless scripted client
  server/server.h  server.cpp     — Server, sessions, tick, broadcast
  server/dedicated.cpp  dedicated.h — dedicated loop + signal handling; server thread for --host
  vendor/stb_easy_font.h   — ALREADY VENDORED (do not recreate)
tests/
  test_main.cpp  test_harness.h   — hand-rolled TEST/CHECK harness
  test_math.cpp  test_net.cpp  test_map.cpp  test_collision.cpp
  test_movement.cpp  test_weapons.cpp  test_snapshot.cpp
```

Dependency rule: `game/` includes only `core/` and `net/protocol.h` (for
serialization). It must NOT include SDL, `render/`, `audio/`, `platform/`.

## core/

```c
// math.h (header-only). All ops as free functions/operators.
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
// operators + - * (scalar and componentwise) on Vec3; and:
float   vec3_dot(Vec3 a, Vec3 b);
Vec3    vec3_cross(Vec3 a, Vec3 b);
float   vec3_length(Vec3 v);
Vec3    vec3_normalize(Vec3 v);          // returns {0,0,0} for near-zero input
Vec3    vec3_lerp(Vec3 a, Vec3 b, float t);
float   lerp(float a, float b, float t);
float   clampf(float v, float lo, float hi);
float   angle_lerp(float a, float b, float t); // shortest-path radians
Vec3    angles_forward(float yaw, float pitch);
Vec3    angles_right(float yaw);

struct Mat4 { float m[16]; };            // column-major
Mat4 mat4_identity();
Mat4 mat4_mul(const Mat4& a, const Mat4& b);          // a * b
Mat4 mat4_perspective(float fovy_rad, float aspect, float znear, float zfar);
Mat4 mat4_ortho(float l, float r, float b, float t, float zn, float zf);
Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up);
Mat4 mat4_translate(Vec3 t);
Mat4 mat4_scale(Vec3 s);
Mat4 mat4_rotate_x(float rad);
Mat4 mat4_rotate_y(float rad);

// log.h
void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
[[noreturn]] void fatal_error(const char* fmt, ...);   // logs then exit(1)

// rng.h (header-only) — xorshift64*
struct Rng { uint64_t state; };          // init with nonzero seed
uint64_t rng_next(Rng& r);
float    rng_float(Rng& r, float lo, float hi);
int      rng_int(Rng& r, int lo, int hi);   // inclusive
```

## platform/socket.h

```c
bool net_init();                          // WSAStartup on Windows; true on success
void net_shutdown();

struct NetAddress { uint32_t ip; uint16_t port; };     // host byte order
bool  net_address_parse(const char* str, uint16_t default_port, NetAddress* out);
                                          // accepts "1.2.3.4", "1.2.3.4:27950", "localhost[:port]"
void  net_address_to_string(NetAddress a, char* buf, int cap);
bool  net_address_equal(NetAddress a, NetAddress b);

struct UdpSocket { uint64_t handle; bool valid; };
UdpSocket udp_open(uint16_t bind_port);   // 0 = ephemeral; non-blocking; valid=false on error
void udp_close(UdpSocket& s);
bool udp_send(UdpSocket& s, NetAddress to, const void* data, int len);
int  udp_recv(UdpSocket& s, void* buf, int cap, NetAddress* from); // bytes, or -1 if none
```

## net/protocol.h

```c
constexpr uint32_t PROTOCOL_MAGIC   = 0x414E5241u; // "ARNA" little-endian
constexpr uint8_t  PROTOCOL_VERSION = 1;
constexpr int      MAX_PACKET       = 2048;
constexpr uint16_t DEFAULT_PORT     = 27950;

enum PacketType : uint8_t {
  PKT_CL_HELLO = 1, PKT_CL_INPUT, PKT_CL_DISCONNECT,
  PKT_SV_ACCEPT, PKT_SV_REJECT, PKT_SV_SNAPSHOT, PKT_SV_SHUTDOWN,
};

struct NetWriter { uint8_t* buf; int cap; int len; bool overflow; };
void nw_init(NetWriter& w, uint8_t* buf, int cap);
void nw_u8(NetWriter& w, uint8_t v);   void nw_u16(NetWriter& w, uint16_t v);
void nw_u32(NetWriter& w, uint32_t v); void nw_f32(NetWriter& w, float v);
void nw_vec3(NetWriter& w, Vec3 v);
void nw_string(NetWriter& w, const char* s, int max_len);  // u8 length prefix
// all little-endian; on overflow set flag, write nothing further

struct NetReader { const uint8_t* buf; int len; int pos; bool error; };
void     nr_init(NetReader& r, const uint8_t* buf, int len);
uint8_t  nr_u8(NetReader& r);  uint16_t nr_u16(NetReader& r);
uint32_t nr_u32(NetReader& r); float    nr_f32(NetReader& r);
Vec3     nr_vec3(NetReader& r);
void     nr_string(NetReader& r, char* out, int cap);
// out-of-bounds read: set error=true, return 0/empty; callers check r.error once at end

void packet_header_write(NetWriter& w, PacketType type);          // magic, version, type
bool packet_header_read(NetReader& r, PacketType* type_out);      // false if bad magic/version
```

Packet layouts (after the 6-byte header):
- `PKT_CL_HELLO`: `string name` (max 15 chars)
- `PKT_CL_INPUT`: `u8 count (1..3)`, then `count` × InputCmd, **newest first**:
  `u32 sequence, u8 buttons, u8 weapon_switch, f32 yaw, f32 pitch`
- `PKT_CL_DISCONNECT`: (empty)
- `PKT_SV_ACCEPT`: `u8 player_index, u8 tick_rate, string map_name`
- `PKT_SV_REJECT`: `string reason`
- `PKT_SV_SNAPSHOT`: output of `snapshot_write` (below)
- `PKT_SV_SHUTDOWN`: (empty)

## game/tuning.h — pinned constants

```c
constexpr int   TICK_RATE = 60;      constexpr float TICK_DT = 1.0f / 60.0f;
constexpr int   MAX_PLAYERS = 8, MAX_ROCKETS = 64, MAX_PICKUPS = 16, MAX_EVENTS = 32;
constexpr float GRAVITY = 20.0f;
constexpr float GROUND_MAX_SPEED = 8.0f, GROUND_ACCEL = 60.0f, GROUND_FRICTION = 6.0f;
constexpr float AIR_ACCEL = 25.0f, AIR_WISH_CAP = 1.0f;
constexpr float JUMP_VELOCITY = 7.0f, JUMP_BUFFER_TIME = 0.1f;
constexpr float DASH_IMPULSE = 12.0f, DASH_COOLDOWN = 2.0f;
constexpr float SLIDE_TRIGGER_SPEED = 9.0f, SLIDE_BOOST = 2.0f;
constexpr float SLIDE_FRICTION = 0.5f, SLIDE_DURATION = 1.0f;
constexpr float STEP_HEIGHT = 0.4f;
constexpr float PLAYER_HALF_W = 0.3f, PLAYER_HEIGHT = 1.8f, PLAYER_CROUCH_HEIGHT = 1.2f;
constexpr float EYE_HEIGHT = 1.62f, CROUCH_EYE_HEIGHT = 1.0f;
constexpr float PLAYER_MAX_HEALTH = 100.0f;
constexpr float RIFLE_DAMAGE = 9.0f, RIFLE_INTERVAL = 0.12f, RIFLE_KNOCKBACK = 0.5f;
constexpr float RIFLE_RANGE = 200.0f;
constexpr float ROCKET_SPEED = 25.0f, ROCKET_DIRECT_DAMAGE = 100.0f;
constexpr float ROCKET_SPLASH_RADIUS = 3.5f, ROCKET_INTERVAL = 0.8f;
constexpr float ROCKET_KNOCKBACK = 14.0f, ROCKET_LIFETIME = 10.0f;
constexpr float HEALTH_PACK_AMOUNT = 25.0f, HEALTH_RESPAWN_TIME = 15.0f;
constexpr float PLAYER_RESPAWN_TIME = 2.0f;
constexpr int   DEFAULT_FRAG_LIMIT = 20;
constexpr float MATCH_RESTART_TIME = 10.0f;
constexpr float CLIENT_TIMEOUT = 5.0f;
```

## game/game_state.h

```c
enum Buttons : uint8_t {
  BTN_FORWARD = 1, BTN_BACK = 2, BTN_LEFT = 4, BTN_RIGHT = 8,
  BTN_JUMP = 16, BTN_CROUCH = 32, BTN_FIRE = 64, BTN_DASH = 128,
};
enum Weapon : uint8_t { WEAPON_RIFLE = 0, WEAPON_ROCKET = 1 };
enum EventType : uint8_t { EV_KILL = 1, EV_SOUND, EV_JOIN, EV_LEAVE, EV_HIT };
enum SoundId : uint8_t {  // shared by events and audio module
  SND_RIFLE = 1, SND_ROCKET_LAUNCH, SND_EXPLOSION, SND_JUMP, SND_DASH,
  SND_SLIDE, SND_PICKUP, SND_HURT, SND_DEATH, SND_RESPAWN, SND_COUNT,
};

struct PlayerInput { uint32_t sequence; uint8_t buttons; uint8_t weapon_switch; float yaw, pitch; };
// weapon_switch: 0 = none, 1 = rifle, 2 = rocket

struct Player {
  bool active; bool alive;
  char name[16];
  Vec3 pos, vel;                 // pos = bottom-center of hull
  float yaw, pitch;
  float health;
  uint8_t weapon;
  bool on_ground, crouching, sliding;
  float fire_cooldown, dash_cooldown, slide_time, respawn_timer, jump_buffer;
  bool jump_held;                // edge detection for jump
  int frags;
  uint32_t last_input_seq;
};
struct Rocket { bool active; Vec3 pos, vel; uint8_t owner; float life; };
struct Pickup { bool present; Vec3 pos; float respawn_timer; };
struct GameEvent { uint32_t id; uint8_t type; uint8_t a, b; Vec3 pos; uint32_t tick; };
// EV_KILL: a=killer,b=victim. EV_SOUND: a=SoundId, pos. EV_JOIN/EV_LEAVE: a=player.
// EV_HIT: a=attacker (client shows hitmarker if a == local player)

struct GameState {
  uint32_t tick;
  Player players[MAX_PLAYERS];
  Rocket rockets[MAX_ROCKETS];
  Pickup pickups[MAX_PICKUPS]; int pickup_count;
  GameEvent events[MAX_EVENTS]; uint32_t next_event_id;   // ring by id % MAX_EVENTS
  int frag_limit; bool match_over; float restart_timer;
};
```

## game/map.h

```c
constexpr int MAX_MAP_BOXES = 256, MAX_SPAWNS = 16;
struct MapBox { Vec3 min, max; Vec3 color; };
struct Map {
  char name[32];
  MapBox boxes[MAX_MAP_BOXES]; int box_count;
  Vec3 spawns[MAX_SPAWNS]; float spawn_yaws[MAX_SPAWNS]; int spawn_count;
  Vec3 health_spawns[MAX_SPAWNS]; int health_count;
  Vec3 light_dir;                // normalized, points FROM light (direction of travel)
  Vec3 fog_color; float fog_density;
  Vec3 sky_color;                // GL clear color
};
bool map_parse(const char* text, Map* out);   // returns false + log_error on bad line
bool map_load(const char* path, Map* out);
```

Map text format, one directive per line (`#` comments allowed):
```
name <string>
box x y z  w h d  r g b        # x,y,z = min corner; w,h,d = size; rgb in 0..1
spawn x y z yaw_degrees
health x y z
light dx dy dz                  # will be normalized
fog r g b density
sky r g b
```

## game/collision.h

```c
struct Aabb { Vec3 min, max; };
Aabb player_aabb(Vec3 pos, bool crouching);   // from PLAYER_HALF_W / heights
bool aabb_overlap(const Aabb& a, const Aabb& b);
bool ray_aabb(Vec3 origin, Vec3 dir, const Aabb& box, float max_t, float* t_out);
float ray_map(const Map& map, Vec3 origin, Vec3 dir, float max_t); // nearest hit t or max_t
bool map_box_overlap(const Map& map, const Aabb& box);

struct MoveResult { Vec3 pos; Vec3 vel; bool on_ground; bool hit_ceiling; };
MoveResult move_slide(const Map& map, Vec3 pos, Vec3 vel, bool crouching, float dt);
// axis-separated integration (X, Z, then Y), zeroing velocity on blocked axes,
// with step-up retry (STEP_HEIGHT) for X/Z when on ground
```

## game/movement.h

```c
void player_move(Player& p, const PlayerInput& in, const Map& map, float dt);
// full pipeline: view angles <- input; wish dir from buttons+yaw; friction
// (ground/slide); accelerate (ground or air w/ AIR_WISH_CAP); jump buffer +
// jump; dash; crouch-slide state machine; gravity; move_slide; timers.
// Emits no events itself — returns; sim.cpp reads flags for sounds (e.g.
// compares on_ground/sliding transitions).
```

## game/weapons.h + sim.h

```c
// weapons.h
void weapon_fire(GameState& s, const Map& map, int shooter);   // respects fire_cooldown
void rockets_tick(GameState& s, const Map& map, float dt);
void explode_rocket(GameState& s, const Map& map, int rocket_index);
int  find_player_ray_hit(const GameState& s, Vec3 origin, Vec3 dir, float max_t,
                         int exclude, float* t_out);           // -1 if none
void damage_player(GameState& s, int victim, int attacker, float amount, Vec3 knockback);

// sim.h
void game_init(GameState& s, const Map& map, int frag_limit);
int  game_player_join(GameState& s, const Map& map, const char* name); // -1 if full
void game_player_leave(GameState& s, int player_index);
void game_tick(GameState& s, const Map& map, const PlayerInput inputs[MAX_PLAYERS], Rng& rng);
void push_event(GameState& s, uint8_t type, uint8_t a, uint8_t b, Vec3 pos);
Vec3 pick_spawn(const GameState& s, const Map& map, Rng& rng, float* yaw_out);
// farthest-from-living-enemies among map.spawns (random tiebreak)
```

## game/snapshot.h

```c
void snapshot_write(const GameState& s, NetWriter& w);
bool snapshot_read(GameState& s, NetReader& r);       // false on r.error
void snapshot_interpolate(const GameState& a, const GameState& b, float t,
                          GameState& out);
// out = b, except: for players/rockets active in BOTH a and b, pos is lerped
// (yaw/pitch via angle_lerp). Teleport guard: if |b.pos - a.pos| > 5m, snap to b.
```

## render/

```c
// gl_loader.h — self-contained: defines GLuint/GLenum/etc., needed GL constants,
// and function pointers via an X-macro table. No system GL headers.
bool gl_load_functions();   // via SDL_GL_GetProcAddress; false if any missing

// shader.h
GLuint shader_compile(const char* vs_src, const char* fs_src); // fatal_error on failure

// mesh.h
struct Vertex { Vec3 pos; Vec3 normal; Vec3 color; };
struct Mesh { GLuint vao, vbo; int vertex_count; };
Mesh mesh_create(const Vertex* verts, int count);
Mesh mesh_create_dynamic(int max_verts);
void mesh_update(Mesh& m, const Vertex* verts, int count);
void mesh_draw(const Mesh& m);
struct MeshBuilder {
  std::vector<Vertex> verts;
  void add_box(Vec3 mn, Vec3 mx, Vec3 color);   // 36 verts, per-face normals
  void add_quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, Vec3 color);
};

// renderer.h
struct Camera { Vec3 pos; float yaw, pitch; };
Mat4 camera_view(const Camera& c);
struct Renderer { /* program, uniform locations, arena Mesh, unit cube Mesh, dynamic meshes */ };
bool renderer_init(Renderer& r, const Map& map);
void renderer_begin_frame(Renderer& r, const Camera& cam, int fb_w, int fb_h, const Map& map);
void renderer_draw_world(Renderer& r);
void renderer_draw_box(Renderer& r, Vec3 center, Vec3 size, Vec3 color, float yaw);
// unit cube scaled/rotated/translated; used for players (body+head), rockets, viewmodel
void renderer_end_frame(Renderer& r);   // no-op placeholder for symmetry

// Main shader (pinned): VS transforms by u_mvp/u_model, FS:
//   lit = color * (u_ambient + u_light_color * max(dot(n, -u_light_dir), 0))
//   fogf = 1 - exp(-u_fog_density * dist(u_cam_pos, world_pos))
//   final = mix(lit, u_fog_color, fogf); uniform u_tint multiplies color.

// particles.h
constexpr int MAX_PARTICLES = 2048;
struct Particle { Vec3 pos, vel; Vec3 color; float life, max_life, size; bool gravity; };
struct ParticleSystem { Particle pool[MAX_PARTICLES]; int alive; Mesh dynamic; };
void particles_init(ParticleSystem& ps);
void particles_update(ParticleSystem& ps, float dt);
void particles_render(ParticleSystem& ps, Renderer& r, const Camera& cam);
void particles_explosion(ParticleSystem& ps, Rng& rng, Vec3 pos);
void particles_sparks(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 normal);
void particles_muzzle_flash(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 dir);
void particles_trail(ParticleSystem& ps, Rng& rng, Vec3 pos);

// hud.h — 2D ortho pass, drawn after world with depth test off
struct Hud { /* program, dynamic mesh, screen size */ };
bool hud_init(Hud& h);
void hud_begin(Hud& h, int screen_w, int screen_h);
void hud_text(Hud& h, float x, float y, float scale, Vec3 color, const char* fmt, ...);
float hud_text_width(const char* text, float scale);
void hud_rect(Hud& h, float x, float y, float w, float h_, Vec3 color, float alpha);
void hud_end(Hud& h);   // flushes draw
```

## audio/

```c
// mixer.h
constexpr int AUDIO_SAMPLE_RATE = 48000;
struct Sound { std::vector<float> samples; };   // mono, 48 kHz, -1..1
struct Mixer { /* SDL stream, SDL_Mutex*, voices, listener */ };
bool audio_init(Mixer& m);                      // false = run silently (log_warn)
void audio_shutdown(Mixer& m);
void audio_register(Mixer& m, int sound_id, Sound s);   // sound_id = SoundId enum
void audio_set_listener(Mixer& m, Vec3 pos, float yaw);
void audio_play(Mixer& m, int sound_id, float gain);            // non-positional
void audio_play_3d(Mixer& m, int sound_id, Vec3 pos, float gain);
// pure helpers (exported for tests):
float audio_attenuation(float dist);    // 1/(1 + 0.08*d + 0.01*d*d)
void  audio_pan(Vec3 listener_pos, float listener_yaw, Vec3 src,
                float* left_gain, float* right_gain);

// synth.h — all return 48 kHz mono Sounds, procedurally generated
Sound synth_make(int sound_id);   // dispatch; internally uses noise/sine/env helpers
```

## client/ + server/

```c
// client/client.h
enum ClientState { CLIENT_DISCONNECTED, CLIENT_CONNECTING, CLIENT_CONNECTED, CLIENT_REJECTED };
struct Client {
  UdpSocket sock; NetAddress server_addr; ClientState state;
  int player_index;                       // from PKT_SV_ACCEPT
  char map_name[32]; char reject_reason[64];
  GameState snap_a, snap_b;               // b = newest
  double snap_b_recv_time; bool have_two_snaps;
  PlayerInput input_history[3]; uint32_t next_input_seq;
  uint32_t last_seen_event_id;            // events with id <= this already handled
  double last_recv_time, connect_start_time;
};
bool client_start(Client& c, NetAddress server, const char* player_name);
void client_send_input(Client& c, const PlayerInput& in);
// appends to history, sends PKT_CL_INPUT with up to last 3, newest first
struct ClientEvents { GameEvent events[MAX_EVENTS]; int count; };
void client_receive(Client& c, double now, ClientEvents* new_events);
// pumps socket: snapshots (rotate a<-b, b<-new), accept/reject/shutdown, timeout check
void client_view_state(const Client& c, double now, GameState* out);
// interpolated remote state; local player (player_index) copied raw from snap_b
void client_disconnect(Client& c);

// server/server.h
struct ClientSlot {
  bool used; NetAddress addr; int player_index;
  double last_recv_time; PlayerInput latest_input; uint32_t highest_input_seq;
};
struct Server {
  UdpSocket sock; GameState state; Map map;
  ClientSlot clients[MAX_PLAYERS]; Rng rng; double now;
};
bool server_init(Server& sv, uint16_t port, const char* map_path, int frag_limit);
void server_pump(Server& sv, double now);   // handshakes, inputs, disconnects, timeouts
void server_tick(Server& sv);               // gather latest inputs -> game_tick
void server_broadcast(Server& sv);          // snapshot to every connected client
void server_shutdown(Server& sv);           // PKT_SV_SHUTDOWN to all, close

// server/dedicated.h — server code uses std::thread/std::chrono/std::atomic,
// NOT SDL (keeps the server path free of SDL beyond what main.cpp does)
int  dedicated_main(uint16_t port, const char* map_path, int frag_limit);
struct ServerThread { std::thread thread; std::atomic<bool> stop; uint16_t port; /* … */ };
bool server_thread_start(ServerThread& st, uint16_t port, const char* map_path, int frag_limit);
void server_thread_stop(ServerThread& st);

// client/bot.h
int bot_main(NetAddress server, const char* name, int lifetime_seconds); // 0 = forever

// client/game_client.h
int game_client_main(NetAddress server, const char* player_name, ServerThread* owned_server);
// owned_server non-null when hosting: stopped on exit

// client/menu.h
enum MenuResult { MENU_HOST, MENU_JOIN, MENU_QUIT };
MenuResult menu_run(char* out_address, int cap);  // owns a temporary window OR
// is integrated into game_client_main's window before connecting — module's choice,
// but menu_run is the entry main.cpp calls when no args are given.
```

## main.cpp CLI

```
arena                          # menu
arena --connect <ip[:port]>    # join
arena --host                   # listen server (thread) + local client via 127.0.0.1
arena --dedicated              # headless server
arena --bot <ip[:port]>        # scripted headless client
options: --port N (default 27950), --fraglimit N (default 20),
         --name S (default "player"), --map PATH (default "maps/arena.txt")
```
`main.cpp` includes `<SDL3/SDL_main.h>` (required by SDL3 for cross-platform main).

## Snapshot wire format (inside PKT_SV_SNAPSHOT)

```
u32 tick
u8  match_over (0/1), f32 restart_timer, u8 frag_limit
u8  player_count, then per active player:
    u8 index, u8 flags (bit0 alive, bit1 on_ground, bit2 crouching, bit3 sliding)
    string name (only chars; always sent — simplicity over bytes)
    vec3 pos, vec3 vel, f32 yaw, f32 pitch, f32 health,
    u8 weapon, f32 fire_cooldown, f32 dash_cooldown, f32 respawn_timer,
    i32 frags (as u32), u32 last_input_seq
u8  rocket_count, then per active rocket: u8 index, vec3 pos, vec3 vel, u8 owner
u8  pickup_count, then per pickup: u8 present (0/1), vec3 pos, f32 respawn_timer
u8  event_count, then per event in window (last 15 ticks):
    u32 id, u8 type, u8 a, u8 b, vec3 pos, u32 tick
```
Size control: cap serialized `event_count` at the 16 newest and `rocket_count`
at the 32 newest (by lowest remaining life). Worst case then ≈ 8 players × ~64B
+ 32 rockets × 14B + 16 pickups × 17B + 16 events × 23B + header ≈ 1.4 KB,
which fits MAX_PACKET = 2048 with headroom. A test must assert the worst-case
snapshot fits.

## tests/test_harness.h

```c
#define TEST(name) /* registers void test_fn via static constructor */
#define CHECK(cond) /* on fail: print file:line + expr, mark test failed */
#define CHECK_NEAR(a, b, eps)
#define CHECK_EQ_INT(a, b)
// test_main.cpp runs all registered tests, prints "PASSED n/m", exit 0/1
```

## Milestone deliverables (each independently runnable)

1. **Skeleton** — toolchain bootstrap (cmake via pacman or verified tarball
   `https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz`
   into `tools/cmake/`), CMake project, SDL3 window + GL 3.3 context + clear
   color + ESC quit, math/log/rng, test harness, math tests green.
2. **World** — map parse + `maps/arena.txt`, GL loader, shader, meshes,
   renderer with light/fog, noclip fly camera (`--fly` debug flag added to CLI).
3. **Movement** — collision + movement + tuning, movement/collision tests
   (playing alone happens via `--host`; a `--fly` noclip flag exists for
   debugging rendering/maps).
4. **Netcode** — sockets, protocol, snapshot serialization, server, client,
   `--host`/`--connect`/`--dedicated` all functional; two clients see each
   other move; net + snapshot tests (incl. truncation fuzz).
5. **Combat** — weapons, rockets, splash + rocket jumps, damage/death/respawn,
   pickups, frags, match end/restart; weapons tests.
6. **Game feel** — audio synth + mixer, particles, HUD (health/weapon/frags/
   kill feed/crosshair/hitmarker/scoreboard/connection status), viewmodel,
   menu, `--bot`.
7. **Ship** — dedicated signal handling, README, MinGW cross-build
   (`pacman -S mingw-w64-gcc` or verified fallback
   `https://github.com/mstorsjo/llvm-mingw/releases/download/20250709/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64.tar.xz`),
   bot soak test, final integration checks.
