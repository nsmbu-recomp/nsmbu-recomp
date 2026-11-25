# Agent verification

Commands, CI mirrors, and gaps for checks in this repo.

For the day-to-day decision, read the `debug` skill (`.cursor/skills/debug/SKILL.md`).
The loop inventory is [agent-debug.md](agent-debug.md).

## Policy stack

**`AGENTS.md`** — Before a live check, a scenario compare, or a decompile, read the `debug` skill.

**`debug` skill** (`.cursor/skills/debug/SKILL.md`) — Pick the smallest loop that can fail the change, then run it.

**Docs** — [agent-debug.md](agent-debug.md) lists the four loops.
[automation.md](automation.md) names `nsmbu scenario` and `nsmbu compare-runs`.
The tiers below are the command list and the CI mirrors.

### Decision rule

Read the `debug` skill.
Pick the smallest loop in [agent-debug.md](agent-debug.md) that can fail the change.
Run that loop.

```mermaid
flowchart TD
  done[Checking a change]
  done --> readDebug[Read the debug skill]
  readDebug --> pick[Pick the smallest loop that can fail]
  pick --> run[Run that loop]
```

## Tier A — No game dump

These use placeholder guest code from `recomp/stubgen.py`, not a `game/` tree.
CI runs them in `.github/workflows/linux.yml` (and related workflows).

### C++ unit tests (`ctest`)

Configure and build like Linux CI, then:

```bash
python3 recomp/stubgen.py build/gen-stub
cmake -S . -B build/linux -G Ninja -DGEN_DIR="$PWD/build/gen-stub" …
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure
```

The root `CMakeLists.txt` registers many tests: mods, input (`input_mapping`, issue #9), interpolation (`interp_picture`, `interp_joints`, `interp_pacing`), portable state, extract crypto, SDL host tests, Vulkan-only tests when enabled, and more.

**Shortcut:** `python3 tools/build/build.py` for a warm debug link after runtime-only edits.
See the `cli` skill for when a recompile is required.

### Python tool tests (Linux CI, after `ctest`)

- `installer/test_setup.py` (includes issue #11 Windows paste test)
- `recomp/test_builds.py`, `test_output.py`, `test_pool.py`
- `tools/android/test_apk_cli.py`
- `tools/lib/test_dev_game_icon.py`
- `tools/test_shaderprep.py`
- `python3 -m unittest discover -s tools/bench -p "test_*.py"`

### Vulkan renderer smoke (no gameplay)

Under `xvfb-run` with validation env vars.
Several variants run in `linux.yml`: default barriers, sync negative control, conservative barriers, BC decode verify.

```bash
./build/linux/nsmbu --renderer-smoke
```

Logs must contain `[renderer smoke] PASS:` and must not show validation errors (exact greps are in the workflow).

### CLI typecheck

`docs/developer/cli.md` mentions `tsc --noEmit` for the CLI itself.
That is not wired into the `verify` skill or CI as a single mandatory agent step.

### Scripted run comparison

Documented in `docs/developer/automation.md`.
Not the same as playtests.

- `nsmbu scenario OUT [VAR=value …]` writes `status.txt` per logic step.
- `nsmbu compare-runs RUN_A RUN_B` aligns two runs on a scene and diffs them.

Useful for comparing two builds without adding a new `PlayCase`.

### Git push hook

`.githooks/pre_push.py` (see `pre-release` skill) builds a clean tree or worktree and runs full `ctest`, `test_setup.py`, and `test_builds.py`.
It does **not** run `nsmbu test`.

## Tier B — Needs `game/` and a debug build

Default dump per `verify`: **`usa-1.3.0`**.
The active dump is the `game/active` file described in `game/README.md`.

### Playtests

The TypeScript playtest runner is gone.
Scripted checks use `tools/run/scripted-run.sh`.
A live look uses `--mcp` on the debug executable.

Registered cases (run `--list` for summaries):

- `startup` — #6 boot with save
- `opening-cinematic`, `save-roundtrip`, `save-state`, `course-enter` — #14 Quick Save / map / high fps
- `course-enter-120` — #15 at 120 fps
- `map-shadow-120` — #20 at 120 fps
- `opened-course` — 0.6.1 course-open / leave-to-map
- `pose-direction-120`, `logic-rate-120` — interpolation / frame pacing
- `replay-1-1`, `replay-2-1` — recorded course input and `[prof]` course measurement (`replay-2-1` skipped in `nsmbu test` for now)
- `replay-bug-1` — intro skip into 2-1, then quit onto `W201`
- `widescreen-map-ui`, `widescreen-title` — 21:9 UI (skipped in `nsmbu test` until screenshot/replay timing is stable)

Each case uses `.tmp/playtest/<name>/` for save, log, and userdata (`NSMBU_USER_DIR` under that folder).

Playtests are **not** in GitHub CI (no game dump in CI).
Agents with a local dump are the main line for game regressions.
`pre-release` requires full `nsmbu test`.

Issue coverage outside playtests (see `docs/developer/automation.md`):

- #9 multiplayer bindings — `ctest -R input_mapping`
- #11 Windows common-key paste — `installer/test_setup.py`
- #7 choppy gameplay — no deterministic playtest; shader precompile and Vulkan smoke cover mitigations
- 0.6.2 mismatched color-target freeze — Vulkan renderer smoke (`mismatched_color_targets`)

### Short scene boots (`scenes` skill)

Recipes in `.agents/skills/scenes/references/*.md`.
Boot, poll `nsmbu wait` / `nsmbu status`, save `nsmbu screenshot .tmp/repro/…`.
Checks that must keep passing belong in a `PlayCase`, not a one-off boot.

### Live look (while writing a case)

`nsmbu open`, `status`, `press`, `stick`, `wait`, `screenshot`, `close`.
One debug game with `--mcp`.
Close it with `session_close` before a scripted boot.

### Scripted boots

`tools/run/scripted-run.sh` reads `NSMBU_EXIT_AT_FRAME`, `NSMBU_DUMP_FRAMES`, `NSMBU_PRESS`, and related vars.
Frame PNGs land in `.tmp/<name>/`.
Flicker checks: `python3 tools/frames/flicker.py`.

### Course replay playtests

`NSMBU_REPLAY_INPUT` plays the pad logs in `tools/replay/`.
`replay-bug-1` opens `2-1` with the intro skip, quits with `leave`, and checks the map player stands on `W201`.
The case passes on a position match.
`[prof]` in `.tmp/playtest/<name>/nsmbu.log` is the course measurement.
See `docs/developer/automation.md`.

### Performance bench

```bash
nsmbu bench --list
nsmbu bench load-overworld
```

Visible window; trace under `.tmp/bench/…`.
Not a pass/fail regression harness like `nsmbu test`.

### Build and dump maintenance

| Situation | Command |
| --- | --- |
| Runtime-only C++ change | `nsmbu build` |
| Hooks / recomp / version switch | `nsmbu game <id> --recomp` then `nsmbu build` |
| Interp pose / camera | `interp_picture_test` (ctest) plus env flow in `docs/developer/env.md`; playtest `pose-direction-120` |

### Special runtime checks

- `nsmbu crash` — host crash path / crash log (Debug build)
- `nsmbu wine` — Windows binary under Wine (heavy; CI has `windows.yml`)
- `nsmbu installer --no-open` — release-style Linux package (`pre-release`)

## Tier C — Android

From `verify`:

- `nsmbu apk` — build release-style setup APK (no game inside), install via adb
- `python3 tools/android/apk.py --list-devices`, `--device SERIAL` when multiple devices

CI `android` workflow: placeholder native build, Gradle release, `tools/release/guard.py`.
No on-device gameplay in CI.

**Gap:** touch controls and phone vibration are Android-only; `nsmbu test` does not cover them (`docs/developer/android/implementation.md`).

## Tier D — Human only

Use the `verify` player-step block: Host, Place, Actions, Pass.
Label **unverified** until a person completes those steps.

Examples from docs:

- Full level play, feel, and sessions beyond what `input_mapping` ctest covers
- Issue #7 where no deterministic playtest exists
- Android touch UX

## Tier E — CI and release (not every local change)

| Workflow | What it proves |
| --- | --- |
| `linux.yml` | Stub build, ctest, Python suites, renderer smoke variants |
| `windows.yml` | Windows stub build, ctest, smoke under Wine/lavapipe |
| `android.yml` | Placeholder APK, legal guard |
| `release.yml` | Multi-platform release artifacts, signing, extra smokes |

The **ci-investigator** subagent is for explaining one failed PR check, not routine finish verification.

## Tier F — Review, not execution

When explicitly requested:

- **Bugbot** subagent — local diff review
- **Security Review** subagent
- User skill `misc/code-review`

These do not replace tests.

## Decomp (separate from the port)

Decomp source is the submodule `vendor/decomp`.
The decompile agent is the only writer of that tree.
Read [decomp.md](decomp.md) for the one-function loop.
The procedure is [`.cursor/agents/decompile.md`](../../.cursor/agents/decompile.md).
The check is the `python3 tools/rpx/ppcdis.py` listing against the C++.
This repo cannot prove a function with generated inputs.
The port compile omits `vendor/decomp`.
Wind Waker HD has these decomp checks.
This repo has neither of the first two.

- a generated-input harness (`tools/verify` and `verify.py`)
- recorded taps (`mktap`)

The decomp build swaps a verified C++ body in for the recompiled function.
That build is not in this tree yet.
The procedure is the `decomp-build` skill (`.cursor/skills/decomp-build/SKILL.md`).

## Gaps and cleanup targets

**Strong today**

- Finish rule in `AGENTS.md` plus `verify` skill
- `nsmbu test` as canonical game regression API with docs in `automation.md`
- Rich Tier A mirror in `linux.yml` that agents can replay locally

**Fragmented or easy to miss**

- `verify` mentions unit tests generically but not `ctest`, Python discovers, or `--renderer-smoke`
- Playtests never run in CI; agents without `game/usa-1.3.0` cannot fully verify game changes
- Three short-check paths overlap: `scenes`, live look, `nsmbu scripted` — skill says lasting checks → playtest, nothing enforces it
- `pre_push` ≠ `verify` ≠ `pre-release` — different command subsets
- Android verify is install-shaped, not gameplay automation
- CLI TypeScript has no first-class “run before finish” step in `verify`.

## Related docs

- [automation.md](automation.md) — playtests, scripted boots, gametest compare
- [cli.md](cli.md) — full `nsmbu` command list
- [env.md](env.md) — `NSMBU_*` variables and interpolation checks
