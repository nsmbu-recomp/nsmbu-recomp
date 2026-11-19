import { spawn } from "node:child_process";
import { cpSync, createWriteStream, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { flagNumber, flagString, parseArgv } from "../parse.ts";
import { runtimeArgs, stripRuntime } from "../runtime_flags.ts";
import { defaultSaveDir, resolveNsmbuBinary } from "./binary.ts";
import { alignLogicStep, countStatusLines, lastStatusStep } from "./trace.ts";

const SCENARIO_FLAGS = [
    "game",
    "save",
    "align",
    "steps",
    "fixed-seed",
    "bin",
    "shader-cache",
    "boot-tries",
    "boot-limit",
    "run-limit",
];

export type ScenarioConfig = {
    out: string;
    game: string;
    save: string;
    align: string;
    steps: number;
    bootTries: number;
    fixedSeed: string;
    bin: string;
    shaderCache?: string;
    bootLimit: number;
    runLimit: number;
    extraGameFlags: string[];
};

function sleep(ms: number): Promise<void> {
    return new Promise((r) => setTimeout(r, ms));
}

export function parseScenarioConfig(repo: string, rest: string[]): ScenarioConfig {
    const { flags, positional } = parseArgv(rest, SCENARIO_FLAGS, SCENARIO_FLAGS);
    const [outArg, ...extraTokens] = positional;
    if (!outArg) {
        throw new Error(
            "usage: nsmbu scenario OUT [--game PATH] [--save PATH] [--align NAME] [--steps N] " +
                "[--fixed-seed N] [--bin PATH] [--shader-cache PATH] [--boot-tries N] " +
                "[--boot-limit N] [--run-limit N] [NSMBU_VAR=value …] [--flag …]",
        );
    }
    const extraGameFlags: string[] = [];
    for (const token of extraTokens) {
        if (token.startsWith("--")) {
            extraGameFlags.push(token);
        } else if (token.startsWith("NSMBU_") && token.includes("=")) {
            const eq = token.indexOf("=");
            extraGameFlags.push(...runtimeArgs({ [token.slice(0, eq)]: token.slice(eq + 1) }));
        } else {
            throw new Error(
                `usage: nsmbu scenario OUT [options] [NSMBU_VAR=value …] [--flag …]; bad token: ${token}`,
            );
        }
    }
    const binOverride = flagString(flags, "bin");
    return {
        out: resolve(outArg),
        game: flagString(flags, "game") ?? join(repo, "game"),
        save: defaultSaveDir(repo, flagString(flags, "save")),
        align: flagString(flags, "align") ?? "Title",
        steps: flagNumber(flags, "steps", 300),
        bootTries: flagNumber(flags, "boot-tries", 3),
        fixedSeed: flagString(flags, "fixed-seed") ?? "1",
        bin: resolveNsmbuBinary(repo, binOverride),
        shaderCache: flagString(flags, "shader-cache"),
        bootLimit: flagNumber(flags, "boot-limit", 240),
        runLimit: flagNumber(flags, "run-limit", 300),
        extraGameFlags,
    };
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
    extraGameFlags: string[],
    bootLimit: number,
    runLimit: number,
    attempt: number,
): Promise<{ aligned: boolean; done: boolean }> {
    rmSync(out, { recursive: true, force: true });
    mkdirSync(join(out, "user"), { recursive: true });
    mkdirSync(join(out, "states"), { recursive: true });
    cpSync(saveSrc, join(out, "save"), { recursive: true });
    writeFileSync(join(out, "status.txt"), "");
    const consolePath = join(out, "console.log");
    const vars: Record<string, string> = {
        NSMBU_HIDDEN_WINDOWS: "1",
        NSMBU_NO_HOST_INPUT: "1",
        NSMBU_NO_AUDIO: "1",
        NSMBU_NO_GAMEPAD: "1",
        NSMBU_VK_PRESENT_MODE: "immediate",
        NSMBU_RES_SCALE: "1",
        NSMBU_USER_DIR: join(out, "user"),
        NSMBU_SHADER_CACHE: shaderCache ?? join(out, "shadercache.bin"),
        NSMBU_STATE_DIR: join(out, "states"),
        NSMBU_LOG_FILE: join(out, "log"),
        NSMBU_TEST_STATUS_TRACE: join(out, "status.txt"),
        NSMBU_TEST_ALIGN_SCENE: align,
        NSMBU_TEST_FIXED_SEED: fixedSeed,
    };
    const gameFlags = [...runtimeArgs(vars), ...extraGameFlags];
    const child = spawn(bin, ["--game", game, "--save", join(out, "save"), ...gameFlags], {
        cwd: out,
        env: stripRuntime(process.env),
        stdio: ["ignore", "pipe", "pipe"],
        detached: false,
    });
    const consoleLog = createWriteStream(consolePath);
    child.stdout?.pipe(consoleLog);
    child.stderr?.pipe(consoleLog);
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
    const cfg = parseScenarioConfig(repo, rest);
    let aligned = false;
    let done = false;
    for (let attempt = 1; attempt <= cfg.bootTries; attempt++) {
        const result = await runAttempt(
            cfg.out,
            cfg.bin,
            cfg.game,
            cfg.save,
            cfg.align,
            cfg.steps,
            cfg.fixedSeed,
            cfg.shaderCache,
            cfg.extraGameFlags,
            cfg.bootLimit,
            cfg.runLimit,
            attempt,
        );
        aligned = result.aligned;
        done = result.done;
        if (aligned) break;
        const bootLog = join(cfg.out, "log");
        const bootFail = `${cfg.out}.bootfail${attempt}.log`;
        try {
            cpSync(bootLog, bootFail);
        } catch {
            try {
                cpSync(join(cfg.out, "console.log"), bootFail);
            } catch {
                /* no log */
            }
        }
    }
    const base = cfg.out.split(/[/\\]/).pop() ?? cfg.out;
    process.stdout.write(
        `${base}: aligned=${aligned ? "y" : "n"} done=${done ? "y" : "n"} ${countStatusLines(cfg.out)} status lines\n`,
    );
    logHalts(cfg.out);
    if (!aligned || !done) {
        process.exit(1);
    }
}