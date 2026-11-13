import { tmpdir } from "node:os";
import { join } from "node:path";

let sessionId: string | null = null;

export function setAgentSession(id: string | null): void {
    sessionId = id;
}

export function agentSession(): string | null {
    return sessionId;
}

export function validateSessionId(id: string): string {
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(id)) {
        throw new Error(
            `invalid session id: ${id} (use letters, digits, '.', '_', or '-', up to 64 characters)`,
        );
    }
    return id;
}

function picked(session: string | null | undefined): string | null {
    if (session === undefined) return sessionId;
    return session;
}

export function agentRoot(repo: string): string {
    return join(repo, ".tmp", "agent");
}

export function agentDir(repo: string, session?: string | null): string {
    const id = picked(session);
    const root = agentRoot(repo);
    if (!id) return root;
    return join(root, id);
}

export function agentUserDir(repo: string, session?: string | null): string | null {
    const id = picked(session);
    if (!id) return null;
    return join(agentDir(repo, id), "user");
}

export function agentPidPath(repo: string, session?: string | null): string {
    return join(agentDir(repo, session), "pid");
}

export function agentSocketPath(repo: string, session?: string | null): string {
    const id = picked(session);
    if (process.platform === "win32") {
        const name = id ? `nsmbu-agent-${id}` : "nsmbu-agent";
        return `\\\\.\\pipe\\${name}`;
    }
    const preferred = join(agentDir(repo, id), "ctl.sock");
    if (preferred.length < 108) return preferred;
    if (!id) return join(tmpdir(), "nsmbu-agent", "ctl.sock");
    return join(tmpdir(), "nsmbu-agent", id, "ctl.sock");
}
