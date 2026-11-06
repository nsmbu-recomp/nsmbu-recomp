import { inflateSync } from "node:zlib";

export type Image = { w: number; h: number; rgba: Uint8Array };

export type Rect = { x: number; y: number; w: number; h: number };

export function decodePng(buf: Buffer): Image {
    if (buf.length < 8 || buf.readUInt32BE(0) !== 0x89504e47) throw new Error("not a png");
    let w = 0;
    let h = 0;
    let color = 0;
    let depth = 0;
    let interlace = 0;
    const idat: Buffer[] = [];
    let o = 8;
    while (o + 12 <= buf.length) {
        const len = buf.readUInt32BE(o);
        const type = buf.toString("ascii", o + 4, o + 8);
        const data = buf.subarray(o + 8, o + 8 + len);
        if (type === "IHDR") {
            w = data.readUInt32BE(0);
            h = data.readUInt32BE(4);
            depth = data[8];
            color = data[9];
            interlace = data[12];
        } else if (type === "IDAT") {
            idat.push(Buffer.from(data));
        } else if (type === "IEND") {
            break;
        }
        o += 12 + len;
    }
    if (w < 1 || h < 1 || depth !== 8 || interlace !== 0 || (color !== 6 && color !== 2)) {
        throw new Error("unsupported png");
    }
    const bpp = color === 6 ? 4 : 3;
    const raw = inflateSync(Buffer.concat(idat));
    const stride = w * bpp;
    const rgba = new Uint8Array(w * h * 4);
    let prev = new Uint8Array(stride);
    let p = 0;
    for (let y = 0; y < h; y++) {
        const filter = raw[p++];
        const row = raw.subarray(p, p + stride);
        p += stride;
        const cur = new Uint8Array(stride);
        for (let i = 0; i < stride; i++) {
            const left = i >= bpp ? cur[i - bpp] : 0;
            const up = prev[i];
            const ul = i >= bpp ? prev[i - bpp] : 0;
            let v = row[i];
            if (filter === 1) v = (v + left) & 255;
            else if (filter === 2) v = (v + up) & 255;
            else if (filter === 3) v = (v + ((left + up) >> 1)) & 255;
            else if (filter === 4) {
                const paeth = left + up - ul;
                const pa = Math.abs(paeth - left);
                const pb = Math.abs(paeth - up);
                const pc = Math.abs(paeth - ul);
                const pr = pa <= pb && pa <= pc ? left : pb <= pc ? up : ul;
                v = (v + pr) & 255;
            } else if (filter !== 0) {
                throw new Error("bad png filter");
            }
            cur[i] = v;
        }
        for (let x = 0; x < w; x++) {
            const s = x * bpp;
            const d = (y * w + x) * 4;
            rgba[d] = cur[s];
            rgba[d + 1] = cur[s + 1];
            rgba[d + 2] = cur[s + 2];
            rgba[d + 3] = bpp === 4 ? cur[s + 3] : 255;
        }
        prev = cur;
    }
    return { w, h, rgba };
}

export function tvRect(rect: Rect | null, w: number, h: number): Rect {
    if (!rect) return { x: 0, y: 0, w, h };
    const sx = w / 1280;
    const sy = h / 720;
    let x = Math.floor(rect.x * sx);
    let y = Math.floor(rect.y * sy);
    let rw = Math.max(1, Math.floor(rect.w * sx));
    let rh = Math.max(1, Math.floor(rect.h * sy));
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w || y >= h) return { x: 0, y: 0, w, h };
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    return { x, y, w: rw, h: rh };
}

export function sample(img: Image, rect: Rect): Uint8Array {
    const out = new Uint8Array(rect.w * rect.h * 4);
    let n = 0;
    for (let y = 0; y < rect.h; y++) {
        for (let x = 0; x < rect.w; x++) {
            const s = ((rect.y + y) * img.w + (rect.x + x)) * 4;
            out[n++] = img.rgba[s];
            out[n++] = img.rgba[s + 1];
            out[n++] = img.rgba[s + 2];
            out[n++] = img.rgba[s + 3];
        }
    }
    return out;
}

export function fnv(bytes: Uint8Array): string {
    let h = 0x811c9dc5;
    for (const b of bytes) {
        h ^= b;
        h = Math.imul(h, 0x01000193);
    }
    return (h >>> 0).toString(16).padStart(8, "0");
}

export function delta(a: Uint8Array, b: Uint8Array): { mean: number; max: number } {
    const n = Math.min(a.length, b.length);
    let sum = 0;
    let max = 0;
    let count = 0;
    for (let i = 0; i < n; i++) {
        if (i % 4 === 3) continue;
        const d = Math.abs(a[i] - b[i]);
        sum += d;
        if (d > max) max = d;
        count++;
    }
    return { mean: count ? sum / count : 0, max };
}

export function newerThan(binMs: number, times: number[]): boolean {
    for (const t of times) {
        if (t > binMs) return true;
    }
    return false;
}

export type PlaceView = {
    scene?: string;
    map?: boolean;
    course?: boolean;
    pack?: string;
};

export function placeKind(place: string): "title" | "file" | "map" | "course" | null {
    const p = place.toLowerCase();
    if (p === "title") return "title";
    if (p === "file" || p === "fileselect") return "file";
    if (p === "map" || p === "courseselect") return "map";
    if (/^[1-9]-[1-9][0-9]*$/.test(p)) return "course";
    return null;
}

export function arrived(place: string, s: PlaceView): boolean {
    const kind = placeKind(place);
    if (kind === "title") return s.scene === "Title";
    if (kind === "file") return s.scene === "FileSelect";
    if (kind === "map") return s.map === true && (s.scene === "Map" || s.scene === "CourseSelect");
    if (kind === "course") return s.course === true && s.scene === "CourseTask" && s.pack === place;
    return false;
}

export function cursorSelected(
    buttons: { name?: string; selected?: boolean }[] | undefined,
    name: string,
): boolean {
    const want = name.toLowerCase();
    return (buttons ?? []).some((b) => b.selected && (b.name ?? "").toLowerCase() === want);
}
