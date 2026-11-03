import { mkdirSync } from "node:fs";
import { join } from "node:path";
import {
    advanceUntilFrame,
    inCourse,
    onMap,
    reachWorldMap,
    waitCourse,
    waitForPortableSave,
    type PlayCase,
} from "../helpers.ts";

function portableSaveAttempts(from: number, to: number, step: number, slot: number): string {
    const parts: string[] = [];
    for (let frame = from; frame <= to; frame += step) {
        parts.push(`${frame}:${slot}`);
    }
    return parts.join(",");
}

export const saveState: PlayCase = {
    name: "save-state",
    summary: "Regression for #14: a portable save state writes on the world map and inside a course.",
    run: async (session) => {
        const states = join(session.repo, ".tmp", "playtest", "save-state", "states");
        mkdirSync(states, { recursive: true });

        await session.start({
            save: "repo",
            env: {
                NSMBU_STATE_DIR: states,
                NSMBU_PORTABLE_SAVE_AT: portableSaveAttempts(4500, 12000, 250, 1),
            },
        });
        await reachWorldMap(session, 180);
        await advanceUntilFrame(session, 12000, 180);
        await waitForPortableSave(session, states, 1, "map", 15);
        await session.stop();

        await session.start({
            save: "keep",
            open: "1-1",
            env: {
                NSMBU_STATE_DIR: states,
                NSMBU_PORTABLE_SAVE_AT: portableSaveAttempts(600, 3600, 120, 2),
            },
        });
        await waitCourse(session, "1-1", 180);
        const playing = await session.status();
        if (!inCourse(playing)) throw new Error(`course did not stay open: ${session.last}`);
        await advanceUntilFrame(session, 3600, 90);
        await waitForPortableSave(session, states, 2, "course", 15);
    },
};
