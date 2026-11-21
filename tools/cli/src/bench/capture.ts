import { spawn } from "node:child_process";
import { cpSync, createWriteStream, existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { runtimeArgs, stripRuntime } from "../runtime_flags.ts";
import { MAP_FRAME, MAP_WINDOWS, parseProf, scoreOverworldTrace } from "./prof.ts";

function gameBinary(repo: string): string {
    for (const base of ["build/debug", "build/release", "build/rel", "build"]) {
        const path = join(repo, base, "nsmbu");
        if (existsSync(path)) return path;
    }
    throw new Error("missing build/debug/nsmbu; run: nsmbu build");
}

function activeGame(repo: string): string {
    const active = join(repo, "game", "active");
    if (!existsSync(active)) throw new Error("missing game/active; run: nsmbu game");
    const id = readFileSync(active, "utf8").trim();
    const dir = join(repo, "game", id);
    if (!existsSync(join(dir, "code", "red-pro2.rpx"))) {
        throw new Error(`missing ${dir}/code/red-pro2.rpx; run: nsmbu game`);
    }
    return dir;
}

function seedUser(repo: string, user: string): void {
    mkdirSync(user, { recursive: true });
    const dest = join(user, "shadercache");
    const src = join(repo, "nsmbu", "shadercache");
    if (!existsSync(dest) && existsSync(src)) cpSync(src, dest, { recursive: true });
}

function copyIf(src: string, dest: string): void {
    if (existsSync(src)) cpSync(src, dest);
}

function baseGameFlags(user: string, runDir: string, extra: Record<string, string> = {}): string[] {
    const vars: Record<string, string> = {
        NSMBU_USER_DIR: user,
        NSMBU_LOG_FILE: join(runDir, "log"),
        NSMBU_SETTINGS: join(runDir, "settings.ini"),
        NSMBU_DISPLAY_SETTINGS: join(runDir, "display.plist"),
        NSMBU_NO_AUDIO: "1",
        NSMBU_NO_GAMEPAD: "1",
        NSMBU_NO_HOST_INPUT: "1",
        NSMBU_PROFILE: "1",
        NSMBU_VK_STATS: "1",
        NSMBU_SYNC_STATS: "1",
        NSMBU_INTERP_PASS_STATS: "1",
        NSMBU_EXIT_AT_FRAME: "9000",
        ...extra,
    };
    return runtimeArgs(vars);
}

function childEnv(runDir: string): NodeJS.ProcessEnv {
    return {
        ...stripRuntime(process.env),
        XDG_CONFIG_HOME: join(runDir, "config"),
    };
}

function logTail(path: string): string {
    try {
        const lines = readFileSync(path, "utf8").trim().split("\n");
        return lines.slice(-20).join("\n");
    } catch {
        return "";
    }
}

function readLog(dir: string): string {
    try {
        return readFileSync(join(dir, "log"), "utf8");
    } catch {
        return "";
    }
}

function loadReady(dir: string): string {
    const log = readLog(dir);
    if (!log.includes("[scene] CourseSelect from save")) return "";
    const map = parseProf(log).filter((w) => w.frame >= MAP_FRAME);
    if (map.length < MAP_WINDOWS) return "";
    return "ready";
}

async function boot(
    repo: string,
    runDir: string,
    gameFlags: string[],
    ready: (dir: string) => string,
): Promise<void> {
    rmSync(runDir, { recursive: true, force: true });
    const save = join(runDir, "save");
    mkdirSync(save, { recursive: true });
    mkdirSync(join(runDir, "config"), { recursive: true });
    const repoSave = join(repo, "nsmbu", "save");
    if (!existsSync(repoSave)) throw new Error(`missing ${repoSave}`);
    cpSync(repoSave, save, { recursive: true });
    copyIf(join(repo, "nsmbu", "settings.ini"), join(runDir, "settings.ini"));
    copyIf(join(repo, "nsmbu", "display.plist"), join(runDir, "display.plist"));
    const bin = gameBinary(repo);
    const game = activeGame(repo);
    const logPath = join(runDir, "log");
    const env = childEnv(runDir);
    await new Promise<void>((resolve, reject) => {
        const child = spawn(bin, ["--game", game, "--save", "save", ...gameFlags], {
            cwd: runDir,
            env,
            stdio: ["ignore", "pipe", "pipe"],
        });
        const stdio = createWriteStream(join(runDir, "stdio"));
        child.stdout?.pipe(stdio, { end: false });
        child.stderr?.pipe(stdio, { end: false });
        let settled = false;
        let lastKey = "";
        const finish = (err?: Error) => {
            if (settled) return;
            settled = true;
            clearInterval(poll);
            clearTimeout(timer);
            stdio.end();
            if (err) reject(err);
            else resolve();
        };
        const stop = () => {
            if (child.exitCode !== null || child.signalCode !== null) return;
            child.kill("SIGTERM");
            setTimeout(() => {
                if (child.exitCode === null && child.signalCode === null) child.kill("SIGKILL");
            }, 5000).unref();
        };
        const poll = setInterval(() => {
            const key = ready(runDir);
            if (key !== "" && key === lastKey) stop();
            lastKey = key;
        }, 250);
        const timer = setTimeout(() => {
            stop();
            finish(new Error(`timed out waiting for the map trace\n${logTail(logPath)}`));
        }, 240_000);
        child.on("error", (e) => finish(e));
        child.on("exit", (code, signal) => {
            if (ready(runDir) !== "") finish();
            else finish(new Error(`game exited ${signal ?? code} before the map trace\n${logTail(logPath)}`));
        });
    });
}

const PRESS =
    "600-620:8000,800-820:8000,1000-1020:8000,1200-1220:8000,1400-1420:8000,1600-1620:8000,1800-1820:8000,2000-2020:8000,2200-2220:8000,2400-2420:8000";

export async function captureLoadOverworld(repo: string, work: string): Promise<string> {
    const user = join(repo, ".tmp", "bench", "user");
    seedUser(repo, user);
    const runDir = join(work, "run");
    const gameFlags = baseGameFlags(user, runDir, { NSMBU_PRESS: PRESS });
    process.stdout.write("bench capture load-overworld\n");
    await boot(repo, runDir, gameFlags, loadReady);
    return readLog(runDir);
}

function overworldReady(dir: string): string {
    return scoreOverworldTrace(readLog(dir)).ok ? "ready" : "";
}

export async function captureOverworld(repo: string, work: string): Promise<string> {
    const user = join(repo, ".tmp", "bench", "user");
    seedUser(repo, user);
    const runDir = join(work, "run");
    const gameFlags = baseGameFlags(user, runDir, { NSMBU_PRESS: PRESS });
    process.stdout.write("bench capture overworld\n");
    await boot(repo, runDir, gameFlags, overworldReady);
    return readLog(runDir);
}