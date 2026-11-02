import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, unlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { AgentClient } from "../agent/client.ts";
import { parseButtonBits } from "../agent/buttons.ts";
import { agentSocketPath } from "../agent/paths.ts";
import { isPidAlive, readAgentPid } from "../agent/process.ts";
import { canConnectSocket, waitForSocket } from "../agent/socket.ts";
import { spawnPython } from "../run.ts";

export type PlayStatus = {
    frame?: number;
    step?: number;
    scene?: string;
    map?: boolean;
    course?: boolean;
    pack?: string;
    entering?: string;
    file?: number;
    cp?: number;
    node?: number;
    world?: number;
    area?: boolean;
    node_name?: string;
    node_x?: number;
    node_y?: number;
    map_x?: number;
    map_y?: number;
    map_lock?: number;
    map_title?: number;
    map_players?: number;
    player?: { x: number; y: number; z: number } | null;
    sound_ready?: boolean;
    audio_buffered?: number;
};

export function formatStatus(s: PlayStatus): string {
    const parts = [`frame=${s.frame ?? "?"}`, `step=${s.step ?? "?"}`];
    if (s.scene) parts.push(`scene=${s.scene}`);
    if (s.map !== undefined) parts.push(`map=${s.map ? 1 : 0}`);
    if (s.course !== undefined) parts.push(`course=${s.course ? 1 : 0}`);
    if (s.pack) parts.push(`pack=${s.pack}`);
    if (s.entering) parts.push(`entering=${s.entering}`);
    if (s.file !== undefined) parts.push(`file=${s.file}`);
    if (s.cp !== undefined) parts.push(`cp=${s.cp}`);
    if (s.node !== undefined) parts.push(`node=${s.node}`);
    if (s.world !== undefined) parts.push(`world=${s.world}`);
    if (s.area !== undefined) parts.push(`area=${s.area ? 1 : 0}`);
    if (s.node_name) parts.push(`name=${s.node_name}`);
    if (s.node_x !== undefined && s.node_y !== undefined) parts.push(`node_pos=(${s.node_x}, ${s.node_y})`);
    if (s.map_x !== undefined && s.map_y !== undefined) parts.push(`map_player=(${s.map_x}, ${s.map_y})`);
    if (s.player) parts.push(`player=(${s.player.x}, ${s.player.y}, ${s.player.z})`);
    else parts.push("player=none");
    if (s.sound_ready !== undefined) parts.push(`sound_ready=${s.sound_ready ? 1 : 0}`);
    if (s.audio_buffered !== undefined) parts.push(`audio_buffered=${s.audio_buffered}`);
    return parts.join(" ");
}

function withTimeout<T>(p: Promise<T>, ms: number, label: string): Promise<T> {
    return new Promise((resolve, reject) => {
        const t = setTimeout(() => reject(new Error(`${label} timed out after ${ms / 1000}s`)), ms);
        p.then(
            (v) => {
                clearTimeout(t);
                resolve(v);
            },
            (e) => {
                clearTimeout(t);
                reject(e instanceof Error ? e : new Error(String(e)));
            },
        );
    });
}

export async function ensureLiveAgentIdle(repo: string): Promise<void> {
    const sock = agentSocketPath(repo);
    if (await canConnectSocket(sock)) {
        throw new Error("agent game already running (control socket is active); nsmbu close first");
    }
    const pid = readAgentPid(repo);
    if (pid !== null && isPidAlive(pid)) {
        throw new Error(`agent game already running (pid ${pid}); nsmbu close first`);
    }
}

export class Session {
    readonly repo: string;
    readonly name: string;
    readonly workDir: string;
    readonly saveDir: string;
    readonly userDir: string;
    readonly logPath: string;
    readonly show: boolean;
    private client: AgentClient | null = null;
    private pid: number | null = null;
    private openPack: string | undefined;
    private env: Record<string, string> = {};
    last = "";

    constructor(repo: string, name: string, opts?: { show?: boolean }) {
        this.repo = repo;
        this.name = name;
        this.show = opts?.show ?? false;
        this.workDir = join(repo, ".tmp", "playtest", name);
        this.saveDir = join(this.workDir, "save");
        this.userDir = join(this.workDir, "user");
        this.logPath = join(this.workDir, "nsmbu.log");
    }

    private sockPath(): string {
        if (process.platform === "win32") {
            return `\\\\.\\pipe\\nsmbu-playtest-${this.name}`;
        }
        const preferred = join(this.workDir, "ctl.sock");
        if (preferred.length < 108) return preferred;
        return join(tmpdir(), "nsmbu-playtest", this.name, "ctl.sock");
    }

    private pidPath(): string {
        return join(this.workDir, "pid");
    }

    private readPid(): number | null {
        try {
            const raw = readFileSync(this.pidPath(), "utf8").trim();
            const pid = Number.parseInt(raw, 10);
            return Number.isInteger(pid) ? pid : null;
        } catch {
            return null;
        }
    }

    private clearAgentFiles(): void {
        for (const p of [this.sockPath(), this.pidPath()]) {
            if (process.platform === "win32" && p.startsWith("\\\\.\\pipe\\")) continue;
            try {
                unlinkSync(p);
            } catch {
                /* absent */
            }
        }
    }

    private killPid(pid: number): void {
        try {
            process.kill(pid, "SIGTERM");
        } catch {
            /* already exited */
        }
    }

    private async reapPid(pid: number, ms: number): Promise<void> {
        const deadline = Date.now() + ms;
        while (isPidAlive(pid) && Date.now() < deadline) {
            await new Promise((r) => setTimeout(r, 100));
        }
        if (isPidAlive(pid)) {
            try {
                process.kill(pid, "SIGKILL");
            } catch {
                /* already exited */
            }
        }
    }

    private async ensureOwnIdle(): Promise<void> {
        const sock = this.sockPath();
        const pid = this.readPid();
        if (await canConnectSocket(sock) || (pid !== null && isPidAlive(pid))) {
            if (pid !== null && isPidAlive(pid)) {
                this.killPid(pid);
                await this.reapPid(pid, 5000);
            }
            this.clearAgentFiles();
            if (await canConnectSocket(sock)) {
                throw new Error(`playtest ${this.name} already running (control socket is active)`);
            }
        } else {
            this.clearAgentFiles();
        }
    }

    async start(opts: {
        save: "fresh" | "repo" | "keep";
        open?: string;
        env?: Record<string, string>;
        warmShaders?: boolean;
    }): Promise<void> {
        await this.ensureOwnIdle();
        if (opts.save !== "keep") {
            rmSync(this.workDir, { recursive: true, force: true });
            mkdirSync(this.saveDir, { recursive: true });
            mkdirSync(this.userDir, { recursive: true });
            if (opts.save === "repo") {
                cpSync(join(this.repo, "nsmbu", "save"), this.saveDir, { recursive: true });
                for (const file of ["settings.ini", "controls.json"]) {
                    const from = join(this.repo, "nsmbu", file);
                    if (existsSync(from)) cpSync(from, join(this.userDir, file));
                }
            }
            if (opts.warmShaders) {
                const from = join(this.repo, "nsmbu", "shadercache");
                if (existsSync(from)) cpSync(from, join(this.userDir, "shadercache"), { recursive: true });
            }
        } else {
            mkdirSync(this.saveDir, { recursive: true });
            mkdirSync(this.userDir, { recursive: true });
        }
        this.openPack = opts.open;
        this.env = opts.env ?? {};
        await this.launch();
    }

    async restart(env?: Record<string, string>): Promise<void> {
        if (env) this.env = { ...this.env, ...env };
        await this.stop();
        await this.launch();
    }

    private async launch(): Promise<void> {
        await this.ensureOwnIdle();
        mkdirSync(this.workDir, { recursive: true });
        mkdirSync(dirname(this.logPath), { recursive: true });
        mkdirSync(dirname(this.sockPath()), { recursive: true });
        const sock = this.sockPath();
        const args = ["--save", this.saveDir];
        if (this.openPack) args.push(this.openPack);
        const extraEnv: Record<string, string> = {
            NSMBU_LOG_FILE: this.logPath,
            NSMBU_AGENT_SOCK: sock,
            NSMBU_USER_DIR: this.userDir,
            NSMBU_HIDDEN_WINDOWS: "1",
            NSMBU_NO_HOST_INPUT: "1",
            NSMBU_NO_AUDIO: "1",
            NSMBU_VK_PRESENT_MODE: "immediate",
            NSMBU_RES_SCALE: "1",
            NSMBU_UNCAPPED: "1",
            ...this.env,
        };
        extraEnv.NSMBU_HIDDEN_WINDOWS = this.show ? "0" : "1";
        if (this.env.NSMBU_NO_AUDIO === "0") delete extraEnv.NSMBU_NO_AUDIO;
        const child = spawnPython(this.repo, "tools/run/launch.py", args, {
            detached: true,
            stdio: "ignore",
            extraEnv,
        });
        child.unref();
        if (child.pid === undefined) throw new Error("failed to spawn game");
        this.pid = child.pid;
        writeFileSync(this.pidPath(), String(child.pid));
        await waitForSocket(sock, 90_000);
        this.client = new AgentClient(this.repo, sock);
    }

    private live(): AgentClient {
        if (!this.client) throw new Error("game is not running");
        return this.client;
    }

    async status(): Promise<PlayStatus> {
        const r = await withTimeout(this.live().status(), 20_000, "status");
        const s: PlayStatus = {
            frame: r.frame,
            step: r.step,
            scene: r.scene,
            map: r.map,
            course: r.course,
            pack: r.pack,
            entering: r.entering,
            file: r.file,
            cp: r.cp,
            node: r.node,
            world: r.world,
            area: r.area,
            node_name: r.node_name,
            node_x: r.node_x,
            node_y: r.node_y,
            map_x: r.map_x,
            map_y: r.map_y,
            map_lock: r.map_lock,
            map_title: r.map_title,
            map_players: r.map_players,
            player: r.player,
            sound_ready: r.sound_ready,
            audio_buffered: r.audio_buffered,
        };
        this.last = formatStatus(s);
        return s;
    }

    async leave(): Promise<void> {
        const r = await withTimeout(this.live().request({ cmd: "leave" }), 20_000, "leave");
        if (!r.ok) throw new Error(r.error ?? "leave failed");
    }

    async eval(code: string, timeoutMs = 8000): Promise<string> {
        const r = await withTimeout(this.live().request({ cmd: "eval", code, timeout: timeoutMs }), timeoutMs + 5000, "eval");
        if (!r.ok) throw new Error(r.error ?? "eval failed");
        return (r.output ?? "").trim();
    }

    async screenshot(path: string): Promise<void> {
        const r = await withTimeout(this.live().request({ cmd: "screenshot", path }), 20_000, "screenshot");
        if (!r.ok) throw new Error(r.error ?? "screenshot failed");
    }

    async press(button: string, frames = 20, tap = false): Promise<void> {
        const r = await withTimeout(
            this.live().request({ cmd: "press", bits: parseButtonBits(button), frames, tap }),
            Math.max(15000, frames * 50),
            "press",
        );
        if (!r.ok) throw new Error(r.error ?? "press failed");
    }

    async stick(x: number, y: number, frames = 60): Promise<void> {
        const r = await withTimeout(
            this.live().request({ cmd: "stick", x, y, frames }),
            Math.max(15000, frames * 50),
            "stick",
        );
        if (!r.ok) throw new Error(r.error ?? "stick failed");
    }

    async wait(pred: (s: PlayStatus) => boolean, timeoutSec: number): Promise<PlayStatus> {
        const start = Date.now();
        for (;;) {
            const s = await this.status();
            if (pred(s)) return s;
            if (Date.now() - start >= timeoutSec * 1000) {
                throw new Error(`wait timed out after ${timeoutSec}s: ${this.last}`);
            }
            await new Promise((r) => setTimeout(r, 100));
        }
    }

    async advance(steps: number, timeoutSec = 30): Promise<void> {
        const now = await this.status();
        const target = (now.step ?? 0) + steps;
        await this.wait((s) => (s.step ?? 0) >= target, timeoutSec);
    }

    async stop(): Promise<void> {
        const client = this.client;
        this.client = null;
        if (client) {
            try {
                await withTimeout(client.request({ cmd: "quit" }), 5000, "quit");
            } catch {
                /* process may already be gone */
            }
            client.close();
        }
        const pid = this.pid ?? this.readPid();
        if (pid !== null) {
            await this.reapPid(pid, 5000);
            if (isPidAlive(pid)) this.killPid(pid);
            await this.reapPid(pid, 2000);
        }
        this.clearAgentFiles();
        this.pid = null;
    }
}
