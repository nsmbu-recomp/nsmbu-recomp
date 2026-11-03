import { mkdirSync, writeFileSync } from "node:fs";
import { isAbsolute, join } from "node:path";
import { parseButtonBits } from "./buttons.ts";
import { AgentClient } from "./client.ts";
import { agentDir, agentPidPath, agentSocketPath } from "./paths.ts";
import { isPidAlive, readAgentPid, removeAgentArtifacts } from "./process.ts";
import { canConnectSocket, waitForSocket } from "./socket.ts";
import { spawnPython } from "../run.ts";
import { flagNumber, flagString, hasFlag, parseArgv } from "../parse.ts";

async function ensureNotRunning(repo: string): Promise<void> {
    const sock = agentSocketPath(repo);
    if (await canConnectSocket(sock)) {
        throw new Error("agent game already running (control socket is active)");
    }
    const pid = readAgentPid(repo);
    if (pid !== null && isPidAlive(pid)) {
        throw new Error(`agent game already running (pid ${pid})`);
    }
    removeAgentArtifacts(repo);
}

export async function cmdOpen(repo: string, rest: string[]): Promise<void> {
    await ensureNotRunning(repo);
    mkdirSync(agentDir(repo), { recursive: true });
    const sock = agentSocketPath(repo);
    const child = spawnPython(repo, "tools/run/launch.py", rest, {
        detached: true,
        stdio: "ignore",
        extraEnv: { NSMBU_AGENT_SOCK: sock },
    });
    child.unref();
    if (child.pid === undefined) {
        throw new Error("failed to spawn game");
    }
    writeFileSync(agentPidPath(repo), String(child.pid));
    await waitForSocket(sock, 60_000);
    console.log(`ready (pid ${child.pid}, socket ${sock})`);
}

function formatStatus(r: Awaited<ReturnType<AgentClient["status"]>>): string {
    const parts = [`frame=${r.frame ?? "?"}`, `step=${r.step ?? "?"}`];
    if (r.pid !== undefined) {
        parts.push(`pid=${r.pid}`);
    }
    if (r.scene) {
        parts.push(`scene=${r.scene}`);
    }
    if (r.map !== undefined) {
        parts.push(`map=${r.map ? 1 : 0}`);
    }
    if (r.course !== undefined) {
        parts.push(`course=${r.course ? 1 : 0}`);
    }
    if (r.pack) {
        parts.push(`pack=${r.pack}`);
    }
    if (r.entering) {
        parts.push(`entering=${r.entering}`);
    }
    if (r.file !== undefined) {
        parts.push(`file=${r.file}`);
    }
    if (r.cp !== undefined) {
        parts.push(`cp=${r.cp}`);
    }
    if (r.node !== undefined) {
        parts.push(`node=${r.node}`);
    }
    if (r.world !== undefined) {
        parts.push(`world=${r.world}`);
    }
    if (r.area !== undefined) {
        parts.push(`area=${r.area ? 1 : 0}`);
    }
    if (r.node_name) {
        parts.push(`name=${r.node_name}`);
    }
    if (r.node_x !== undefined && r.node_y !== undefined) {
        parts.push(`node_pos=(${r.node_x}, ${r.node_y})`);
    }
    if (r.map_x !== undefined && r.map_y !== undefined) {
        parts.push(`map_player=(${r.map_x}, ${r.map_y})`);
    }
    if (r.player) {
        parts.push(`player=(${r.player.x}, ${r.player.y}, ${r.player.z})`);
    } else {
        parts.push("player=none");
    }
    return parts.join(" ");
}

function printStatus(r: Awaited<ReturnType<AgentClient["status"]>>): void {
    console.log(formatStatus(r));
}

export async function cmdStatus(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        printStatus(await client.status());
    } finally {
        client.close();
    }
}

export async function cmdLeave(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "leave" });
        if (!r.ok) {
            throw new Error(r.error ?? "leave failed");
        }
    } finally {
        client.close();
    }
}

export async function cmdPress(repo: string, argv: string[]): Promise<void> {
    const { flags, positional } = parseArgv(argv, ["frames"]);
    if (positional.length < 1) {
        throw new Error("usage: nsmbu press <button|hex> [--frames N]");
    }
    const bits = parseButtonBits(positional[0]);
    const frames = flagNumber(flags, "frames", 20);
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "press", bits, frames });
        if (!r.ok) {
            throw new Error(r.error ?? "press failed");
        }
    } finally {
        client.close();
    }
}

export async function cmdStick(repo: string, argv: string[], right: boolean): Promise<void> {
    const { flags, positional } = parseArgv(argv, ["frames"]);
    if (positional.length < 2) {
        throw new Error(`usage: nsmbu ${right ? "rstick" : "stick"} <x> <y> [--frames N]`);
    }
    const x = Number(positional[0]);
    const y = Number(positional[1]);
    const frames = flagNumber(flags, "frames", 20);
    const client = new AgentClient(repo);
    try {
        const r = await client.request({
            cmd: right ? "rstick" : "stick",
            x,
            y,
            frames,
        });
        if (!r.ok) {
            throw new Error(r.error ?? "stick failed");
        }
    } finally {
        client.close();
    }
}

export async function cmdScreenshot(repo: string, argv: string[]): Promise<void> {
    const { positional } = parseArgv(argv, []);
    const pathArg = positional[0];
    const client = new AgentClient(repo);
    try {
        const path =
            pathArg === undefined
                ? join(agentDir(repo), `shot-${Date.now()}.png`)
                : isAbsolute(pathArg)
                  ? pathArg
                  : join(repo, pathArg);
        const body: Record<string, unknown> = { cmd: "screenshot", path };
        const r = await client.request(body);
        if (!r.ok) {
            throw new Error(r.error ?? "screenshot failed");
        }
        if (r.path) {
            console.log(r.path);
        }
    } finally {
        client.close();
    }
}

export async function cmdWait(repo: string, argv: string[]): Promise<void> {
    const { flags } = parseArgv(argv, ["frame", "step", "scene", "timeout"]);
    const frameTarget = hasFlag(flags, "frame") ? flagNumber(flags, "frame", 0) : null;
    const stepTarget = hasFlag(flags, "step") ? flagNumber(flags, "step", 0) : null;
    const sceneTarget = flagString(flags, "scene");
    if (hasFlag(flags, "scene") && sceneTarget === undefined) {
        throw new Error("usage: nsmbu wait --frame N | --step N | --scene NAME [--timeout SEC]");
    }
    if (frameTarget === null && stepTarget === null && sceneTarget === undefined) {
        throw new Error("usage: nsmbu wait --frame N | --step N | --scene NAME [--timeout SEC]");
    }
    const timeoutMs = flagNumber(flags, "timeout", 120) * 1000;
    const client = new AgentClient(repo);
    const start = Date.now();
    let last = "";
    try {
        for (;;) {
            const r = await client.status();
            last = formatStatus(r);
            const frameOk = frameTarget === null || (r.frame ?? 0) >= frameTarget;
            const stepOk = stepTarget === null || (r.step ?? 0) >= stepTarget;
            const sceneOk = sceneTarget === undefined || r.scene === sceneTarget;
            if (frameOk && stepOk && sceneOk) {
                console.log(last);
                return;
            }
            if (Date.now() - start >= timeoutMs) {
                const detail = last ? `: ${last}` : "";
                throw new Error(`wait timed out after ${timeoutMs / 1000}s${detail}`);
            }
            await new Promise((r) => setTimeout(r, 100));
        }
    } finally {
        client.close();
    }
}

export async function cmdQuit(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "quit" });
        if (!r.ok) {
            throw new Error(r.error ?? "quit failed");
        }
    } catch (e) {
        if (!(await canConnectSocket(agentSocketPath(repo), 200))) {
            removeAgentArtifacts(repo);
            return;
        }
        throw e;
    } finally {
        client.close();
    }
    await new Promise((r) => setTimeout(r, 300));
    const pid = readAgentPid(repo);
    if (pid !== null && !isPidAlive(pid)) {
        removeAgentArtifacts(repo);
    }
    if (!(await canConnectSocket(agentSocketPath(repo), 200))) {
        removeAgentArtifacts(repo);
    }
}
