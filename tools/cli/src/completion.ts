import { mkdirSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { BUTTON_NAMES } from "./agent/buttons.ts";

type CommandSpec = {
    name: string;
    description: string;
    flags?: { name: string; description: string; takesFile?: boolean }[];
    noFiles?: boolean;
};

const COMMANDS: CommandSpec[] = [
    {
        name: "build",
        description: "Configure and build",
        flags: [
            { name: "release", description: "Release build" },
            { name: "sanitizer", description: "Sanitizer build" },
        ],
    },
    {
        name: "launch",
        description: "Launch the game",
        flags: [
            { name: "no-debug", description: "Launch without debug binary" },
            { name: "trace", description: "Trace launch" },
            { name: "gdb", description: "Launch under gdb" },
            { name: "lldb", description: "Launch under lldb" },
        ],
    },
    {
        name: "crash",
        description: "Crash on purpose (Debug: exit non-zero, no dialog)",
        noFiles: true,
    },
    {
        name: "run",
        description: "Build then launch",
        flags: [{ name: "release", description: "Release build" }],
    },
    {
        name: "wine",
        description: "Build Windows nsmbu.exe then launch under Wine",
        flags: [
            { name: "no-debug", description: "Release build without debug env" },
            { name: "trace", description: "Trace launch" },
        ],
    },
    {
        name: "game",
        description: "Set up / select dump, extract, optional recomp",
        noFiles: true,
        flags: [
            { name: "list", description: "Print dump JSON (no TUI)" },
            { name: "extract", description: "Extract (or re-extract) after activating" },
            { name: "recomp", description: "Recomp after activating" },
            { name: "import", description: "Import a .wux/.wud from PATH", takesFile: true },
            { name: "id", description: "Version folder id (with --import)" },
            { name: "create", description: "Create an empty version folder" },
        ],
    },
    { name: "extract", description: "Extract the active dump (prefer nsmbu game)", noFiles: true },
    {
        name: "save",
        description: "Convert save data to JSON and back",
        flags: [{ name: "output", description: "Write to PATH", takesFile: true }],
    },
    { name: "record", description: "Record a course replay", noFiles: true },
    {
        name: "replay",
        description: "Replay a recorded course",
        noFiles: true,
        flags: [
            { name: "release", description: "Release binary" },
            { name: "uncapped", description: "Uncapped frame rate" },
            { name: "no-exit-on-input", description: "Keep running after the recorded input ends" },
            { name: "exit-at", description: "Exit at frame N" },
            { name: "log", description: "Write log to PATH", takesFile: true },
        ],
    },
    { name: "profile", description: "Profiling helpers" },
    { name: "perf", description: "Perf helpers" },
    {
        name: "installer",
        description: "Build the installer",
        flags: [
            { name: "version", description: "Release version string" },
            { name: "no-open", description: "Stop after the package is ready" },
        ],
    },
    {
        name: "apk",
        description: "Build an Android APK and install on a device",
        noFiles: true,
        flags: [
            { name: "abi", description: "arm64-v8a or x86_64" },
            { name: "device", description: "adb serial" },
            { name: "build-only", description: "Build without adb" },
            { name: "install-only", description: "Install the last APK only" },
            { name: "yes", description: "Skip prompts (needs --device if several devices)" },
        ],
    },
    { name: "lint", description: "Build TypeScript mods and format the Lua", noFiles: true },
    { name: "recomp", description: "Run recomp only", noFiles: true },
    { name: "scripted", description: "Headless scripted boot", noFiles: true },
    {
        name: "test",
        description: "Run playtests",
        noFiles: true,
        flags: [{ name: "list", description: "List playtests" }],
    },
    {
        name: "bench",
        description: "Run performance benchmarks",
        noFiles: true,
        flags: [{ name: "list", description: "List visual benchmarks" }],
    },
    { name: "open", description: "Open agent-controlled game" },
    { name: "status", description: "Agent status", noFiles: true },
    { name: "press", description: "Pulse a button", noFiles: true },
    { name: "stick", description: "Pulse left stick", noFiles: true },
    { name: "rstick", description: "Pulse right stick", noFiles: true },
    { name: "screenshot", description: "Capture a PNG" },
    { name: "wait", description: "Wait for frame, step, or scene", noFiles: true },
    { name: "quit", description: "Quit the agent game", noFiles: true },
    { name: "close", description: "Quit the agent game", noFiles: true },
    { name: "completion", description: "Print or install shell completions", noFiles: true },
];

function fishEscape(s: string): string {
    return s.replace(/'/g, "\\'");
}

function fishCompletionScript(): string {
    const buttons = BUTTON_NAMES.map((b) => fishEscape(b)).join(" ");
    const lines: string[] = [
        "# fish completions for nsmbu — generated by `nsmbu completion fish`",
        "",
        "function __nsmbu_seen",
        "    set -l cmd (commandline -opc)",
        "    set -e cmd[1]",
        "    contains -- $argv[1] $cmd",
        "end",
        "",
        "function __nsmbu_no_subcommand",
        "    set -l cmd (commandline -opc)",
        "    set -e cmd[1]",
        "    for c in $cmd",
        "        switch $c",
        "            case '-*'",
        "                continue",
        "            case '*'",
        "                return 1",
        "        end",
        "    end",
        "    return 0",
        "end",
        "",
        "function __nsmbu_replay_courses",
        "    set -l dir (pwd)",
        "    while test \"$dir\" != /",
        "        if test -d \"$dir/replays\"",
        "            for f in \"$dir\"/replays/*.replay",
        "                test -e \"$f\"; or continue",
        "                string replace -r '\\.replay$' '' (path basename \"$f\")",
        "            end",
        "            return",
        "        end",
        "        set dir (path dirname \"$dir\")",
        "    end",
        "end",
        "",
        "complete -c nsmbu -f",
        "complete -c nsmbu -n '__nsmbu_no_subcommand' -l help -s h -d 'Show help'",
        "",
    ];

    for (const cmd of COMMANDS) {
        lines.push(
            `complete -c nsmbu -n '__nsmbu_no_subcommand' -a '${fishEscape(cmd.name)}' -d '${fishEscape(cmd.description)}'`,
        );
    }
    lines.push("");

    for (const cmd of COMMANDS) {
        if (cmd.noFiles) {
            lines.push(`complete -c nsmbu -n '__nsmbu_seen ${cmd.name}' -f`);
        }
        for (const flag of cmd.flags ?? []) {
            const file = flag.takesFile ? " -r -F" : " -f";
            lines.push(
                `complete -c nsmbu -n '__nsmbu_seen ${cmd.name}' -l ${flag.name}${file} -d '${fishEscape(flag.description)}'`,
            );
        }
    }

    lines.push(
        "",
        "complete -c nsmbu -n '__nsmbu_seen record' -a '(__nsmbu_replay_courses)' -d 'Course'",
        "complete -c nsmbu -n '__nsmbu_seen replay' -a '(__nsmbu_replay_courses)' -d 'Replay'",
        "complete -c nsmbu -n '__nsmbu_seen launch' -a '(__nsmbu_replay_courses)' -d 'Course'",
        "complete -c nsmbu -n '__nsmbu_seen run' -a '(__nsmbu_replay_courses)' -d 'Course'",
        "complete -c nsmbu -n '__nsmbu_seen wine' -a '(__nsmbu_replay_courses)' -d 'Course'",
        "complete -c nsmbu -n '__nsmbu_seen open' -a '(__nsmbu_replay_courses)' -d 'Course'",
        "complete -c nsmbu -n '__nsmbu_seen open' -a '(for f in game/content/Common/course_res_pack/*.szs; test -e $f; and echo $f; end)' -d 'Course file'",
        "complete -c nsmbu -n '__nsmbu_seen open' -r -F",
        "",
        `complete -c nsmbu -n '__nsmbu_seen press' -a '${buttons}' -d 'Button'`,
        "complete -c nsmbu -n '__nsmbu_seen press' -l frames -r -f -d 'Pulse frames'",
        "complete -c nsmbu -n '__nsmbu_seen stick' -l frames -r -f -d 'Pulse frames'",
        "complete -c nsmbu -n '__nsmbu_seen rstick' -l frames -r -f -d 'Pulse frames'",
        "",
        "complete -c nsmbu -n '__nsmbu_seen wait' -l frame -r -f -d 'Wait until frame N'",
        "complete -c nsmbu -n '__nsmbu_seen wait' -l step -r -f -d 'Wait until logic step N'",
        "complete -c nsmbu -n '__nsmbu_seen wait' -l scene -r -f -d 'Wait until scene name'",
        "complete -c nsmbu -n '__nsmbu_seen wait' -l timeout -r -f -d 'Timeout seconds'",
        "complete -c nsmbu -n '__nsmbu_seen wait; and __fish_seen_argument -l scene' -f -a 'Boot Flow FileSelect SaveSelect Title Menu Map CourseTask CourseSelect' -d 'Scene'",
        "",
        "complete -c nsmbu -n '__nsmbu_seen save' -r -F",
        "",
        "complete -c nsmbu -n '__nsmbu_seen screenshot' -r -F",
        "",
        "complete -c nsmbu -n '__nsmbu_seen completion' -a 'fish' -d 'Shell'",
        "complete -c nsmbu -n '__nsmbu_seen completion' -l install -f -d 'Install into fish completions dir'",
        "",
    );

    return lines.join("\n");
}

function fishInstallPath(): string {
    const xdg = process.env.XDG_CONFIG_HOME;
    const config = xdg && xdg.length > 0 ? xdg : join(homedir(), ".config");
    return join(config, "fish", "completions", "nsmbu.fish");
}

export function cmdCompletion(argv: string[]): void {
    const shell = argv[0];
    if (!shell || shell === "--help" || shell === "-h") {
        throw new Error("usage: nsmbu completion <shell> [--install]\nsupported shells: fish");
    }
    if (shell !== "fish") {
        throw new Error(`unsupported shell: ${shell}\nsupported shells: fish`);
    }
    const install = argv.includes("--install");
    const script = fishCompletionScript();
    if (!install) {
        process.stdout.write(script);
        return;
    }
    const path = fishInstallPath();
    mkdirSync(join(path, ".."), { recursive: true });
    writeFileSync(path, script);
    console.log(`installed fish completions: ${path}`);
    console.log("new fish sessions pick them up automatically; in this shell run: source " + path);
}
