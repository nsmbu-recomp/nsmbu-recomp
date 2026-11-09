# Pause menu Settings handoff

Continue shrinks and fades the whole pause menu.
Settings does not.
Closing the settings overlay does not fade that menu back in.
The menu pops or glitches instead.

The next change makes Settings use that same shrink-and-fade.
The overlay opens after the fade finishes.
Closing the overlay plays the menu fade-in.
Continue and Quit keep their own actions.
The controller window stays closed.

## Done when

A on Settings plays the same whole-menu shrink and fade as Continue.
The overlay opens after that fade, and the controller window stays closed.
Closing the overlay plays the fade-in, and the pause menu is visible again.
Continue still returns to the course.
Quit still leaves the course.
B still leaves the pause menu.
One new host test fails on the old immediate bounce and passes with this change.
The person at the machine sees both fades.
A log line is not that check.

## Cause

Settings confirm enters the same close state Continue uses.
That state plays the layout anim named Out.
The hook then bounces back to the open menu on that same visit.
The bounce cancels Out.
The hook also stops layout anim 0 on the press.
Anim 0 is that menu transition.
`block_game_press` keeps `button+0x84` at 1, so the game can skip the decide that starts Out.

The overlay hide writes the draw byte to 0.
That pops the menu out.
On close, the hook writes the draw byte back and calls the In anim.
In does nothing visible when the menu never finished Out.
The menu pops back in.

## States

The course pause object is the pointer at `0x101D830C`.
`0x025FD500` steps it before hook `0x025F3834`.
The state machine is at `+0x220`.
`changeState` is `0x029C4AF0`.

`0x025FE980` fills each state.
Enter is `+0x0C`.
Execute is `+0x14`.
Exit is `+0x1C`.

The open menu is `0x10216DB4`.
Its enter is `0x025FDE00`.
Its execute is `0x025FDF98`.
Enter calls `0x02682848` and clears the button manager.

A decided button stores 3 at `+0x1C`.
Execute then enters `0x10216DD4`.
That is the Continue close path.
Plus or Back (`0x4800`) enters `0x10216DF4` instead.

`0x10216DD4` enter `0x025FE3F4` plays Out and sets `+0xA91`.
Its execute `0x025FE43C` waits, then enters `0x10216D74`.
Its exit `0x025FE48C` clears `+0x8`, `+0xA90`, and `+0xA91`, then resets the button manager.

`0x10216DF4` enter `0x025FE4A4` plays the same Out.
Its execute `0x025FE4EC` waits, then enters `0x10216D74`.
Its exit `0x025FE53C` hides the menu the same way.

The wait reads layout anim 0 through `0x02671AF4(layout, 0)`.
The low bit of anim `+0x2C` means the anim is finished.
Out is the name at `0x10216E34`.
In is the name at `0x10216E14`.
Both play through `0x02671680(layout, 0, name, 0)`.
The layout pointer is at pause `+0`.

`0x10216D74` and `0x10216D94` are the controller window.
They come after Out.
They are the states to leave.
`0x10216DD4` and `0x10216DF4` are the fade.
Leave them in place until Out finishes.

`pause_controller_state` in `runtime/src/hooks/pause_menu_settings.h` currently treats all four ids as the controller window.
`leave_controller_window` bounces the first match back to `0x10216DB4`.
That bounce is the cancel.

## What to change

Let `0x10216DD4` and `0x10216DF4` run for Settings.
On the frame the wait would enter `0x10216D74`, leave that window state and open the overlay.
Call `close_controller_window` if the window object already advanced.
Keep `+0x1C` from starting the window again on the next step.

Remove the `stop_menu_in` call from the Settings press.
That call stops the Out anim.

Stop forcing `button+0x84` to 1 before the game has started Out.
The game decide is what starts this fade.
The port press can still play the button click and the button squash with that fade.
They start together.
A second click after the fade is a separate sound.

Open the overlay when Out has finished.
The fixed 40-frame delay cuts the fade short when it is still running.
The menu hide belongs to the state exit.
Writing the draw byte to 0 during Out pops the menu.

On overlay close, play In with `0x02671680` and then `0x02672A4C`.
Restore the open menu only after that call.
The draw byte alone pops the menu in.
`PauseGame.h` still says `0x10216DD4` starts the controller-window animation.
That sentence is the Out fade.
Correct it in `vendor/decomp/include` when the behavior change lands.

## Checks that stay

The story rows stay Continue, Quit, and Settings.
Settings stays on `Pa_PlayStyle_00`.
The blue panel keeps its original height.
There is no fourth row.
Challenge layouts and `PaCommonWindow` stay unchanged.
The world-map Settings button stays opt-in.
Pause Settings stays on unless the debug checkbox turns it off.
B still leaves this menu.

## Test

Add the host check in `tests/host/pause_menu_settings_test.cpp`.
The check fails while `0x10216DD4` still bounces on the first frame.
The check passes when that state is allowed to play Out.
Run `cmake --build build/linux --target pause_menu_settings_test`, then `./build/linux/tests/pause_menu_settings_test`.
Build the game with `cmake --build build/debug --target nsmbu`.
A normal build does not regenerate hook stubs.
The running process keeps the old image until it starts again.

## Live check

Use a private process.
The command is:

```bash
XDG_CONFIG_HOME=$PWD/.tmp/pause-check/config \
NSMBU_LOG_FILE=$PWD/.tmp/pause-check/nsmbu.log \
NSMBU_AGENT_SOCK=$PWD/.tmp/pause-check/ctl.sock \
./build/debug/nsmbu --game $PWD/game/usa-1.3.0 \
--save $PWD/.tmp/pause-check/save --open 1-1
```

Delete `.tmp/pause-check/nsmbu.log` and `ctl.sock` before the boot.
Press Plus when the log file shows `[input] version=1`.
A Goomba walks into Mario when 1-1 sits idle.
After the menu is up, pause briefly between Down and A so the fade is visible.
One Down from Continue lands on Settings.
Quit is dimmed at the start of 1-1.
Send the Settings A for one frame.
A held A enters the controller path before the fade can finish.
Close the overlay with `nsmbu click close`.
Watch the shrink-and-fade before the overlay.
Watch the fade-in after the close.
Leave the pause menu up afterward.
Confirm `ps` before killing a game.
Kill only the `nsmbu` process whose command contains `.tmp/pause-check`.

The last private session left the course and the process has exited.
`scene=CourseSelect` means the course was left.
Start from 1-1 again.

## Repo

The fade-in line under `CHANGELOG.md` Unreleased is ahead of the live menu.
Update that line when the fade matches Continue.
Record the shrink-and-fade in that same changelog when it is visible.
Commit only when asked.
Leave `vendor/ZeldaWWHDRecomp` unstaged.
Leave `.tmp` untracked.
Game files stay untracked.
Header comments in `vendor/decomp/include` commit in that repo first.

The button squash and its click can still disagree.
A live log showed `Settings press speed 0.00` after the press call.
`0x02AB8920` stores speed 0 when one step runs past the last frame.
That bug is separate.
This handoff is the whole-menu fade.
