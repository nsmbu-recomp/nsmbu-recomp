import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { alignStep, compareRunFolders, rebasedSteps } from "../src/gametest/compare.ts";

function status(step: number, scene = "CourseTask", x = 1.0, y = 2.0, z = 0.0, pack = "1-1"): string {
    return `${step} ${scene} 0 1 ${pack} ${x.toFixed(3)} ${y.toFixed(3)} ${z.toFixed(3)}`;
}

function makeRun(root: string, name: string, lines: string[], marker?: number): string {
    const run = join(root, name);
    mkdirSync(run, { recursive: true });
    writeFileSync(join(run, "status.txt"), `${lines.join("\n")}\n`);
    let log = "[boot] start\n";
    if (marker !== undefined) {
        log += `[test] align at logic step ${marker} scene=CourseTask\n`;
    }
    writeFileSync(join(run, "log"), log);
    return run;
}

function course(first: number, count: number, offset = 0): string[] {
    return Array.from({ length: count }, (_, i) => status(first + i, "CourseTask", 1.0 + 0.5 * i + offset));
}

let tmp = "";
test.beforeEach(() => {
    tmp = mkdtempSync(join(tmpdir(), "nsmbu-compare-"));
});
test.afterEach(() => {
    rmSync(tmp, { recursive: true, force: true });
});

test("same trace with shifted boot is identical", () => {
    const bootA = Array.from({ length: 39 }, (_, i) => status(i + 1, "Title", 1, 2, 0, "-"));
    const bootB = Array.from({ length: 89 }, (_, i) => status(i + 1, "Title", 1, 2, 0, "-"));
    const a = makeRun(tmp, "a", [...bootA, ...course(40, 30)], 40);
    const b = makeRun(tmp, "b", [...bootB, ...course(90, 30)], 90);
    const { code, report } = compareRunFolders(a, b, "CourseTask");
    assert.equal(code, 0);
    assert.match(report[1], /30 steps, identical/);
});

test("first differing step is rebased", () => {
    const aLines = course(40, 30);
    const bLines = course(90, 30);
    bLines[7] = status(97, "CourseTask", 99.0);
    const a = makeRun(tmp, "a", aLines, 40);
    const b = makeRun(tmp, "b", bLines, 90);
    const { code, report } = compareRunFolders(a, b, "CourseTask");
    assert.equal(code, 1);
    assert.match(report[1], /first at step 7/);
    assert.match(report.join("\n"), /differs in: x/);
});

test("marker missing falls back to status lines", () => {
    const a = makeRun(tmp, "a", [status(3, "Title", 1, 2, 0, "-"), ...course(10, 5)]);
    const b = makeRun(tmp, "b", [status(3, "Title", 1, 2, 0, "-"), ...course(20, 5)]);
    assert.equal(alignStep(a, "coursetask"), 10);
    assert.equal(alignStep(b, "CourseTask"), 20);
    assert.equal(compareRunFolders(a, b, "CourseTask").code, 0);
});

test("missing scene is exit two", () => {
    const a = makeRun(tmp, "a", course(1, 5), 1);
    const b = makeRun(
        tmp,
        "b",
        Array.from({ length: 4 }, (_, i) => status(i + 1, "Title", 1, 2, 0, "-")),
    );
    const { code, report } = compareRunFolders(a, b, "CourseTask");
    assert.equal(code, 2);
    assert.match(report[report.length - 1], /never reached/);
});

test("only the common range counts", () => {
    const a = makeRun(tmp, "a", course(10, 50), 10);
    const b = makeRun(tmp, "b", course(30, 20), 30);
    assert.equal(compareRunFolders(a, b, "CourseTask").code, 0);
});

test("steps limit ignores later difference", () => {
    const aLines = course(10, 40);
    const bLines = course(10, 40);
    bLines[30] = status(40, "CourseTask", -5.0);
    const a = makeRun(tmp, "a", aLines, 10);
    const b = makeRun(tmp, "b", bLines, 10);
    assert.equal(compareRunFolders(a, b, "CourseTask").code, 1);
    assert.equal(compareRunFolders(a, b, "CourseTask", 20).code, 0);
});

test("scene change is a difference", () => {
    const aLines = course(10, 10);
    const bLines = course(10, 10);
    bLines[4] = status(14, "Map");
    const a = makeRun(tmp, "a", aLines, 10);
    const b = makeRun(tmp, "b", bLines, 10);
    const { code, report } = compareRunFolders(a, b, "CourseTask");
    assert.equal(code, 1);
    assert.match(report[1], /first at step 4/);
    assert.match(report[report.length - 1], /scene/);
});

test("partial trailing line is ignored", () => {
    const lines = [...course(10, 5), "15 CourseTask 0 1"];
    const a = makeRun(tmp, "a", lines, 10);
    assert.equal(rebasedSteps(a, 10).size, 5);
});
