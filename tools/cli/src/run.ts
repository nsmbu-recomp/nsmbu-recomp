import { spawn, spawnSync } from "node:child_process";
import type { SpawnOptions } from "node:child_process";
import { join } from "node:path";
import { nsmbuUserDir } from "./repo.ts";

export function baseEnv(repo: string, extra: Record<string, string> = {}): NodeJS.ProcessEnv {
    return {
        ...process.env,
        NSMBU_CLI: "1",
        NSMBU_USER_DIR: nsmbuUserDir(repo),
        ...extra,
    };
}

export function runPython(
    repo: string,
    script: string,
    args: string[],
    extraEnv: Record<string, string> = {},
): void {
    const status = runPythonChecked(repo, script, args, extraEnv);
    process.exit(status);
}

export function runPythonChecked(
    repo: string,
    script: string,
    args: string[],
    extraEnv: Record<string, string> = {},
): number {
    const r = spawnSync("python3", [join(repo, script), ...args], {
        cwd: repo,
        env: baseEnv(repo, extraEnv),
        stdio: "inherit",
    });
    if (r.error) {
        throw r.error;
    }
    return r.status ?? 1;
}

export function runPythonCapture(
    repo: string,
    script: string,
    args: string[],
    extraEnv: Record<string, string> = {},
): { status: number; stdout: string; stderr: string } {
    const r = spawnSync("python3", [join(repo, script), ...args], {
        cwd: repo,
        env: baseEnv(repo, extraEnv),
        encoding: "utf8",
    });
    if (r.error) {
        throw r.error;
    }
    return {
        status: r.status ?? 1,
        stdout: r.stdout ?? "",
        stderr: r.stderr ?? "",
    };
}

export function spawnPython(
    repo: string,
    script: string,
    args: string[],
    options: SpawnOptions & { extraEnv?: Record<string, string> } = {},
): ReturnType<typeof spawn> {
    const { extraEnv = {}, ...spawnOpts } = options;
    return spawn("python3", [join(repo, script), ...args], {
        cwd: repo,
        env: baseEnv(repo, extraEnv),
        ...spawnOpts,
    });
}

export function runShell(repo: string, command: string, extraEnv: Record<string, string> = {}): void {
    const r = spawnSync(command, {
        cwd: repo,
        env: baseEnv(repo, extraEnv),
        stdio: "inherit",
        shell: true,
    });
    if (r.error) {
        console.error(r.error.message);
        process.exit(1);
    }
    process.exit(r.status ?? 1);
}
