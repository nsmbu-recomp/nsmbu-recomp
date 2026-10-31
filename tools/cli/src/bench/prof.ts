export type ProfWindow = {
    frame: number;
    frame_ms: number;
    swaps_per_s: number;
    logic_steps_per_s: number;
    render_cpu_ms: number;
    render_idle_ms: number;
    wait_gpu_ms?: number;
    wait_acquire_ms?: number;
    wait_present_ms?: number;
};

const HEADER =
    /^\[prof\] frame (\d+): [\d.]+ frames \([\d.]+ hold\), ([\d.]+) ms\/frame, ([\d.]+) swaps\/s, ([\d.]+) logic steps\/s; render thread CPU ([\d.]+) ms\/frame, in ops [\d.]+ ms\/frame, idle \(waiting for commands\) ([\d.]+) ms\/frame/;

const WAITS = /^\[prof\] render thread waits ms\/frame: GPU ([\d.]+) \([\d.]+\/frame\) acquire ([\d.]+) present ([\d.]+)/;

export const MAP_FRAME = 3200;
export const MAP_WINDOWS = 6;

export function parseProf(log: string): ProfWindow[] {
    const windows: ProfWindow[] = [];
    let current: ProfWindow | null = null;
    for (const line of log.split("\n")) {
        const header = HEADER.exec(line);
        if (header) {
            current = {
                frame: Number(header[1]),
                frame_ms: Number(header[2]),
                swaps_per_s: Number(header[3]),
                logic_steps_per_s: Number(header[4]),
                render_cpu_ms: Number(header[5]),
                render_idle_ms: Number(header[6]),
            };
            windows.push(current);
            continue;
        }
        if (!current) continue;
        const waits = WAITS.exec(line);
        if (!waits) continue;
        current.wait_gpu_ms = Number(waits[1]);
        current.wait_acquire_ms = Number(waits[2]);
        current.wait_present_ms = Number(waits[3]);
    }
    return windows;
}

function mean(values: number[]): number {
    return values.reduce((sum, n) => sum + n, 0) / values.length;
}

export type TraceScore = {
    ok: boolean;
    detail: string;
    windows: ProfWindow[];
    map: ProfWindow[];
};

export function scoreLoadTrace(log: string): TraceScore {
    const windows = parseProf(log);
    const map = windows.filter((w) => w.frame >= MAP_FRAME);
    if (!log.includes("[scene] FileSelect")) {
        return { ok: false, detail: "load-overworld never opened the file select", windows, map };
    }
    if (!log.includes("[scene] CourseSelect from save")) {
        return { ok: false, detail: "load-overworld never loaded the overworld", windows, map };
    }
    if (map.length < MAP_WINDOWS) {
        return {
            ok: false,
            detail: `load-overworld recorded ${map.length} map windows, need ${MAP_WINDOWS}`,
            windows,
            map,
        };
    }
    const frameMs = mean(map.map((w) => w.frame_ms));
    const swaps = mean(map.map((w) => w.swaps_per_s));
    const cpu = mean(map.map((w) => w.render_cpu_ms));
    const gpu = map.filter((w) => w.wait_gpu_ms !== undefined).map((w) => w.wait_gpu_ms as number);
    const gpuText = gpu.length > 0 ? `, gpu wait ${mean(gpu).toFixed(2)} ms` : "";
    return {
        ok: true,
        detail: `load-overworld map ${frameMs.toFixed(2)} ms/frame, ${swaps.toFixed(1)} swaps/s, render ${cpu.toFixed(2)} ms${gpuText} (${map.length} windows)`,
        windows,
        map,
    };
}
