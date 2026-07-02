#pragma once

#include "game/game_state.h"
#include "net/protocol.h"

void snapshot_write(const GameState& s, NetWriter& w);
bool snapshot_read(GameState& s, NetReader& r);
void snapshot_interpolate(const GameState& a, const GameState& b, float t, GameState& out);
