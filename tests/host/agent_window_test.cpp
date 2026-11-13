#include "core/env.h"
#include "platform/agent_window.h"

#include <cstdio>
#include <cstdlib>

static void put_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    env::unset(env::AGENT_SOCK);
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "1");
    agent_window::apply_hints();
    check(SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true), "normal show still activates");
    check(SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, true), "normal raise still activates");
    check(SDL_GetHintBoolean(SDL_HINT_MOUSE_AUTO_CAPTURE, true), "normal click can capture the mouse");
    check((agent_window::flags(0) & SDL_WINDOW_NOT_FOCUSABLE) == 0, "normal window can take focus");

    put_env(env::AGENT_SOCK.name(), "/tmp/nsmbu-agent.sock");
    agent_window::apply_hints();
    check(!SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true), "agent show stays back");
    check(!SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, true), "agent raise stays back");
    check(!SDL_GetHintBoolean(SDL_HINT_MOUSE_AUTO_CAPTURE, true), "agent click does not capture the mouse");
    check((agent_window::flags(SDL_WINDOW_RESIZABLE) & SDL_WINDOW_NOT_FOCUSABLE) != 0, "agent window is not focusable");
    check((agent_window::flags(SDL_WINDOW_RESIZABLE) & SDL_WINDOW_RESIZABLE) != 0, "agent window keeps its other flags");

    env::unset(env::AGENT_SOCK);
    return failures ? 1 : 0;
}
