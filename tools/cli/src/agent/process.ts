import { readdirSync, readFileSync, unlinkSync } from "node:fs";
import { agentPidPath, agentRoot, agentSocketPath } from "./paths.ts";
import { canConnectSocket } from "./socket.ts";

export function isPidAlive(pid: number): boolean {
    if (!Number.isInteger(pid) || pid <= 0) {
        return false;
    }
    try {
        process.kill(pid, 0);
        return true;
    } catch {
        return false;
    }
}

function readPidFile(path: string): number | null {
    try {
        const raw = readFileSync(path, "utf8").trim();
        const pid = Number.parseInt(raw, 10);
        return Number.isInteger(pid) ? pid : null;
    } catch {
        return null;
    }
}

export function readAgentPid(repo: string, session?: string | null): number | null {
    return readPidFile(agentPidPath(repo, session));
}

export function removeAgentArtifacts(repo: string, session?: string | null): void {
    for (const p of [agentSocketPath(repo, session), agentPidPath(repo, session)]) {
        try {
            unlinkSync(p);
        } catch {
            /* absent */
        }
    }
}

export type LiveAgent = {
    id: string | null;
    pid: number | null;
    socket: boolean;
};

export async function liveAgents(repo: string): Promise<LiveAgent[]> {
    const ids: (string | null)[] = [null];
    try {
        for (const ent of readdirSync(agentRoot(repo), { withFileTypes: true })) {
            if (!ent.isDirectory()) continue;
            if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(ent.name)) continue;
            ids.push(ent.name);
        }
    } catch {
        /* no agent dir */
    }
    const found: LiveAgent[] = [];
    for (const id of ids) {
        const socket = await canConnectSocket(agentSocketPath(repo, id), 200);
        const pid = readAgentPid(repo, id);
        const alive = pid !== null && isPidAlive(pid);
        if (socket || alive) found.push({ id, pid: alive ? pid : null, socket });
    }
    return found;
}

export function formatLiveAgent(agent: LiveAgent): string {
    const where = agent.id ? `session ${agent.id}, ` : "";
    if (agent.pid !== null) return `agent game already running (${where}pid ${agent.pid})`;
    return `agent game already running (${where}control socket is active)`;
}

export function closeLiveAgent(agent: LiveAgent): string {
    if (agent.id) return `nsmbu -s ${agent.id} close`;
    return "nsmbu close";
}
