#include <cassert>
#include "hle/pad_players.h"

int main() {
    assert(kpad_player(0, true) == -1);
    assert(kpad_player(1, true) == 1);
    assert(kpad_player(2, true) == 2);
    assert(kpad_player(3, true) == 3);
    assert(kpad_player(4, true) == -1);
    assert(kpad_player(0, false) == 0);
    assert(kpad_player(1, false) == 1);
    assert(kpad_player(2, false) == 2);
    assert(kpad_player(3, false) == 3);
    assert(kpad_player(4, false) == -1);
    return 0;
}
