import { onMap, reachWorldMap, type PlayCase } from "../helpers.ts";
import type { Session } from "../session.ts";

const caps = [120, 144, 165, 240];
const lowCaps = [30];

async function measure(session: Session, cap: number, first: boolean): Promise<void> {
    const env = { NSMBU_INTERP: "1", NSMBU_INTERP_FPS: String(cap), NSMBU_UNCAPPED: "0" };
    if (first) {
        await session.start({ save: "repo", env });
    } else {
        await session.restart(env);
    }
    await reachWorldMap(session, 180);
    const a = await session.status();
    if (!onMap(a)) throw new Error(`not on map at ${cap}: ${session.last}`);
    const t0 = Date.now();
    const step0 = a.step ?? 0;
    const frame0 = a.frame ?? 0;
    await session.advance(180, 20);
    const b = await session.status();
    const elapsed = (Date.now() - t0) / 1000;
    if (elapsed < 0.5) throw new Error(`window too short at ${cap}: ${elapsed}s`);
    const steps = (b.step ?? 0) - step0;
    const frames = (b.frame ?? 0) - frame0;
    const stepsPerSec = steps / elapsed;
    const framesPerSec = frames / elapsed;
    if (stepsPerSec < 50 || stepsPerSec > 70) {
        throw new Error(`logic rate ${stepsPerSec.toFixed(1)}/s out of band at ${cap} (want ~60); ${session.last}`);
    }
    const presentFloor = cap >= 240 ? cap * 0.7 : cap * 0.75;
    if (framesPerSec < presentFloor) {
        throw new Error(
            `present rate ${framesPerSec.toFixed(1)}/s too low for ${cap} cap; ${session.last}`,
        );
    }
    if (frames < steps * 1.3) {
        throw new Error(`presents ${frames} not clearly above logic steps ${steps} at ${cap}; ${session.last}`);
    }
}

async function measureLow(session: Session, cap: number, first: boolean): Promise<void> {
    const env = { NSMBU_INTERP: "1", NSMBU_INTERP_FPS: String(cap), NSMBU_UNCAPPED: "0" };
    if (first) {
        await session.start({ save: "repo", env });
    } else {
        await session.restart(env);
    }
    await reachWorldMap(session, 180);
    const a = await session.status();
    if (!onMap(a)) throw new Error(`not on map at ${cap}: ${session.last}`);
    const t0 = Date.now();
    const step0 = a.step ?? 0;
    const frame0 = a.frame ?? 0;
    await session.advance(180, 20);
    const b = await session.status();
    const elapsed = (Date.now() - t0) / 1000;
    if (elapsed < 0.5) throw new Error(`window too short at ${cap}: ${elapsed}s`);
    const steps = (b.step ?? 0) - step0;
    const frames = (b.frame ?? 0) - frame0;
    const stepsPerSec = steps / elapsed;
    const framesPerSec = frames / elapsed;
    if (framesPerSec < cap * 0.5 || framesPerSec > cap * 1.35) {
        throw new Error(`present rate ${framesPerSec.toFixed(1)}/s out of band for ${cap}; ${session.last}`);
    }
    const ratio = frames > 0 ? steps / frames : 0;
    if (ratio < 1.6 || ratio > 2.4) {
        throw new Error(
            `logic ${stepsPerSec.toFixed(1)}/s is ${ratio.toFixed(2)}x presents ${framesPerSec.toFixed(1)}/s at ${cap} (want ~2); ${session.last}`,
        );
    }
}

export const logicRate120: PlayCase = {
    name: "logic-rate-120",
    summary: "At 30 fps, logic steps stay near twice the presents. At 120, 144, 165, and 240, logic stays near 60/s and presents track the cap.",
    run: async (session) => {
        for (let i = 0; i < lowCaps.length; i++) await measureLow(session, lowCaps[i], i === 0);
        for (let i = 0; i < caps.length; i++) await measure(session, caps[i], false);
    },
};
