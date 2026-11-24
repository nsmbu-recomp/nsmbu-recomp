import assert from "node:assert/strict";
import test from "node:test";
import { parseTestArgs } from "../src/playtest/cmd.ts";

test("names and the show flag", () => {
    assert.deepEqual(parseTestArgs(["replay-1-1", "--show"]), {
        list: false,
        show: true,
        names: ["replay-1-1"],
    });
});

test("list stays a flag", () => {
    assert.deepEqual(parseTestArgs(["--list"]), { list: true, show: false, names: [] });
});

test("unknown flags are rejected", () => {
    assert.throws(() => parseTestArgs(["--window"]), /unknown option: --window/);
});
