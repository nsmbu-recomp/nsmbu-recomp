import { readFileSync } from "node:fs";
import { join } from "node:path";
import { formatStatus, type PlayStatus, type Session } from "./session.ts";

export type PlayCase = {
    name: string;
    summary: string;
    run: (session: Session) => Promise<void>;
};

export type MapView = {
    lock: number;
    players: number;
    world: number;
    node: number;
    title: number;
    name: string;
    px: number;
    py: number;
    nx: number;
    ny: number;
    raw: string;
};

export function isCourseNode(name: string): boolean {
    return /^W[1-9]\d{2}$/.test(name);
}

function moved(a: MapView, b: MapView): boolean {
    if (a.node !== b.node || a.name !== b.name) return true;
    return Math.abs(a.px - b.px) > 1 || Math.abs(a.py - b.py) > 1;
}

async function mapView(session: Session): Promise<MapView | null> {
    const s = await session.status();
    if (s.map !== true) return null;
    if (
        s.world === undefined ||
        s.node === undefined ||
        s.node_name === undefined ||
        s.node_x === undefined ||
        s.node_y === undefined ||
        s.map_x === undefined ||
        s.map_y === undefined ||
        s.map_lock === undefined ||
        s.map_title === undefined ||
        s.map_players === undefined
    ) {
        return null;
    }
    return {
        lock: s.map_lock,
        players: s.map_players,
        world: s.world,
        node: s.node,
        title: s.map_title,
        name: s.node_name,
        px: s.map_x,
        py: s.map_y,
        nx: s.node_x,
        ny: s.node_y,
        raw: formatStatus(s),
    };
}

export async function waitMap(session: Session, timeoutSec: number, needInput: boolean): Promise<MapView> {
    const start = Date.now();
    let last = "";
    for (;;) {
        let view: MapView | null = null;
        try {
            view = await mapView(session);
        } catch (e) {
            last = e instanceof Error ? e.message : String(e);
        }
        if (!view && last === "") await session.status();
        if (view) last = view.raw;
        if (view && view.players >= 1 && view.title >= 0 && view.name !== "" && (!needInput || view.lock === 0)) {
            return view;
        }
        if (Date.now() - start >= timeoutSec * 1000) {
            throw new Error(`map did not become ready: ${last} ${session.last}`);
        }
        await session.advance(20, 15);
    }
}

async function hold(session: Session, frames: number, run: () => Promise<void>): Promise<void> {
    const now = await session.status();
    const mark = now.step ?? 0;
    await run();
    await session.wait((s) => (s.step ?? 0) >= mark + frames, 30);
}

async function walk(session: Session, x: number, y: number): Promise<MapView | null> {
    const before = await mapView(session);
    await hold(session, 180, () => session.stick(x, y, 180));
    const after = await mapView(session);
    if (!before || !after || !moved(before, after)) return null;
    await hold(session, 240, () => session.stick(x, y, 240));
    return (await mapView(session)) ?? after;
}

export async function waitUntilMoves(session: Session, timeoutSec: number): Promise<MapView> {
    const start = Date.now();
    let last = "";
    while (Date.now() - start < timeoutSec * 1000) {
        const before = await mapView(session);
        if (!before || before.lock !== 0 || before.players < 1) {
            await hold(session, 20, () => session.press("A", 12));
            continue;
        }
        last = before.raw;
        await session.advance(60, 15);
        const settled = await mapView(session);
        if (!settled || moved(before, settled)) continue;
        await hold(session, 20, () => session.press("A", 12));
        for (const [x, y] of [
            [1, 0],
            [0, 1],
            [-1, 0],
            [0, -1],
        ] as const) {
            const stepped = await walk(session, x, y);
            if (!stepped) continue;
            last = stepped.raw;
            if (isCourseNode(stepped.name) && stepped.title >= 0 && stepped.players >= 1) return stepped;
        }
    }
    throw new Error(`map did not take control on a course node: ${last} ${session.last}`);
}

export function coursePlaying(s: PlayStatus, pack?: string): boolean {
    return (
        s.area === true &&
        s.course === true &&
        s.map !== true &&
        (pack === undefined ? !!s.pack : s.pack === pack) &&
        s.player != null &&
        Math.abs(s.player.x) + Math.abs(s.player.y) > 10
    );
}

export async function waitCourse(session: Session, pack: string, timeoutSec: number): Promise<void> {
    await session.wait((s) => coursePlaying(s, pack), timeoutSec);
}

export async function reachMap(session: Session, timeoutSec: number): Promise<void> {
    await session.wait((s) => s.scene !== undefined && s.scene !== "Boot", 180);
    const start = Date.now();
    let last = "";
    while (Date.now() - start < timeoutSec * 1000) {
        const s = await session.status();
        if (s.map === true && (s.scene === "Map" || s.scene === "CourseSelect")) return;
        last = await session.eval("return nsmbu.Scene.open_first_save(0)");
        if (!last.startsWith("true")) {
            await session.eval("return nsmbu.Scene.open_title()");
            await session.press("A", 15);
        }
        await session.advance(40, 20);
    }
    throw new Error(`opening never reached the map: ${last} ${session.last}`);
}

export function onMap(s: PlayStatus): boolean {
    return s.map === true;
}

export function onWorldMap(s: PlayStatus): boolean {
    return s.map === true && (s.scene === "Map" || s.scene === "CourseSelect");
}

export function inCourse(s: PlayStatus): boolean {
    return coursePlaying(s);
}

export async function reachWorldMap(session: Session, timeoutSec: number): Promise<PlayStatus> {
    const start = Date.now();
    while (Date.now() - start < timeoutSec * 1000) {
        const s = await session.status();
        if (onMap(s)) return s;
        await session.press("A", 20);
        await session.advance(50, 20);
    }
    throw new Error(`never reached the world map: ${session.last}`);
}

export async function advanceUntilFrame(session: Session, frame: number, timeoutSec: number): Promise<PlayStatus> {
    const start = Date.now();
    while (Date.now() - start < timeoutSec * 1000) {
        const s = await session.status();
        if ((s.frame ?? 0) >= frame) return s;
        await session.advance(30, 20);
    }
    throw new Error(`frame ${frame} not reached: ${session.last}`);
}

export function assertPortablePlace(session: Session, states: string, slot: number, place: "map" | "course"): void {
    const path = join(states, `slot${slot}.nsstate`);
    let text = "";
    try {
        text = readFileSync(path, "utf8");
    } catch {
        text = "";
    }
    if (!text.includes(`place = ${place}\n`)) {
        const why = playtestLog(session)
            .split("\n")
            .filter((line) => line.includes("[savestate]") && (line.includes(`slot ${slot}:`) || line.includes(`Slot ${slot}:`)))
            .slice(-3)
            .join(" | ");
        throw new Error(`slot ${slot} is not a ${place} state (${why || "no savestate log"})`);
    }
}

export async function enterCourse(session: Session): Promise<string> {
    let view: MapView | null = null;
    try {
        view = await mapView(session);
    } catch {
        view = null;
    }
    if (!view || view.lock !== 0 || view.players < 1 || !isCourseNode(view.name)) {
        await waitUntilMoves(session, 90);
    } else {
        await session.advance(90, 20);
    }
    const start = Date.now();
    while (Date.now() - start < 90_000) {
        const s = await session.status();
        if (inCourse(s)) return s.pack ?? "";
        if (!s.entering) await session.press("A", 25);
        await session.advance(60, 20);
    }
    throw new Error(`course did not start: ${session.last}`);
}

export async function leaveCourse(session: Session): Promise<void> {
    await session.advance(90, 30);
    for (let i = 0; i < 10; i++) {
        if (onWorldMap(await session.status())) return;
        await session.stick(1, 0, 40);
        await session.advance(40, 15);
    }
    const start = Date.now();
    while (Date.now() - start < 90_000) {
        if (onWorldMap(await session.status())) return;
        await session.press("B", 2);
        await session.advance(20, 15);
        await session.press("Plus", 8);
        await session.advance(55, 15);
        if (onWorldMap(await session.status())) return;
        for (let i = 0; i < 3; i++) {
            await session.press("Up", 1, true);
            await session.advance(20, 15);
        }
        await session.press("Down", 1, true);
        await session.advance(35, 15);
        await session.press("A", 4);
        await session.advance(45, 15);
        if (onWorldMap(await session.status())) return;
        await session.press("Right", 1, true);
        await session.advance(30, 15);
        await session.press("A", 4);
        const until = Date.now() + 10_000;
        while (Date.now() < until) {
            if (onWorldMap(await session.status())) return;
            await session.advance(30, 15);
        }
    }
    throw new Error(`did not return to the map: ${session.last}`);
}

export async function walkToOtherNode(session: Session, from: number | undefined): Promise<PlayStatus> {
    const start = Date.now();
    while (Date.now() - start < 60_000) {
        let view: MapView | null = null;
        try {
            view = await mapView(session);
        } catch {
            view = null;
        }
        if (!view || view.players < 1) {
            await session.advance(30, 15);
            continue;
        }
        if (view.lock !== 0) {
            if (!isCourseNode(view.name)) await session.press("A", 2);
            else await session.advance(30, 15);
            continue;
        }
        for (const [x, y] of [
            [1, 0],
            [-1, 0],
            [0, 1],
            [0, -1],
        ] as const) {
            await hold(session, 180, () => session.stick(x, y, 180));
            const s = await session.status();
            if (onMap(s) && s.node !== undefined && s.node !== from) return s;
        }
    }
    throw new Error(`map node did not change: ${session.last}`);
}

export function hostQuickNode(saveDir: string, file = 0): { exists: number; node: number } | null {
    let buf: Buffer;
    try {
        buf = readFileSync(join(saveDir, "common", "rp_savedata.dat"));
    } catch {
        return null;
    }
    const off = 0xc28 + file * 0x204;
    if (buf.length < off + 7) return null;
    return { exists: buf[off], node: buf[off + 6] };
}

export type HostRecord = {
    exists: number;
    lives: number;
    complete: number;
    flags: number;
    world: number;
    section: number;
    node: number;
};

export function hostRecord(saveDir: string, base: number, file = 0): HostRecord | null {
    let buf: Buffer;
    try {
        buf = readFileSync(join(saveDir, "common", "rp_savedata.dat"));
    } catch {
        return null;
    }
    const off = base + file * 0x204;
    if (buf.length < off + 0x6b + 123) return null;
    let flags = 0;
    for (let i = 0; i < 123; i++) if (buf[off + 0x6b + i]) flags++;
    return {
        exists: buf[off],
        lives: buf[off + 0x0c],
        complete: buf[off + 0x1e],
        flags,
        world: buf[off + 4],
        section: buf[off + 5],
        node: buf[off + 6],
    };
}

type SlotLog = HostRecord & { uses: "quick" | "main" };

function playtestLog(session: Session): string {
    try {
        return readFileSync(session.logPath, "utf8");
    } catch {
        return "";
    }
}

function lastFileSelectSlot(session: Session, file: number): SlotLog | null {
    const re =
        /\[save\] file-select slot file (\d+) uses (quick|main) exists=(\d+) lives=(\d+) complete=(\d+) flags=(-?\d+) map=(\d+)\.(\d+)\.(\d+)/g;
    let found: SlotLog | null = null;
    for (const m of playtestLog(session).matchAll(re)) {
        if (Number(m[1]) !== file) continue;
        found = {
            uses: m[2] === "quick" ? "quick" : "main",
            exists: Number(m[3]),
            lives: Number(m[4]),
            complete: Number(m[5]),
            flags: Number(m[6]),
            world: Number(m[7]),
            section: Number(m[8]),
            node: Number(m[9]),
        };
    }
    return found;
}

function lastFileSelectComplete(session: Session, file: number): number | null {
    const re = /\[save\] file-select complete file (\d+) byte=(\d+)/g;
    let byte: number | null = null;
    for (const m of playtestLog(session).matchAll(re)) {
        if (Number(m[1]) === file) byte = Number(m[2]);
    }
    return byte;
}

export function sameProgress(a: HostRecord, b: HostRecord): boolean {
    return a.lives === b.lives && a.flags === b.flags && a.world === b.world && a.section === b.section && a.node === b.node;
}

export function assertFileSelectQuick(session: Session, saved: HostRecord, file = 0): void {
    const logged = lastFileSelectSlot(session, file);
    if (!logged) throw new Error("file-select slot did not log which record it used");
    if (logged.uses !== "quick") throw new Error(`file-select slot file ${file} uses ${logged.uses}, want quick`);
    if (!sameProgress(logged, saved) || logged.exists !== saved.exists || logged.complete !== saved.complete) {
        throw new Error(
            `file-select slot file ${file} logged exists=${logged.exists} lives=${logged.lives} complete=${logged.complete} flags=${logged.flags} map=${logged.world}.${logged.section}.${logged.node}, quick save exists=${saved.exists} lives=${saved.lives} complete=${saved.complete} flags=${saved.flags} map=${saved.world}.${saved.section}.${saved.node}`,
        );
    }
    const byte = lastFileSelectComplete(session, file);
    if (byte === null) throw new Error("file-select slot did not log the completion byte");
    if (byte !== saved.complete) {
        throw new Error(`file-select completion byte is ${byte}, quick save complete=${saved.complete}`);
    }
    const main = hostRecord(session.saveDir, 0x10, file);
    if (!main || main.exists === 0 || !sameProgress(main, saved)) {
        throw new Error(
            `story record after continue is exists=${main?.exists} lives=${main?.lives} flags=${main?.flags} map=${main?.world}.${main?.section}.${main?.node}, quick save lives=${saved.lives} flags=${saved.flags} map=${saved.world}.${saved.section}.${saved.node}`,
        );
    }
}

export async function quickSaveHere(session: Session): Promise<number> {
    await session.advance(120, 30);
    const here = await session.status();
    if (!onMap(here) || here.node === undefined) throw new Error(`not on the map: ${session.last}`);
    const node = here.node;
    await session.press("Plus", 20);
    await session.advance(45, 15);
    await session.press("Down", 15);
    await session.advance(30, 15);
    await session.press("A", 20);
    await session.advance(60, 15);
    await session.press("Right", 15);
    await session.advance(30, 15);
    await session.press("A", 20);
    const start = Date.now();
    let host = hostQuickNode(session.saveDir);
    while (Date.now() - start < 30_000) {
        host = hostQuickNode(session.saveDir);
        if (host && host.exists !== 0 && host.node === node) return node;
        await session.advance(20, 15);
    }
    throw new Error(`quick save did not store node ${node} (host exists=${host?.exists} node=${host?.node}) ${session.last}`);
}

export async function loadPack(session: Session, pack: string): Promise<void> {
    const start = Date.now();
    let last = "";
    for (;;) {
        last = await session.eval(`return nsmbu.Scene.load_pack("${pack}")`);
        if (last.startsWith("true")) return;
        if (Date.now() - start >= 90_000) throw new Error(`load_pack ${pack} failed: ${last} ${session.last}`);
        await session.advance(30, 20);
    }
}

export async function courseEnterRoundtrip(session: Session, fps: string): Promise<void> {
    await session.start({
        save: "repo",
        env: { NSMBU_INTERP: "1", NSMBU_INTERP_FPS: fps, NSMBU_UNCAPPED: "0" },
    });
    await reachWorldMap(session, 180);
    const first = await enterCourse(session);
    if (!first) throw new Error(`first course pack missing: ${session.last}`);
    await leaveCourse(session);
    const onMapAfter = await session.status();
    await walkToOtherNode(session, onMapAfter.node);
    const second = await enterCourse(session);
    if (!second) throw new Error(`second course pack missing: ${session.last}`);
}
