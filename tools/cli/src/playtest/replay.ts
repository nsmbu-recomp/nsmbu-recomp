import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { Session } from "./session.ts";

export type ReplayVerdict = "pass" | "fail" | "pending";

const FINISHED = /\[input\] replay finished at step \d+, ([^\n]+)/;

export function replayFile(repo: string, name: string): string {
    return join(repo, "tools/cli/src/playtest/replays", `${name}.replay`);
}

export function replayCourse(text: string): string {
    for (const line of text.split("\n")) {
        const match = /^course=(\S+)/.exec(line);
        if (match) return match[1];
    }
    throw new Error("replay has no course= line");
}

export function replayVerdict(log: string): ReplayVerdict {
    if (log.includes("[input] replay diverged")) return "fail";
    const finished = FINISHED.exec(log);
    if (finished) return finished[1].includes("player state matched") ? "pass" : "fail";
    if (log.includes("[input] replay stopped at step")) return "pass";
    return "pending";
}

function logTail(log: string): string {
    const lines = log.split("\n").filter((line) => line.includes("[input]") || line.includes("[prof]"));
    return lines.slice(-12).join("\n");
}

function pidAlive(workDir: string): boolean {
    try {
        const pid = Number.parseInt(readFileSync(join(workDir, "pid"), "utf8").trim(), 10);
        if (!Number.isInteger(pid)) return false;
        process.kill(pid, 0);
        return true;
    } catch {
        return false;
    }
}

export async function runReplay(session: Session, name: string, timeoutSec: number): Promise<void> {
    const path = replayFile(session.repo, name);
    const text = readFileSync(path, "utf8");
    const course = replayCourse(text);
    await session.start({
        save: "repo",
        open: course,
        warmShaders: true,
        env: {
            NSMBU_REPLAY_INPUT: path,
            NSMBU_PROFILE: "1",
            NSMBU_VK_STATS: "1",
            NSMBU_SYNC_STATS: "1",
            NSMBU_CRASH_RECOVERY: "1",
        },
    });
    const deadline = Date.now() + timeoutSec * 1000;
    let last = "";
    for (;;) {
        try {
            last = readFileSync(session.logPath, "utf8");
        } catch {
            last = "";
        }
        const verdict = replayVerdict(last);
        if (verdict === "pass") return;
        if (verdict === "fail") {
            throw new Error(`replay ${name} diverged\n${logTail(last)}`);
        }
        if (!pidAlive(session.workDir)) {
            try {
                last = readFileSync(session.logPath, "utf8");
            } catch {
                last = "";
            }
            const again = replayVerdict(last);
            if (again === "pass") return;
            if (again === "fail") throw new Error(`replay ${name} diverged\n${logTail(last)}`);
            throw new Error(`replay ${name} exited before it finished\n${logTail(last)}`);
        }
        if (Date.now() >= deadline) {
            throw new Error(`replay ${name} timed out after ${timeoutSec}s\n${logTail(last)}`);
        }
        await new Promise((r) => setTimeout(r, 200));
    }
}
