interface Data {
    read(this: void, name: string): Result<string>;
    write(this: void, name: string, data: string): Result<number>;
    remove(this: void, name: string): Result<boolean>;
}
