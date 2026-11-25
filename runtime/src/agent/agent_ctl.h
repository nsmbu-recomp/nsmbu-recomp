#pragma once

#include <string>

#include "input/input.h"

namespace agent_ctl {

void init();
void shutdown();
void service();
void guest_service();
void guest_observe();
void apply_pad(input::PadState& pad);
bool active();

// Debug builds only. Call before init(). --mcp on the game executable.
void use_mcp(bool on);
// One agent command JSON object with numeric "id" and "cmd". Returns the reply line.
std::string exchange(const std::string& request_json, int timeout_ms);

}
