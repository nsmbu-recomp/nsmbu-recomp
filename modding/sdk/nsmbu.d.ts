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
    Gui: Gui;
    Data: Data;
    Enemy: Enemy;
    Boss: Boss;
    Blocks: Blocks;
    Env: Env;
    Level: Level;
    Player: Players;
    Scene: Scene;
}

declare const nsmbu: Nsmbu;
