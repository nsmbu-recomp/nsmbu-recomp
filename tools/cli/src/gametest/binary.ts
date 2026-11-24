import { existsSync } from "node:fs";
import { join } from "node:path";

export function resolveNsmbuBinary(repo: string, override?: string): string {
    if (override) {
        if (!existsSync(override)) {
            throw new Error(`missing nsmbu binary; set NSMBU_BIN (${override})`);
        }
        return override;
    }
    for (const base of ["build/debug", "build/release", "build/rel", "build"]) {
        for (const name of ["nsmbu", "nsmbu.exe"]) {
            const path = join(repo, base, name);
            if (existsSync(path)) return path;
        }
    }
    throw new Error("missing nsmbu binary; run: nsmbu build (or set NSMBU_BIN)");
}

export function defaultSaveDir(repo: string, override?: string): string {
    if (override) {
        if (!existsSync(override)) {
            throw new Error(`missing save folder ${override}`);
        }
        return override;
    }
    const primary = join(repo, "nsmbu", "save");
    if (existsSync(primary)) return primary;
    const fallback = join(repo, "user", "save");
    if (existsSync(fallback)) return fallback;
    throw new Error(`missing save folder ${primary}`);
}
