const heldMode: Record<number, number | undefined> = {};
const prevSpeedY: Record<number, number | undefined> = {};
const scaledJump: Record<number, boolean | undefined> = {};

function freezeTimer(): void {
    nsmbu.Level.set_time(999);
}

function keepLives(): void {
    for (const p of nsmbu.Player.get_players()) {
        const [lives] = p.lives();
        if (lives !== undefined && lives > 0 && lives < 99) {
            p.set_lives(99);
        }
    }
}

function keepPowerup(): void {
    for (let i = 0; i < 4; i++) {
        const [p] = nsmbu.Player.get_player(i);
        let entered = false;
        if (p) {
            const [value] = p.entered();
            entered = value === true;
        }
        if (p && entered) {
            const [mode] = p.mode();
            if (mode !== undefined) {
                const held = heldMode[i];
                if (held !== undefined && held >= mode) {
                    if (mode < held && held > 0) {
                        p.set_mode(held);
                    }
                } else {
                    heldMode[i] = mode;
                }
            }
        } else {
            heldMode[i] = undefined;
        }
    }
}

function scaleJump(p: Player, i: number, scale: number): void {
    const [speed] = p.speed_y();
    if (speed === undefined) {
        prevSpeedY[i] = undefined;
        scaledJump[i] = false;
        return;
    }
    const prev = prevSpeedY[i] ?? 0;
    if (speed > 1.5 && prev <= 1.5) {
        scaledJump[i] = false;
    } else if (speed <= 0) {
        scaledJump[i] = false;
    } else if (!scaledJump[i] && speed > 1.5) {
        p.set_speed_y(speed * scale);
        scaledJump[i] = true;
    }
    prevSpeedY[i] = speed;
}

function scaleJumps(scale: number): void {
    for (let i = 0; i < 4; i++) {
        const [p] = nsmbu.Player.get_player(i);
        if (p) {
            scaleJump(p, i, scale);
        }
    }
}

nsmbu.Hooks.onLoaded = () => {};

nsmbu.Hooks.onLogicStep = (step) => {
    const [infiniteTime] = nsmbu.Config.bool("infinite_time", false);
    if (infiniteTime) {
        freezeTimer();
    }
    const [infiniteLives] = nsmbu.Config.bool("infinite_lives", false);
    if (infiniteLives) {
        keepLives();
    }
    const [keepPowerupOption] = nsmbu.Config.bool("keep_powerup", false);
    if (keepPowerupOption) {
        keepPowerup();
    }
    const [scale] = nsmbu.Config.number("jump_scale", 1);
    if (scale !== undefined && scale !== 1) {
        scaleJumps(scale);
    }
};

export {};
