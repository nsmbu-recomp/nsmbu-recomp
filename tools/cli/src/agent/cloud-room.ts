import { existsSync, mkdirSync, readFileSync } from "node:fs";
import { dirname, isAbsolute, join } from "node:path";
import { cmdOpen } from "./commands.ts";
import { AgentClient, type AgentResponse } from "./client.ts";
import { decodePng } from "./probe.ts";
import { nsmbuUserDir } from "../repo.ts";

export type PlayerPos = { x: number; y: number; z: number };

const JUMP_BITS = 0x8200;
const BRIDGE_TIMEOUT_MS = 90_000;
const ROOM_TIMEOUT_MS = 25_000;

export function onPipeBridge(player: PlayerPos): boolean {
    return player.x > 6000 && player.x < 6400 && player.y > -700 && player.y < -400 && player.z > 2000;
}

export function inCloudRoom(player: PlayerPos): boolean {
    return player.x > 300 && player.x < 700 && player.y > -750 && player.y < -500 && player.z > 2000;
}

export function frameHasPicture(rgba: Uint8Array, width: number, height: number): boolean {
    const x0 = Math.floor(width * 0.75);
    let dark = 0;
    let seen = 0;
    for (let y = 0; y < height; y += 8) {
        for (let x = x0; x < width; x += 8) {
            const i = (y * width + x) * 4;
            const r = rgba[i] ?? 0;
            const g = rgba[i + 1] ?? 0;
            const b = rgba[i + 2] ?? 0;
            seen++;
            if (r + g + b < 30) dark++;
        }
    }
    return seen > 0 && dark / seen < 0.2;
}

function playerOf(status: AgentResponse): PlayerPos | null {
    const player = status.player;
    if (!player) return null;
    return player;
}

function placeLine(status: AgentResponse): string {
    const player = playerOf(status);
    if (!player) return "player=none";
    return `player=(${player.x}, ${player.y}, ${player.z})`;
}

async function sleep(ms: number): Promise<void> {
    await new Promise((resolve) => setTimeout(resolve, ms));
}

async function pollUntil(client: AgentClient, timeoutMs: number, ready: (player: PlayerPos) => boolean): Promise<AgentResponse> {
    const start = Date.now();
    let last = "player=none";
    for (;;) {
        const status = await client.status();
        last = placeLine(status);
        const player = playerOf(status);
        if (player && ready(player)) return status;
        if (Date.now() - start >= timeoutMs) {
            throw new Error(`timed out after ${timeoutMs / 1000}s: ${last}`);
        }
        await sleep(400);
    }
}

export async function cmdCloudRoom(repo: string, argv: string[]): Promise<void> {
    const pathArg = argv[0];
    if (argv.length > 1) {
        throw new Error("usage: nsmbu cloud-room [path]");
    }
    const state = join(nsmbuUserDir(repo), "states", "slot1.nsstate");
    if (!existsSync(state)) {
        throw new Error(`missing portable save ${state}`);
    }
    const shot = pathArg === undefined ? join(repo, ".tmp", "cloud-room.png") : isAbsolute(pathArg) ? pathArg : join(repo, pathArg);
    mkdirSync(dirname(shot), { recursive: true });

    await cmdOpen(repo, ["1-1"], { NSMBU_PORTABLE_LOAD: state });
    const client = new AgentClient(repo);
    try {
        const bridge = await pollUntil(client, BRIDGE_TIMEOUT_MS, onPipeBridge);
        console.log(`bridge ${placeLine(bridge)}`);
        // The portable load holds the saved spot for a short time after it first appears.
        // A jump during that hold is thrown away.
        await sleep(2500);
        const settled = await client.status();
        const settledPlayer = playerOf(settled);
        if (!settledPlayer || !onPipeBridge(settledPlayer)) {
            throw new Error(`left the pipe before the jump: ${placeLine(settled)}`);
        }
        const pressed = await client.request({ cmd: "press", bits: JUMP_BITS, frames: 20 });
        if (!pressed.ok) throw new Error(pressed.error ?? "jump failed");
        const aimed = await client.request({ cmd: "stick", x: 0, y: 1, frames: 45 });
        if (!aimed.ok) throw new Error(aimed.error ?? "aim failed");
        const room = await pollUntil(client, ROOM_TIMEOUT_MS, inCloudRoom);
        console.log(`room ${placeLine(room)}`);
        const shown = Date.now();
        for (;;) {
            const picture = await client.request({ cmd: "screenshot", path: shot });
            if (!picture.ok) throw new Error(picture.error ?? "screenshot failed");
            const image = decodePng(readFileSync(shot));
            if (frameHasPicture(image.rgba, image.w, image.h)) break;
            if (Date.now() - shown >= 15_000) throw new Error("the cloud room stayed black");
            await sleep(500);
        }
        console.log(shot);
    } finally {
        client.close();
    }
}
