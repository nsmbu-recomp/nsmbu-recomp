import { join } from "node:path";
import { advanceUntilFrame, type PlayCase } from "../helpers.ts";
import { readPng } from "./widescreen-map-ui.ts";

function marioFromCenter(img: { w: number; h: number; rgb: Uint8Array }): number | null {
    const y0 = Math.floor(img.h * 0.55);
    const y1 = Math.floor(img.h * 0.8);
    const x0 = Math.floor(img.w * 0.3);
    const x1 = Math.floor(img.w * 0.8);
    const xs: number[] = [];
    for (let y = y0; y < y1; y += 2) {
        const row = y * img.w * 3;
        for (let x = x0; x < x1; x += 2) {
            const i = row + x * 3;
            const r = img.rgb[i];
            const g = img.rgb[i + 1];
            const b = img.rgb[i + 2];
            if (r > 160 && g < 80 && b < 90 && r > g + 80) xs.push(x);
        }
    }
    if (xs.length < 80) return null;
    xs.sort((a, b) => a - b);
    return xs[xs.length >> 1] - img.w / 2;
}

export const widescreenTitle: PlayCase = {
    name: "widescreen-title",
    summary: "21:9 title characters stay centered under the logo.",
    skip: "21:9 title screenshot timing; tracked for re-enable",
    run: async (session) => {
        await session.start({
            save: "repo",
            env: { NSMBU_ASPECT: "21:9", NSMBU_HIDDEN_WINDOWS: "0" },
        });
        await session.goto("title");
        await session.wait((s) => s.scene === "Title", 360);
        await advanceUntilFrame(session, 850, 120);
        const path = join(session.workDir, "title.png");
        await session.screenshot(path);
        const ready = Date.now() + 5000;
        let img = { w: 0, h: 0, rgb: new Uint8Array() };
        for (;;) {
            try {
                img = readPng(path);
                break;
            } catch (err) {
                if (Date.now() > ready) throw err;
                await new Promise((r) => setTimeout(r, 50));
            }
        }
        const fromCenter = marioFromCenter(img);
        const width = img.w;
        if (fromCenter === null) throw new Error(`title characters not found in ${path}`);
        const min = width * 0.04;
        const max = width * 0.12;
        if (fromCenter < min || fromCenter > max) {
            throw new Error(
                `title characters off center at 21:9: Mario is ${Math.round(fromCenter)}px from center of ${width} (want ${Math.round(min)} to ${Math.round(max)})`,
            );
        }
    },
};
