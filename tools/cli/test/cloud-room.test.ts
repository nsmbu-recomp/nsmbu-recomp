import assert from "node:assert/strict";
import test from "node:test";
import { frameHasPicture, inCloudRoom, onPipeBridge } from "../src/agent/cloud-room.ts";

test("slot 1 bridge is the pipe spot", () => {
    assert.equal(onPipeBridge({ x: 6162.9, y: -592, z: 3000 }), true);
    assert.equal(onPipeBridge({ x: 600, y: -848, z: 3000 }), false);
});

function solid(width: number, height: number, rgb: [number, number, number]): Uint8Array {
    const rgba = new Uint8Array(width * height * 4);
    for (let i = 0; i < rgba.length; i += 4) {
        rgba[i] = rgb[0];
        rgba[i + 1] = rgb[1];
        rgba[i + 2] = rgb[2];
        rgba[i + 3] = 255;
    }
    return rgba;
}

test("a pipe iris is not a finished picture", () => {
    assert.equal(frameHasPicture(solid(32, 16, [0, 0, 0]), 32, 16), false);
    assert.equal(frameHasPicture(solid(32, 16, [80, 160, 220]), 32, 16), true);
    const iris = solid(32, 16, [80, 160, 220]);
    for (let y = 0; y < 16; y++) {
        for (let x = 24; x < 32; x++) {
            const i = (y * 32 + x) * 4;
            iris[i] = 0;
            iris[i + 1] = 0;
            iris[i + 2] = 0;
        }
    }
    assert.equal(frameHasPicture(iris, 32, 16), false);
});

test("the pipe room is not the bridge or the pipe ride", () => {
    assert.equal(inCloudRoom({ x: 472, y: -608, z: 3000 }), true);
    assert.equal(inCloudRoom({ x: 6162.9, y: -592, z: 3000 }), false);
    assert.equal(inCloudRoom({ x: 448, y: -608, z: -1800 }), false);
});
