#pragma once

#include "platform/socket.h"

// `skill` is an AgentSkill (see ai/agent.h); taken as int so this header
// does not drag the AI into every caller.
int bot_main(NetAddress server, const char* name, int lifetime_seconds, int skill);
