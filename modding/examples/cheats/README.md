# Cheats

Phase 2 Lua package for common NSMBU course assists.
It uses the high-level `nsmbu.Level` and `nsmbu.Player` APIs each logic step.
Those helpers also cover course coins/score and player pos/speed/grounded (see `docs/modding/api.md`).
It does not need hooks.

Effects when the matching option is on:

- Unlimited time calls `level.set_time(999)`.
- Unlimited lives sets each entered player's lives to 99.
- Keep power-up restores the highest power-up mode seen this course when a hit drops it.
- Jump height scale multiplies vertical speed once at the start of each rise.

Enable the package in Mods, turn on the options you want, and play a course.
Script-only packages apply live without a restart.
