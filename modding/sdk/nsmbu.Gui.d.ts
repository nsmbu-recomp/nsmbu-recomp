interface Gui {
    clear(this: void): Result<boolean>;
    rect(
        this: void,
        x: number,
        y: number,
        w: number,
        h: number,
        color: number,
        thickness?: number,
        rounding?: number,
    ): Result<boolean>;
    rect_filled(
        this: void,
        x: number,
        y: number,
        w: number,
        h: number,
        color: number,
        rounding?: number,
    ): Result<boolean>;
    line(
        this: void,
        x0: number,
        y0: number,
        x1: number,
        y1: number,
        color: number,
        thickness?: number,
    ): Result<boolean>;
    circle(
        this: void,
        x: number,
        y: number,
        radius: number,
        color: number,
        thickness?: number,
    ): Result<boolean>;
    circle_filled(this: void, x: number, y: number, radius: number, color: number): Result<boolean>;
    triangle_filled(
        this: void,
        x0: number,
        y0: number,
        x1: number,
        y1: number,
        x2: number,
        y2: number,
        color: number,
    ): Result<boolean>;
    text(this: void, x: number, y: number, text: string, color: number, scale?: number): Result<boolean>;
}
