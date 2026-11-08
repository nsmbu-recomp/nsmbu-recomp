---
name: profile
description: "Measure NSMBU frame time, CPU samples, or GPU timestamp intervals, or score a world-map bench. Use when a frame is slow, a hot path needs a sample, GPU time needs a timestamp, or the world map needs a bench score."
---

# Profile

Pick one run below.
The printed lines are the result.

## Course frame time

Run `nsmbu build --release`, then `nsmbu profile`.
That loads save slot 1 at TV frame 700 and quits at frame 1100.
The binary is `build/release/nsmbu` when that file exists.
The `[profile]` block is the last `[prof]` window: ms/frame, render CPU, waits, top ops, and top draw phases.
The log path is on that block.
Done when the `[profile]` block has been printed.

## Course CPU sample

Run `nsmbu profile --cpu` on that same scene.
It prints the top Linux `perf` symbols and the `[profile]` block.
`nsmbu profile --cpu --attach PID --seconds 30` samples a game that is already running.
Done when the top symbols have been printed.

## GPU intervals

Use this when the question is time on the GPU.
The digest line `waits ms/frame: gpu` is render-thread time blocked on the GPU.
Draw phases in that digest are render-thread CPU.
A GPU load percentage is the Android performance report (`GPU busy`).
Desktop and macOS use Vulkan timestamp queries.

Run the course scene again with the timestamp switch already in the environment:

```sh
NSMBU_VK_GPU_TIMESTAMPS=1 nsmbu profile
```

`nsmbu profile` keeps a `NSMBU_*` value that is already set.
Read `[vulkan GPU timestamps] submission interval sum` in `.tmp/repro/nsmbu.log`.
That number is the summed submission interval per frame.
It can include queue stalls.
`unsupported` or `all-zero/emulated timing UNUSABLE` means this device cannot time the GPU.
Done when that submission-interval line is in the log, or the log says the queries are unsupported or unusable.

Name a pass only after that line is usable.
Set `NSMBU_VK_GPU_PASS_TIMESTAMPS=1` as well and run `nsmbu profile` again.
Read `[vulkan GPU scope N]` for the top format and extent buckets.
Turn the pass switch off before a frame-time or bench score.
The scope queries change the frame time.
The switch list is "Optional performance diagnostics" in `docs/developer/vulkan.md`.

## World map

`nsmbu bench overworld` scores six `[prof]` windows after the map has settled.
`nsmbu bench load-overworld` scores the windows that cover the load into the map.
The bench copies `nsmbu/` settings and leaves the window visible.
The score line and the trace path are the result.
Done when that score line has been printed.

## One name from the printout

Follow the phase, symbol, or GPU scope named in those lines.
Open `.tmp/repro/nsmbu.log` or `.tmp/perf/report.txt` for that name.
A check that the recorded 1-1 path still matches is `nsmbu test replay-1-1`.
