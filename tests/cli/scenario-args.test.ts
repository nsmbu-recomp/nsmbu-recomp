import assert from "node:assert/strict";
import { closeSync, existsSync, mkdirSync, openSync } from "node:fs";
import test from "node:test";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { parseScenarioConfig } from "../../tools/cli/src/gametest/scenario.ts";
import { runtimeArgs, stripRuntime } from "../../tools/cli/src/runtime_flags.ts";

const repo = join(fileURLToPath(new URL(".", import.meta.url)), "..", "..");
const saveDir = join(repo, "nsmbu", "save");
const binPath = join(repo, ".tmp", "scenario-test-bin");

test("scenario --align is parsed and not passed via NSMBU_ALIGN env", () => {
    if (!existsSync(saveDir)) mkdirSync(saveDir, { recursive: true });
    if (!existsSync(binPath)) {
        mkdirSync(join(repo, ".tmp"), { recursive: true });
        closeSync(openSync(binPath, "w"));
    }
    const cfg = parseScenarioConfig(repo, [
        join(repo, ".tmp", "scenario-test-out"),
        "--align",
        "Menu",
        "--save",
        saveDir,
        "--bin",
        binPath,
    ]);
    assert.equal(cfg.align, "Menu");
    const vars = { NSMBU_TEST_ALIGN_SCENE: cfg.align };
    assert.deepEqual(runtimeArgs(vars), ["--test-align-scene=Menu"]);
    const env = stripRuntime({ ...process.env, NSMBU_ALIGN: "Menu" });
    assert.equal(env.NSMBU_ALIGN, undefined);
});