import { spawn } from "node:child_process";
import { cpSync, createWriteStream, existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { MAP_FRAME, MAP_WINDOWS, parseProf } from "./prof.ts";

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

function baseEnv(user: string, runDir: string): NodeJS.ProcessEnv {
    const env: NodeJS.ProcessEnv = { ...process.env };
    for (const key of [
        "NSMBU_UNCAPPED",
        "NSMBU_VK_UNCAPPED",
        "NSMBU_DUMP_FRAMES",
        "NSMBU_DUMP_TITLE",
        "NSMBU_PRESS",
        "NSMBU_STICK",
        "NSMBU_KEYS",
        "NSMBU_AGENT_SOCK",
        "NSMBU_INTERP",
        "NSMBU_INTERP_FPS",
        "NSMBU_HIDDEN_WINDOWS",
        "NSMBU_VK_PRESENT_MODE",
        "NSMBU_DISPLAY_HZ",
        "NSMBU_RES_SCALE",
    ]) {
        delete env[key];
    }
    env.NSMBU_USER_DIR = user;
    env.NSMBU_LOG_FILE = join(runDir, "log");
    env.NSMBU_SETTINGS = join(runDir, "settings.ini");
    env.NSMBU_DISPLAY_SETTINGS = join(runDir, "display.plist");
    env.XDG_CONFIG_HOME = join(runDir, "config");
    env.NSMBU_NO_AUDIO = "1";
    env.NSMBU_NO_GAMEPAD = "1";
    env.NSMBU_NO_HOST_INPUT = "1";
    env.NSMBU_PROFILE = "1";
    env.NSMBU_VK_STATS = "1";
    env.NSMBU_SYNC_STATS = "1";
    env.NSMBU_INTERP_PASS_STATS = "1";
    env.NSMBU_EXIT_AT_FRAME = "9000";
    return env;
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

async function boot(repo: string, runDir: string, env: NodeJS.ProcessEnv): Promise<void> {
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
    await new Promise<void>((resolve, reject) => {
        const child = spawn(bin, ["--game", game, "--save", "save"], {
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
            const key = loadReady(runDir);
            if (key !== "" && key === lastKey) stop();
            lastKey = key;
        }, 250);
        const timer = setTimeout(() => {
            stop();
            finish(new Error(`timed out waiting for the map trace\n${logTail(logPath)}`));
        }, 240_000);
        child.on("error", (e) => finish(e));
        child.on("exit", (code, signal) => {
            if (loadReady(runDir) !== "") finish();
            else finish(new Error(`game exited ${signal ?? code} before the map trace\n${logTail(logPath)}`));
        });
    });
}

export async function captureLoadOverworld(repo: string, work: string): Promise<string> {
    const user = join(repo, ".tmp", "bench", "user");
    seedUser(repo, user);
    const runDir = join(work, "run");
    const env = baseEnv(user, runDir);
    env.NSMBU_PRESS =
        "600-620:8000,800-820:8000,1000-1020:8000,1200-1220:8000,1400-1420:8000,1600-1620:8000,1800-1820:8000,2000-2020:8000,2200-2220:8000,2400-2420:8000";
    process.stdout.write("bench capture load-overworld\n");
    await boot(repo, runDir, env);
    return readLog(runDir);
}
