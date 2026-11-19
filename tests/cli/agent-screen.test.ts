import assert from "node:assert/strict";
import test from "node:test";
import { formatStatus } from "../../tools/cli/src/agent/commands.ts";
import { filterLog, formatScreen, logTags, screenKind, type ScreenFacts } from "../../tools/cli/src/agent/screen.ts";

const player = { x: 1, y: 2, z: 3 };

test("a course with a player stays a course when scene still says Map", () => {
    const facts: ScreenFacts = {
        scene: "Map",
        map: true,
        course: true,
        pack: "1-1",
        player,
    };
    assert.equal(screenKind(facts), "course");
    assert.equal(formatScreen(facts), "screen=course pack=1-1");
    assert.ok(logTags("course").includes("input"));
});

test("the loading card stays loading while the map is still reported", () => {
    const facts: ScreenFacts = { scene: "Map", map: true, entering: "1-1" };
    assert.equal(formatScreen(facts), "screen=loading pack=1-1");
});

test("file select, title, and save files use the picture name", () => {
    assert.equal(formatScreen({ scene: "FileSelect" }), "screen=gamepad");
    assert.equal(formatScreen({ scene: "Title" }), "screen=title");
    assert.equal(formatScreen({ scene: "Menu" }), "screen=saves");
});

test("the start menu names the selected button", () => {
    const facts: ScreenFacts = { scene: "CourseSelect", map: true, menu: true, cursorName: "Settings" };
    assert.equal(formatScreen(facts), "screen=start-menu cursor=Settings");
});

test("the pause row and the controller page under settings are named", () => {
    assert.equal(
        formatScreen({ scene: "CourseTask", course: true, player, pause: true, pauseRow: "Continue" }),
        "screen=pause row=Continue",
    );
    assert.equal(
        formatScreen({
            scene: "CourseTask",
            course: true,
            player,
            overlay: true,
            tab: "Video",
            controller: true,
        }),
        "screen=settings tab=Video behind=controller",
    );
});

test("a locked map is the iris and an open map names the node", () => {
    assert.equal(formatScreen({ map: true, map_lock: 1, node_name: "W1M0" }), "screen=world-map-iris name=W1M0");
    assert.equal(formatScreen({ map: true, map_lock: 0, node_name: "W101" }), "screen=world-map name=W101");
});

test("the status line ends with the picture", () => {
    const line = formatStatus({
        id: 1,
        ok: true,
        frame: 10,
        step: 20,
        scene: "Map",
        map: true,
        course: true,
        pack: "1-1",
        player,
    });
    assert.match(line, /scene=Map /);
    assert.match(line, /screen=course pack=1-1$/);
});

test("log lines follow the current picture", () => {
    const lines = [
        "[scene] Map",
        "[vulkan] swap",
        "[prof] 16ms",
        "[pause] Settings opens the overlay",
        "[overlay] opened",
    ];
    const kept = filterLog(lines, logTags("settings"), 40);
    assert.deepEqual(kept, ["[scene] Map", "[pause] Settings opens the overlay", "[overlay] opened"]);
});
