import type { AgentResponse } from "./client.ts";

export type ScreenKind =
    | "boot"
    | "gamepad"
    | "title"
    | "saves"
    | "challenge"
    | "loading"
    | "course"
    | "world-map"
    | "world-map-iris"
    | "start-menu"
    | "pause"
    | "controller"
    | "settings"
    | "unknown";

export type ScreenFacts = {
    scene?: string;
    map?: boolean;
    course?: boolean;
    pack?: string;
    entering?: string;
    node_name?: string;
    map_lock?: number;
    player?: { x: number; y: number; z: number } | null;
    overlay?: boolean;
    tab?: string;
    menu?: boolean;
    cursorName?: string;
    pause?: boolean;
    pauseRow?: string;
    controller?: boolean;
};

const ROW_NAMES: Record<string, string> = {
    Pa_Continue_00: "Continue",
    Pa_Exit_00: "Quit",
    Pa_SetBtn_00: "Settings",
    Pa_PlayStyle_00: "Change Controller",
};

export function pauseRowLabel(name: string | undefined, index: number | undefined): string | undefined {
    if (name && ROW_NAMES[name]) return ROW_NAMES[name];
    if (name) return name;
    if (index === 0) return "Continue";
    if (index === 1) return "Quit";
    if (index === 2) return "Settings";
    return undefined;
}

export function factsFrom(status: AgentResponse, ui: AgentResponse | null): ScreenFacts {
    const selected = ui?.buttons?.find((b) => b.selected);
    return {
        scene: status.scene,
        map: status.map,
        course: status.course,
        pack: status.pack,
        entering: status.entering,
        node_name: status.node_name,
        map_lock: status.map_lock,
        player: status.player,
        overlay: ui?.overlay ?? status.overlay,
        tab: ui?.tab ?? status.tab,
        menu: ui?.menu ?? status.menu,
        cursorName: selected?.name || ui?.cursor_name || status.cursor_name,
        pause: ui?.pause ?? status.pause,
        pauseRow: pauseRowLabel(ui?.pause_row ?? status.pause_row, ui?.pause_index ?? status.pause_index),
        controller: ui?.controller ?? status.controller,
    };
}

function courseUp(f: ScreenFacts): boolean {
    return f.scene === "CourseTask" || (f.course === true && f.player != null);
}

export function screenKind(f: ScreenFacts): ScreenKind {
    if (f.overlay) return "settings";
    if (f.controller) return "controller";
    if (f.pause) return "pause";
    if (f.menu) return "start-menu";
    if (f.entering) return "loading";
    if (courseUp(f)) return "course";
    if (f.map && f.map_lock) return "world-map-iris";
    if (f.map || f.scene === "Map" || f.scene === "CourseSelect") return "world-map";
    switch (f.scene) {
        case "Boot":
        case "Flow":
            return "boot";
        case "FileSelect":
            return "gamepad";
        case "Title":
            return "title";
        case "Menu":
            return "saves";
        case "SaveSelect":
            return "challenge";
        default:
            return "unknown";
    }
}

function coveredKind(f: ScreenFacts): ScreenKind {
    return screenKind({ ...f, overlay: false });
}

export function formatScreen(f: ScreenFacts): string {
    const kind = screenKind(f);
    const parts = [`screen=${kind}`];
    if (kind === "settings" && f.tab) parts.push(`tab=${f.tab}`);
    if (kind === "settings") {
        const under = coveredKind(f);
        if (under !== "unknown" && under !== "settings") parts.push(`behind=${under}`);
    }
    if (kind === "pause" && f.pauseRow) parts.push(`row=${f.pauseRow}`);
    if (kind === "start-menu" && f.cursorName) parts.push(`cursor=${f.cursorName}`);
    if ((kind === "course" || kind === "loading") && (f.entering || f.pack)) parts.push(`pack=${f.entering || f.pack}`);
    if ((kind === "world-map" || kind === "world-map-iris") && f.node_name) parts.push(`name=${f.node_name}`);
    return parts.join(" ");
}

const LOG_TAGS: Record<ScreenKind, string[]> = {
    boot: ["scene", "open", "boot"],
    gamepad: ["scene", "input"],
    title: ["scene", "input"],
    saves: ["scene", "input"],
    challenge: ["scene", "input"],
    loading: ["open", "scene"],
    course: ["open", "scene", "pause", "input"],
    "world-map": ["scene", "menu", "input"],
    "world-map-iris": ["scene", "menu", "input"],
    "start-menu": ["menu", "overlay", "input"],
    pause: ["pause", "overlay", "menu", "input"],
    controller: ["pause", "overlay", "input"],
    settings: ["overlay", "pause", "menu"],
    unknown: ["scene", "open", "pause", "menu", "overlay", "input"],
};

export function logTags(kind: ScreenKind): string[] {
    return LOG_TAGS[kind];
}

function hasTag(line: string, tags: string[]): boolean {
    return tags.some((tag) => line.includes(`[${tag}]`));
}

export function filterLog(lines: string[], tags: string[], limit: number): string[] {
    let lastScene: string | undefined;
    for (let i = lines.length - 1; i >= 0; i--) {
        if (lines[i].includes("[scene]")) {
            lastScene = lines[i];
            break;
        }
    }
    const picked = lines.filter((line) => hasTag(line, tags) || line === lastScene);
    const n = limit > 0 ? limit : 40;
    return picked.slice(-n);
}
