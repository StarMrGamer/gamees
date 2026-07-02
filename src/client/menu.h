#pragma once

enum MenuResult {
  MENU_HOST,
  MENU_JOIN,
  MENU_QUIT,
};

MenuResult menu_run(char* out_address, int cap);
