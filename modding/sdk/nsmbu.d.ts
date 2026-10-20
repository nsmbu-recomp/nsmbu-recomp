/// <reference types="lua-types/5.1" />
/// <reference types="@typescript-to-lua/language-extensions" />

type GuestAddr = number;

type Result<T> = LuaMultiReturn<[T | undefined, string | undefined]>;

interface Vec3 {
    x: number;
    y: number;
    z: number;
}

interface Package {
    id: string;
    version: string;
    path: string;
}

interface Config {
    string(this: void, id: string, fallback: string): Result<string>;
    number(this: void, id: string, fallback: number): Result<number>;
    bool(this: void, id: string, fallback: boolean): Result<boolean>;
}

interface Guest {
    read_u8(this: void, addr: GuestAddr): Result<number>;
    read_s8(this: void, addr: GuestAddr): Result<number>;
    read_u16(this: void, addr: GuestAddr): Result<number>;
    read_s16(this: void, addr: GuestAddr): Result<number>;
    read_u32(this: void, addr: GuestAddr): Result<number>;
    read_s32(this: void, addr: GuestAddr): Result<number>;
    read_f32(this: void, addr: GuestAddr): Result<number>;
    read_f64(this: void, addr: GuestAddr): Result<number>;
    read(this: void, addr: GuestAddr, size: number): Result<string>;
    write_u8(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_s8(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_u16(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_s16(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_u32(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_s32(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_f32(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write_f64(this: void, addr: GuestAddr, value: number): Result<boolean>;
    write(this: void, addr: GuestAddr, data: string): Result<boolean>;
}

type Fn = Record<string, GuestAddr | undefined>;

interface SceneFactory {
    Boot: number;
    Flow: number;
    FileSelect: number;
    SaveSelect: number;
    Title: number;
    Menu: number;
    Map: number;
    CourseTask: number;
    CourseSelect: number;
}

interface Scene {
    Factory: SceneFactory;
    title(this: void): Result<boolean>;
    file_select(this: void): Result<boolean>;
    save_select(this: void): Result<boolean>;
    course_task(this: void): Result<boolean>;
    file_index(this: void): Result<number>;
    flow_phase(this: void): Result<boolean>;
    map_phase(this: void): Result<boolean>;
    phase(this: void): Result<number>;
    course_select(this: void): Result<boolean>;
    course(this: void): Result<boolean>;
    open_title(this: void): Result<boolean>;
    apply_save(this: void, file?: number): Result<boolean>;
    open_first_save(this: void, file?: number): Result<boolean>;
    open_file_select(this: void): Result<boolean>;
    open_course_select(this: void): Result<boolean>;
    confirm_file(this: void): Result<boolean>;
    load_story(this: void, world_no: number, course_no: number): Result<boolean>;
    load_pack(this: void, name: string): Result<boolean>;
}

type ButtonName =
    | "A"
    | "B"
    | "X"
    | "Y"
    | "L"
    | "R"
    | "ZL"
    | "ZR"
    | "Plus"
    | "Minus"
    | "Home"
    | "Up"
    | "Down"
    | "Left"
    | "Right"
    | "StickL"
    | "StickR";

interface Pad {
    buttons: number;
    lx: number;
    ly: number;
    rx: number;
    ry: number;
    touch: boolean;
    tx: number;
    ty: number;
}

interface Button {
    A: number;
    B: number;
    X: number;
    Y: number;
    L: number;
    R: number;
    ZL: number;
    ZR: number;
    Plus: number;
    Minus: number;
    Home: number;
    Up: number;
    Down: number;
    Left: number;
    Right: number;
    StickL: number;
    StickR: number;
    held(this: void, name: ButtonName): Result<boolean>;
    pressed(this: void, name: ButtonName): Result<boolean>;
}

interface Input {
    (): Pad;
    button: Button;
    press(this: void, buttons: string | string[], steps: number): Result<boolean>;
}

interface Data {
    read(this: void, name: string): Result<string>;
    write(this: void, name: string, data: string): Result<number>;
    remove(this: void, name: string): Result<boolean>;
}

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

interface Hooks {
    onLogicStep?: (this: void, step: number) => void;
    onSceneChange?: (this: void, factory: number) => void;
    onConfigChanged?: (this: void) => void;
    onLoaded?: (this: void) => void;
    onUnload?: (this: void) => void;
}

type Empty = Record<string, never>;

interface Nsmbu {
    Package: Package;
    log(this: void, message: string): Result<boolean>;
    log_int(this: void, label: string, value: number): Result<boolean>;
    log_hex(this: void, label: string, value: number): Result<boolean>;
    log_float(this: void, label: string, value: number): Result<boolean>;
    status(this: void, message: string): Result<boolean>;
    logic_step(this: void): number;
    logic_dt(this: void): number;
    Config: Config;
    Guest: Guest;
    Fn: Fn;
    Hooks: Hooks;
    Input: Input;
    Data: Data;
    Enemy: Empty;
    Boss: Empty;
    Blocks: Empty;
    Env: Empty;
    Level: Level;
    Player: Players;
    Scene: Scene;
}

declare const nsmbu: Nsmbu;
