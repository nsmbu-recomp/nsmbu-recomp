import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { CASES } from "./cases/index.ts";
import { ensureLiveAgentIdle } from "./session.ts";

const playtestFile = join(dirname(fileURLToPath(import.meta.url)), "../../test/playtests.test.ts");

export function parseTestArgs(rest: string[]): { list: boolean; show: boolean; names: string[] } {
    const list = rest.includes("--list");
    const show = rest.includes("--show");
    const names: string[] = [];
    const bad: string[] = [];
    for (const a of rest) {
        if (a === "--list" || a === "--show") continue;
        if (a.startsWith("-")) bad.push(a);
        else names.push(a);
    }
    if (bad.length > 0) {
        throw new Error(`unknown option: ${bad.join(", ")}\nusage: nsmbu test [--list] [--show] [name…]`);
    }
    return { list, show, names };
}

export async function cmdTest(repo: string, rest: string[]): Promise<void> {
    const { list, show, names } = parseTestArgs(rest);
    if (list && names.length === 0) {
        for (const c of CASES) {
            const tag = c.skip ? "\t(skipped)" : "";
            console.log(`${c.name}${tag}\t${c.summary}`);
        }
        return;
    }
    const unknown = names.filter((n) => !CASES.some((c) => c.name === n));
    if (unknown.length > 0) {
        throw new Error(`unknown playtest: ${unknown.join(", ")}\nknown: ${CASES.map((c) => c.name).join(", ")}`);
    }
    await ensureLiveAgentIdle(repo);
    const args = ["--test", playtestFile, "--test-concurrency", show ? "1" : String(Math.max(1, Number.parseInt(process.env.NSMBU_TEST_JOBS ?? "2", 10) || 2))];
    const env: NodeJS.ProcessEnv = {
        ...process.env,
        NSMBU_REPO: repo,
        NSMBU_TEST_SHOW: show ? "1" : "",
        NSMBU_TEST_NAMES: names.length > 0 ? names.join(",") : "",
    };
    if (show) {
        env.NSMBU_TEST_JOBS = "1";
    }
    const result = spawnSync(process.execPath, args, {
        cwd: join(repo, "tools/cli"),
        env,
        stdio: "inherit",
    });
    if (result.status !== 0) {
        throw new Error(result.error?.message ?? "playtests failed");
    }
}
