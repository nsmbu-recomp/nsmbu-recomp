import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { CASES } from "../src/playtest/cases/index.ts";
import { ensureLiveAgentIdle, Session } from "../src/playtest/session.ts";

const here = dirname(fileURLToPath(import.meta.url));
const repo = process.env.NSMBU_REPO ?? join(here, "../../..");
const show = process.env.NSMBU_TEST_SHOW === "1";

const runCase = process.env.NSMBU_REPO ? test : test.skip;

function casesToRun() {
    const raw = process.env.NSMBU_TEST_NAMES?.trim();
    if (!raw) return CASES;
    const names = new Set(raw.split(",").filter(Boolean));
    return CASES.filter((c) => names.has(c.name));
}

test.before(async () => {
    if (process.env.NSMBU_REPO) {
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
