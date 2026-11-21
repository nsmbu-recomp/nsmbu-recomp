import assert from "node:assert/strict";
import test from "node:test";
import { evalBody } from "../../tools/cli/src/agent/eval.ts";

test("eval --list", () => {
    assert.deepEqual(evalBody(["--list"]), { cmd: "eval", list: true });
});

test("eval name", () => {
    assert.deepEqual(evalBody(["ping"]), { cmd: "eval", name: "ping" });
});

test("eval json object", () => {
    assert.deepEqual(evalBody(["ping", "{\"n\":1}"]), { cmd: "eval", name: "ping", args: { n: 1 } });
});

test("eval rejects a json fragment", () => {
    assert.throws(() => evalBody(["ping", "{"]), /not valid/);
});
