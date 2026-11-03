import { onMap, reachWorldMap, type PlayCase } from "../helpers.ts";

export const mapShadow120: PlayCase = {
    name: "map-shadow-120",
    summary:
        "Regression for #20 at 120 fps: the world map stays open (in-between passes do not redraw outside a course).",
    run: async (session) => {
        await session.start({
            save: "repo",
            env: { NSMBU_INTERP: "1", NSMBU_INTERP_FPS: "120", NSMBU_UNCAPPED: "0" },
        });
        const map = await reachWorldMap(session, 180);
        const start = map.frame ?? 0;
        await session.advance(180, 30);
        const held = await session.status();
        if (!onMap(held)) throw new Error(`left the map at 120 fps: ${session.last}`);
        if ((held.frame ?? 0) <= start) throw new Error(`map frame did not advance: ${session.last}`);
    },
};
