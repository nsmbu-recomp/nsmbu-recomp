import assert from "node:assert/strict";
import test from "node:test";
import { parseTestArgs } from "../../tools/cli/src/playtest/cmd.ts";

test("names and the show flag", () => {
    assert.deepEqual(parseTestArgs(["replay-1-1", "--show"]), {
        list: false,
        show: true,
        names: ["replay-1-1"],
        jobs: 2,
    });
});

test("list stays a flag", () => {
    assert.deepEqual(parseTestArgs(["--list"]), { list: true, show: false, names: [], jobs: 2 });
});

test("--jobs is parsed", () => {
    assert.deepEqual(parseTestArgs(["--jobs", "4", "replay-1-1"]), {
        list: false,
        show: false,
        names: ["replay-1-1"],
        jobs: 4,
    });
});

test("unknown flags are rejected", () => {
    assert.throws(() => parseTestArgs(["--window"]), /unknown option: --window/);
});
