import { readFileSync } from "node:fs";
import { join } from "node:path";
import { alignStep } from "./compare.ts";

export function alignLogicStep(runDir: string, scene: string): string | null {
    const step = alignStep(runDir, scene);
    return step === null ? null : String(step);
}

export function lastStatusStep(runDir: string): string | null {
    let status = "";
    try {
        status = readFileSync(join(runDir, "status.txt"), "utf8");
    } catch {
        return null;
    }
    const lines = status.trim().split("\n").filter(Boolean);
    if (lines.length === 0) return null;
    const parts = lines[lines.length - 1].trim().split(/\s+/);
    return parts[0] ?? null;
}

export function countStatusLines(runDir: string): number {
    try {
        const status = readFileSync(join(runDir, "status.txt"), "utf8");
        return status.split("\n").filter((l) => l.trim()).length;
    } catch {
        return 0;
    }
}
