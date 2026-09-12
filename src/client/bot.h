#pragma once

#include "platform/socket.h"

// `skill` is an AgentSkill (see ai/agent.h); taken as int so this header
// does not drag the AI into every caller.
int bot_main(NetAddress server, const char* name, int lifetime_seconds, int skill);

// Bots that live and die with the process that started them.
//
// Running bots as separate background processes leaves them alive when the
// game is closed: they keep playing against an empty server, or against a
// server that has gone away, and the next session finds strangers already in
// it. These run as threads instead, so quitting takes them with it.
struct BotSwarm;
BotSwarm* bot_swarm_start(NetAddress server, int count, int skill);
void bot_swarm_stop(BotSwarm* swarm);
