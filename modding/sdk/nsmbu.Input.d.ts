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
