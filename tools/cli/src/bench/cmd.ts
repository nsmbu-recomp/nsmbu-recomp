import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { isPidAlive } from "../agent/process.ts";
import { ensureLiveAgentIdle } from "../playtest/session.ts";
import { loadOverworld } from "./cases/load-overworld.ts";

export type BenchCase = {
    name: string;
    summary: string;
    run: (repo: string) => Promise<void>;
};

const CASES: BenchCase[] = [loadOverworld];

export function parseBenchArgs(rest: string[]): { list: boolean; names: string[] } {
    const list = rest.includes("--list");
    const names: string[] = [];
    const bad: string[] = [];
    for (const a of rest) {
        if (a === "--list") continue;
        if (a.startsWith("-")) bad.push(a);
        else names.push(a);
    }
    if (bad.length > 0) {
        throw new Error(`unknown option: ${bad.join(", ")}\nusage: nsmbu bench [--list] [name…]`);
    }
    return { list, names };
}

export function otherGameLines(listing: string): string[] {
    const found: string[] = [];
    for (const line of listing.split("\n")) {
        const match = /^\s*(\d+)\s+(\S+)/.exec(line);
        if (!match) continue;
        const base = match[2].split("/").pop() ?? "";
        if (base.startsWith("nsmbu")) found.push(line.trim());
    }
    return found;
}

function runningGames(): string[] {
    try {
        const listing = execFileSync("ps", ["-Ao", "pid=,comm="], { encoding: "utf8" });
        return otherGameLines(listing);
    } catch {
        return [];
    }
}

function acquireLock(repo: string): () => void {
    const path = join(repo, ".tmp", "bench", "lock");
    mkdirSync(join(repo, ".tmp", "bench"), { recursive: true });
    for (;;) {
        try {
            writeFileSync(path, `${process.pid}\n`, { flag: "wx" });
            return () => rmSync(path, { force: true });
        } catch (e) {
            const code = e && typeof e === "object" && "code" in e ? String(e.code) : "";
            if (code !== "EEXIST") throw e;
            const pid = Number(readFileSync(path, "utf8").trim());
            if (Number.isFinite(pid) && isPidAlive(pid)) {
                throw new Error(`bench already running (pid ${pid})`);
            }
            rmSync(path, { force: true });
        }
    }
}

export async function cmdBench(repo: string, rest: string[]): Promise<void> {
    const { list, names } = parseBenchArgs(rest);
    if (list && names.length === 0) {
        for (const c of CASES) console.log(`${c.name}\t${c.summary}`);
        return;
    }
    const unknown = names.filter((n) => !CASES.some((c) => c.name === n));
    if (unknown.length > 0) {
        throw new Error(`unknown bench: ${unknown.join(", ")}\nknown: ${CASES.map((c) => c.name).join(", ")}`);
    }
    const selected = names.length === 0 ? CASES : CASES.filter((c) => names.includes(c.name));
    await ensureLiveAgentIdle(repo);
    const busy = runningGames();
    if (busy.length > 0) {
        throw new Error(`a game is already running; close it first\n${busy.join("\n")}`);
    }
    const release = acquireLock(repo);
    try {
        for (const c of selected) {
            const again = runningGames();
            if (again.length > 0) {
                throw new Error(`a game is already running; close it first\n${again.join("\n")}`);
            }
            const t0 = Date.now();
            process.stdout.write(`bench ${c.name}\n`);
            try {
                await c.run(repo);
                const sec = ((Date.now() - t0) / 1000).toFixed(1);
                process.stdout.write(`bench ${c.name} PASS ${sec}s\n`);
            } catch (e) {
                const msg = e instanceof Error ? e.message : String(e);
                process.stdout.write(`bench ${c.name} FAIL ${msg}\n`);
                throw new Error(`bench ${c.name} failed`);
            }
        }
    } finally {
        release();
    }
}
