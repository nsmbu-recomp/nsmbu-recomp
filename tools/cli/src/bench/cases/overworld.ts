import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { captureOverworld } from "../capture.ts";
import { scoreOverworldTrace } from "../prof.ts";

export const overworld = {
    name: "overworld",
    summary: "World map only, after the save load has settled.",
    run: async (repo: string) => {
        const work = join(repo, ".tmp", "bench", "overworld");
        mkdirSync(work, { recursive: true });
        const log = await captureOverworld(repo, work);
        const score = scoreOverworldTrace(log);
        const trace = join(work, "trace.json");
        writeFileSync(trace, JSON.stringify({ map: score.map }, null, 2) + "\n");
        process.stdout.write(`${score.detail}\ntrace ${trace}\n`);
        if (!score.ok) throw new Error(score.detail);
    },
};
