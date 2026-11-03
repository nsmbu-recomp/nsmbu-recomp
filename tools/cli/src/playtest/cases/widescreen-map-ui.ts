import { inflateSync } from "node:zlib";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { type PlayCase } from "../helpers.ts";

export function avgBrightness(img: { rgb: Uint8Array }): number {
    let sum = 0;
    const n = img.rgb.length / 3;
    for (let i = 0; i < img.rgb.length; i += 3) {
        sum += img.rgb[i] + img.rgb[i + 1] + img.rgb[i + 2];
    }
    return n ? sum / n : 0;
}

export function readPng(path: string): { w: number; h: number; rgb: Uint8Array } {
    const file = readFileSync(path);
    if (file.length < 8 || file[0] !== 137) throw new Error(`not a png: ${path}`);
    let w = 0;
    let h = 0;
    let channels = 3;
    let off = 8;
    const idat: Buffer[] = [];
    while (off + 12 <= file.length) {
        const len = file.readUInt32BE(off);
        const type = file.toString("ascii", off + 4, off + 8);
        const data = file.subarray(off + 8, off + 8 + len);
        if (type === "IHDR") {
            w = data.readUInt32BE(0);
            h = data.readUInt32BE(4);
            const depth = data[8];
            const color = data[9];
            if (depth !== 8 || (color !== 2 && color !== 6) || data[12] !== 0) {
                throw new Error(`unsupported png depth ${depth} color ${color}`);
            }
            channels = color === 6 ? 4 : 3;
        } else if (type === "IDAT") {
            idat.push(Buffer.from(data));
        } else if (type === "IEND") {
            break;
        }
        off += 12 + len;
    }
    if (!w || !h || idat.length === 0) throw new Error(`empty png: ${path}`);
    const raw = inflateSync(Buffer.concat(idat));
    const srcStride = w * channels;
    const stride = w * 3;
    const rgb = new Uint8Array(stride * h);
    let prev = new Uint8Array(srcStride);
    const src = new Uint8Array(srcStride);
    let p = 0;
    for (let y = 0; y < h; y++) {
        const filter = raw[p++];
        for (let i = 0; i < srcStride; i++) {
            const v = raw[p++];
            const a = i >= channels ? src[i - channels] : 0;
            const b = prev[i];
            const c = i >= channels ? prev[i - channels] : 0;
            let pr = 0;
            if (filter === 1) pr = a;
            else if (filter === 2) pr = b;
            else if (filter === 3) pr = (a + b) >> 1;
            else if (filter === 4) {
                const guess = a + b - c;
                const pa = Math.abs(guess - a);
                const pb = Math.abs(guess - b);
                const pc = Math.abs(guess - c);
                pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
            } else if (filter !== 0) {
                throw new Error(`png filter ${filter}`);
            }
            src[i] = (v + pr) & 255;
        }
        const row = rgb.subarray(y * stride, (y + 1) * stride);
        for (let x = 0; x < w; x++) {
            row[x * 3] = src[x * channels];
            row[x * 3 + 1] = src[x * channels + 1];
            row[x * 3 + 2] = src[x * channels + 2];
        }
        prev = src.slice();
    }
    return { w, h, rgb };
}

function blue(rgb: Uint8Array, stride: number, x: number, y: number): boolean {
    const i = y * stride + x * 3;
    const r = rgb[i];
    const g = rgb[i + 1];
    const b = rgb[i + 2];
    return b > 150 && r < 90 && g > 70 && g < 170 && b > r + 60 && b > g + 20;
}

function buttonEdges(img: { w: number; h: number; rgb: Uint8Array }): { left: number; right: number } | null {
    const stride = img.w * 3;
    const y0 = Math.floor(img.h * 0.7);
    const y1 = Math.floor(img.h * 0.98);
    let left = img.w;
    let right = 0;
    for (let y = y0; y < y1; y += 2) {
        let x = 0;
        while (x < img.w) {
            if (!blue(img.rgb, stride, x, y)) {
                x += 2;
                continue;
            }
            const x0 = x;
            while (x < img.w && blue(img.rgb, stride, x, y)) x += 2;
            if (x - x0 < 36) continue;
            if (x0 < img.w / 2) left = Math.min(left, x0);
            if (x > img.w / 2) right = Math.max(right, x);
        }
    }
    if (left === img.w || right === 0) return null;
    return { left, right };
}

function pillSpan(
    img: { w: number; h: number; rgb: Uint8Array },
    y0: number,
    y1: number,
    side: "left" | "right",
): { x0: number; x1: number; y0: number; y1: number } | null {
    const stride = img.w * 3;
    let x0 = img.w;
    let x1 = 0;
    let top = img.h;
    let bot = 0;
    for (let y = y0; y < y1; y += 2) {
        let x = 0;
        while (x < img.w) {
            if (!blue(img.rgb, stride, x, y)) {
                x += 2;
                continue;
            }
            const start = x;
            while (x < img.w && blue(img.rgb, stride, x, y)) x += 2;
            if (x - start < 80) continue;
            const onSide = side === "left" ? start < img.w / 2 : x > img.w / 2;
            if (!onSide) continue;
            x0 = Math.min(x0, start);
            x1 = Math.max(x1, x);
            top = Math.min(top, y);
            bot = Math.max(bot, y);
        }
    }
    if (x1 - x0 < 80) return null;
    return { x0, x1, y0: top, y1: bot };
}

function labelInside(img: { w: number; h: number; rgb: Uint8Array }, pill: { x0: number; x1: number; y0: number; y1: number }, side: "left" | "right"): boolean {
    const stride = img.w * 3;
    const pad = 90;
    const from = Math.max(0, pill.x0 - (side === "left" ? pad : 8));
    const to = Math.min(img.w, pill.x1 + (side === "right" ? pad : 8));
    const xs: number[] = [];
    let outside = 0;
    for (let y = pill.y0; y <= pill.y1; y++) {
        for (let x = from; x < to; x++) {
            const i = y * stride + x * 3;
            const r = img.rgb[i];
            const g = img.rgb[i + 1];
            const b = img.rgb[i + 2];
            if (r < 210 || g < 210 || b < 180) continue;
            xs.push(x);
            if (side === "left" ? x < pill.x0 : x >= pill.x1) outside++;
        }
    }
    if (xs.length < 25) return false;
    xs.sort((a, b) => a - b);
    const mid = xs[xs.length >> 1];
    return mid > pill.x0 && mid < pill.x1 && outside < xs.length * 0.2;
}

function worldNameInset(img: { w: number; h: number; rgb: Uint8Array }): number | null {
    const stride = img.w * 3;
    const y0 = Math.floor(img.h * 0.05);
    const y1 = Math.floor(img.h * 0.16);
    const x1 = Math.floor(img.w * 0.28);
    let best = img.w;
    let columns = 0;
    for (let x = 0; x < x1; x++) {
        let n = 0;
        for (let y = y0; y < y1; y += 2) {
            const i = y * stride + x * 3;
            if (img.rgb[i] < 35 && img.rgb[i + 1] < 35 && img.rgb[i + 2] < 35) n++;
        }
        if (n >= 6) {
            best = Math.min(best, x);
            columns++;
        }
    }
    if (columns < 8) return null;
    return best;
}

export const widescreenMapUi: PlayCase = {
    name: "widescreen-map-ui",
    summary: "21:9 world map keeps Map View, Items, and their labels on the screen edges.",
    skip: "21:9 map screenshot timing; tracked for re-enable",
    run: async (session) => {
        await session.start({
            save: "repo",
            env: { NSMBU_ASPECT: "21:9", NSMBU_HIDDEN_WINDOWS: "0" },
        });
        await session.advance(180, 45);
        await session.eval("return nsmbu.Scene.open_first_save(0)");
        await session.advance(90, 30);
        const path = join(session.workDir, "map.png");
        let edges: { left: number; right: number } | null = null;
        let img = { w: 0, h: 0, rgb: new Uint8Array() };
        let limit = 0;
        let labelsOk = false;
        for (let i = 0; i < 12; i++) {
            await session.advance(60, 20);
            await session.screenshot(path);
            const ready = Date.now() + 5000;
            for (;;) {
                try {
                    img = readPng(path);
                    break;
                } catch (err) {
                    if (Date.now() > ready) throw err;
                    await new Promise((r) => setTimeout(r, 50));
                }
            }
            if (avgBrightness(img) < 4) continue;
            edges = buttonEdges(img);
            limit = img.w * 0.12;
            if (!edges || edges.left > limit || img.w - edges.right > limit) continue;
            const view = pillSpan(img, Math.floor(img.h * 0.7), Math.floor(img.h * 0.98), "left");
            const items = pillSpan(img, Math.floor(img.h * 0.7), Math.floor(img.h * 0.98), "right");
            const menu = pillSpan(img, Math.floor(img.h * 0.02), Math.floor(img.h * 0.18), "right");
            labelsOk = !!(
                view && labelInside(img, view, "left") &&
                items && labelInside(img, items, "right") &&
                menu && labelInside(img, menu, "right")
            );
            if (labelsOk) break;
        }
        if (!edges) throw new Error(`map buttons not found in ${path}`);
        if (edges.left > limit || img.w - edges.right > limit) {
            throw new Error(
                `map buttons inset at 21:9: left ${edges.left}px right ${img.w - edges.right}px of ${img.w} (limit ${Math.round(limit)})`,
            );
        }
        if (!labelsOk) throw new Error(`map button labels are outside their buttons in ${path}`);
        const name = worldNameInset(img);
        if (name != null && name < img.w * 0.012) {
            throw new Error(`world name is clipped at the left edge (${name}px of ${img.w}) in ${path}`);
        }
    },
};
