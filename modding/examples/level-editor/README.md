# Level editor

Runtime level editing on top of the `nsmbu.Level` actor APIs.
It spawns the selected object near player 1, and on option also deletes actors of that profile (see `docs/modding/api.md`).
Actor handles come from `level.actors()`, spawns go through `level.spawn(profile_id, x, y, z)`.

The spawn interval and the X/Y offsets are options.
Objects spawn one frame after the call, so the status line may trail by one step.

Profile ids come from the game: coin 395, goomba 467, mushroom 591, fire flower 592, one-up 597, star 853.
Script-only packages apply live without a restart.
