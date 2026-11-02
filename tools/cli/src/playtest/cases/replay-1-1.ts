import type { PlayCase } from "../helpers.ts";
import { runReplay } from "../replay.ts";

export const replay11: PlayCase = {
    name: "replay-1-1",
    summary: "Play 1-1 from the recorded pad log and require a position match. [prof] in the case log is the course measurement.",
    run: (session) => runReplay(session, "1-1", 300),
};
