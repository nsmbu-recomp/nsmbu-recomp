import { setAgentSession, validateSessionId } from "./paths.ts";

export function takeSessionFlag(argv: string[]): { session: string | null; rest: string[] } {
    const rest: string[] = [];
    let session: string | null = null;
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        let value: string | undefined;
        if (a === "-s" || a === "--session") {
            value = argv[i + 1];
            if (value === undefined || value.startsWith("-")) {
                throw new Error("usage: nsmbu -s <id> <command> …");
            }
            i++;
        } else if (a.startsWith("--session=")) {
            value = a.slice("--session=".length);
            if (!value) throw new Error("usage: nsmbu -s <id> <command> …");
        } else {
            rest.push(a);
            continue;
        }
        const id = validateSessionId(value);
        if (session !== null && session !== id) {
            throw new Error("session id given more than once");
        }
        session = id;
    }
    return { session, rest };
}

export function applySession(argv: string[]): string[] {
    const taken = takeSessionFlag(argv);
    setAgentSession(taken.session);
    return taken.rest;
}
