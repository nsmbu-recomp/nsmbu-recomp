import { spawnSync } from "node:child_process";
import { mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { isAbsolute, join } from "node:path";
import { parseButtonBits } from "./buttons.ts";
import { AgentClient, type AgentResponse } from "./client.ts";
import { agentDir, agentPidPath, agentSocketPath } from "./paths.ts";
import { isPidAlive, readAgentPid, removeAgentArtifacts } from "./process.ts";
import { arrived, cursorSelected, decodePng, delta, fnv, newerThan, placeKind, sample, tvRect, type Rect } from "./probe.ts";
import { evalBody } from "./eval.ts";
import { canConnectSocket, waitForSocket } from "./socket.ts";
import { spawnPython } from "../run.ts";
import { flagNumber, flagString, hasFlag, parseArgv } from "../parse.ts";

async function ensureNotRunning(repo: string): Promise<void> {
    const sock = agentSocketPath(repo);
    if (await canConnectSocket(sock)) {
        throw new Error("agent game already running (control socket is active)");
    }
    const pid = readAgentPid(repo);
    if (pid !== null && isPidAlive(pid)) {
        throw new Error(`agent game already running (pid ${pid})`);
    }
    removeAgentArtifacts(repo);
}

export async function cmdOpen(repo: string, rest: string[]): Promise<void> {
    await ensureNotRunning(repo);
    mkdirSync(agentDir(repo), { recursive: true });
    const sock = agentSocketPath(repo);
    const child = spawnPython(repo, "tools/run/launch.py", rest, {
        detached: true,
        stdio: "ignore",
        extraEnv: { NSMBU_AGENT_SOCK: sock },
    });
    child.unref();
    if (child.pid === undefined) {
        throw new Error("failed to spawn game");
    }
    writeFileSync(agentPidPath(repo), String(child.pid));
    await waitForSocket(sock, 60_000);
    console.log(`ready (pid ${child.pid}, socket ${sock})`);
}

function formatStatus(r: Awaited<ReturnType<AgentClient["status"]>>): string {
    const parts = [`frame=${r.frame ?? "?"}`, `step=${r.step ?? "?"}`];
    if (r.pid !== undefined) {
        parts.push(`pid=${r.pid}`);
    }
    if (r.scene) {
        parts.push(`scene=${r.scene}`);
    }
    if (r.map !== undefined) {
        parts.push(`map=${r.map ? 1 : 0}`);
    }
    if (r.course !== undefined) {
        parts.push(`course=${r.course ? 1 : 0}`);
    }
    if (r.pack) {
        parts.push(`pack=${r.pack}`);
    }
    if (r.entering) {
        parts.push(`entering=${r.entering}`);
    }
    if (r.file !== undefined) {
        parts.push(`file=${r.file}`);
    }
    if (r.cp !== undefined) {
        parts.push(`cp=${r.cp}`);
    }
    if (r.node !== undefined) {
        parts.push(`node=${r.node}`);
    }
    if (r.world !== undefined) {
        parts.push(`world=${r.world}`);
    }
    if (r.area !== undefined) {
        parts.push(`area=${r.area ? 1 : 0}`);
    }
    if (r.node_name) {
        parts.push(`name=${r.node_name}`);
    }
    if (r.node_x !== undefined && r.node_y !== undefined) {
        parts.push(`node_pos=(${r.node_x}, ${r.node_y})`);
    }
    if (r.map_x !== undefined && r.map_y !== undefined) {
        parts.push(`map_player=(${r.map_x}, ${r.map_y})`);
    }
    if (r.player) {
        parts.push(`player=(${r.player.x}, ${r.player.y}, ${r.player.z})`);
    } else {
        parts.push("player=none");
    }
    return parts.join(" ");
}

function printStatus(r: Awaited<ReturnType<AgentClient["status"]>>): void {
    console.log(formatStatus(r));
}

export async function cmdStatus(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        printStatus(await client.status());
    } finally {
        client.close();
    }
}

export async function cmdLeave(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "leave" });
        if (!r.ok) {
            throw new Error(r.error ?? "leave failed");
        }
    } finally {
        client.close();
    }
}

export async function cmdPress(repo: string, argv: string[]): Promise<void> {
    const { flags, positional } = parseArgv(argv, ["frames"]);
    if (positional.length < 1) {
        throw new Error("usage: nsmbu press <button|hex> [--frames N]");
    }
    const bits = parseButtonBits(positional[0]);
    const frames = flagNumber(flags, "frames", 20);
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "press", bits, frames });
        if (!r.ok) {
            throw new Error(r.error ?? "press failed");
        }
    } finally {
        client.close();
    }
}

export async function cmdStick(repo: string, argv: string[], right: boolean): Promise<void> {
    const { flags, positional } = parseArgv(argv, ["frames"]);
    if (positional.length < 2) {
        throw new Error(`usage: nsmbu ${right ? "rstick" : "stick"} <x> <y> [--frames N]`);
    }
    const x = Number(positional[0]);
    const y = Number(positional[1]);
    const frames = flagNumber(flags, "frames", 20);
    const client = new AgentClient(repo);
    try {
        const r = await client.request({
            cmd: right ? "rstick" : "stick",
            x,
            y,
            frames,
        });
        if (!r.ok) {
            throw new Error(r.error ?? "stick failed");
        }
    } finally {
        client.close();
    }
}

export async function cmdScreenshot(repo: string, argv: string[]): Promise<void> {
    const { positional } = parseArgv(argv, []);
    const pathArg = positional[0];
    const client = new AgentClient(repo);
    try {
        const path =
            pathArg === undefined
                ? join(agentDir(repo), `shot-${Date.now()}.png`)
                : isAbsolute(pathArg)
                  ? pathArg
                  : join(repo, pathArg);
        const body: Record<string, unknown> = { cmd: "screenshot", path };
        const r = await client.request(body);
        if (!r.ok) {
            throw new Error(r.error ?? "screenshot failed");
        }
        if (r.path) {
            console.log(r.path);
        }
    } finally {
        client.close();
    }
}

export async function cmdWait(repo: string, argv: string[]): Promise<void> {
    const { flags } = parseArgv(argv, ["frame", "step", "scene", "cursor", "timeout"]);
    const frameTarget = hasFlag(flags, "frame") ? flagNumber(flags, "frame", 0) : null;
    const stepTarget = hasFlag(flags, "step") ? flagNumber(flags, "step", 0) : null;
    const sceneTarget = flagString(flags, "scene");
    const cursorTarget = flagString(flags, "cursor");
    if ((hasFlag(flags, "scene") && sceneTarget === undefined) || (hasFlag(flags, "cursor") && cursorTarget === undefined)) {
        throw new Error("usage: nsmbu wait --frame N | --step N | --scene NAME | --cursor NAME [--timeout SEC]");
    }
    if (frameTarget === null && stepTarget === null && sceneTarget === undefined && cursorTarget === undefined) {
        throw new Error("usage: nsmbu wait --frame N | --step N | --scene NAME | --cursor NAME [--timeout SEC]");
    }
    const timeoutMs = flagNumber(flags, "timeout", 120) * 1000;
    const client = new AgentClient(repo);
    const start = Date.now();
    let last = "";
    try {
        for (;;) {
            const r = await client.status();
            last = formatStatus(r);
            const frameOk = frameTarget === null || (r.frame ?? 0) >= frameTarget;
            const stepOk = stepTarget === null || (r.step ?? 0) >= stepTarget;
            const sceneOk = sceneTarget === undefined || r.scene === sceneTarget;
            let cursorOk = cursorTarget === undefined;
            if (cursorTarget !== undefined) {
                const ui = await client.request({ cmd: "ui" });
                if (!ui.ok) throw new Error(ui.error ?? "ui failed");
                cursorOk = cursorSelected(ui.buttons, cursorTarget);
                if (cursorOk) last = `${last} cursor=${cursorTarget}`;
            }
            if (frameOk && stepOk && sceneOk && cursorOk) {
                console.log(last);
                return;
            }
            if (Date.now() - start >= timeoutMs) {
                const detail = last ? `: ${last}` : "";
                throw new Error(`wait timed out after ${timeoutMs / 1000}s${detail}`);
            }
            await new Promise((r) => setTimeout(r, 100));
        }
    } finally {
        client.close();
    }
}

export async function cmdQuit(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "quit" });
        if (!r.ok) {
            throw new Error(r.error ?? "quit failed");
        }
    } catch (e) {
        if (!(await canConnectSocket(agentSocketPath(repo), 200))) {
            removeAgentArtifacts(repo);
            return;
        }
        throw e;
    } finally {
        client.close();
    }
    await new Promise((r) => setTimeout(r, 300));
    const pid = readAgentPid(repo);
    if (pid !== null && !isPidAlive(pid)) {
        removeAgentArtifacts(repo);
    }
    if (!(await canConnectSocket(agentSocketPath(repo), 200))) {
        removeAgentArtifacts(repo);
    }
}

function num(n: number): string {
    return Number.isInteger(n) ? String(n) : n.toFixed(1);
}

function printUi(r: AgentResponse): void {
    const parts = [`overlay=${r.overlay ? 1 : 0}`];
    if (r.tab) parts.push(`tab=${r.tab}`);
    parts.push(`menu=${r.menu ? 1 : 0}`);
    if (r.cursor !== undefined) parts.push(`cursor=${r.cursor}`);
    console.log(parts.join(" "));
    for (const b of r.buttons ?? []) {
        const rect =
            b.x0 !== undefined && b.y0 !== undefined && b.x1 !== undefined && b.y1 !== undefined
                ? ` rect=${b.x0.toFixed(0)},${b.y0.toFixed(0)} ${b.x1.toFixed(0)},${b.y1.toFixed(0)}`
                : "";
        console.log(`${b.index} name=${b.name} selected=${b.selected ? 1 : 0}${rect}`);
    }
}

export async function cmdUi(repo: string): Promise<void> {
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "ui" });
        if (!r.ok) throw new Error(r.error ?? "ui failed");
        printUi(r);
    } finally {
        client.close();
    }
}

export async function cmdActors(repo: string, argv: string[]): Promise<void> {
    const { flags } = parseArgv(argv, ["limit", "radius"]);
    const body: Record<string, unknown> = { cmd: "actors" };
    if (hasFlag(flags, "limit")) body.limit = flagNumber(flags, "limit", 16);
    if (hasFlag(flags, "radius")) body.radius = flagNumber(flags, "radius", 0);
    const client = new AgentClient(repo);
    try {
        const r = await client.request(body);
        if (!r.ok) throw new Error(r.error ?? "actors failed");
        if (r.power_name) console.log(`power=${r.power_name}`);
        else if (r.power !== undefined) console.log(`power=${r.power}`);
        const actors = r.actors ?? [];
        if (actors.length === 0) console.log("actors=0");
        for (const a of actors) {
            console.log(
                `profile=${a.profile} pos=(${num(a.x)}, ${num(a.y)}, ${num(a.z)}) vel=(${num(a.vx)}, ${num(a.vy)}, ${num(a.vz)})`,
            );
        }
    } finally {
        client.close();
    }
}

export async function cmdEval(repo: string, argv: string[]): Promise<void> {
    const body = evalBody(argv);
    const client = new AgentClient(repo);
    try {
        const r = await client.request(body);
        if (!r.ok) throw new Error(r.error ?? "eval failed");
        if (body.list) console.log(JSON.stringify({ evals: r.evals ?? [] }));
        else console.log(JSON.stringify(r.value));
    } finally {
        client.close();
    }
}

export async function cmdLog(repo: string, argv: string[]): Promise<void> {
    const { flags } = parseArgv(argv, ["tag", "lines"]);
    const body: Record<string, unknown> = { cmd: "log" };
    const tag = flagString(flags, "tag");
    if (tag) body.tag = tag;
    const client = new AgentClient(repo);
    try {
        const r = await client.request(body);
        if (!r.ok) throw new Error(r.error ?? "log failed");
        const lines = r.lines ?? [];
        const n = hasFlag(flags, "lines") ? flagNumber(flags, "lines", 40) : 40;
        for (const line of lines.slice(-n)) console.log(line);
    } finally {
        client.close();
    }
}

function parseRect(text: string | undefined): Rect | null {
    if (text === undefined) return null;
    const parts = text.split(",").map((p) => Number(p));
    if (parts.length !== 4 || parts.some((n) => !Number.isFinite(n))) {
        throw new Error("usage: nsmbu picture [--frames N] [--rect x,y,w,h]");
    }
    return { x: parts[0], y: parts[1], w: parts[2], h: parts[3] };
}

export async function cmdPicture(repo: string, argv: string[]): Promise<void> {
    const { flags } = parseArgv(argv, ["frames", "rect"]);
    const frames = flagNumber(flags, "frames", 3);
    if (frames < 2 || frames > 8) throw new Error("picture frames must be from 2 to 8");
    const rect = parseRect(flagString(flags, "rect"));
    const stamp = Date.now();
    const paths = [];
    const ahead = [];
    for (let i = 0; i < frames; i++) {
        paths.push(join(agentDir(repo), `picture-${stamp}-${i}.png`));
        ahead.push(2 + i);
    }
    const client = new AgentClient(repo);
    try {
        const r = await client.request({ cmd: "picture", paths, ahead });
        if (!r.ok) throw new Error(r.error ?? "picture failed");
        const files = r.paths ?? paths;
        let prev: Uint8Array | null = null;
        for (let i = 0; i < files.length; i++) {
            const img = decodePng(readFileSync(files[i]));
            const bytes = sample(img, tvRect(rect, img.w, img.h));
            const hash = fnv(bytes);
            if (!prev) {
                console.log(`${i} ${img.w}x${img.h} hash=${hash}`);
            } else {
                const d = delta(prev, bytes);
                console.log(`${i} hash=${hash} mean=${d.mean.toFixed(2)} max=${d.max}`);
            }
            prev = bytes;
        }
    } finally {
        client.close();
    }
}

async function sleep(ms: number): Promise<void> {
    await new Promise((r) => setTimeout(r, ms));
}

export async function cmdGoto(repo: string, argv: string[]): Promise<void> {
    const { flags, positional } = parseArgv(argv, ["timeout"]);
    const place = positional[0];
    if (!place || placeKind(place) === null) {
        throw new Error("usage: nsmbu goto <title|file|map|W-C> [--timeout SEC]");
    }
    const timeoutMs = flagNumber(flags, "timeout", 120) * 1000;
    const client = new AgentClient(repo);
    const start = Date.now();
    let asked = false;
    let last = "";
    try {
        for (;;) {
            const s = await client.status();
            last = formatStatus(s);
            if (arrived(place, s)) {
                console.log(last);
                return;
            }
            const scene = s.scene;
            const ready = scene !== "Boot" && scene !== "Flow" && (scene !== undefined || (s.frame ?? 0) > 120);
            if (!asked && ready) {
                const r = await client.request({ cmd: "goto", place });
                if (r.ok) asked = true;
                else last = r.error ?? last;
            }
            if (Date.now() - start >= timeoutMs) {
                throw new Error(`goto timed out after ${timeoutMs / 1000}s: ${last}`);
            }
            await sleep(200);
        }
    } finally {
        client.close();
    }
}

function sourceTimes(repo: string): number[] {
    const times: number[] = [];
    const take = (path: string) => {
        let st;
        try {
            st = statSync(path);
        } catch {
            return;
        }
        if (st.isDirectory()) {
            for (const name of readdirSync(path)) take(join(path, name));
            return;
        }
        if (/\.(cpp|h|mm|c)$/.test(path)) times.push(st.mtimeMs);
    };
    take(join(repo, "runtime/src"));
    take(join(repo, "runtime/include"));
    try {
        times.push(statSync(join(repo, "CMakeLists.txt")).mtimeMs);
    } catch {
        /* missing */
    }
    return times;
}

export async function cmdRestart(repo: string): Promise<void> {
    let place = "";
    const look = new AgentClient(repo);
    try {
        const s = await look.status();
        if (s.course && s.pack) place = s.pack;
        else if (s.map) place = "map";
        else if (s.scene === "Title") place = "title";
        else if (s.scene === "FileSelect") place = "file";
    } catch {
        place = "";
    } finally {
        look.close();
    }
    try {
        await cmdQuit(repo);
    } catch {
        /* already stopped */
    }
    const deadline = Date.now() + 5000;
    while ((await canConnectSocket(agentSocketPath(repo), 200)) && Date.now() < deadline) {
        await sleep(100);
    }
    const bin = join(repo, "build/debug/nsmbu");
    let stale = true;
    try {
        stale = newerThan(statSync(bin).mtimeMs, sourceTimes(repo));
    } catch {
        stale = true;
    }
    if (stale) {
        const built = spawnSync("cmake", ["--build", join(repo, "build/debug"), "--target", "nsmbu", "-j"], {
            cwd: repo,
            stdio: "inherit",
        });
        if ((built.status ?? 1) !== 0) throw new Error("rebuild failed");
        console.log("rebuilt");
    } else {
        console.log("binary is current");
    }
    const course = placeKind(place) === "course";
    await cmdOpen(repo, course ? [place] : []);
    if (place && !course) await cmdGoto(repo, [place, "--timeout", "180"]);
}
