import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { CASES } from "../src/playtest/cases/index.ts";
import { ensureLiveAgentIdle, Session } from "../src/playtest/session.ts";

const here = dirname(fileURLToPath(import.meta.url));

function parseRunnerArgv(): { repo?: string; show: boolean; names?: string } {
    const argv = process.argv.slice(2);
    let repo: string | undefined;
    let show = false;
    let names: string | undefined;
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === "--repo") {
            repo = argv[++i];
            continue;
        }
        if (a.startsWith("--repo=")) {
            repo = a.slice("--repo=".length);
            continue;
        }
        if (a === "--show") {
            show = true;
            continue;
        }
        if (a === "--names") {
            names = argv[++i];
            continue;
        }
        if (a.startsWith("--names=")) {
            names = a.slice("--names=".length);
        }
    }
    return { repo, show, names };
}

const runner = parseRunnerArgv();
const repo = runner.repo ?? join(here, "../../..");
const show = runner.show;
const runCase = runner.repo ? test : test.skip;

function casesToRun() {
    const raw = runner.names?.trim();
    if (!raw) return CASES.filter((c) => !c.manual);
    const names = new Set(raw.split(",").filter(Boolean));
    return CASES.filter((c) => names.has(c.name));
}

test.before(async () => {
    if (runner.repo) {
        await ensureLiveAgentIdle(repo);
    }
});

for (const c of casesToRun()) {
    const register = c.skip ? test.skip : runCase;
    register(
        c.name,
        { timeout: 600_000 },
        async () => {
            if (c.skip) return;
            const session = new Session(repo, c.name, { show });
            try {
                await c.run(session);
            } catch (e) {
                const msg = e instanceof Error ? e.message : String(e);
                const where = session.last ? ` (${session.last})` : "";
                throw new Error(`${msg}${where}`, { cause: e });
            } finally {
                await session.stop();
            }
        },
    );
}