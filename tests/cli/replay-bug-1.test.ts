import assert from "node:assert/strict";
import test from "node:test";
import { quitPlaceError } from "../../tools/cli/src/playtest/cases/replay-bug-1.ts";

test("a quit on W201 with the player on that node passes", () => {
    assert.equal(
        quitPlaceError({
            node_name: "W201",
            world: 1,
            node_x: -5500,
            node_y: 270,
            map_x: -5490,
            map_y: 270,
        }),
        null,
    );
});

test("a quit on W1M0 fails", () => {
    const err = quitPlaceError({
        node_name: "W1M0",
        world: 0,
        node_x: -5440,
        node_y: 57.5,
        map_x: -5440,
        map_y: 57.5,
    });
    assert.match(err ?? "", /W1M0/);
});

test("a player away from W201 fails", () => {
    const err = quitPlaceError({
        node_name: "W201",
        world: 1,
        node_x: -5500,
        node_y: 270,
        map_x: -2950,
        map_y: 117.5,
    });
    assert.match(err ?? "", /off W201/);
});
