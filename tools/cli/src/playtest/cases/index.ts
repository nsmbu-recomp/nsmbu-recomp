import type { PlayCase } from "../helpers.ts";
import { courseEnter } from "./course-enter.ts";
import { courseEnter120 } from "./course-enter-120.ts";
import { logicRate120 } from "./logic-rate-120.ts";
import { mapShadow120 } from "./map-shadow-120.ts";
import { openedCourse } from "./opened-course.ts";
import { poseDirection120 } from "./pose-direction-120.ts";
import { openingCinematic } from "./opening-cinematic.ts";
import { saveRoundtrip } from "./save-roundtrip.ts";
import { saveState } from "./save-state.ts";
import { startup } from "./startup.ts";
import { widescreenMapUi } from "./widescreen-map-ui.ts";
import { widescreenTitle } from "./widescreen-title.ts";

export type { PlayCase } from "../helpers.ts";

export const CASES: PlayCase[] = [
    startup,
    openedCourse,
    poseDirection120,
    openingCinematic,
    saveRoundtrip,
    saveState,
    courseEnter,
    courseEnter120,
    mapShadow120,
    logicRate120,
    widescreenMapUi,
    widescreenTitle,
];
