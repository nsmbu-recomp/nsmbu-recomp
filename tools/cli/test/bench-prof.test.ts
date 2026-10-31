import assert from "node:assert/strict";
import test from "node:test";
import { otherGameLines } from "../src/bench/cmd.ts";
import { parseProf, scoreLoadTrace } from "../src/bench/prof.ts";

function window(frame: number): string {
    return [
        `[prof] frame ${frame}: 120 frames (40 hold), 16.70 ms/frame, 59.9 swaps/s, 60.0 logic steps/s; render thread CPU 4.20 ms/frame, in ops 3.10 ms/frame, idle (waiting for commands) 8.00 ms/frame`,
        `[prof] render thread waits ms/frame: GPU 1.50 (0.20/frame) acquire 0.40 present 0.10`,
    ].join("\n");
}

const LOG = [
    "[scene] FileSelect (controller GamePad)",
    window(240),
    "[scene] CourseSelect from save (controller GamePad)",
    window(3320),
    window(3440),
    window(3560),
    window(3680),
    window(3800),
    window(3920),
].join("\n");

test("prof windows keep frame time and the gpu wait", () => {
    const windows = parseProf(LOG);
    assert.equal(windows.length, 7);
    assert.equal(windows[1].frame, 3320);
    assert.equal(windows[1].frame_ms, 16.7);
    assert.equal(windows[1].wait_gpu_ms, 1.5);
});

test("load-overworld trace wants the map and six windows on it", () => {
    const score = scoreLoadTrace(LOG);
    assert.equal(score.ok, true);
    assert.equal(score.map.length, 6);
    assert.match(score.detail, /16\.70 ms\/frame/);
    assert.equal(scoreLoadTrace(window(3320)).ok, false);
    assert.equal(scoreLoadTrace(["[scene] FileSelect", "[scene] CourseSelect from save", window(3320)].join("\n")).ok, false);
});

test("a running nsmbu blocks another bench", () => {
    const lines = otherGameLines("  12 /usr/bin/nsmbu\n  13 node\n  14 /home/econn/build/debug/nsmbu\n");
    assert.deepEqual(lines, ["12 /usr/bin/nsmbu", "14 /home/econn/build/debug/nsmbu"]);
});
