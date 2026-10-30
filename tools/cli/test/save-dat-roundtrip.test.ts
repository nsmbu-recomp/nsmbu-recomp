import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import test from "node:test";
import { decodeSave, encodeSave, verifySavedataBlockCrcs } from "../src/save.ts";

function assertRoundtrip(path: string): void {
    const orig = readFileSync(path);
    const back = encodeSave(decodeSave(orig));
    assert.equal(back.length, orig.length, `${path}: length`);
    assert.equal(back.compare(orig), 0, `${path}: bytes`);
}

test("rp_savedata.dat round-trips through JSON unchanged when entry CRCs are stored", () => {
    const repo = join(import.meta.dirname, "../../..");
    assertRoundtrip(join(repo, "rp_savedata.bak.dat"));
});

test("encodeSave seals block CRCs after JSON edits", () => {
    const repo = join(import.meta.dirname, "../../..");
    const path = join(repo, "save/common/rp_savedata.dat");
    const json = decodeSave(readFileSync(path));
    json.mainSlots[0].lives[0] = 42;
    json.challenge.challenges[0].replayCrc = 0xdeadbeef;
    const out = encodeSave(json);
    assert.equal(verifySavedataBlockCrcs(out), true);
    assert.equal(out[0x10 + 0x0c], 42);
});

test("repo save round-trips with fresh CRCs", () => {
    const repo = join(import.meta.dirname, "../../..");
    const path = join(repo, "save/common/rp_savedata.dat");
    const out = encodeSave(decodeSave(readFileSync(path)));
    assert.equal(verifySavedataBlockCrcs(out), true);
});
