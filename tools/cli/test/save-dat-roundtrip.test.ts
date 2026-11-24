import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import test from "node:test";
import { decodeSave, encodeSave, verifySavedataBlockCrcs } from "../src/save.ts";

const repo = join(import.meta.dirname, "../../..");
const repoSave = join(repo, "nsmbu/save/common/rp_savedata.dat");
const bakSave = join(repo, "rp_savedata.bak.dat");

function assertRoundtrip(path: string): void {
    const orig = readFileSync(path);
    const back = encodeSave(decodeSave(orig));
    assert.equal(back.length, orig.length, `${path}: length`);
    assert.equal(back.compare(orig), 0, `${path}: bytes`);
}

test("rp_savedata.dat round-trips through JSON unchanged when entry CRCs are stored", (t) => {
    if (!existsSync(bakSave)) {
        return t.skip("missing rp_savedata.bak.dat");
    }
    assertRoundtrip(bakSave);
});

test("encodeSave seals block CRCs after JSON edits", (t) => {
    if (!existsSync(repoSave)) {
        return t.skip("missing nsmbu/save/common/rp_savedata.dat");
    }
    const json = decodeSave(readFileSync(repoSave));
    json.mainSlots[0].lives[0] = 42;
    json.challenge.challenges[0].replayCrc = 0xdeadbeef;
    const out = encodeSave(json);
    assert.equal(verifySavedataBlockCrcs(out), true);
    assert.equal(out[0x10 + 0x0c], 42);
});

test("repo save round-trips with fresh CRCs", (t) => {
    if (!existsSync(repoSave)) {
        return t.skip("missing nsmbu/save/common/rp_savedata.dat");
    }
    const out = encodeSave(decodeSave(readFileSync(repoSave)));
    assert.equal(verifySavedataBlockCrcs(out), true);
});
