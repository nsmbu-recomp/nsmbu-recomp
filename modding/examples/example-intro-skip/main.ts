const Factory = nsmbu.Scene.Factory;

let done = false;
let opened = false;
let requested = false;
let last = "";
let logicStep = 0;
let pendingPopup = false;
let pendingApply = false;
let pendingLoad = false;
let pendingCourseUp = false;
let fileSelectSteps = 0;
let popupPulses = 0;
let pressErrLogged = false;

function note(step: number, message: string): void {
    if (message === last) {
        return;
    }
    last = message;
    nsmbu.log(string.format("intro-skip step %d %s", step, message));
}

function phaseText(): string {
    const [phase] = nsmbu.Scene.phase();
    if (phase === undefined) {
        return "none";
    }
    return string.format("%08X", phase);
}

function saveIndex(): number {
    const [picked] = nsmbu.Config.string("save_file", "1");
    if (picked === "2") {
        return 1;
    } else if (picked === "3") {
        return 2;
    }
    return 0;
}

function coursePack(): string {
    const [pack] = nsmbu.Config.string("course_pack", "1-1");
    if (pack === undefined || pack === "") {
        return "1-1";
    }
    return pack;
}

function sceneBusy(err: string | undefined): boolean {
    if (err === undefined) {
        return false;
    }
    const [found] = string.find(err, "scene busy", 1, true);
    return found !== undefined;
}

function tryLoadPack(step: number): void {
    const pack = coursePack();
    const [ok, err] = nsmbu.Scene.load_pack(pack);
    if (ok === true) {
        requested = true;
        pendingLoad = false;
        note(step, string.format("load_pack %s", pack));
        return;
    }
    if (sceneBusy(err)) {
        pendingLoad = true;
    } else {
        pendingLoad = true;
        note(step, string.format("load_pack %s %s", pack, err ?? "refused"));
    }
}

function tryApplySave(step: number): void {
    const save = saveIndex();
    const [ok, err] = nsmbu.Scene.apply_save(save);
    if (ok === true) {
        opened = true;
        pendingApply = false;
        pendingPopup = false;
        pendingLoad = true;
        fileSelectSteps = 0;
        popupPulses = 0;
        note(step, string.format("apply save %d", save + 1));
        return;
    }
    if (err === "file not ready" || sceneBusy(err)) {
        pendingApply = true;
    } else {
        pendingApply = true;
        note(step, string.format("apply_save %s phase=%s", err ?? "refused", phaseText()));
    }
}

nsmbu.Hooks.onSceneChange = (factory) => {
    if (done) {
        return;
    }
    if (factory === Factory.SaveSelect) {
        note(logicStep, "unexpected challenge select");
        done = true;
        return;
    }
    if (factory === Factory.CourseTask) {
        if (requested) {
            pendingCourseUp = true;
        }
        return;
    }
    if (factory === Factory.FileSelect) {
        pendingPopup = true;
        pendingApply = true;
        fileSelectSteps = 0;
        popupPulses = 0;
        return;
    }
    if (factory === Factory.Boot) {
        note(logicStep, "wait boot");
        return;
    }
    if (!opened) {
        pendingApply = true;
        return;
    }
    if (!requested) {
        pendingLoad = true;
    }
};

nsmbu.Hooks.onLogicStep = (step) => {
    logicStep = step;
    if (done) {
        return;
    }
    if (pendingCourseUp || requested) {
        let courseUp = pendingCourseUp;
        if (!courseUp) {
            const [inCourseTask] = nsmbu.Scene.course_task();
            courseUp = inCourseTask === true;
        }
        if (courseUp) {
            note(step, "course up");
            done = true;
            return;
        }
    }
    if (!pendingPopup && !pendingApply && !pendingLoad) {
        return;
    }
    if (pendingPopup) {
        const [inFileSelect] = nsmbu.Scene.file_select();
        if (!inFileSelect) {
            pendingPopup = false;
            fileSelectSteps = 0;
            popupPulses = 0;
        } else {
            fileSelectSteps = fileSelectSteps + 1;
            if (popupPulses < 4) {
                if (fileSelectSteps % 30 === 0) {
                    popupPulses = popupPulses + 1;
                    const [ok, err] = nsmbu.Input.press("A", 3);
                    note(step, string.format("press A file_select phase=%s", phaseText()));
                    if (ok === undefined && err !== undefined && !pressErrLogged) {
                        pressErrLogged = true;
                        note(step, err);
                    }
                }
            } else {
                const [ok, err] = nsmbu.Scene.open_title();
                if (ok === true) {
                    note(step, "open_title");
                } else {
                    note(step, string.format("open_title %s", err ?? "refused"));
                }
            }
        }
    }
    if (pendingApply && !opened) {
        tryApplySave(step);
    }
    if (pendingLoad && opened && !requested) {
        tryLoadPack(step);
    }
};

nsmbu.Hooks.onUnload = () => {
    done = false;
    opened = false;
    requested = false;
    last = "";
    logicStep = 0;
    pendingPopup = false;
    pendingApply = false;
    pendingLoad = false;
    pendingCourseUp = false;
    fileSelectSteps = 0;
    popupPulses = 0;
    pressErrLogged = false;
};

export {};
