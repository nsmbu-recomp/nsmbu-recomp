import { readFileSync } from "node:fs";
import { waitCourse, type PlayCase } from "../helpers.ts";
import type { Session } from "../session.ts";

type Pose = {
    line: string;
    hold: boolean;
    elem: number;
    ePrev: number;
    eCur: number;
    eDrawn: number;
    travel: number;
    prev: [number, number, number];
    drawn: [number, number, number];
    camPrev: [number, number, number, number];
    camCur: [number, number, number, number];
    camDrawn: [number, number, number, number];
};

const poseRe =
    /\[interp\] pose hold=(\d) t=[\d.]+ step=\d+ bones=\d+ bone=\d+ elem=(\d+) ePrev=([-\d.]+) eCur=([-\d.]+) eDrawn=([-\d.]+) travel=([\d.]+) prev=([-\d.]+),([-\d.]+),([-\d.]+) cur=[-\d.]+,[-\d.]+,[-\d.]+ drawn=([-\d.]+),([-\d.]+),([-\d.]+) camPrev=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) camCur=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) camDrawn=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)/g;

export function parsePoses(log: string): Pose[] {
    const out: Pose[] = [];
    for (const m of log.matchAll(poseRe)) {
        const n = (i: number) => Number(m[i]);
        out.push({
            line: m[0],
            hold: m[1] === "1",
            elem: n(2),
            ePrev: n(3),
            eCur: n(4),
            eDrawn: n(5),
            travel: n(6),
            prev: [n(7), n(8), n(9)],
            drawn: [n(10), n(11), n(12)],
            camPrev: [n(13), n(14), n(15), n(16)],
            camCur: [n(17), n(18), n(19), n(20)],
            camDrawn: [n(21), n(22), n(23), n(24)],
        });
    }
    return out;
}

function centerX(box: [number, number, number, number]): number {
    return (box[0] + box[2]) * 0.5;
}

export function poseFault(p: Pose): string | null {
    const span = p.eCur - p.ePrev;
    if (Math.abs(span) < 0.05 || Math.abs(span) > 400) return null;
    const forward = (p.eDrawn - p.ePrev) / span;
    if (forward < 0.2 || forward > 0.8) {
        return `element ${p.elem} drawn fraction ${forward.toFixed(2)} is off the segment (want 0.20..0.80)`;
    }
    const camSpan = centerX(p.camCur) - centerX(p.camPrev);
    const camDx = centerX(p.camDrawn) - centerX(p.camPrev);
    if (Math.abs(camSpan) > 1) {
        const camForward = camDx / camSpan;
        if (camForward < 0.2 || camForward > 0.8) {
            return `camera fraction ${camForward.toFixed(2)} is off the segment (want 0.20..0.80)`;
        }
    }
    const modelDx = p.drawn[0] - p.prev[0];
    if (p.travel > 1 && Math.abs(camDx) > 0.5 && Math.abs(modelDx) > 0.5 && camDx * modelDx < 0) {
        return `model moved ${modelDx.toFixed(2)} while the camera moved ${camDx.toFixed(2)}`;
    }
    return null;
}

export const poseDirection120: PlayCase = {
    name: "pose-direction-120",
    summary: "At 120 fps, an in-between bone stays between the two logic poses and moves with the camera.",
    run: async (session: Session) => {
        await session.start({
            save: "repo",
            open: "1-1",
            env: {
                NSMBU_INTERP: "1",
                NSMBU_INTERP_FPS: "120",
                NSMBU_INTERP_MODEL_TRACE: "8000",
            },
        });
        await waitCourse(session, "1-1", 180);
        await session.advance(40, 20);
        const mark = (await session.status()).step ?? 0;
        await session.stick(1, 0, 600);
        await session.wait((s) => (s.step ?? 0) >= mark + 600, 40);
        const poses = parsePoses(readFileSync(session.logPath, "utf8"));
        const holds = poses.filter((p) => p.hold && Math.abs(p.eCur - p.ePrev) > 0.05 && Math.abs(p.eCur - p.ePrev) < 400);
        const scrolling = holds.filter((p) => Math.abs(centerX(p.camCur) - centerX(p.camPrev)) > 1);
        if (holds.length < 8) {
            throw new Error(`wanted at least 8 moving in-between poses, got ${holds.length} of ${poses.length}`);
        }
        if (scrolling.length < 4) {
            const last = poses.at(-1);
            throw new Error(`wanted the camera to scroll, got ${scrolling.length} moving holds. last=${last?.line ?? "none"} ${session.last}`);
        }
        const bad = holds.flatMap((p) => {
            const fault = poseFault(p);
            return fault ? [`${fault}: ${p.line}`] : [];
        });
        if (bad.length) throw new Error(bad.slice(0, 5).join("\n"));
        const headRe =
            /\[interp\] player head n=\d+ bone=\d+ ubo=\d+ shp=\d+ ent=\d+ frac=([-\d.]+) prev=([-\d.]+) cur=([-\d.]+)/g;
        const heads = [...readFileSync(session.logPath, "utf8").matchAll(headRe)].filter((m) => {
            const span = Number(m[3]) - Number(m[2]);
            const frac = Number(m[1]);
            return Math.abs(span) > 0.05 && Math.abs(span) < 400 && frac >= 0.2 && frac <= 0.8;
        });
        if (heads.length < 2) {
            throw new Error(`wanted the head shape matrix between the two logic poses, got ${heads.length}`);
        }
    },
};
