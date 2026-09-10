#include "test_harness.h"

#include "client/client.h"
#include "game/game_state.h"

// Regression: the input button mask widened to 16 bits for BTN_AIRJUMP (256).
// The client's pending-button accumulation must carry that bit through to the
// serialized command, or a rebound (or default) double jump is silently lost.
TEST(client_send_input_preserves_airjump_bit) {
  NetAddress addr{};
  CHECK(net_address_parse("127.0.0.1", 27960, &addr));

  Client c{};
  CHECK(client_start(c, addr, "t"));
  c.state = CLIENT_CONNECTED;
  c.net_now = 1.0;
  c.next_input_send_time = 1.0;
  c.newest_snap_tick = 10;
  c.render_clock_init = true;
  c.render_tick = 8.2f;

  PlayerInput in{};
  in.buttons = static_cast<uint16_t>(BTN_JUMP | BTN_AIRJUMP);
  client_send_input(c, in);

  CHECK((c.input_history[0].buttons & BTN_AIRJUMP) != 0);
  uint32_t seq = c.input_history[0].sequence;
  CHECK((c.sent_inputs[seq % CLIENT_INPUT_RING].buttons & BTN_AIRJUMP) != 0);
  // The sampled view tick rides along for server-side lag compensation.
  CHECK_EQ_INT(c.sent_inputs[seq % CLIENT_INPUT_RING].view_tick, 8);

  client_disconnect(c);
}

TEST(client_view_tick_tracks_render_clock) {
  Client c{};
  c.newest_snap_tick = 100;
  CHECK_EQ_INT(client_view_tick(c), 100);  // no clock yet: report newest

  c.render_clock_init = true;
  c.render_tick = 98.4f;
  CHECK_EQ_INT(client_view_tick(c), 98);
  c.render_tick = 98.6f;
  CHECK_EQ_INT(client_view_tick(c), 99);
}

// The render clock is a jitter buffer: it should sit roughly
// INTERP_TARGET_DELAY_TICKS behind the newest snapshot so a late packet is
// absorbed instead of freezing remote players.
//
// Regression: the clock was only ever clamped, never pulled back. A single
// stall pushed it up against the max clamp (0.5 ticks behind newest) and it
// stayed there for the rest of the session, so the buffer protected against
// exactly one hiccup and every later one stuttered.
TEST(client_render_clock_refills_buffer_after_a_stall) {
  NetAddress addr{};
  CHECK(net_address_parse("127.0.0.1", 27960, &addr));

  Client c{};
  CHECK(client_start(c, addr, "t"));
  c.state = CLIENT_CONNECTED;

  const double frame = 1.0 / TICK_RATE;
  double now = 100.0;
  uint32_t tick = 1;

  // Feed one snapshot per frame until the clock settles.
  for (int i = 0; i < 240; ++i) {
    now += frame;
    c.newest_snap_tick = tick++;
    c.last_recv_time = now;
    client_receive(c, now, nullptr);
  }
  float settled = static_cast<float>(c.newest_snap_tick) - c.render_tick;
  CHECK_NEAR(settled, INTERP_TARGET_DELAY_TICKS, 0.35f);

  // A stall: 10 frames pass with no new snapshot. The clock keeps advancing
  // and eats into the buffer, which is what the buffer is for.
  for (int i = 0; i < 10; ++i) {
    now += frame;
    client_receive(c, now, nullptr);
  }
  float after_stall = static_cast<float>(c.newest_snap_tick) - c.render_tick;
  CHECK(after_stall < settled);

  // Once snapshots resume, the buffer must refill rather than stay spent.
  for (int i = 0; i < 240; ++i) {
    now += frame;
    c.newest_snap_tick = tick++;
    c.last_recv_time = now;
    client_receive(c, now, nullptr);
  }
  float recovered = static_cast<float>(c.newest_snap_tick) - c.render_tick;
  CHECK(recovered > after_stall);
  CHECK_NEAR(recovered, INTERP_TARGET_DELAY_TICKS, 0.35f);
}

// The clock must never render past the newest snapshot the client actually
// holds, however long the server has been silent.
TEST(client_render_clock_never_outruns_received_data) {
  NetAddress addr{};
  CHECK(net_address_parse("127.0.0.1", 27960, &addr));

  Client c{};
  CHECK(client_start(c, addr, "t"));
  c.state = CLIENT_CONNECTED;

  double now = 50.0;
  c.newest_snap_tick = 600;
  c.last_recv_time = now;
  client_receive(c, now, nullptr);

  for (int i = 0; i < 400; ++i) {
    now += 1.0 / TICK_RATE;
    client_receive(c, now, nullptr);
    CHECK(c.render_tick <= static_cast<float>(c.newest_snap_tick));
  }
}
