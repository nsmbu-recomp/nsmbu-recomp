import assert from "node:assert/strict";
import test from "node:test";
import { replayCourse, replayVerdict } from "../../tools/cli/src/playtest/replay.ts";

test("course line is the pack to open", () => {
    assert.equal(replayCourse("version=1\ncourse=2-1\norigin=control\n"), "2-1");
});

test("a matched finish passes", () => {
    const log = "[input] replaying 85 samples from x (85 position checks)\n[input] replay finished at step 2441, player state matched\n";
    assert.equal(replayVerdict(log), "pass");
});

test("a mismatch finish fails", () => {
    const log = "[input] replay finished at step 100, 1 position mismatches, first at step 40\n";
    assert.equal(replayVerdict(log), "fail");
});

test("divergence fails before a later stop line", () => {
    const log = "[input] replay diverged at step 12: recorded 1 2 3 now 9 9 9\n[input] replay stopped at step 12; live input from now on\n";
    assert.equal(replayVerdict(log), "fail");
});

test("exit=none stop passes when nothing diverged", () => {
    assert.equal(replayVerdict("[input] replay stopped at step 319; live input from now on\n"), "pass");
});

test("a running log is still pending", () => {
    assert.equal(replayVerdict("[input] replaying 4 samples from x (4 position checks)\n"), "pending");
});
