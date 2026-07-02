#pragma once

#include "game/game_state.h"
#include "game/map.h"

void player_move(Player& p, const PlayerInput& in, const Map& map, float dt);
