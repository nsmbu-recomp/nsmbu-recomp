interface SpawnOptions {
    param0?: number;
    param1?: number;
    layer?: number;
}

interface Level {
    active(this: void): boolean;
    time(this: void): Result<number>;
    set_time(this: void, seconds: number): Result<boolean>;
    add_time(this: void, seconds: number): Result<boolean>;
    coins(this: void): Result<number>;
    set_coins(this: void, coins: number): Result<boolean>;
    score(this: void): Result<number>;
    set_score(this: void, score: number): Result<boolean>;
    actors(this: void): Actor[];
    count(this: void, profile_id?: number): Result<number>;
    spawn(
        this: void,
        profile_id: number,
        x: number,
        y: number,
        z: number,
        options?: SpawnOptions,
    ): Result<boolean>;
}

interface Actor {
    addr: number;
    address(): Result<number>;
    id(): Result<number>;
    profile_id(): Result<number>;
    is_actor(): Result<boolean>;
    is_map_actor(): Result<boolean>;
    active(): Result<boolean>;
    layer(): Result<number>;
    player_no(): Result<number>;
    param0(): Result<number>;
    param1(): Result<number>;
    pos(): Result<Vec3>;
    set_pos(x: number | Vec3, y?: number, z?: number): Result<boolean>;
    speed(): Result<Vec3>;
    set_speed(x: number | Vec3, y?: number, z?: number): Result<boolean>;
    scale(): Result<Vec3>;
    set_scale(x: number | Vec3, y?: number, z?: number): Result<boolean>;
    delete(): Result<boolean>;
}
