interface Player {
    index: number;
    entered(): Result<boolean>;
    lives(): Result<number>;
    set_lives(lives: number): Result<boolean>;
    mode(): Result<number>;
    set_mode(mode: number): Result<boolean>;
    coins(): Result<number>;
    set_coins(coins: number): Result<boolean>;
    speed_y(): Result<number>;
    set_speed_y(speed: number): Result<boolean>;
    pos(): Result<Vec3>;
    set_pos(x: number | Vec3, y?: number, z?: number): Result<boolean>;
    speed(): Result<Vec3>;
    set_speed(x: number | Vec3, y?: number, z?: number): Result<boolean>;
    grounded(): Result<boolean>;
    actor(): Result<number>;
}

interface Players {
    get_player(this: void, index: number): Result<Player>;
    get_players(this: void): Player[];
}
