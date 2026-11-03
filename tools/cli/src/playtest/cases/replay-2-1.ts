import type { PlayCase } from "../helpers.ts";
import { runReplay } from "../replay.ts";

export const replay21: PlayCase = {
    name: "replay-2-1",
    summary: "Play 2-1 from the recorded pad log and require a position match. [prof] in the case log is the course measurement.",
    skip: "replay witness drift and long runtime; re-enable after replay refresh",
    run: (session) => runReplay(session, "2-1", 420),
};
