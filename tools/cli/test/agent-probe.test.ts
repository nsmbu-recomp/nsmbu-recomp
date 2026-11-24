import assert from "node:assert/strict";
import test from "node:test";
import { deflateSync } from "node:zlib";
import { arrived, cursorSelected, decodePng, delta, newerThan, placeKind, sample, tvRect } from "../src/agent/probe.ts";

function chunk(type: string, data: Buffer): Buffer {
    const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(0);
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    return Buffer.concat([len, body, crc]);
}

function png(w: number, h: number, rgba: Uint8Array): Buffer {
    const stride = w * 4;
    const raw = Buffer.alloc((stride + 1) * h);
    for (let y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0;
        Buffer.from(rgba.subarray(y * stride, (y + 1) * stride)).copy(raw, y * (stride + 1) + 1);
    }
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0);
    ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8;
    ihdr[9] = 6;
    const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
    return Buffer.concat([sig, chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}

test("png decode and pixel delta", () => {
    const a = new Uint8Array([10, 20, 30, 255, 0, 0, 0, 255]);
    const b = new Uint8Array([10, 20, 40, 255, 0, 0, 0, 255]);
    const img = decodePng(png(2, 1, a));
    assert.equal(img.w, 2);
    assert.equal(img.h, 1);
    assert.deepEqual(img.rgba, a);
    const d = delta(sample(img, tvRect(null, 2, 1)), b);
    assert.equal(d.max, 10);
    assert.ok(d.mean > 0 && d.mean < 10);
});

test("place and cursor matching", () => {
    assert.equal(placeKind("1-1"), "course");
    assert.equal(placeKind("title"), "title");
    assert.equal(placeKind("nope"), null);
    assert.equal(arrived("map", { scene: "CourseSelect", map: true }), true);
    assert.equal(arrived("1-1", { scene: "CourseTask", course: true, pack: "1-1" }), true);
    assert.equal(arrived("1-1", { scene: "CourseTask", course: true, pack: "1-2" }), false);
    assert.equal(cursorSelected([{ name: "Settings", selected: true }], "settings"), true);
    assert.equal(cursorSelected([{ name: "Settings", selected: false }], "Settings"), false);
});

test("binary is stale when a source is newer", () => {
    assert.equal(newerThan(10, [1, 9]), false);
    assert.equal(newerThan(10, [11]), true);
});
