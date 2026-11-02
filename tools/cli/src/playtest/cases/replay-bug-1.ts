import { waitMap, type PlayCase } from "../helpers.ts";
import type { PlayStatus } from "../session.ts";

export function quitPlaceError(s: PlayStatus): string | null {
    if (
        s.node_name === undefined ||
        s.world === undefined ||
        s.node_x === undefined ||
        s.node_y === undefined ||
        s.map_x === undefined ||
        s.map_y === undefined
    ) {
        return "map place missing";
    }
    if (s.node_name !== "W201" || s.world !== 1) {
        return `intro skip quit landed on ${s.node_name} world ${s.world}, want W201`;
    }
    if (Math.abs(s.map_x - s.node_x) > 80 || Math.abs(s.map_y - s.node_y) > 80) {
        return `map player is off W201 (${s.map_x}, ${s.map_y}) vs node (${s.node_x}, ${s.node_y})`;
    }
    return null;
}

export const replayBug1: PlayCase = {
    name: "replay-bug-1",
    summary: "Intro skip into 2-1, then quit: the map player stands on W201.",
    run: async (session) => {
        await session.start({ save: "repo", open: "2-1" });
        await session.wait(
            (s) =>
                s.area === true &&
                s.course === true &&
                s.map !== true &&
                s.pack === "2-1" &&
                s.player != null &&
                Math.abs(s.player.x) + Math.abs(s.player.y) > 10,
            180,
        );
        await session.leave();
        await session.wait((s) => s.map === true && (s.scene === "Map" || s.scene === "CourseSelect"), 120);
        await waitMap(session, 90, false);
        const err = quitPlaceError(await session.status());
        if (err) throw new Error(err);
    },
};
