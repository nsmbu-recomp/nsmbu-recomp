import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { captureLoadOverworld } from "../capture.ts";
import { scoreLoadTrace } from "../prof.ts";

export const loadOverworld = {
    name: "load-overworld",
    summary: "Visible load through the save file onto the world map, then a render trace.",
    run: async (repo: string) => {
        const work = join(repo, ".tmp", "bench", "load-overworld");
        mkdirSync(work, { recursive: true });
        const log = await captureLoadOverworld(repo, work);
        const score = scoreLoadTrace(log);
        const trace = join(work, "trace.json");
        writeFileSync(
            trace,
            JSON.stringify({ windows: score.windows, map: score.map }, null, 2) + "\n",
        );
        process.stdout.write(`${score.detail}\ntrace ${trace}\n`);
        if (!score.ok) throw new Error(score.detail);
    },
};
