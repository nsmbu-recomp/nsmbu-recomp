import { join } from "node:path";
import { waitCourse, type PlayCase } from "../helpers.ts";
import { readPng } from "./widescreen-map-ui.ts";

function bandXs(
    img: { w: number; h: number; rgb: Uint8Array },
    y0f: number,
    y1f: number,
    match: (r: number, g: number, b: number) => boolean,
    x0 = 0,
    x1 = img.w,
): number[] {
    const xs: number[] = [];
    const y0 = Math.floor(img.h * y0f);
    const y1 = Math.floor(img.h * y1f);
    for (let y = y0; y < y1; y += 2) {
        const row = y * img.w * 3;
        for (let x = x0; x < x1; x++) {
            const i = row + x * 3;
            if (match(img.rgb[i], img.rgb[i + 1], img.rgb[i + 2])) xs.push(x);
        }
    }
    return xs;
}

function hat(r: number, g: number, b: number): boolean {
    return r > 170 && g < 90 && b < 90 && r > g + 80;
}

function ink(r: number, g: number, b: number): boolean {
    return r < 45 && g < 45 && b < 55;
}

export const widescreenCourseUi: PlayCase = {
    name: "widescreen-course-ui",
    summary: "21:9 course lives, coins, score, and timer sit on the screen edges.",
    run: async (session) => {
        await session.start({ save: "repo", open: "1-1", env: { NSMBU_ASPECT: "21:9" } });
        await waitCourse(session, "1-1", 180);
        const path = join(session.workDir, "course.png");
        await session.advance(180, 20);
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
        const left = bandXs(img, 0.02, 0.16, hat, 0, Math.floor(img.w * 0.5));
        const right = bandXs(img, 0.02, 0.14, ink, Math.floor(img.w * 0.5));
        if (left.length < 40) throw new Error(`lives icon not found in ${path}`);
        left.sort((a, b) => a - b);
        const hatX = left[0];
        if (right.length < 40) throw new Error(`timer ink not found in ${path} (lives at ${hatX})`);
        right.sort((a, b) => a - b);
        const timerX = right[right.length - 1];
        const limit = img.w * 0.12;
        if (hatX > limit || img.w - timerX > limit) {
            throw new Error(
                `course HUD inset at 21:9: lives ${hatX}px timer ${img.w - timerX}px of ${img.w} (limit ${Math.round(limit)})`,
            );
        }
        if (hatX < img.w * 0.005 || timerX > img.w - 2) {
            throw new Error(`course HUD clipped at 21:9: lives ${hatX}px timer ${timerX}px of ${img.w}`);
        }
    },
};
