export function flagName(envName: string): string {
    const bare = envName.startsWith("NSMBU_") ? envName.slice(6) : envName;
    return "--" + bare.toLowerCase().replaceAll("_", "-");
}

export function runtimeArgs(vars: Record<string, string | undefined>): string[] {
    const args: string[] = [];
    for (const [key, value] of Object.entries(vars)) {
        if (value === undefined || !key.startsWith("NSMBU_")) continue;
        const flag = flagName(key);
        if (value === "1") args.push(flag);
        else args.push(`${flag}=${value}`);
    }
    return args;
}

export function stripRuntime(env: NodeJS.ProcessEnv): NodeJS.ProcessEnv {
    const out: NodeJS.ProcessEnv = { ...env };
    for (const key of Object.keys(out)) {
        if (key.startsWith("NSMBU_") || key === "WIIU_COMMON_KEY") delete out[key];
    }
    return out;
}
