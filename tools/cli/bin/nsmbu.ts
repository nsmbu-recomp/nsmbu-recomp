#!/usr/bin/env node
import { findRepoRoot } from "../src/repo.ts";
import { HELP } from "../src/help.ts";
import { flagString, hasFlag, parseArgv } from "../src/parse.ts";
import {
    cmdApk,
    cmdBuild,
    cmdCrash,
    cmdExtract,
    cmdGame,
    cmdInstaller,
    cmdLaunch,
    cmdLint,
    cmdPerf,
    cmdProfile,
    cmdRecomp,
    cmdRecord,
    cmdReplay,
    cmdRun,
    cmdScripted,
    cmdWine,
} from "../src/dev.ts";
import {
    cmdOpen,
    cmdPress,
    cmdQuit,
    cmdScreenshot,
    cmdStatus,
    cmdStick,
    cmdWait,
} from "../src/agent/commands.ts";
import { cmdCompletion } from "../src/completion.ts";
import { cmdSave } from "../src/save.ts";
import { cmdBench } from "../src/bench/cmd.ts";
import { cmdTest } from "../src/playtest/cmd.ts";

async function main(): Promise<void> {
    const argv = process.argv.slice(2);
    if (argv.length === 0 || argv[0] === "--help" || argv[0] === "-h") {
        console.log(HELP.trimEnd());
        return;
    }
    const [cmd, ...rest] = argv;
    if (cmd === "completion") {
        try {
            cmdCompletion(rest);
        } catch (e) {
            const msg = e instanceof Error ? e.message : String(e);
            console.error(msg);
            process.exit(1);
        }
        return;
    }
    const repo = findRepoRoot();

    try {
        switch (cmd) {
            case "build": {
                const { flags, positional } = parseArgv(rest, ["release", "sanitizer"], []);
                cmdBuild(repo, hasFlag(flags, "release"), hasFlag(flags, "sanitizer"), positional);
                break;
            }
            case "launch": {
                const { flags, positional } = parseArgv(rest, ["no-debug", "trace", "gdb", "lldb"], []);
                cmdLaunch(
                    repo,
                    {
                        noDebug: hasFlag(flags, "no-debug"),
                        trace: hasFlag(flags, "trace"),
                        gdb: hasFlag(flags, "gdb"),
                        lldb: hasFlag(flags, "lldb"),
                    },
                    positional,
                );
                break;
            }
            case "crash":
                cmdCrash(repo);
                break;
            case "run": {
                const { flags, positional } = parseArgv(rest, ["release"], []);
                cmdRun(repo, hasFlag(flags, "release"), positional);
                break;
            }
            case "wine": {
                const { flags, positional } = parseArgv(rest, ["no-debug", "trace"], []);
                cmdWine(
                    repo,
                    {
                        noDebug: hasFlag(flags, "no-debug"),
                        trace: hasFlag(flags, "trace"),
                    },
                    positional,
                );
                break;
            }
            case "extract":
                cmdExtract(repo, rest);
                break;
            case "game":
                await cmdGame(repo, rest);
                break;
            case "save": {
                const { flags, positional } = parseArgv(rest, ["output"], ["output"]);
                const [input, output] = positional;
                if (!input) {
                    throw new Error("usage: nsmbu save <save.dat|save.json> [output] [--output PATH]");
                }
                if (positional.length > 2) {
                    throw new Error("usage: nsmbu save <save.dat|save.json> [output] [--output PATH]");
                }
                cmdSave(input, flagString(flags, "output") ?? output);
                break;
            }
            case "record": {
                const [course, ...tail] = rest;
                if (!course) {
                    throw new Error("usage: nsmbu record <course> [name] […]");
                }
                const named = tail.length > 0 && !tail[0].startsWith("-");
                const name = named ? tail[0] : course;
                cmdRecord(repo, course, name, named ? tail.slice(1) : tail);
                break;
            }
            case "replay": {
                const { flags, positional } = parseArgv(
                    rest,
                    ["release", "uncapped", "exit-at", "log", "no-exit-on-input"],
                    ["exit-at", "log"],
                );
                const [name, ...launchRest] = positional;
                if (!name) {
                    throw new Error(
                        "usage: nsmbu replay [--release] [--uncapped] [--no-exit-on-input] [--exit-at N] [--log PATH] <name> […]",
                    );
                }
                const exitAt = flagString(flags, "exit-at");
                if (hasFlag(flags, "exit-at") && (exitAt === undefined || !/^\d+$/.test(exitAt))) {
                    throw new Error("usage: nsmbu replay --exit-at N");
                }
                const log = flagString(flags, "log");
                if (hasFlag(flags, "log") && !log) {
                    throw new Error("usage: nsmbu replay --log PATH");
                }
                cmdReplay(
                    repo,
                    name,
                    {
                        release: hasFlag(flags, "release"),
                        uncapped: hasFlag(flags, "uncapped"),
                        noExitOnInput: hasFlag(flags, "no-exit-on-input"),
                        exitAt,
                        log,
                    },
                    launchRest,
                );
                break;
            }
            case "profile":
                cmdProfile(repo, rest);
                break;
            case "perf":
                cmdPerf(repo, rest);
                break;
            case "installer":
                cmdInstaller(repo, rest);
                break;
            case "apk":
                await cmdApk(repo, rest);
                break;
            case "lint":
                cmdLint(repo);
                break;
            case "recomp":
                cmdRecomp(repo);
                break;
            case "scripted": {
                const [name, ...scriptRest] = rest;
                if (!name) {
                    throw new Error("usage: nsmbu scripted <name> […]");
                }
                cmdScripted(repo, name, scriptRest);
                break;
            }
            case "test":
                await cmdTest(repo, rest);
                break;
            case "bench":
                await cmdBench(repo, rest);
                break;
            case "open":
                await cmdOpen(repo, rest);
                break;
            case "status":
                await cmdStatus(repo);
                break;
            case "press":
                await cmdPress(repo, rest);
                break;
            case "stick":
                await cmdStick(repo, rest, false);
                break;
            case "rstick":
                await cmdStick(repo, rest, true);
                break;
            case "screenshot":
                await cmdScreenshot(repo, rest);
                break;
            case "wait":
                await cmdWait(repo, rest);
                break;
            case "quit":
            case "close":
                await cmdQuit(repo);
                break;
            default:
                console.error(`unknown command: ${cmd}\n`);
                console.log(HELP.trimEnd());
                process.exit(1);
        }
    } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        console.error(msg);
        process.exit(1);
    }
}

await main();
