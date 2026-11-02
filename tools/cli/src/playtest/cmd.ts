import { CASES } from "./cases/index.ts";
import { ensureLiveAgentIdle, Session } from "./session.ts";

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

async function mapPool<T, R>(items: T[], limit: number, fn: (item: T) => Promise<R>): Promise<R[]> {
    const out = new Array<R>(items.length);
    let next = 0;
    const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
        for (;;) {
            const i = next++;
            if (i >= items.length) return;
            out[i] = await fn(items[i]);
        }
    });
    await Promise.all(workers);
    return out;
}

export async function cmdTest(repo: string, rest: string[]): Promise<void> {
    const { list, show, names } = parseTestArgs(rest);
    if (list && names.length === 0) {
        for (const c of CASES) console.log(`${c.name}\t${c.summary}`);
        return;
    }
    const unknown = names.filter((n) => !CASES.some((c) => c.name === n));
    if (unknown.length > 0) {
        throw new Error(`unknown playtest: ${unknown.join(", ")}\nknown: ${CASES.map((c) => c.name).join(", ")}`);
    }
    const selected = names.length === 0 ? CASES : CASES.filter((c) => names.includes(c.name));
    await ensureLiveAgentIdle(repo);
    let jobs = Math.max(1, Number.parseInt(process.env.NSMBU_TEST_JOBS ?? "2", 10) || 2);
    if (show) jobs = 1;
    const results = await mapPool(selected, jobs, async (c) => {
        const session = new Session(repo, c.name, { show });
        const t0 = Date.now();
        process.stdout.write(`playtest ${c.name}\n`);
        try {
            await c.run(session);
            const sec = ((Date.now() - t0) / 1000).toFixed(1);
            process.stdout.write(`playtest ${c.name} PASS ${sec}s\n`);
            return true;
        } catch (e) {
            const msg = e instanceof Error ? e.message : String(e);
            const where = session.last ? ` (${session.last})` : "";
            process.stdout.write(`playtest ${c.name} FAIL ${msg}${where}\n`);
            return false;
        } finally {
            await session.stop();
        }
    });
    const failed = results.filter((ok) => !ok).length;
    if (failed > 0) {
        throw new Error(`${failed} playtest${failed === 1 ? "" : "s"} failed`);
    }
}
