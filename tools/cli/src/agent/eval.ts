import { hasFlag, parseArgv } from "../parse.ts";

const usage = "usage: nsmbu eval --list | nsmbu eval <name> [json]";

export function evalBody(argv: string[]): Record<string, unknown> {
    const { flags, positional } = parseArgv(argv, ["list"], []);
    if (hasFlag(flags, "list")) {
        if (positional.length !== 0) {
            throw new Error(usage);
        }
        return { cmd: "eval", list: true };
    }
    if (positional.length < 1 || positional.length > 2) {
        throw new Error(usage);
    }
    const body: Record<string, unknown> = { cmd: "eval", name: positional[0] };
    if (positional.length === 2) {
        let args: unknown;
        try {
            args = JSON.parse(positional[1]);
        } catch {
            throw new Error("eval json is not valid");
        }
        if (args === null || typeof args !== "object" || Array.isArray(args)) {
            throw new Error("eval json must be an object");
        }
        body.args = args;
    }
    return body;
}
