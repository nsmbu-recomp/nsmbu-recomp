import { loadPack, waitCourse, waitMap, type PlayCase } from "../helpers.ts";

export const openedCourse: PlayCase = {
    name: "opened-course",
    summary: "0.6.1 course-open regressions: loading card stays put, a second pack loads, leave returns to W102.",
    run: async (session) => {
        await session.start({ save: "repo", open: "1-5" });
        await waitCourse(session, "1-5", 180);
        const glyphs = (await session.status()).banner ?? "none";
        if (glyphs === "4 255 255") {
            throw new Error(`direct open rewrote the loading-card digits for 1-5: ${glyphs}`);
        }
        await loadPack(session, "1-2");
        await waitCourse(session, "1-2", 180);
        await session.leave();
        await session.wait((s) => s.map === true && (s.scene === "Map" || s.scene === "CourseSelect"), 120);
        const view = await waitMap(session, 90, false);
        if (view.name !== "W102") throw new Error(`map node is ${view.name}, want W102 (${view.raw})`);
        if (view.world !== 0) throw new Error(`map world is ${view.world}, want 0`);
        const status = await session.status();
        if (status.cp === undefined || status.cp < 0) throw new Error(`map checkpoint missing: ${session.last}`);
    },
};
