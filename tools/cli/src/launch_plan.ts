export function launchArgs(
    opts: { noDebug: boolean; trace: boolean; gdb: boolean; lldb: boolean; wine?: boolean },
    rest: string[],
): string[] {
    const args: string[] = [];
    if (opts.noDebug) {
        args.push("--no-debug");
    }
    if (opts.trace) {
        args.push("--trace");
    }
    if (opts.gdb) {
        args.push("--gdb");
    }
    if (opts.lldb) {
        args.push("--lldb");
    }
    if (opts.wine) {
        args.push("--wine");
    }
    args.push("--cli", ...rest);
    return args;
}

export function runBuildArgs(release: boolean): string[] {
    return release ? ["--release"] : [];
}