import { join } from "node:path";

export function agentDir(repo: string): string {
    return join(repo, ".tmp", "agent");
}

export function agentPidPath(repo: string): string {
    return join(agentDir(repo), "pid");
}

export function agentSocketPath(repo: string): string {
    if (process.platform === "win32") {
        return "\\\\.\\pipe\\nsmbu-agent";
    }
    return join(agentDir(repo), "ctl.sock");
}
