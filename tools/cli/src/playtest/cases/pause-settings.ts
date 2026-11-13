import { spawnSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { PlayCase } from "../helpers.ts";
import type { PlayStatus, Session } from "../session.ts";

const SHOTS = [
    "01-pause.png",
    "02-settings-row.png",
    "03-overlay.png",
    "03-overlay-window.png",
    "04-after-close.png",
    "05-second-open.png",
    "05-second-open-window.png",
] as const;

function ran(cmd: string, args: string[]): string {
    const r = spawnSync(cmd, args, { encoding: "utf8" });
    const out = `${r.stdout ?? ""} ${r.stderr ?? ""}`.trim().replace(/\s+/g, " ");
    return `${cmd} ${r.status ?? "err"}${out ? ` ${out}` : ""}`;
}

function captureWindow(path: string): string | null {
    const notes = [ran("xdotool", ["search", "--name", "Super Mario"])];
    const ids = (spawnSync("xdotool", ["search", "--name", "Super Mario"], { encoding: "utf8" }).stdout ?? "")
        .trim()
        .split("\n")
        .filter(Boolean);
    let best: string | null = null;
    let bestArea = 0;
    for (const id of ids) {
        const info = spawnSync("xdotool", ["getwindowgeometry", "--shell", id], { encoding: "utf8" });
        const width = Number(/WIDTH=(\d+)/.exec(info.stdout ?? "")?.[1] ?? 0);
        const height = Number(/HEIGHT=(\d+)/.exec(info.stdout ?? "")?.[1] ?? 0);
        if (width * height > bestArea) {
            bestArea = width * height;
            best = id;
        }
    }
    if (!best) return notes.join(" | ");
    const shot = spawnSync("import", ["-window", best, path], { encoding: "utf8" });
    notes.push(`import ${shot.status ?? "err"} ${(shot.stderr ?? "").trim()}`);
    if (shot.status === 0 && existsSync(path)) return null;
    return notes.join(" | ");
}

function slotReady(s: PlayStatus): boolean {
    return s.course === true && s.area === true && s.pack === "1-1" && s.player != null;
}

async function waitSlot(session: Session): Promise<void> {
    const start = Date.now();
    while (Date.now() - start < 180_000) {
        let text = "";
        if (existsSync(session.logPath)) text = readFileSync(session.logPath, "utf8");
        const s = await session.status();
        if (text.includes("Loaded slot 1") && slotReady(s)) return;
        await session.advance(20, 30);
    }
    throw new Error(`slot 1 did not load: ${session.last}`);
}

async function waitOverlay(session: Session, open: boolean, timeoutSec: number): Promise<boolean> {
    const start = Date.now();
    while (Date.now() - start < timeoutSec * 1000) {
        if ((await session.ui()).overlay === open) return true;
        await new Promise((r) => setTimeout(r, 100));
    }
    return (await session.ui()).overlay === open;
}

async function closeOverlay(session: Session): Promise<void> {
    const start = Date.now();
    let last = "click did not run";
    while (Date.now() - start < 8_000) {
        try {
            await session.clickClose();
            return;
        } catch (e) {
            last = e instanceof Error ? e.message : String(e);
            await new Promise((r) => setTimeout(r, 200));
        }
    }
    throw new Error(last);
}

function centerWhite(path: string): number {
    const probe = spawnSync(
        "python3",
        [
            "-c",
            "import sys; from PIL import Image; im=Image.open(sys.argv[1]).convert('RGB'); w,h=im.size; px=im.load(); n=0\n" +
                "x0,x1,y0,y1=int(w*0.3),int(w*0.7),int(h*0.25),int(h*0.75)\n" +
                "\nfor y in range(y0,y1,2):\n for x in range(x0,x1,2):\n  r,g,b=px[x,y]\n  n+=r>220 and g>220 and b>210\nprint(n)",
            path,
        ],
        { encoding: "utf8" },
    );
    if (probe.status !== 0) throw new Error(probe.stderr || `could not read ${path}`);
    return Number(probe.stdout.trim());
}

function upperPixel(path: string): [number, number, number] {
    const probe = spawnSync(
        "python3",
        [
            "-c",
            "import sys; from PIL import Image; im=Image.open(sys.argv[1]).convert('RGB'); w,h=im.size; r,g,b=im.load()[w//2,int(h*0.38)]; print(r,g,b)",
            path,
        ],
        { encoding: "utf8" },
    );
    if (probe.status !== 0) throw new Error(probe.stderr || `could not read ${path}`);
    const [r, g, b] = probe.stdout.trim().split(/\s+/).map(Number);
    return [r, g, b];
}

function pauseLines(session: Session): string[] {
    if (!existsSync(session.logPath)) return [];
    return readFileSync(session.logPath, "utf8")
        .split("\n")
        .filter((line) => line.includes("[pause]"));
}

export const pauseSettings: PlayCase = {
    name: "pause-settings",
    manual: true,
    summary: "Load save slot 1, open Settings, continue, then open the pause menu again. Shots stay in the case folder.",
    run: async (session) => {
        const states = join(session.repo, "nsmbu", "states");
        const slot = join(states, "slot1.bin");
        if (!existsSync(slot)) throw new Error(`missing ${slot}`);
        await session.start({
            save: "repo",
            open: "1",
            env: {
                NSMBU_STATE_DIR: states,
                NSMBU_STATE_LOAD_AT: "700:1",
                NSMBU_UNCAPPED: "0",
            },
        });
        await waitSlot(session);
        await session.advance(15, 20);
        await session.press("Plus", 12);
        await session.advance(45, 20);
        await session.screenshot(join(session.workDir, SHOTS[0]));
        // Quit is dim at the start of 1-1. One Down lands on Settings.
        // A tap is one sample. A held A enters the controller path before the fade ends.
        await session.press("Down", 1, true);
        await session.advance(25, 20);
        await session.screenshot(join(session.workDir, SHOTS[1]));
        await session.press("A", 1, true);
        const first = await waitOverlay(session, true, 12);
        await session.advance(10, 15);
        await session.screenshot(join(session.workDir, "03-overlay.png"));
        const firstWindowError = session.show ? captureWindow(join(session.workDir, "03-overlay-window.png")) : null;
        const firstWindow = session.show && firstWindowError === null;
        if (first) {
            await closeOverlay(session);
            await waitOverlay(session, false, 8);
            await session.advance(80, 20);
        }
        await session.screenshot(join(session.workDir, "04-after-close.png"));
        // One Up returns to Continue. Quit is dim, so it is not a stop.
        await session.press("Up", 1, true);
        await session.advance(25, 20);
        await session.screenshot(join(session.workDir, "05-continue-row.png"));
        await session.press("A", 1, true);
        await session.advance(8, 15);
        await session.screenshot(join(session.workDir, "06-continue-a.png"));
        if (session.show) captureWindow(join(session.workDir, "06-continue-a-window.png"));
        await session.advance(20, 15);
        await session.screenshot(join(session.workDir, "06-continue-b.png"));
        await session.advance(40, 20);
        await session.screenshot(join(session.workDir, "07-course.png"));
        await session.press("Plus", 12);
        await session.advance(45, 20);
        await session.screenshot(join(session.workDir, "08-pause-again.png"));
        const secondWindowError = session.show ? captureWindow(join(session.workDir, "08-pause-again-window.png")) : null;
        const secondWindow = session.show && secondWindowError === null;
        const second = false;
        const lines = pauseLines(session);
        writeFileSync(
            join(session.workDir, "result.txt"),
            [
                `first_overlay=${first ? 1 : 0}`,
                `second_overlay=${second ? 1 : 0}`,
                `first_window=${firstWindow ? 1 : 0}`,
                `second_window=${secondWindow ? 1 : 0}`,
                ...SHOTS.map((name) => join(session.workDir, name)),
                ...lines,
                "",
            ].join("\n"),
        );
        if (!first) throw new Error("the first Settings press did not open the overlay");
        if (session.show && !firstWindow) throw new Error(`the first overlay window shot was not saved: ${firstWindowError}`);
        if (session.show && !secondWindow) throw new Error(`the second pause window shot was not saved: ${secondWindowError}`);
        const fade = centerWhite(join(session.workDir, "06-continue-a.png"));
        const back = centerWhite(join(session.workDir, "06-continue-b.png"));
        const again = centerWhite(join(session.workDir, "08-pause-again.png"));
        const [ar, ag, ab] = upperPixel(join(session.workDir, "08-pause-again.png"));
        if (fade > 2500) throw new Error(`Continue showed the pause fade (${fade} white pixels)`);
        if (back > 2500) throw new Error(`Continue left a menu on screen (${back} white pixels)`);
        if (session.show) {
            const glitch = centerWhite(join(session.workDir, "06-continue-a-window.png"));
            if (glitch > 12000) throw new Error(`Continue showed the controller page (${glitch} white pixels)`);
        }
        if (again < 4000) throw new Error(`the next pause menu did not show (${again} white pixels)`);
        // The button list is dark blue here. The controller page is cyan, and the course sky is pale.
        if (ag > 160 || ar > 80) throw new Error(`the next pause is not the button list (${ar},${ag},${ab})`);
    },
};
