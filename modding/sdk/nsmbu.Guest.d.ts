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
