interface Config {
    string(this: void, id: string, fallback: string): Result<string>;
    number(this: void, id: string, fallback: number): Result<number>;
    bool(this: void, id: string, fallback: boolean): Result<boolean>;
}
