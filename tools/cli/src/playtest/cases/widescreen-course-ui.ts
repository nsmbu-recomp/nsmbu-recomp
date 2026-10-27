import { unlinkSync } from "node:fs";
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
        const measure = (shot: { w: number; h: number; rgb: Uint8Array }) => {
            const lives = bandXs(shot, 0.02, 0.16, hat, 0, Math.floor(shot.w * 0.5));
            const timer = bandXs(shot, 0.02, 0.14, ink, Math.floor(shot.w * 0.5));
            if (lives.length < 40) throw new Error(`lives icon not found in ${path}`);
            lives.sort((a, b) => a - b);
            if (timer.length < 40) throw new Error(`timer ink not found in ${path} (lives at ${lives[0]})`);
            timer.sort((a, b) => a - b);
            return { hatX: lives[0], timerX: timer[timer.length - 1], w: shot.w };
        };
        const first = measure(img);
        unlinkSync(path);
        await session.advance(1, 20);
        await session.screenshot(path);
        const ready2 = Date.now() + 5000;
        for (;;) {
            try {
                img = readPng(path);
                break;
            } catch (err) {
                if (Date.now() > ready2) throw err;
                await new Promise((r) => setTimeout(r, 50));
            }
        }
        const second = measure(img);
        const limit = first.w * 0.12;
        for (const m of [first, second]) {
            if (m.hatX > limit || m.w - m.timerX > limit) {
                throw new Error(
                    `course HUD inset at 21:9: lives ${m.hatX}px timer ${m.w - m.timerX}px of ${m.w} (limit ${Math.round(limit)})`,
                );
            }
            if (m.hatX < m.w * 0.005 || m.timerX > m.w - 2) {
                throw new Error(`course HUD clipped at 21:9: lives ${m.hatX}px timer ${m.timerX}px of ${m.w}`);
            }
        }
        if (Math.abs(first.hatX - second.hatX) > 12 || Math.abs(first.timerX - second.timerX) > 12) {
            throw new Error(
                `course HUD jumped: lives ${first.hatX}->${second.hatX} timer ${first.timerX}->${second.timerX} of ${first.w}`,
            );
        }
    },
};
