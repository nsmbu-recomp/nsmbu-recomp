import { flagString, parseArgv } from "../parse.ts";
import { compareRunFolders } from "./compare.ts";

export function cmdCompareRuns(_repo: string, rest: string[]): void {
    const { flags, positional } = parseArgv(rest, ["scene", "steps"], ["scene", "steps"]);
    const [runA, runB] = positional;
    if (!runA || !runB) {
        throw new Error("usage: nsmbu compare-runs RUN_A RUN_B [--scene NAME] [--steps N]");
    }
    const scene = flagString(flags, "scene") ?? process.env.ALIGN ?? "Title";
    const stepsRaw = flagString(flags, "steps");
    let steps: number | undefined;
    if (stepsRaw !== undefined) {
        if (!/^\d+$/.test(stepsRaw)) {
            throw new Error("usage: nsmbu compare-runs --steps N");
        }
        steps = Number.parseInt(stepsRaw, 10);
    }
    const { code, report } = compareRunFolders(runA, runB, scene, steps);
    process.stdout.write(`${report.join("\n")}\n`);
    if (code !== 0) {
        process.exit(code);
    }
}
