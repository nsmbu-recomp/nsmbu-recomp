#pragma once

#include "guest_addr.h"
#include "input/input.h"
#include "ppc.h"

// MainGame + 0x14 is the game's single-player GamePad flag (SysController id 0).
// Non-zero: id 0 is the GamePad, and ids 1..3 are Pro Controller channels 1..3.
// Zero: ids 0..3 are Pro Controller channels 0..3, and the GamePad is not a player.
inline bool gamepad_is_player_one() {
    const uint32_t game = ld32(GD(0x101E7D7C));
    if (input::pro_controller()) {
        if (game && ld8(game + 0x14)) st8(game + 0x14, 0);
        return false;
    }
    if (!game) return true;
    return ld8(game + 0x14) != 0;
}

// Host player for a KPAD channel. -1 means that channel stays disconnected.
inline int kpad_player(uint32_t chan, bool gamepad_player) {
    if (chan >= (uint32_t)input::kMaxPlayers) return -1;
    if (gamepad_player && chan == 0) return -1;
    return (int)chan;
}
