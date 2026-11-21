import assert from "node:assert/strict";
import test from "node:test";
import { launchArgs, runBuildArgs } from "../../tools/cli/src/launch_plan.ts";

test("cmdRun builds with only --release and launches with --cli and game flags", () => {
    assert.deepEqual(runBuildArgs(true), ["--release"]);
    assert.deepEqual(runBuildArgs(false), []);

    const launch = launchArgs({ noDebug: true, trace: false, gdb: false, lldb: false }, ["1-1", "--no-audio"]);
    assert.ok(launch.includes("--cli"));
    assert.ok(launch.includes("--no-debug"));
    assert.ok(launch.includes("1-1"));
    assert.ok(launch.includes("--no-audio"));
    assert.equal(launch.filter((a) => a === "--release").length, 0);
});

test("cmdCrash passes --test-host-crash as a game flag", () => {
    const launch = launchArgs(
        { noDebug: false, trace: false, gdb: false, lldb: false },
        ["--test-host-crash"],
    );
    assert.ok(launch.includes("--cli"));
    assert.ok(launch.includes("--test-host-crash"));
});