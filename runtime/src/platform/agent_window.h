#pragma once

#include "core/env.h"

#include <SDL3/SDL.h>

namespace agent_window {

inline bool quiet() {
    const char* sock = env::AGENT_SOCK();
    return sock && *sock;
}

inline void apply_hints() {
    if (!quiet()) return;
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
}

inline SDL_WindowFlags flags(SDL_WindowFlags base) {
    if (quiet()) base |= SDL_WINDOW_NOT_FOCUSABLE;
    return base;
}

}
