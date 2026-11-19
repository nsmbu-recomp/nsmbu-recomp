import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { flagNumber, hasFlag, parseArgv } from "../parse.ts";
import { CASES } from "./cases/index.ts";
import { ensureLiveAgentIdle } from "./session.ts";

const playtestFile = join(dirname(fileURLToPath(import.meta.url)), "../../test/playtests.test.ts");

export function parseTestArgs(rest: string[]): { list: boolean; show: boolean; names: string[]; jobs: number } {
    const { flags, positional } = parseArgv(rest, ["list", "show", "jobs"], ["jobs"]);
    const names: string[] = [];
    const bad: string[] = [];
    for (const a of positional) {
        if (a.startsWith("-")) bad.push(a);
        else names.push(a);
    }
    if (bad.length > 0) {
        throw new Error(`unknown option: ${bad.join(", ")}\nusage: nsmbu test [--list] [--show] [--jobs N] [name…]`);
    }
    return {
        list: hasFlag(flags, "list"),
        show: hasFlag(flags, "show"),
        names,
        jobs: Math.max(1, flagNumber(flags, "jobs", 2)),
    };
}

export async function cmdTest(repo: string, rest: string[]): Promise<void> {
    const { list, show, names, jobs } = parseTestArgs(rest);
    if (list && names.length === 0) {
        for (const c of CASES) {
            const tag = c.skip ? "\t(skipped)" : c.manual ? "\t(manual)" : "";
            console.log(`${c.name}${tag}\t${c.summary}`);
        }
        return;
    }
    const unknown = names.filter((n) => !CASES.some((c) => c.name === n));
    if (unknown.length > 0) {
        throw new Error(`unknown playtest: ${unknown.join(", ")}\nknown: ${CASES.map((c) => c.name).join(", ")}`);
    }
    await ensureLiveAgentIdle(repo);
    const args = [
        "--test-concurrency",
        show ? "1" : String(jobs),
        playtestFile,
        "--repo",
        repo,
    ];
    if (show) args.push("--show");
    if (names.length > 0) args.push("--names", names.join(","));
    const result = spawnSync(process.execPath, args, {
        cwd: join(repo, "tools/cli"),
        env: process.env,
        stdio: "inherit",
    });
    if (result.status !== 0) {
        throw new Error(result.error?.message ?? "playtests failed");
    }
}