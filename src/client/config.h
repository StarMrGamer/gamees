#pragma once

#include "client/game_client.h"

const char* client_config_path();
const char* client_jump_bind_name(uint8_t bind);
bool client_jump_bind_parse(const char* text, uint8_t* out);
const char* client_airjump_bind_name(uint8_t bind);
bool client_airjump_bind_parse(const char* text, uint8_t* out);
bool client_config_load(ClientSettings& settings);
bool client_config_save(const ClientSettings& settings);
