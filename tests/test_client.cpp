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

  PlayerInput in{};
  in.buttons = static_cast<uint16_t>(BTN_JUMP | BTN_AIRJUMP);
  client_send_input(c, in);

  CHECK((c.input_history[0].buttons & BTN_AIRJUMP) != 0);
  uint32_t seq = c.input_history[0].sequence;
  CHECK((c.sent_inputs[seq % CLIENT_INPUT_RING].buttons & BTN_AIRJUMP) != 0);

  client_disconnect(c);
}
