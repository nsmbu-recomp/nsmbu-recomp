import { readFileSync, unlinkSync } from "node:fs";
import { agentPidPath, agentSocketPath } from "./paths.ts";

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

export function readAgentPid(repo: string): number | null {
    try {
        const raw = readFileSync(agentPidPath(repo), "utf8").trim();
        const pid = Number.parseInt(raw, 10);
        return Number.isInteger(pid) ? pid : null;
    } catch {
        return null;
    }
}

export function removeAgentArtifacts(repo: string): void {
    for (const p of [agentSocketPath(repo), agentPidPath(repo)]) {
        try {
            unlinkSync(p);
        } catch {
            /* absent */
        }
    }
}
