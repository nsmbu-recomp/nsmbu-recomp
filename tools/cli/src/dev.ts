import { existsSync, mkdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { flagString, hasFlag, parseArgv } from "./parse.ts";
import { nsmbuUserDir } from "./repo.ts";
import { runPython, runPythonCapture, runPythonChecked, runShell } from "./run.ts";
import { clack, confirm, selectOption, textInput } from "./select.ts";

const GAME_USAGE =
    "usage: nsmbu game [id] [--extract] [--recomp]\n" +
    "       nsmbu game --list\n" +
    "       nsmbu game --import PATH --id ID [--extract] [--recomp]\n" +
    "       nsmbu game --create ID";

function runPythonInherit(repo: string, script: string, args: string[], extraEnv: Record<string, string> = {}): void {
    const status = runPythonChecked(repo, script, args, extraEnv);
    if (status !== 0) {
        process.exit(status);
    }
}

export function cmdBuild(repo: string, release: boolean, sanitizer: boolean, rest: string[]): void {
    const args: string[] = [];
    if (release) {
        args.push("--release");
    }
    if (sanitizer) {
        args.push("--sanitizer");
    }
    args.push(...rest);
    runPython(repo, "tools/build/build.py", args);
}

export function cmdLaunch(
    repo: string,
    opts: { noDebug: boolean; trace: boolean; gdb: boolean; lldb: boolean; wine?: boolean },
    rest: string[],
    extraEnv: Record<string, string> = {},
): void {
    const args: string[] = [];
    if (opts.noDebug) {
        args.push("--no-debug");
    }
    if (opts.trace) {
        args.push("--trace");
    }
    if (opts.gdb) {
        args.push("--gdb");
    }
    if (opts.lldb) {
        args.push("--lldb");
    }
    if (opts.wine) {
        args.push("--wine");
    }
    args.push(...rest);
    runPython(repo, "tools/run/launch.py", args, extraEnv);
}

export function cmdCrash(repo: string): void {
    cmdLaunch(repo, { noDebug: false, trace: false, gdb: false, lldb: false }, [], {
        NSMBU_TEST_HOST_CRASH: "1",
    });
}

export function cmdWine(
    repo: string,
    opts: { noDebug: boolean; trace: boolean },
    rest: string[],
): void {
    const buildArgs: string[] = opts.noDebug ? ["--release"] : [];
    buildArgs.push(...rest);
    runPythonInherit(repo, "tools/build/build_windows.py", buildArgs);
    cmdLaunch(repo, { noDebug: opts.noDebug, trace: opts.trace, gdb: false, lldb: false, wine: true }, rest);
}

export function cmdRun(repo: string, release: boolean, rest: string[]): void {
    const buildArgs: string[] = release ? ["--release"] : [];
    buildArgs.push(...rest);
    runPythonInherit(repo, "tools/build/build.py", buildArgs);
    const launchArgs = [...rest];
    cmdLaunch(repo, { noDebug: release, trace: false, gdb: false, lldb: false }, launchArgs);
}

export function cmdExtract(repo: string, rest: string[]): void {
    if (rest.length > 0) {
        runPython(repo, "tools/disc/extract.py", rest);
        return;
    }
    console.error("tip: `nsmbu game` walks through dump setup, extract, and optional recomp");
    runPython(repo, "tools/disc/extract.py", []);
}

type GameVersion = {
    id: string;
    label: string;
    active: boolean;
    extracted: boolean;
    has_wux: boolean;
    has_key: boolean;
};

type LooseImage = {
    path: string;
    name: string;
    has_key: boolean;
    has_common: boolean;
    title_id: string | null;
};

type GameListState = {
    flat: boolean;
    active: string | null;
    versions: GameVersion[];
    loose_images: LooseImage[];
    has_common_key: boolean;
    common_key_src: string | null;
};

const RECOMMENDED_IDS = [
    "usa-base",
    "usa-1.3.0",
    "eu-base",
    "eu-1.3.0",
    "nsmbu-nslu-usa",
    "nsmbu-nslu-eu",
];

function gameList(repo: string): GameListState {
    const r = runPythonCapture(repo, "tools/disc/game_select.py", ["--list"]);
    if (r.status !== 0) {
        throw new Error((r.stderr || r.stdout || "game --list failed").trim());
    }
    const parsed = JSON.parse(r.stdout) as Partial<GameListState>;
    return {
        flat: !!parsed.flat,
        active: parsed.active ?? null,
        versions: parsed.versions ?? [],
        loose_images: parsed.loose_images ?? [],
        has_common_key: !!parsed.has_common_key,
        common_key_src: parsed.common_key_src ?? null,
    };
}

function versionHint(v: GameVersion): string {
    const bits = [v.label];
    if (v.active) {
        bits.push("active");
    }
    if (!v.extracted && v.has_wux && v.has_key) {
        bits.push("needs extract");
    } else if (!v.extracted && v.has_wux) {
        bits.push("needs keys in game/");
    } else if (!v.extracted) {
        bits.push("no disc");
    }
    return bits.join(" · ");
}

function activateDump(repo: string, id: string): void {
    const status = runPythonChecked(repo, "tools/disc/game_select.py", ["--activate", id]);
    if (status !== 0) {
        process.exit(status);
    }
}

function extractActive(repo: string): void {
    const status = runPythonChecked(repo, "tools/disc/extract.py", []);
    if (status !== 0) {
        process.exit(status);
    }
}

function recompActive(repo: string): void {
    const status = runPythonChecked(repo, "tools/disc/game_select.py", ["--recomp"]);
    if (status !== 0) {
        process.exit(status);
    }
}

async function pickVersionId(existing: string[]): Promise<string> {
    const taken = new Set(existing);
    const options = [
        ...RECOMMENDED_IDS.filter((id) => !taken.has(id)).map((id) => ({ value: id, label: id })),
        { value: "__custom", label: "Other…", hint: "type a custom id" },
    ];
    if (options.length === 1) {
        return textInput("Version folder name", "usa-base");
    }
    const pick = await selectOption("Folder id for this dump", options, options[0]?.value);
    if (pick !== "__custom") {
        return pick;
    }
    return textInput("Version folder name", "usa-base");
}

function keysHint(state: GameListState): string[] {
    const lines = [
        "Keys: game/keys.txt (Cemu format — common= and title-id lines).",
        "Disc images stay under game/: New Super Mario Bros. U (USA).wux / (Europe).wux.",
        "Optional: WIIU_COMMON_KEY for the common key only.",
    ];
    if (state.has_common_key) {
        lines.push(`Common key found (${state.common_key_src}).`);
    } else {
        lines.push("No common key in game/keys.txt yet.");
    }
    return lines;
}

async function importLooseImage(repo: string, state: GameListState): Promise<string> {
    const images = state.loose_images;
    if (images.length === 0) {
        throw new Error("no loose .wux/.wud under game/");
    }
    let image = images[0]!;
    if (images.length > 1) {
        const name = await selectOption(
            "Which disc image?",
            images.map((img) => ({
                value: img.name,
                label: img.name,
                hint: [
                    img.has_key ? "keys ok" : "need keys.txt",
                    img.has_common ? "common ok" : "need common=",
                ].join(" · "),
            })),
            images[0]!.name,
        );
        image = images.find((img) => img.name === name) ?? image;
    }

    clack.note(
        [
            `Importing game/${image.name}`,
            "It stays under game/ and is renamed to the standard USA or Europe name.",
            "Version folders only hold the extract (code/, content/, meta/).",
            "",
            ...keysHint(state),
        ].join("\n"),
        "Disc image",
    );

    if (!image.has_key) {
        clack.log.warn("Add the disc title key and common= to game/keys.txt before extract.");
    }
    if (!state.has_common_key) {
        clack.log.warn("Add common= to game/keys.txt (Cemu style) before extract.");
    }

    const id = await pickVersionId(state.versions.map((v) => v.id));
    const status = runPythonChecked(repo, "tools/disc/game_select.py", ["--import", image.path, id]);
    if (status !== 0) {
        process.exit(status);
    }
    return id;
}

async function createEmptyDump(repo: string, state: GameListState): Promise<string> {
    const id = await pickVersionId(state.versions.map((v) => v.id));
    const status = runPythonChecked(repo, "tools/disc/game_select.py", ["--create", id]);
    if (status !== 0) {
        process.exit(status);
    }
    clack.note(
        [
            `Created game/${id}/`,
            "",
            "Drop a .wux into game/ and run nsmbu game again to rename it and extract,",
            "or place New Super Mario Bros. U (USA).wux / (Europe).wux there yourself.",
            "",
            ...keysHint(state),
        ].join("\n"),
        "Add your dump",
    );
    clack.outro(`Folder ready at game/${id}/. Drop a .wux into game/ then run nsmbu game.`);
    process.exit(0);
}

async function maybeExtract(repo: string, v: GameVersion): Promise<GameVersion> {
    if (v.extracted) {
        if (v.has_wux && (await confirm("Re-extract the disc into this folder?", false))) {
            extractActive(repo);
            return gameList(repo).versions.find((x) => x.id === v.id) ?? v;
        }
        return v;
    }
    if (!v.has_wux) {
        clack.log.warn(
            `No disc image for game/${v.id}/ — put New Super Mario Bros. U (USA).wux or (Europe).wux under game/.`,
        );
        return v;
    }
    if (!v.has_key) {
        clack.log.warn(
            `Keys missing for game/${v.id}/ — add common= and the title key in game/keys.txt, then run nsmbu game again.`,
        );
        return v;
    }
    if (await confirm(`Extract game/${v.id}/ now?`, true)) {
        extractActive(repo);
        return gameList(repo).versions.find((x) => x.id === v.id) ?? { ...v, extracted: true };
    }
    return v;
}

async function maybeRecomp(repo: string, v: GameVersion): Promise<void> {
    if (!v.extracted) {
        clack.log.info("Skipping recomp until the dump is extracted.");
        return;
    }
    if (await confirm(`Run recomp for game/${v.id}/ into build/gen?`, true)) {
        recompActive(repo);
    } else {
        clack.log.info("Skipped recomp. Run `nsmbu recomp` or `nsmbu game` later when you want it.");
    }
}

function activateNonInteractive(
    repo: string,
    id: string,
    opts: { extract: boolean; recomp: boolean },
): void {
    activateDump(repo, id);
    let state = gameList(repo);
    let v = state.versions.find((x) => x.id === id);
    if (!v) {
        throw new Error(`unknown version ${id}`);
    }
    const needExtract = opts.extract || (!v.extracted && v.has_wux && v.has_key);
    if (needExtract) {
        if (!v.has_wux) {
            throw new Error(
                `no disc image for game/${id}/; put New Super Mario Bros. U (USA).wux or (Europe).wux under game/`,
            );
        }
        if (!v.has_key) {
            throw new Error(`keys missing for game/${id}/; add common= and the title key in game/keys.txt`);
        }
        extractActive(repo);
        state = gameList(repo);
        v = state.versions.find((x) => x.id === id) ?? v;
    }
    if (opts.recomp) {
        if (!v.extracted) {
            throw new Error(`game/${id}/ is not extracted; pass --extract or extract first`);
        }
        recompActive(repo);
    }
}

export async function cmdGame(repo: string, rest: string[]): Promise<void> {
    const { flags, positional } = parseArgv(
        rest,
        ["list", "recomp", "extract", "import", "id", "create"],
        ["import", "id", "create"],
    );
    if (positional.some((a) => a.startsWith("-"))) {
        throw new Error(GAME_USAGE);
    }

    const wantList = hasFlag(flags, "list");
    const wantRecomp = hasFlag(flags, "recomp");
    const wantExtract = hasFlag(flags, "extract");
    const importPath = flagString(flags, "import");
    const createId = flagString(flags, "create");
    const idFlag = flagString(flags, "id");
    const idArg = positional[0] ?? idFlag;

    if (wantList) {
        if (positional.length > 0 || importPath || createId || wantRecomp || wantExtract || idFlag) {
            throw new Error(GAME_USAGE);
        }
        const r = runPythonCapture(repo, "tools/disc/game_select.py", ["--list"]);
        if (r.status !== 0) {
            throw new Error((r.stderr || r.stdout || "game --list failed").trim());
        }
        process.stdout.write(r.stdout.endsWith("\n") ? r.stdout : `${r.stdout}\n`);
        return;
    }

    if (createId) {
        if (positional.length > 0 || importPath || wantRecomp || wantExtract || idFlag) {
            throw new Error(GAME_USAGE);
        }
        const status = runPythonChecked(repo, "tools/disc/game_select.py", ["--create", createId]);
        if (status !== 0) {
            process.exit(status);
        }
        console.log(`created: game/${createId}/`);
        return;
    }

    if (importPath) {
        if (!idArg || positional.length > 1) {
            throw new Error(GAME_USAGE);
        }
        const status = runPythonChecked(repo, "tools/disc/game_select.py", [
            "--import",
            importPath,
            idArg,
        ]);
        if (status !== 0) {
            process.exit(status);
        }
        activateNonInteractive(repo, idArg, { extract: wantExtract, recomp: wantRecomp });
        return;
    }

    if (idArg) {
        if (positional.length > 1) {
            throw new Error(GAME_USAGE);
        }
        activateNonInteractive(repo, idArg, { extract: wantExtract, recomp: wantRecomp });
        return;
    }

    if (wantRecomp || wantExtract || idFlag) {
        throw new Error(GAME_USAGE);
    }

    clack.intro("NSMBU game dump");

    let state = gameList(repo);
    if (state.flat && state.versions.length > 0) {
        throw new Error(
            `game/ has both a flat code/ tree and version folders (${state.versions.map((v) => v.id).join(", ")}); ` +
                "move the flat files into a version folder (or remove the folders) then retry",
        );
    }
    if (state.flat && state.versions.length === 0) {
        if (!(await confirm("Migrate flat game/{code,content,meta} into a version folder?", true))) {
            throw new Error("migration cancelled");
        }
        const status = runPythonChecked(repo, "tools/disc/game_select.py", ["--migrate"]);
        if (status !== 0) {
            process.exit(status);
        }
        state = gameList(repo);
    }

    let chosenId: string;
    if (state.loose_images.length > 0 && state.versions.length === 0) {
        clack.log.step(`Found disc image in game/: ${state.loose_images.map((i) => i.name).join(", ")}`);
        chosenId = await importLooseImage(repo, state);
    } else if (state.versions.length === 0) {
        clack.log.step("No dumps under game/ yet.");
        clack.note(
            [
                "Drop any named .wux into game/ (plus keys), then run nsmbu game again.",
                "",
                ...keysHint(state),
            ].join("\n"),
            "How to add a dump",
        );
        if (await confirm("Create an empty version folder anyway?", false)) {
            await createEmptyDump(repo, state);
        }
        clack.outro("Drop a .wux into game/ then run nsmbu game.");
        return;
    } else {
        const options = [
            ...state.versions.map((v) => ({
                value: v.id,
                label: v.id,
                hint: versionHint(v),
            })),
            ...(state.loose_images.length > 0
                ? [
                      {
                          value: "__import",
                          label: "Import a disc image from game/…",
                          hint: state.loose_images.map((i) => i.name).join(", "),
                      },
                  ]
                : []),
            { value: "__new", label: "Add an empty dump folder…", hint: "create game/<id>/" },
        ];
        const active = state.versions.find((v) => v.active)?.id;
        chosenId = await selectOption("Which dump?", options, active);
        if (chosenId === "__import") {
            chosenId = await importLooseImage(repo, state);
        } else if (chosenId === "__new") {
            await createEmptyDump(repo, state);
        }
    }

    activateDump(repo, chosenId);
    state = gameList(repo);
    let v = state.versions.find((x) => x.id === chosenId);
    if (!v) {
        throw new Error(`missing game/${chosenId}`);
    }

    v = await maybeExtract(repo, v);
    await maybeRecomp(repo, v);

    const final = gameList(repo).versions.find((x) => x.id === chosenId);
    clack.outro(
        final?.extracted
            ? `Active dump: game/${chosenId}/ (${final.label}). Next: nsmbu build`
            : `Active dump: game/${chosenId}/. Fix keys under game/ (or drop a .wux) and run nsmbu game again.`,
    );
}

const REPLAY_NAME = /^[A-Za-z0-9._-]+$/;

function replayName(name: string): string {
    const trimmed = name.toLowerCase().endsWith(".replay") ? name.slice(0, -".replay".length) : name;
    if (!trimmed || trimmed === "." || trimmed === ".." || !REPLAY_NAME.test(trimmed)) {
        throw new Error(`bad replay name: ${name}`);
    }
    return trimmed;
}

function courseToken(arg: string): string | undefined {
    const base = arg.replace(/\\/g, "/").split("/").pop() ?? arg;
    if (base.toLowerCase().endsWith(".szs")) {
        const match = /^([1-9])-([1-9]\d*)\.szs$/i.exec(base);
        return match ? `${match[1]}-${match[2]}` : undefined;
    }
    return /^\d+-\d+$/.test(arg) ? arg : undefined;
}

export function cmdRecord(repo: string, course: string, name: string, rest: string[]): void {
    const replays = join(repo, "replays");
    mkdirSync(replays, { recursive: true });
    const replayPath = join(replays, `${replayName(name)}.replay`);
    const extra: Record<string, string> = { NSMBU_RECORD_INPUT: replayPath };
    const token = courseToken(course);
    if (token) {
        extra.NSMBU_RECORD_COURSE = token;
    }
    console.log(`[record] ${replayPath}`);
    cmdLaunch(repo, { noDebug: false, trace: false, gdb: false, lldb: false }, [course, ...rest], extra);
}

const REPLAY_PROFILE_ENV: Record<string, string> = {
    NSMBU_PROFILE: "1",
    NSMBU_VK_STATS: "1",
    NSMBU_SYNC_STATS: "1",
    NSMBU_CRASH_RECOVERY: "1",
};

export type ReplayOpts = {
    release: boolean;
    uncapped: boolean;
    noExitOnInput: boolean;
    exitAt?: string;
    log?: string;
};

function replayCourse(path: string, name: string): string {
    const text = readFileSync(path, "utf8");
    for (const line of text.split("\n")) {
        const match = /^course=(\S+)/.exec(line) ?? /^[!#]\s*course\s+(\S+)/.exec(line);
        if (match) {
            return match[1];
        }
    }
    const token = courseToken(name);
    if (token) {
        return token;
    }
    throw new Error(`${path} has no "course=" line; add one or name the replay after its course`);
}

export function cmdReplay(repo: string, name: string, opts: ReplayOpts, rest: string[]): void {
    const replayPath = join(repo, "replays", `${replayName(name)}.replay`);
    if (!existsSync(replayPath)) {
        console.error(`missing ${replayPath}`);
        process.exit(1);
    }
    const course = replayCourse(replayPath, name);
    const extra: Record<string, string> = {
        NSMBU_REPLAY_INPUT: replayPath,
    };
    for (const [key, value] of Object.entries(REPLAY_PROFILE_ENV)) {
        if (process.env[key] === undefined) {
            extra[key] = value;
        }
    }
    if (opts.uncapped) {
        extra.NSMBU_UNCAPPED = "1";
    }
    if (opts.noExitOnInput) {
        extra.NSMBU_REPLAY_EXIT = "none";
    }
    if (opts.exitAt !== undefined) {
        extra.NSMBU_EXIT_AT_FRAME = opts.exitAt;
    }
    const log = opts.log ?? join(nsmbuUserDir(repo), "logs", "nsmbu.log");
    if (opts.log) {
        mkdirSync(dirname(opts.log), { recursive: true });
        extra.NSMBU_LOG_FILE = opts.log;
    }
    console.log(`[replay] log ${log}`);
    cmdLaunch(repo, { noDebug: opts.release, trace: false, gdb: false, lldb: false }, [course, ...rest], extra);
}

export function cmdProfile(repo: string, rest: string[]): void {
    runPython(repo, "tools/run/profile.py", rest);
}

export function cmdPerf(repo: string, rest: string[]): void {
    runPython(repo, "tools/run/perf.py", rest);
}

export function cmdInstaller(repo: string, rest: string[]): void {
    runPythonInherit(repo, "tools/release/host_artifacts.py", rest);
}

export async function cmdApk(repo: string, rest: string[]): Promise<void> {
    const { flags, positional } = parseArgv(
        rest,
        ["abi", "device", "build-only", "install-only", "yes"],
        ["abi", "device"],
    );
    if (positional.length > 0) {
        throw new Error(
            "usage: nsmbu apk [--abi arm64-v8a|x86_64] [--device SERIAL] [--build-only] [--install-only] [--yes]",
        );
    }
    const buildOnly = hasFlag(flags, "build-only");
    const installOnly = hasFlag(flags, "install-only");
    if (buildOnly && installOnly) {
        throw new Error("use either --build-only or --install-only, not both");
    }
    const abi = flagString(flags, "abi");
    if (abi !== undefined && abi !== "arm64-v8a" && abi !== "x86_64") {
        throw new Error("usage: nsmbu apk --abi arm64-v8a|x86_64");
    }
    const args: string[] = [];
    if (abi) {
        args.push("--abi", abi);
    }
    let device = flagString(flags, "device");
    if (hasFlag(flags, "build-only")) {
        args.push("--build-only");
    }
    if (hasFlag(flags, "install-only")) {
        args.push("--install-only");
    }
    const yes = hasFlag(flags, "yes");
    if (!buildOnly && !installOnly && !device) {
        const listed = runPythonCapture(repo, "tools/android/apk.py", ["--list-devices"]);
        if (listed.status !== 0) {
            throw new Error((listed.stderr || listed.stdout || "adb device list failed").trim());
        }
        const devices = JSON.parse(listed.stdout) as { serial: string; model: string | null }[];
        if (devices.length === 0) {
            throw new Error("no adb device in 'device' state; plug one in and enable USB debugging");
        }
        if (devices.length === 1) {
            device = devices[0].serial;
        } else if (yes) {
            throw new Error("multiple adb devices; pass --device SERIAL or drop --yes");
        } else {
            device = await selectOption(
                "Android device",
                devices.map((d) => ({
                    value: d.serial,
                    label: d.model ? `${d.serial} (${d.model})` : d.serial,
                })),
            );
        }
    }
    if (device) {
        args.push("--device", device);
    }
    runPythonInherit(repo, "tools/android/apk.py", args);
}

export function cmdLint(repo: string): void {
    const verify = existsSync(join(repo, "modding", "node_modules", "typescript-to-lua")) ? "" : "npm --prefix modding ci && ";
    runShell(
        repo,
        `${verify}npm --prefix modding run --silent build && stylua --config-path modding/.stylua.toml modding/examples`,
    );
}

export function cmdRecomp(repo: string): void {
    const listed = runPythonCapture(repo, "tools/disc/game_select.py", ["--list"]);
    if (listed.status !== 0) {
        throw new Error((listed.stderr || listed.stdout || "game --list failed").trim());
    }
    const state = JSON.parse(listed.stdout) as GameListState;
    const id = state.active ?? state.versions.find((v) => v.active)?.id ?? state.versions[0]?.id;
    if (!id) {
        throw new Error("no active dump; run nsmbu game");
    }
    const v = state.versions.find((x) => x.id === id);
    if (!v?.extracted) {
        throw new Error(`game/${id}/ is not extracted yet; run nsmbu game`);
    }
    runPython(repo, "recomp/recomp.py", [`game/${id}/code/red-pro2.rpx`, "build/gen"]);
}

export function cmdScripted(repo: string, name: string, rest: string[]): void {
    runShell(repo, `bash tools/run/scripted-run.sh ${shellQuote(name)} ${rest.map(shellQuote).join(" ")}`.trim());
}

function shellQuote(s: string): string {
    if (/^[a-zA-Z0-9._/-]+$/.test(s)) {
        return s;
    }
    return `'${s.replace(/'/g, "'\\''")}'`;
}
