import { spawn } from "node:child_process";
import { cpSync, createWriteStream, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { defaultSaveDir, resolveNsmbuBinary } from "./binary.ts";
import { alignLogicStep, countStatusLines, lastStatusStep } from "./trace.ts";

function sleep(ms: number): Promise<void> {
    return new Promise((r) => setTimeout(r, ms));
}

function intEnv(name: string, fallback: number): number {
    const raw = process.env[name];
    if (raw === undefined || raw === "") return fallback;
    const n = Number.parseInt(raw, 10);
    return Number.isFinite(n) ? n : fallback;
}

function parseExtraEnv(args: string[]): Record<string, string> {
    const out: Record<string, string> = {};
    for (const a of args) {
        const eq = a.indexOf("=");
        if (eq <= 0) {
            throw new Error(`usage: nsmbu scenario OUT [VAR=value …]; bad token: ${a}`);
        }
        out[a.slice(0, eq)] = a.slice(eq + 1);
    }
    return out;
}

function logHalts(runDir: string): void {
    let log = "";
    try {
        log = readFileSync(join(runDir, "log"), "utf8");
    } catch {
        return;
    }
    const re = /halt|crash|assert|fatal/i;
    let n = 0;
    for (const line of log.split("\n")) {
        if (!re.test(line)) continue;
        process.stdout.write(`${line}\n`);
        n++;
        if (n >= 3) break;
    }
}

async function stopGame(child: ReturnType<typeof spawn> | null): Promise<void> {
    if (!child || child.exitCode !== null || child.signalCode !== null) return;
    child.kill("SIGTERM");
    await sleep(1000);
    if (child.exitCode === null && child.signalCode === null) {
        child.kill("SIGKILL");
    }
    await new Promise<void>((resolve) => {
        child.on("exit", () => resolve());
        setTimeout(resolve, 5000).unref();
    });
}

async function runAttempt(
    out: string,
    bin: string,
    game: string,
    saveSrc: string,
    align: string,
    steps: number,
    fixedSeed: string,
    shaderCache: string | undefined,
    extraEnv: Record<string, string>,
    attempt: number,
): Promise<{ aligned: boolean; done: boolean }> {
    rmSync(out, { recursive: true, force: true });
    mkdirSync(join(out, "user"), { recursive: true });
    mkdirSync(join(out, "states"), { recursive: true });
    cpSync(saveSrc, join(out, "save"), { recursive: true });
    writeFileSync(join(out, "status.txt"), "");
    const consolePath = join(out, "console.log");
    const env: NodeJS.ProcessEnv = {
        ...process.env,
        ...extraEnv,
        NSMBU_HIDDEN_WINDOWS: "1",
        NSMBU_NO_HOST_INPUT: "1",
        NSMBU_NO_AUDIO: "1",
        NSMBU_NO_GAMEPAD: "1",
        NSMBU_VK_PRESENT_MODE: "immediate",
        NSMBU_RES_SCALE: process.env.NSMBU_RES_SCALE ?? extraEnv.NSMBU_RES_SCALE ?? "1",
        NSMBU_USER_DIR: join(out, "user"),
        NSMBU_SHADER_CACHE: shaderCache ?? join(out, "shadercache.bin"),
        NSMBU_STATE_DIR: join(out, "states"),
        NSMBU_LOG_FILE: join(out, "log"),
        NSMBU_TEST_STATUS_TRACE: join(out, "status.txt"),
        NSMBU_TEST_ALIGN_SCENE: align,
        NSMBU_TEST_FIXED_SEED: fixedSeed,
    };
    const child = spawn(bin, ["--game", game, "--save", join(out, "save")], {
        cwd: out,
        env,
        stdio: ["ignore", "pipe", "pipe"],
        detached: false,
    });
    const consoleLog = createWriteStream(consolePath);
    child.stdout?.pipe(consoleLog);
    child.stderr?.pipe(consoleLog);
    const bootLimit = intEnv("BOOT_LIMIT", 240);
    const runLimit = intEnv("RUN_LIMIT", 300);
    const t0 = Date.now();
    let aligned = false;
    let start: string | null = null;
    for (let i = 0; i < bootLimit * 10; i++) {
        if (child.exitCode !== null || child.signalCode !== null) break;
        start = alignLogicStep(out, align);
        if (start !== null) {
            aligned = true;
            break;
        }
        await sleep(100);
    }
    const t1 = Date.now();
    let done = false;
    if (aligned && start !== null) {
        const origin = Number.parseInt(start, 10);
        const target = origin + steps;
        for (let i = 0; i < runLimit * 10; i++) {
            if (child.exitCode !== null || child.signalCode !== null) break;
            const last = lastStatusStep(out);
            if (last !== null) {
                const n = Number.parseInt(last, 10);
                if (Number.isFinite(n) && n >= target) {
                    done = true;
                    break;
                }
            }
            await sleep(100);
        }
    }
    const t2 = Date.now();
    await stopGame(child);
    consoleLog.end();
    writeFileSync(
        join(out, "perf.txt"),
        `wall_boot_s ${Math.floor((t1 - t0) / 1000)} wall_scenario_s ${Math.floor((t2 - t1) / 1000)} aligned ${aligned ? "y" : "n"} done ${done ? "y" : "n"} attempt ${attempt}\n`,
    );
    rmSync(join(out, "save"), { recursive: true, force: true });
    rmSync(join(out, "states"), { recursive: true, force: true });
    return { aligned, done };
}

export async function cmdScenario(repo: string, rest: string[]): Promise<void> {
    if (rest.length < 1) {
        throw new Error("usage: nsmbu scenario OUT [VAR=value …]");
    }
    const [outArg, ...extraArgs] = rest;
    const out = resolve(outArg);
    const game = process.env.GAME ?? join(repo, "game");
    const save = defaultSaveDir(repo, process.env.SAVE);
    const align = process.env.ALIGN ?? "Title";
    const steps = intEnv("STEPS", 300);
    const bootTries = intEnv("BOOT_TRIES", 3);
    const fixedSeed = process.env.FIXED_SEED ?? "1";
    const bin = resolveNsmbuBinary(repo, process.env.NSMBU_BIN);
    const extraEnv = parseExtraEnv(extraArgs);
    let aligned = false;
    let done = false;
    for (let attempt = 1; attempt <= bootTries; attempt++) {
        const result = await runAttempt(
            out,
            bin,
            game,
            save,
            align,
            steps,
            fixedSeed,
            process.env.SHADER_CACHE,
            extraEnv,
            attempt,
        );
        aligned = result.aligned;
        done = result.done;
        if (aligned) break;
        const bootLog = join(out, "log");
        const bootFail = `${out}.bootfail${attempt}.log`;
        try {
            cpSync(bootLog, bootFail);
        } catch {
            try {
                cpSync(join(out, "console.log"), bootFail);
            } catch {
                /* no log */
            }
        }
    }
    const base = out.split(/[/\\]/).pop() ?? out;
    process.stdout.write(
        `${base}: aligned=${aligned ? "y" : "n"} done=${done ? "y" : "n"} ${countStatusLines(out)} status lines\n`,
    );
    logHalts(out);
    if (!aligned || !done) {
        process.exit(1);
    }
}
