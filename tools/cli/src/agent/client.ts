import net from "node:net";
import readline from "node:readline";
import { agentSocketPath } from "./paths.ts";

type AgentResponse = {
    id: number;
    ok: boolean;
    error?: string;
    frame?: number;
    step?: number;
    pid?: number;
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
    banner?: string;
    player?: { x: number; y: number; z: number } | null;
    sound_ready?: boolean;
    audio_buffered?: number;
    path?: string;
    output?: string;
    overlay?: boolean;
    tab?: string;
    menu?: boolean;
    cursor?: number;
    buttons?: { index: number; name: string; selected: boolean; x0?: number; y0?: number; x1?: number; y1?: number }[];
    power?: number;
    power_name?: string;
    actors?: { profile: number; x: number; y: number; z: number; vx: number; vy: number; vz: number }[];
    lines?: string[];
    paths?: string[];
};

export type { AgentResponse };

export class AgentClient {
    private socket: net.Socket | null = null;
    private rl: readline.Interface | null = null;
    private nextId = 1;
    private pending = new Map<number, { resolve: (v: AgentResponse) => void; reject: (e: Error) => void }>();
    private readonly repo: string;
    private readonly overrideSocket: string | undefined;

    constructor(repo: string, socketPath?: string) {
        this.repo = repo;
        this.overrideSocket = socketPath;
    }

    socketPath(): string {
        return this.overrideSocket ?? agentSocketPath(this.repo);
    }

    async connect(): Promise<void> {
        if (this.socket) {
            return;
        }
        const path = this.socketPath();
        await new Promise<void>((resolve, reject) => {
            const socket = net.createConnection(path);
            socket.once("connect", () => {
                this.socket = socket;
                this.rl = readline.createInterface({ input: socket });
                this.rl.on("line", (line) => this.onLine(line));
                this.rl.on("error", (err) => this.failAll(err instanceof Error ? err : new Error(String(err))));
                socket.on("error", (err) => this.failAll(err));
                socket.on("close", () => this.failAll(new Error("agent connection closed")));
                resolve();
            });
            socket.once("error", reject);
        });
    }

    close(): void {
        this.rl?.close();
        this.socket?.destroy();
        this.rl = null;
        this.socket = null;
        this.pending.clear();
    }

    private onLine(line: string): void {
        let msg: AgentResponse;
        try {
            msg = JSON.parse(line) as AgentResponse;
        } catch {
            return;
        }
        if (typeof msg.id !== "number") {
            return;
        }
        const p = this.pending.get(msg.id);
        if (!p) {
            return;
        }
        this.pending.delete(msg.id);
        p.resolve(msg);
    }

    private failAll(err: Error): void {
        for (const p of this.pending.values()) {
            p.reject(err);
        }
        this.pending.clear();
    }

    async request(body: Record<string, unknown>): Promise<AgentResponse> {
        await this.connect();
        const id = this.nextId++;
        const req = { ...body, id };
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.socket?.write(`${JSON.stringify(req)}\n`, (err) => {
                if (err) {
                    this.pending.delete(id);
                    reject(err);
                }
            });
        });
    }

    async status(): Promise<AgentResponse> {
        const r = await this.request({ cmd: "status" });
        if (!r.ok) {
            throw new Error(r.error ?? "status failed");
        }
        return r;
    }
}
