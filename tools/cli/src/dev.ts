import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { nsmbuUserDir } from "./repo.ts";
import { baseEnv, runPython, runShell } from "./run.ts";

function runPythonInherit(repo: string, script: string, args: string[], extraEnv: Record<string, string> = {}): void {
    const r = spawnSync("python3", [join(repo, script), ...args], {
        cwd: repo,
        env: baseEnv(repo, extraEnv),
        stdio: "inherit",
    });
    if (r.error) {
        console.error(r.error.message);
        process.exit(1);
    }
    if ((r.status ?? 1) !== 0) {
        process.exit(r.status ?? 1);
    }
}

function runPythonCapture(repo: string, script: string, args: string[]): string {
    const r = spawnSync("python3", [join(repo, script), ...args], {
        cwd: repo,
        env: baseEnv(repo),
        encoding: "utf8",
    });
    if (r.error) {
        console.error(r.error.message);
        process.exit(1);
    }
    if ((r.status ?? 1) !== 0) {
        if (r.stderr) {
            process.stderr.write(r.stderr);
        }
        process.exit(r.status ?? 1);
    }
    return (r.stdout ?? "").trim();
}

export function cmdBuild(repo: string, release: boolean, sanitizer: boolean, rest: string[]): void {
    const args: string[] = [];
    if (release) {
        args.push("--release");
    }
    if (sanitizer) {
        args.push("--sanitizer");
    }
    args.push(...rest);
    runPython(repo, "tools/build.py", args);
}

export function cmdLaunch(
    repo: string,
    opts: { noDebug: boolean; trace: boolean; gdb: boolean; lldb: boolean; wine?: boolean },
    rest: string[],
    extraEnv: Record<string, string> = {},
): void {
    const args: string[] = [];
    if (opts.noDebug) {
        args.push("--no-debug");
    }
    if (opts.trace) {
        args.push("--trace");
    }
    if (opts.gdb) {
        args.push("--gdb");
    }
    if (opts.lldb) {
        args.push("--lldb");
    }
    if (opts.wine) {
        args.push("--wine");
    }
    args.push(...rest);
    runPython(repo, "tools/launch.py", args, extraEnv);
}

export function cmdCrash(repo: string): void {
    cmdLaunch(repo, { noDebug: false, trace: false, gdb: false, lldb: false }, [], {
        NSMBU_TEST_HOST_CRASH: "1",
    });
}

export function cmdWine(
    repo: string,
    opts: { noDebug: boolean; trace: boolean },
    rest: string[],
): void {
    const buildArgs: string[] = opts.noDebug ? ["--release"] : [];
    buildArgs.push(...rest);
    runPythonInherit(repo, "tools/build_windows.py", buildArgs);
    cmdLaunch(repo, { noDebug: opts.noDebug, trace: opts.trace, gdb: false, lldb: false, wine: true }, rest);
}

export function cmdRun(repo: string, release: boolean, rest: string[]): void {
    const buildArgs: string[] = release ? ["--release"] : [];
    buildArgs.push(...rest);
    runPythonInherit(repo, "tools/build.py", buildArgs);
    const launchArgs = [...rest];
    cmdLaunch(repo, { noDebug: release, trace: false, gdb: false, lldb: false }, launchArgs);
}

export function cmdExtract(repo: string, rest: string[]): void {
    runPython(repo, "tools/extract.py", rest);
}

export function cmdRecord(repo: string, course: string, rest: string[]): void {
    const replays = join(repo, "replays");
    mkdirSync(replays, { recursive: true });
    const replayPath = join(replays, `${course}.replay`);
    cmdLaunch(repo, { noDebug: false, trace: false, gdb: false, lldb: false }, [course, ...rest], {
        NSMBU_RECORD_INPUT: replayPath,
    });
}

const REPLAY_PROFILE_ENV: Record<string, string> = {
    NSMBU_PROFILE: "1",
    NSMBU_VK_STATS: "1",
    NSMBU_SYNC_STATS: "1",
    NSMBU_CRASH_RECOVERY: "1",
};

export type ReplayOpts = {
    release: boolean;
    uncapped: boolean;
    exitAt?: string;
    log?: string;
};

export function cmdReplay(repo: string, course: string, opts: ReplayOpts, rest: string[]): void {
    const replayPath = join(repo, "replays", `${course}.replay`);
    if (!existsSync(replayPath)) {
        console.error(`missing ${replayPath}`);
        process.exit(1);
    }
    const extra: Record<string, string> = {
        NSMBU_REPLAY_INPUT: replayPath,
    };
    for (const [key, value] of Object.entries(REPLAY_PROFILE_ENV)) {
        if (process.env[key] === undefined) {
            extra[key] = value;
        }
    }
    if (opts.uncapped) {
        extra.NSMBU_UNCAPPED = "1";
    }
    if (opts.exitAt !== undefined) {
        extra.NSMBU_EXIT_AT_FRAME = opts.exitAt;
    }
    const log = opts.log ?? join(nsmbuUserDir(repo), "logs", "nsmbu.log");
    if (opts.log) {
        mkdirSync(dirname(opts.log), { recursive: true });
        extra.NSMBU_LOG_FILE = opts.log;
    }
    console.log(`[replay] log ${log}`);
    cmdLaunch(repo, { noDebug: opts.release, trace: false, gdb: false, lldb: false }, [course, ...rest], extra);
}

export function cmdProfile(repo: string, rest: string[]): void {
    runPython(repo, "tools/profile.py", rest);
}

export function cmdPerf(repo: string, rest: string[]): void {
    runPython(repo, "tools/perf.py", rest);
}

export function cmdInstaller(repo: string, rest: string[]): void {
    const rel = "build/release";
    const cmakeExtra = ["-G", "Ninja", "-DNSMBU_SETUP_GUI=ON"];
    for (const target of ["nsmbu", "nsmbu-extract", "nsmbu-setup"]) {
        runPythonInherit(repo, "tools/build.py", [
            "--release",
            "--target",
            target,
            "--",
            ...cmakeExtra,
            ...rest,
        ]);
    }
    const pkg = runPythonCapture(repo, "tools/installer/dev_pkg.py", []);
    const relAbs = join(repo, rel);
    const env = {
        NSMBU_INSTALLER_BUILD: relAbs,
        NSMBU_SETUP_PKG: `${pkg}/`,
    };
    const launcher = join(pkg, "nsmbu-launcher");
    if (existsSync(launcher)) {
        runShell(repo, `"${launcher}" --setup`, env);
        return;
    }
    runShell(repo, `"${join(relAbs, "nsmbu-setup")}" --setup`, env);
}

export function cmdLint(repo: string): void {
    const verify = existsSync(join(repo, "modding", "node_modules", "typescript-to-lua")) ? "" : "npm --prefix modding ci && ";
    runShell(
        repo,
        `${verify}npm --prefix modding run --silent build && stylua --config-path modding/.stylua.toml modding/examples`,
    );
}

export function cmdRecomp(repo: string): void {
    runPython(repo, "tools/recomp/recomp.py", ["game/code/red-pro2.rpx", "build/gen"]);
}

export function cmdScripted(repo: string, name: string, rest: string[]): void {
    runShell(repo, `bash tools/scripted-run.sh ${shellQuote(name)} ${rest.map(shellQuote).join(" ")}`.trim());
}

function shellQuote(s: string): string {
    if (/^[a-zA-Z0-9._/-]+$/.test(s)) {
        return s;
    }
    return `'${s.replace(/'/g, "'\\''")}'`;
}
