import { mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { dirname, isAbsolute, join } from "node:path";
import { isPidAlive, readAgentPid } from "./agent/process.ts";
import { flagString, parseArgv } from "./parse.ts";
import { nsmbuUserDir } from "./repo.ts";

export const SETTINGS_HEADER = "# New Super Mario Bros. U settings (settings overlay, ` or F12)\n";

const FPS_KEYS = ["fps60", "fpsUnlocked", "fpsCustom", "interpFps"] as const;
const LADDER = [30, 60, 120, 144, 165, 240];

type Values = Map<string, string>;

type Spec = {
    key: string;
    hint: string;
    listed: boolean;
    read: (values: Values) => string | undefined;
    apply: (values: Values, raw: string) => void;
    clear: (values: Values) => void;
};

function formatNum(n: number): string {
    const rounded = Math.round(n * 1e6) / 1e6;
    return String(rounded);
}

function parseBool(raw: string, label: string): string {
    const text = raw.trim().toLowerCase();
    if (text === "1" || text === "on" || text === "true" || text === "yes") return "1";
    if (text === "0" || text === "off" || text === "false" || text === "no") return "0";
    throw new Error(`${label} is 0 or 1`);
}

function parseEnum(raw: string, values: string[], label: string): string {
    const text = raw.trim().toLowerCase();
    const found = values.find((value) => value.toLowerCase() === text);
    if (!found) throw new Error(`${label} is ${values.join(", ")}`);
    return found;
}

function parseIntRange(raw: string, min: number, max: number, label: string, names?: Record<string, string>): string {
    const text = raw.trim();
    const mapped = names?.[text.toLowerCase()];
    if (mapped !== undefined) return mapped;
    if (!/^-?\d+$/.test(text)) {
        const choices = names ? Object.keys(names).join(", ") : `${min} to ${max}`;
        throw new Error(`${label} is ${choices}`);
    }
    const n = Number(text);
    if (n < min || n > max) throw new Error(`${label} is ${min} to ${max}`);
    return String(n);
}

function parseFloatRange(raw: string, min: number, max: number, label: string, percent = false): string {
    const text = raw.trim();
    const n = percent && text.endsWith("%") ? Number(text.slice(0, -1)) / 100 : Number(text);
    if (!Number.isFinite(n) || n < min || n > max) throw new Error(`${label} is ${min} to ${max}`);
    return formatNum(n);
}

function direct(
    key: string,
    hint: string,
    parse: (raw: string) => string,
    listed = true,
): Spec {
    return {
        key,
        hint,
        listed,
        read: (values) => values.get(key),
        apply: (values, raw) => {
            values.set(key, parse(raw));
        },
        clear: (values) => {
            values.delete(key);
        },
    };
}

function boolSpec(key: string, hint: string, listed = true): Spec {
    return direct(key, hint, (raw) => parseBool(raw, key), listed);
}

function readFps(values: Values): string | undefined {
    if (!FPS_KEYS.some((key) => values.has(key))) return undefined;
    if (values.get("fpsUnlocked") === "1") return "unlocked";
    if (values.get("fpsCustom") === "1") return values.get("interpFps") ?? "60";
    if (values.has("interpFps") && values.get("fps60") !== "0") return values.get("interpFps");
    return "60";
}

function applyFps(values: Values, raw: string): void {
    let text = raw.trim().toLowerCase().replace(/\s*fps$/, "");
    if (text !== "unlocked" && !/^\d+$/.test(text)) {
        throw new Error("fps is 30, 60, 120, 144, 165, 240, unlocked, or an integer from 60 to 1000");
    }
    values.delete("fps");
    if (text === "unlocked") {
        values.set("fps60", "1");
        values.set("fpsUnlocked", "1");
        values.set("fpsCustom", "0");
        const current = Number(values.get("interpFps"));
        if (!LADDER.includes(current)) values.set("interpFps", "60");
        return;
    }
    const n = Number(text);
    const listed = LADDER.includes(n);
    if (!listed && (n < 60 || n > 1000)) {
        throw new Error("fps is 30, 60, 120, 144, 165, 240, unlocked, or an integer from 60 to 1000");
    }
    values.set("fps60", n === 60 ? "0" : "1");
    values.set("fpsUnlocked", "0");
    values.set("fpsCustom", listed ? "0" : "1");
    values.set("interpFps", String(n));
}

function parseAspect(raw: string): string {
    const text = raw.trim();
    const lower = text.toLowerCase();
    if (lower === "window" || lower === "match") return "window";
    if (lower === "16:9" || lower === "16:10" || lower === "21:9" || lower === "32:9") return lower;
    const colon = text.indexOf(":");
    let ratio = Number.NaN;
    if (colon !== -1) {
        const left = Number(text.slice(0, colon));
        const right = Number(text.slice(colon + 1));
        if (right > 0) ratio = left / right;
    } else {
        ratio = Number(text);
    }
    if (!(ratio > 0.5 && ratio < 8)) {
        throw new Error("aspect is 16:9, window, 16:10, 21:9, 32:9, or a ratio from 0.5 to 8");
    }
    return formatNum(ratio);
}

const RES_NAMES: Record<string, string> = {
    "360p": "0.5",
    "540p": "0.75",
    "720p": "1",
    "1080p": "1.5",
    "1440p": "2",
    "2160p": "3",
    "4k": "3",
    "2160p/4k": "3",
};

function parseResScale(raw: string): string {
    const named = RES_NAMES[raw.trim().toLowerCase()];
    if (named) return named;
    return parseFloatRange(raw, 0.5, 4, "resScale");
}

const LANGUAGE_NAMES = [
    "Japanese",
    "English",
    "French",
    "German",
    "Italian",
    "Spanish",
    "Chinese",
    "Korean",
    "Dutch",
    "Portuguese",
    "Russian",
    "Chinese (Taiwan)",
];

function parseLanguage(raw: string): string {
    const text = raw.trim().toLowerCase();
    const index = LANGUAGE_NAMES.findIndex((name) => name.toLowerCase() === text);
    if (index >= 0) return String(index);
    return parseIntRange(raw, 0, LANGUAGE_NAMES.length - 1, "language");
}

const SPECS: Spec[] = [
    {
        key: "fps",
        hint: "30, 60, 120, 144, 165, 240, unlocked, or 60-1000",
        listed: true,
        read: readFps,
        apply: applyFps,
        clear: (values) => {
            values.delete("fps");
            for (const key of FPS_KEYS) values.delete(key);
        },
    },
    direct("fps60", "0 or 1", (raw) => parseBool(raw, "fps60"), false),
    direct("fpsUnlocked", "0 or 1", (raw) => parseBool(raw, "fpsUnlocked"), false),
    direct("fpsCustom", "0 or 1", (raw) => parseBool(raw, "fpsCustom"), false),
    direct("interpFps", "1 to 1000", (raw) => parseIntRange(raw, 1, 1000, "interpFps"), false),
    boolSpec("fps60Paced", "0 or 1"),
    direct(
        "resScale",
        "0.5 to 4, or 360p, 540p, 720p, 1080p, 1440p, 2160p",
        parseResScale,
    ),
    boolSpec("aniso", "0 or 1"),
    boolSpec("fxaa", "0 or 1"),
    boolSpec("asyncShaders", "0 or 1"),
    direct(
        "scaleFilter",
        "smooth, sharp, integer",
        (raw) => parseIntRange(raw, 0, 2, "scaleFilter", { smooth: "0", sharp: "1", integer: "2" }),
    ),
    {
        key: "vkPresentMode",
        hint: "fifo, mailbox, immediate",
        listed: true,
        read: (values) => values.get("vkPresentMode"),
        apply: (values, raw) => {
            values.set(
                "vkPresentMode",
                parseIntRange(raw, 0, 2, "vkPresentMode", { fifo: "0", mailbox: "1", immediate: "2" }),
            );
            values.set("vkPresentChosen", "1");
        },
        clear: (values) => {
            values.delete("vkPresentMode");
            values.delete("vkPresentChosen");
        },
    },
    direct("vkPresentChosen", "0 or 1", (raw) => parseBool(raw, "vkPresentChosen"), false),
    direct("gpu", "dedicated, integrated, or a device id", (raw) => {
        const text = raw.trim();
        if (!text) throw new Error("gpu is dedicated, integrated, or a device id");
        const lower = text.toLowerCase();
        if (lower === "dedicated" || lower === "discrete") return "dedicated";
        if (lower === "integrated") return "integrated";
        return text;
    }),
    direct("renderer", "metal or vulkan", (raw) => parseEnum(raw, ["metal", "vulkan"], "renderer")),
    direct("aspect", "16:9, window, 16:10, 21:9, 32:9, or a ratio", parseAspect),
    boolSpec("aspectStretch", "0 or 1"),
    boolSpec("tvFullScreen", "0 or 1"),
    direct("windowWidth", "320 to 7680", (raw) => parseIntRange(raw, 320, 7680, "windowWidth")),
    direct("windowHeight", "240 to 4320", (raw) => parseIntRange(raw, 240, 4320, "windowHeight")),
    boolSpec("runInBackground", "0 or 1"),
    direct("drcMode", "window, pip, auto, off, gamepad", (raw) =>
        parseEnum(raw, ["window", "pip", "auto", "off", "gamepad"], "drcMode"),
    ),
    direct("pipCorner", "tl, tr, bl, br", (raw) => parseEnum(raw, ["tl", "tr", "bl", "br"], "pipCorner")),
    direct("pipSize", "0.1 to 0.5", (raw) => parseFloatRange(raw, 0.1, 0.5, "pipSize", true)),
    direct("pipOpacity", "0.2 to 1", (raw) => parseFloatRange(raw, 0.2, 1, "pipOpacity", true)),
    boolSpec("audioMute", "0 or 1"),
    direct("audioVolume", "0 to 1", (raw) => parseFloatRange(raw, 0, 1, "audioVolume", true)),
    direct("audioOutput", "auto, tv, gamepad", (raw) => parseEnum(raw, ["auto", "tv", "gamepad"], "audioOutput")),
    boolSpec("proController", "0 or 1"),
    boolSpec("rumble", "0 or 1"),
    direct("language", LANGUAGE_NAMES.join(", "), parseLanguage),
    boolSpec("mapMenuSettings", "0 or 1"),
    boolSpec("pauseMenuSettings", "0 or 1"),
    boolSpec("collisionViewer", "0 or 1"),
    boolSpec("hotReloadCourses", "0 or 1"),
    boolSpec("perfOverlay", "0 or 1"),
    boolSpec("perfTiming", "0 or 1"),
    boolSpec("perfExtended", "0 or 1"),
    boolSpec("perfPresentation", "0 or 1"),
    boolSpec("perfDetail", "0 or 1"),
    boolSpec("screenshotGamePad", "0 or 1"),
    direct("gyro.source", "off, controller, cemuhook, mouse", (raw) =>
        parseEnum(raw, ["off", "controller", "cemuhook", "mouse"], "gyro.source"),
    ),
    direct("gyro.axis", "player, yaw, roll", (raw) => parseEnum(raw, ["player", "yaw", "roll"], "gyro.axis")),
    direct("gyro.sensitivityX", "0.05 to 5", (raw) => parseFloatRange(raw, 0.05, 5, "gyro.sensitivityX")),
    direct("gyro.sensitivityY", "0.05 to 5", (raw) => parseFloatRange(raw, 0.05, 5, "gyro.sensitivityY")),
    boolSpec("gyro.invertX", "0 or 1"),
    boolSpec("gyro.invertY", "0 or 1"),
    direct("gyro.mouseDegrees", "0.005 to 2", (raw) => parseFloatRange(raw, 0.005, 2, "gyro.mouseDegrees")),
    direct("gyro.dsuHost", "host name", (raw) => {
        const text = raw.trim();
        if (!text || text.length > 255) throw new Error("gyro.dsuHost is a host name");
        return text;
    }),
    direct("gyro.dsuPort", "1 to 65535", (raw) => parseIntRange(raw, 1, 65535, "gyro.dsuPort")),
    direct("gyro.dsuSlot", "0 to 3", (raw) => parseIntRange(raw, 0, 3, "gyro.dsuSlot")),
    direct("gyro.recenterPad", "0 to 64", (raw) => parseIntRange(raw, 0, 64, "gyro.recenterPad")),
    direct("gyro.recenterKey", "-1 to 255", (raw) => parseIntRange(raw, -1, 255, "gyro.recenterKey")),
    boolSpec("touchControls", "0 or 1"),
    direct(
        "touchButtons",
        "automatic, always, hide",
        (raw) => parseIntRange(raw, 0, 2, "touchButtons", { automatic: "0", always: "1", hide: "2" }),
    ),
    direct(
        "phoneRumble",
        "off, touch, fallback",
        (raw) => parseIntRange(raw, 0, 2, "phoneRumble", { off: "0", touch: "1", fallback: "2" }),
    ),
    direct("heatGuard", "normal, relaxed, off", (raw) => parseEnum(raw, ["normal", "relaxed", "off"], "heatGuard")),
];

const SPEC_BY_KEY = new Map(SPECS.map((spec) => [spec.key, spec]));

export function settingKeys(): string[] {
    return SPECS.map((spec) => spec.key);
}

function findSpec(key: string): Spec | undefined {
    return SPEC_BY_KEY.get(key) ?? SPECS.find((spec) => spec.key.toLowerCase() === key.toLowerCase());
}

export function readSettingsText(text: string): Values {
    const values: Values = new Map();
    for (const line of text.split(/\r?\n/)) {
        if (!line || line.startsWith("#")) continue;
        const eq = line.indexOf("=");
        if (eq === -1) continue;
        const key = line.slice(0, eq);
        if (key === "fps") continue;
        values.set(key, line.slice(eq + 1));
    }
    return values;
}

export function writeSettingsText(values: Values): string {
    const keys = [...values.keys()].sort();
    let text = SETTINGS_HEADER;
    for (const key of keys) text += `${key}=${values.get(key)}\n`;
    return text;
}

export function readSettings(file: string): Values {
    try {
        return readSettingsText(readFileSync(file, "utf8"));
    } catch (error) {
        const code = (error as NodeJS.ErrnoException).code;
        if (code === "ENOENT") return new Map();
        throw error;
    }
}

export function writeSettings(file: string, values: Values): void {
    mkdirSync(dirname(file), { recursive: true });
    const tmp = `${file}.tmp`;
    writeFileSync(tmp, writeSettingsText(values));
    try {
        renameSync(tmp, file);
    } catch (error) {
        const code = (error as NodeJS.ErrnoException).code;
        if (code !== "EEXIST" && code !== "EPERM") throw error;
        rmSync(file, { force: true });
        renameSync(tmp, file);
    }
}

export function listSettings(values: Values): string {
    const lines: string[] = [];
    const known = new Set(SPECS.map((spec) => spec.key));
    for (const spec of SPECS) {
        if (!spec.listed) continue;
        const value = spec.read(values) ?? "-";
        const field = `${spec.key}=${value}`;
        lines.push(field.padEnd(32) + spec.hint);
    }
    const extra = [...values.keys()].filter((key) => !known.has(key) && key !== "vkPresentChosen").sort();
    for (const key of extra) lines.push(`${key}=${values.get(key)}`);
    return lines.join("\n");
}

export function getSetting(values: Values, key: string): string {
    const spec = findSpec(key);
    if (spec) {
        const value = spec.read(values);
        if (value === undefined) throw new Error(`not set: ${spec.key}`);
        return value;
    }
    if (!values.has(key)) throw new Error(`unknown setting: ${key}`);
    return values.get(key)!;
}

function rejectRaw(raw: string): void {
    if (raw.includes("\n") || raw.includes("\r")) throw new Error("setting values stay on one line");
}

export function setSetting(values: Values, key: string, raw: string): void {
    rejectRaw(raw);
    const spec = findSpec(key);
    if (spec) {
        if (spec.key !== key) values.delete(key);
        spec.apply(values, raw);
        return;
    }
    if (!/^[A-Za-z][A-Za-z0-9._]*$/.test(key)) throw new Error(`unknown setting: ${key}`);
    const value = raw.trim();
    if (!value) throw new Error(`${key} needs a value`);
    values.set(key, value);
}

export function unsetSetting(values: Values, key: string): void {
    const spec = findSpec(key);
    if (spec) {
        const present =
            spec.read(values) !== undefined ||
            values.has(spec.key) ||
            (spec.key === "vkPresentMode" && values.has("vkPresentChosen"));
        if (!present) throw new Error(`not set: ${spec.key}`);
        spec.clear(values);
        return;
    }
    if (!values.has(key)) throw new Error(`not set: ${key}`);
    values.delete(key);
}

export function settingsFile(repo: string, file: string | undefined): string {
    if (!file) return join(nsmbuUserDir(repo), "settings.ini");
    return isAbsolute(file) ? file : join(repo, file);
}

const USAGE = `usage:
  nsmbu settings [list] [--file PATH]
  nsmbu settings get KEY [--file PATH]
  nsmbu settings set KEY VALUE [--file PATH]
  nsmbu settings unset KEY [--file PATH]`;

export function settingsCommand(file: string, argv: string[]): string {
    const { positional } = parseArgv(argv, ["file"], ["file"]);
    const [sub, ...rest] = positional;
    const values = readSettings(file);
    if (!sub || sub === "list") {
        if (rest.length > 0) throw new Error(USAGE);
        return listSettings(values);
    }
    if (sub === "get") {
        if (rest.length !== 1) throw new Error(USAGE);
        return getSetting(values, rest[0]);
    }
    if (sub === "set") {
        if (rest.length < 2) throw new Error(USAGE);
        const key = rest[0];
        setSetting(values, key, rest.slice(1).join(" "));
        writeSettings(file, values);
        const spec = findSpec(key);
        return `${spec?.key ?? key}=${getSetting(values, key)}`;
    }
    if (sub === "unset") {
        if (rest.length !== 1) throw new Error(USAGE);
        unsetSetting(values, rest[0]);
        writeSettings(file, values);
        const spec = findSpec(rest[0]);
        return `unset ${spec?.key ?? rest[0]}`;
    }
    throw new Error(`unknown settings command: ${sub}\n${USAGE}`);
}

function warnIfGameRunning(repo: string): void {
    const pid = readAgentPid(repo);
    if (pid !== null && isPidAlive(pid)) {
        console.error("a running game keeps the settings it already loaded and can overwrite this file when it saves");
    }
}

export function cmdSettings(repo: string, argv: string[]): void {
    const { flags } = parseArgv(argv, ["file"], ["file"]);
    const file = settingsFile(repo, flagString(flags, "file"));
    const { positional } = parseArgv(argv, ["file"], ["file"]);
    const sub = positional[0];
    if (!sub || sub === "list") console.error(`settings file: ${file}`);
    console.log(settingsCommand(file, argv));
    if (sub === "set" || sub === "unset") warnIfGameRunning(repo);
}
