import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";

export const STATUS_FIELDS = ["scene", "map", "course", "pack", "x", "y", "z"] as const;
export type StatusRow = readonly [
    string,
    string,
    string,
    string,
    string,
    string,
    string,
];

const ALIGN_MARKER = /\[test\] align at logic step (\d+) scene=(\S+)/;

function readLines(path: string): string[] {
    if (!existsSync(path)) return [];
    return readFileSync(path, { encoding: "utf8" }).split("\n");
}

export function parseStatusLines(lines: string[]): { step: number; row: StatusRow }[] {
    const rows: { step: number; row: StatusRow }[] = [];
    for (const line of lines) {
        const parts = line.trim().split(/\s+/);
        if (parts.length !== 1 + STATUS_FIELDS.length) continue;
        const step = Number.parseInt(parts[0], 10);
        if (!Number.isFinite(step)) continue;
        rows.push({ step, row: parts.slice(1) as StatusRow });
    }
    return rows;
}

export function alignStep(runDir: string, scene: string): number | null {
    const wanted = scene.toLowerCase();
    for (const line of readLines(join(runDir, "log"))) {
        const m = ALIGN_MARKER.exec(line);
        if (m && m[2].toLowerCase() === wanted) {
            return Number.parseInt(m[1], 10);
        }
    }
    for (const { step, row } of parseStatusLines(readLines(join(runDir, "status.txt")))) {
        if (row[0].toLowerCase() === wanted) return step;
    }
    return null;
}

export function rebasedSteps(
    runDir: string,
    origin: number,
    steps?: number,
): Map<number, StatusRow> {
    const out = new Map<number, StatusRow>();
    for (const { step, row } of parseStatusLines(readLines(join(runDir, "status.txt")))) {
        const rel = step - origin;
        if (rel < 0 || (steps !== undefined && rel > steps)) continue;
        out.set(rel, row);
    }
    return out;
}

function describeRow(row: StatusRow | undefined): string {
    if (row === undefined) return "(no line)";
    return STATUS_FIELDS.map((name, i) => `${name}=${row[i]}`).join(" ");
}

export function compareRunFolders(
    runA: string,
    runB: string,
    scene: string,
    steps?: number,
): { code: number; report: string[] } {
    const originA = alignStep(runA, scene);
    const originB = alignStep(runB, scene);
    const report = [`align ${scene}: ${runA} step ${originA}, ${runB} step ${originB}`];
    if (originA === null || originB === null) {
        report.push(`no align step (the run never reached scene ${scene})`);
        return { code: 2, report };
    }
    const a = rebasedSteps(runA, originA, steps);
    const b = rebasedSteps(runB, originB, steps);
    if (a.size === 0 || b.size === 0) {
        report.push("no status lines at or after the align step");
        return { code: 2, report };
    }
    const last = Math.min(Math.max(...a.keys()), Math.max(...b.keys()));
    const keys = [...new Set([...a.keys(), ...b.keys()])].filter((k) => k <= last).sort((x, y) => x - y);
    const bad = keys.filter((k) => {
        const ra = a.get(k);
        const rb = b.get(k);
        if (ra === undefined || rb === undefined) return ra !== rb;
        return STATUS_FIELDS.some((_, i) => ra[i] !== rb[i]);
    });
    if (bad.length === 0) {
        report.push(`status  ${keys.length} steps, identical`);
        return { code: 0, report };
    }
    const first = bad[0];
    report.push(`status  ${keys.length} steps, ${bad.length} differ, first at step ${first}`);
    report.push(`   A: ${describeRow(a.get(first))}`);
    report.push(`   B: ${describeRow(b.get(first))}`);
    const ra = a.get(first);
    const rb = b.get(first);
    if (ra && rb) {
        const names = STATUS_FIELDS.filter((_, i) => ra[i] !== rb[i]);
        if (names.length > 0) {
            report.push(`   differs in: ${names.join(", ")}`);
        }
    }
    return { code: 1, report };
}
