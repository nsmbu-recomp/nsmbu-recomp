const PROFILE_IDS: Record<string, number> = {
    coin: 395,
    goomba: 467,
    mushroom: 591,
    "fire flower": 592,
    "one-up": 597,
    star: 853,
};

let spawned = 0;
let spawnedFailed = 0;

function spawnId(): number | undefined {
    const [name] = nsmbu.Config.string("spawn_kind", "off");
    if (name === undefined || name === "off") {
        return undefined;
    }
    return PROFILE_IDS[name];
}

function spawnCountdown(step: number): boolean {
    const [every] = nsmbu.Config.number("spawn_every", 2);
    return step % math.floor(math.max(every ?? 1, 1) * 30) === 0;
}

function spawnNearPlayer(): void {
    const id = spawnId();
    if (id === undefined) {
        return;
    }
    const [p] = nsmbu.Player.get_player(0);
    if (!p) {
        return;
    }
    const [entered] = p.entered();
    if (!entered) {
        return;
    }
    const [pos] = p.pos();
    if (!pos) {
        return;
    }
    const [offsetX] = nsmbu.Config.number("spawn_offset_x", 0);
    const [offsetY] = nsmbu.Config.number("spawn_offset_y", 0);
    const [done, err] = nsmbu.Level.spawn(
        id,
        pos.x + (offsetX ?? 0),
        pos.y + (offsetY ?? 0) + 32,
        pos.z,
    );
    if (done) {
        spawned = spawned + 1;
    } else {
        spawnedFailed = spawnedFailed + 1;
        if (spawnedFailed === 1) {
            nsmbu.log(err ?? "spawn failed");
        }
    }
}

function clearKindActors(): void {
    const id = spawnId();
    if (id === undefined) {
        return;
    }
    for (const a of nsmbu.Level.actors()) {
        const [pid] = a.profile_id();
        if (pid === id) {
            a.delete();
        }
    }
}

nsmbu.Hooks.onLogicStep = (step) => {
    if (!nsmbu.Level.active()) {
        return;
    }
    const [clearKind] = nsmbu.Config.bool("clear_kind", false);
    if (clearKind) {
        clearKindActors();
    }
    if (spawnCountdown(step)) {
        spawnNearPlayer();
        const [count] = nsmbu.Level.count();
        nsmbu.status(string.format("spawned %d, live %d", spawned, count ?? 0));
    }
};

nsmbu.Hooks.onUnload = () => {
    nsmbu.log_int("spawned", spawned);
};

export {};
