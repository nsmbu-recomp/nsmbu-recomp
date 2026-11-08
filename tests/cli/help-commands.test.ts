import assert from "node:assert/strict";
import test from "node:test";
import { HELP } from "../../tools/cli/src/help.ts";

test("removed CLI commands stay out of help", () => {
    assert.doesNotMatch(HELP, /\bnsmbu wine\b/);
    assert.doesNotMatch(HELP, /\bnsmbu scripted\b/);
    assert.doesNotMatch(HELP, /\bnsmbu perf\b/);
    assert.match(HELP, /nsmbu profile \[slot\] \[--cpu\]/);
});
