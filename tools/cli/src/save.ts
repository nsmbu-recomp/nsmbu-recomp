import { readFileSync, writeFileSync } from "node:fs";
import { extname } from "node:path";

export const SAVE_SIZE = 0xb134;

const MAGIC = "RPSD";
const HEADER_SIZE = 0x10;
const SLOT_SIZE = 0x204;
const SLOT_DATA_SIZE = 0x200;
const SLOT_COUNT = 6;
const MAIN_OFFSET = 0x10;
const QUICK_OFFSET = 0xc28;
const CHALLENGE_OFFSET = 0x1840;
const CHALLENGE_ENTRY_OFFSET = 0x1848;
const CHALLENGE_ENTRY_SIZE = 0x6c;
const CHALLENGE_ENTRY_CRC_AT = 0x68;
const CHALLENGE_ENTRY_COUNT = 80;
const CHALLENGE_SIZE = 0x360c;
const CHALLENGE_CRC_OFFSET = 0x3608;
const CHALLENGE_UNKNOWN_SIZE = 0x1440;
const BOOST_OFFSET = 0x4e4c;
const BOOST_SIZE = 0xa8;
const BOOST_CRC_OFFSET = 0xa4;
const COIN_OFFSET = 0x4ef4;
const COIN_SIZE = 0x5f38;
const COIN_CRC_OFFSET = 0x5f34;
const COIN_STAGE_SIZE = 0x984;
const COIN_STAGE_COUNT = 10;
const COIN_COUNT = 300;
const COIN_STAR_COUNT = 3;
const TELEMETRY_OFFSET = 0xae2c;
const TELEMETRY_SIZE = 0x94;
const TELEMETRY_CRC_OFFSET = 0x90;
const MII_OFFSET = 0xaec0;
const MII_SIZE = 0x274;
const MII_CRC_OFFSET = 0x270;
const MII_COUNT = 62;
const MII_ENTRY_SIZE = 10;

type JsonObject = Record<string, unknown>;

const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) {
            c = (c & 1) !== 0 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        }
        table[n] = c >>> 0;
    }
    return table;
})();

export function crc32(data: Buffer): number {
    let c = 0xffffffff;
    for (const byte of data) {
        c = CRC_TABLE[(c ^ byte) & 0xff] ^ (c >>> 8);
    }
    return (c ^ 0xffffffff) >>> 0;
}

class Reader {
    readonly buf: Buffer;
    pos: number;

    constructor(buf: Buffer, pos = 0) {
        this.buf = buf;
        this.pos = pos;
    }

    u8(): number {
        const value = this.buf[this.pos];
        this.pos += 1;
        return value;
    }

    u16(): number {
        const value = this.buf.readUInt16BE(this.pos);
        this.pos += 2;
        return value;
    }

    u32(): number {
        const value = this.buf.readUInt32BE(this.pos);
        this.pos += 4;
        return value;
    }

    hex(size: number): string {
        const value = this.buf.toString("hex", this.pos, this.pos + size);
        this.pos += size;
        return value;
    }

    skip(size: number): void {
        this.pos += size;
    }

    u8s(size: number): number[] {
        const values: number[] = [];
        for (let i = 0; i < size; i++) {
            values.push(this.u8());
        }
        return values;
    }
}

class Writer {
    readonly buf: Buffer;
    pos: number;

    constructor(buf: Buffer, pos = 0) {
        this.buf = buf;
        this.pos = pos;
    }

    u8(value: number): void {
        this.buf[this.pos] = value;
        this.pos += 1;
    }

    u16(value: number): void {
        this.buf.writeUInt16BE(value, this.pos);
        this.pos += 2;
    }

    u32(value: number): void {
        this.buf.writeUInt32BE(value, this.pos);
        this.pos += 4;
    }

    raw(value: Buffer): void {
        value.copy(this.buf, this.pos);
        this.pos += value.length;
    }

    skip(size: number): void {
        this.pos += size;
    }

    u8s(values: number[]): void {
        for (const value of values) {
            this.u8(value);
        }
    }
}

function asObject(value: unknown, path: string): JsonObject {
    if (typeof value !== "object" || value === null || Array.isArray(value)) {
        throw new Error(`${path}: expected an object`);
    }
    return value as JsonObject;
}

function asArray(value: unknown, path: string, size: number): unknown[] {
    if (!Array.isArray(value)) {
        throw new Error(`${path}: expected an array`);
    }
    if (value.length !== size) {
        throw new Error(`${path}: expected ${size} entries, got ${value.length}`);
    }
    return value;
}

function asByte(value: unknown, path: string): number {
    if (typeof value !== "number" || !Number.isInteger(value) || value < 0 || value > 0xff) {
        throw new Error(`${path}: expected a byte, got ${String(value)}`);
    }
    return value;
}

function asU16(value: unknown, path: string): number {
    if (typeof value !== "number" || !Number.isInteger(value) || value < 0 || value > 0xffff) {
        throw new Error(`${path}: expected a 16-bit unsigned integer, got ${String(value)}`);
    }
    return value;
}

function asU32(value: unknown, path: string): number {
    if (typeof value !== "number" || !Number.isInteger(value) || value < 0 || value > 0xffffffff) {
        throw new Error(`${path}: expected a 32-bit unsigned integer, got ${String(value)}`);
    }
    return value;
}

function asFloatBits(value: unknown, path: string): number {
    if (typeof value === "number" && Number.isFinite(value) && !Object.is(value, -0)) {
        const bits = Buffer.alloc(4);
        bits.writeFloatBE(value);
        return bits.readUInt32BE(0);
    }
    if (typeof value === "string") {
        const match = /^0x([0-9a-fA-F]{8})$/.exec(value);
        if (match) {
            return Number.parseInt(match[1], 16);
        }
    }
    throw new Error(`${path}: expected a float number or 0x-prefixed 32-bit hex`);
}

function asHex(value: unknown, path: string, size: number): Buffer {
    if (typeof value !== "string") {
        throw new Error(`${path}: expected a hex string`);
    }
    if (value.length !== size * 2 || !/^[0-9a-fA-F]*$/.test(value)) {
        throw new Error(`${path}: expected ${size} bytes of hex`);
    }
    return Buffer.from(value, "hex");
}

function asBytes(value: unknown, path: string, size: number): number[] {
    return asArray(value, path, size).map((entry, index) => asByte(entry, `${path}[${index}]`));
}

function asMagic(value: unknown): string {
    if (typeof value !== "string" || value.length !== 4) {
        throw new Error("$.magic: expected a 4-character string");
    }
    return value;
}

function decodeLocation(reader: Reader): JsonObject {
    return {
        world: reader.u8(),
        section: reader.u8(),
        node: reader.u8(),
    };
}

function encodeLocation(writer: Writer, value: unknown, path: string): void {
    const location = asObject(value, path);
    writer.u8(asByte(location.world, `${path}.world`));
    writer.u8(asByte(location.section, `${path}.section`));
    writer.u8(asByte(location.node, `${path}.node`));
}

function decodeAmbushEnemy(reader: Reader): JsonObject {
    return {
        ...decodeLocation(reader),
        unknown3: reader.u8(),
        unknown4: reader.u8(),
    };
}

function encodeAmbushEnemy(writer: Writer, value: unknown, path: string): void {
    const enemy = asObject(value, path);
    encodeLocation(writer, enemy, path);
    writer.u8(asByte(enemy.unknown3, `${path}.unknown3`));
    writer.u8(asByte(enemy.unknown4, `${path}.unknown4`));
}

function decodeAmbush(reader: Reader): JsonObject {
    const groups: Record<string, number> = {
        world1: 2,
        world2: 3,
        world3: 2,
        world4: 5,
        world5: 3,
        world7: 3,
    };
    const ambush: JsonObject = {};
    for (const [name, count] of Object.entries(groups)) {
        const enemies: JsonObject[] = [];
        for (let i = 0; i < count; i++) {
            enemies.push(decodeAmbushEnemy(reader));
        }
        ambush[name] = enemies;
    }
    ambush.unknown = reader.hex(18);
    ambush.unknown6C = reader.u8();
    return ambush;
}

function encodeAmbush(writer: Writer, value: unknown, path: string): void {
    const ambush = asObject(value, path);
    const groups: Record<string, number> = {
        world1: 2,
        world2: 3,
        world3: 2,
        world4: 5,
        world5: 3,
        world7: 3,
    };
    for (const [name, count] of Object.entries(groups)) {
        const enemies = asArray(ambush[name], `${path}.${name}`, count);
        enemies.forEach((enemy, index) => encodeAmbushEnemy(writer, enemy, `${path}.${name}[${index}]`));
    }
    writer.raw(asHex(ambush.unknown, `${path}.unknown`, 18));
    writer.u8(asByte(ambush.unknown6C, `${path}.unknown6C`));
}

const SECRET_ISLAND_KEYS = [
    "coinsCollected",
    "starCoinsCollected",
    "goalsReached",
    "timesApplauded",
    "boostBlockDistance",
    "miniBoostBlockDistance",
    "nabbitsCaught",
    "goombasStomped",
    "itemsCollected",
    "goalsWithYoshi",
    "goalsWithBabyYoshi",
    "oneUpsAtGoal",
    "fireworksAtGoal",
    "threeUpMoons",
];

function decodeSecretIsland(reader: Reader): JsonObject {
    const stats: JsonObject = {};
    for (const key of SECRET_ISLAND_KEYS) {
        stats[key] = reader.u32();
    }
    return stats;
}

function encodeSecretIsland(writer: Writer, value: unknown, path: string): void {
    const stats = asObject(value, path);
    for (const key of SECRET_ISLAND_KEYS) {
        writer.u32(asU32(stats[key], `${path}.${key}`));
    }
}

function decodeSaveSlot(reader: Reader): JsonObject {
    const slot: JsonObject = {
        valid: reader.u8(),
        lastPlayers: reader.u8(),
        coins: reader.u8(),
        rockCandy: reader.u8(),
        worldMapLocation: decodeLocation(reader),
        secondaryWorldMapLocation: decodeLocation(reader),
        unknown0A: reader.u8(),
        unknown0B: reader.u8(),
        lives: reader.u8s(4),
        characters: reader.u8s(4),
        powerups: reader.u8s(4),
        gameOvers: reader.u8s(4),
        finishedWorlds: reader.u16(),
        completionFlags: reader.u8(),
        unknown1F: reader.u8(),
        unknownVector: reader.hex(12),
        unknown2C: reader.u8s(4),
        score: reader.u32(),
        unknown34: reader.hex(16),
        creditsHighscore: reader.u16(),
        airshipLocation: decodeLocation(reader),
        nabbitWorldA: reader.u8(),
        nabbitWorldB: reader.u8(),
        unknown4B: reader.u8s(10),
        availableBabyYoshi: reader.u8(),
        activeBabyYoshi: reader.u8(),
        unknown57: reader.u8s(3),
        balloonYoshiCounter: reader.u8(),
        bubbleYoshiCounter: reader.u8(),
        unknown5C: reader.u8s(4),
        ambushItems: reader.u8s(4),
        nabbitRelated: reader.u8s(7),
        levelFlags: reader.u8s(123),
        deaths: reader.u8s(62),
        starCoins: reader.u8s(41),
        inventory: reader.u8s(10),
        ambush: decodeAmbush(reader),
        secretIsland: decodeSecretIsland(reader),
        caughtNabbit: reader.u8(),
        padding: reader.hex(3),
    };
    return slot;
}

function encodeSaveSlot(writer: Writer, value: unknown, path: string): void {
    const slot = asObject(value, path);
    writer.u8(asByte(slot.valid, `${path}.valid`));
    writer.u8(asByte(slot.lastPlayers, `${path}.lastPlayers`));
    writer.u8(asByte(slot.coins, `${path}.coins`));
    writer.u8(asByte(slot.rockCandy, `${path}.rockCandy`));
    encodeLocation(writer, slot.worldMapLocation, `${path}.worldMapLocation`);
    encodeLocation(writer, slot.secondaryWorldMapLocation, `${path}.secondaryWorldMapLocation`);
    writer.u8(asByte(slot.unknown0A, `${path}.unknown0A`));
    writer.u8(asByte(slot.unknown0B, `${path}.unknown0B`));
    writer.u8s(asBytes(slot.lives, `${path}.lives`, 4));
    writer.u8s(asBytes(slot.characters, `${path}.characters`, 4));
    writer.u8s(asBytes(slot.powerups, `${path}.powerups`, 4));
    writer.u8s(asBytes(slot.gameOvers, `${path}.gameOvers`, 4));
    writer.u16(asU16(slot.finishedWorlds, `${path}.finishedWorlds`));
    writer.u8(asByte(slot.completionFlags, `${path}.completionFlags`));
    writer.u8(asByte(slot.unknown1F, `${path}.unknown1F`));
    writer.raw(asHex(slot.unknownVector, `${path}.unknownVector`, 12));
    writer.u8s(asBytes(slot.unknown2C, `${path}.unknown2C`, 4));
    writer.u32(asU32(slot.score, `${path}.score`));
    writer.raw(asHex(slot.unknown34, `${path}.unknown34`, 16));
    writer.u16(asU16(slot.creditsHighscore, `${path}.creditsHighscore`));
    encodeLocation(writer, slot.airshipLocation, `${path}.airshipLocation`);
    writer.u8(asByte(slot.nabbitWorldA, `${path}.nabbitWorldA`));
    writer.u8(asByte(slot.nabbitWorldB, `${path}.nabbitWorldB`));
    writer.u8s(asBytes(slot.unknown4B, `${path}.unknown4B`, 10));
    writer.u8(asByte(slot.availableBabyYoshi, `${path}.availableBabyYoshi`));
    writer.u8(asByte(slot.activeBabyYoshi, `${path}.activeBabyYoshi`));
    writer.u8s(asBytes(slot.unknown57, `${path}.unknown57`, 3));
    writer.u8(asByte(slot.balloonYoshiCounter, `${path}.balloonYoshiCounter`));
    writer.u8(asByte(slot.bubbleYoshiCounter, `${path}.bubbleYoshiCounter`));
    writer.u8s(asBytes(slot.unknown5C, `${path}.unknown5C`, 4));
    writer.u8s(asBytes(slot.ambushItems, `${path}.ambushItems`, 4));
    writer.u8s(asBytes(slot.nabbitRelated, `${path}.nabbitRelated`, 7));
    writer.u8s(asBytes(slot.levelFlags, `${path}.levelFlags`, 123));
    writer.u8s(asBytes(slot.deaths, `${path}.deaths`, 62));
    writer.u8s(asBytes(slot.starCoins, `${path}.starCoins`, 41));
    writer.u8s(asBytes(slot.inventory, `${path}.inventory`, 10));
    encodeAmbush(writer, slot.ambush, `${path}.ambush`);
    encodeSecretIsland(writer, slot.secretIsland, `${path}.secretIsland`);
    writer.u8(asByte(slot.caughtNabbit, `${path}.caughtNabbit`));
    writer.raw(asHex(slot.padding, `${path}.padding`, 3));
}

function decodeChallenges(reader: Reader): JsonObject[] {
    const challenges: JsonObject[] = [];
    for (let i = 0; i < 80; i++) {
        challenges.push({
            state: reader.u8(),
            player: reader.u8(),
            unknown: reader.hex(2),
            mii: reader.hex(0x60),
            highscore: reader.u32(),
            replayCrc: reader.u32(),
        });
    }
    return challenges;
}

function encodeChallenges(writer: Writer, value: unknown, path: string): void {
    const challenges = asArray(value, path, 80);
    challenges.forEach((entry, index) => {
        const challenge = asObject(entry, `${path}[${index}]`);
        const at = `${path}[${index}]`;
        writer.u8(asByte(challenge.state, `${at}.state`));
        writer.u8(asByte(challenge.player, `${at}.player`));
        writer.raw(asHex(challenge.unknown, `${at}.unknown`, 2));
        writer.raw(asHex(challenge.mii, `${at}.mii`, 0x60));
        writer.u32(asU32(challenge.highscore, `${at}.highscore`));
        writer.u32(0);
    });
}

function decodeChallenge(reader: Reader): JsonObject {
    return {
        valid: reader.u8(),
        medals: reader.u8s(5),
        unknown06: reader.u8(),
        unknown07: reader.u8(),
        challenges: decodeChallenges(reader),
        unknown: reader.hex(CHALLENGE_UNKNOWN_SIZE),
    };
}

function encodeChallenge(writer: Writer, value: unknown): void {
    const challenge = asObject(value, "$.challenge");
    writer.u8(asByte(challenge.valid, "$.challenge.valid"));
    writer.u8s(asBytes(challenge.medals, "$.challenge.medals", 5));
    writer.u8(asByte(challenge.unknown06, "$.challenge.unknown06"));
    writer.u8(asByte(challenge.unknown07, "$.challenge.unknown07"));
    encodeChallenges(writer, challenge.challenges, "$.challenge.challenges");
    writer.raw(asHex(challenge.unknown, "$.challenge.unknown", CHALLENGE_UNKNOWN_SIZE));
}

function decodeBoostRush(reader: Reader): JsonObject {
    const unknown00 = reader.u8s(4);
    const highscores: number[] = [];
    for (let i = 0; i < 32; i++) {
        highscores.push(reader.u32());
    }
    return {
        unknown00,
        highscores,
        unknown84: reader.u8s(32),
    };
}

function encodeBoostRush(writer: Writer, value: unknown): void {
    const boost = asObject(value, "$.boostRush");
    writer.u8s(asBytes(boost.unknown00, "$.boostRush.unknown00", 4));
    const highscores = asArray(boost.highscores, "$.boostRush.highscores", 32);
    highscores.forEach((score, index) => writer.u32(asU32(score, `$.boostRush.highscores[${index}]`)));
    writer.u8s(asBytes(boost.unknown84, "$.boostRush.unknown84", 32));
}

function decodeCoin(reader: Reader): JsonObject {
    return {
        area: reader.u8(),
        zone: reader.u8(),
        valid: reader.u8(),
        unknown: reader.u8(),
        x: reader.u16(),
        y: reader.u16(),
    };
}

function encodeCoin(writer: Writer, value: unknown, path: string): void {
    const coin = asObject(value, path);
    writer.u8(asByte(coin.area, `${path}.area`));
    writer.u8(asByte(coin.zone, `${path}.zone`));
    writer.u8(asByte(coin.valid, `${path}.valid`));
    writer.u8(asByte(coin.unknown, `${path}.unknown`));
    writer.u16(asU16(coin.x, `${path}.x`));
    writer.u16(asU16(coin.y, `${path}.y`));
}

function decodeFloat(reader: Reader): number | string {
    const bits = reader.u32();
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(bits);
    const value = bytes.readFloatBE(0);
    if (Number.isFinite(value) && !Object.is(value, -0)) {
        return value;
    }
    return `0x${bits.toString(16).padStart(8, "0")}`;
}

function decodeStarCoin(reader: Reader): JsonObject {
    return {
        area: reader.u8(),
        zone: reader.u8(),
        unknown0: reader.u8(),
        unknown1: reader.u8(),
        x: decodeFloat(reader),
        y: decodeFloat(reader),
    };
}

function encodeStarCoin(writer: Writer, value: unknown, path: string): void {
    const coin = asObject(value, path);
    writer.u8(asByte(coin.area, `${path}.area`));
    writer.u8(asByte(coin.zone, `${path}.zone`));
    writer.u8(asByte(coin.unknown0, `${path}.unknown0`));
    writer.u8(asByte(coin.unknown1, `${path}.unknown1`));
    writer.u32(asFloatBits(coin.x, `${path}.x`));
    writer.u32(asFloatBits(coin.y, `${path}.y`));
}

function decodeCoinEdit(reader: Reader): JsonObject {
    const stages: JsonObject[] = [];
    for (let stage = 0; stage < COIN_STAGE_COUNT; stage++) {
        const coins: JsonObject[] = [];
        for (let i = 0; i < COIN_COUNT; i++) {
            coins.push(decodeCoin(reader));
        }
        const starCoins: JsonObject[] = [];
        for (let i = 0; i < COIN_STAR_COUNT; i++) {
            starCoins.push(decodeStarCoin(reader));
        }
        stages.push({ coins, starCoins });
    }
    return {
        stages,
        unlocked: reader.hex(12),
    };
}

function encodeCoinEdit(writer: Writer, value: unknown): void {
    const coinEdit = asObject(value, "$.coinEdit");
    const stages = asArray(coinEdit.stages, "$.coinEdit.stages", COIN_STAGE_COUNT);
    stages.forEach((entry, stageIndex) => {
        const stage = asObject(entry, `$.coinEdit.stages[${stageIndex}]`);
        const at = `$.coinEdit.stages[${stageIndex}]`;
        const coins = asArray(stage.coins, `${at}.coins`, COIN_COUNT);
        coins.forEach((coin, index) => encodeCoin(writer, coin, `${at}.coins[${index}]`));
        const starCoins = asArray(stage.starCoins, `${at}.starCoins`, COIN_STAR_COUNT);
        starCoins.forEach((coin, index) => encodeStarCoin(writer, coin, `${at}.starCoins[${index}]`));
    });
    writer.raw(asHex(coinEdit.unlocked, "$.coinEdit.unlocked", 12));
}

const TELEMETRY_U32_KEYS = [
    "secondsPlayed",
    "storySeconds",
    "levelsPlayed",
    "levelsPlayedAlone",
    "levelsPlayedWithTwo",
    "levelsPlayedWithThree",
    "levelsPlayedWithFour",
    "unknown1C",
    "nabbitsChased",
    "superGuidesSeen",
    "boostRushGames",
    "boostRushAlone",
    "boostRushWithTwo",
    "boostRushWithThree",
    "boostRushWithFour",
    "unknown3C",
    "unknown40",
    "coinBattles",
    "coinBattlesAlone",
    "coinBattlesWithTwo",
    "coinBattlesWithThree",
    "coinBattlesWithFour",
    "unknown58",
    "unknown5C",
    "editedCoinBattleLevels",
    "challengesPlayed",
    "unknown68",
    "challengeReplaysWatched",
    "boostModeChallenges",
    "nsluLevels",
    "unknown78",
    "unknown7C",
    "powerupsUsed",
];

const TELEMETRY_BYTE_KEYS = [
    "unknown84",
    "unknown85",
    "unknown86",
    "unknown87",
    "unknown88",
    "unknown89",
    "goldMedals",
    "silverMedals",
    "bronzeMedals",
    "boostModeChallengesCompleted",
    "unknown8E",
    "unknown8F",
];

function decodeTelemetry(reader: Reader): JsonObject {
    const telemetry: JsonObject = {};
    for (const key of TELEMETRY_U32_KEYS) {
        telemetry[key] = reader.u32();
    }
    for (const key of TELEMETRY_BYTE_KEYS) {
        telemetry[key] = reader.u8();
    }
    return telemetry;
}

function encodeTelemetry(writer: Writer, value: unknown): void {
    const telemetry = asObject(value, "$.telemetry");
    for (const key of TELEMETRY_U32_KEYS) {
        writer.u32(asU32(telemetry[key], `$.telemetry.${key}`));
    }
    for (const key of TELEMETRY_BYTE_KEYS) {
        writer.u8(asByte(telemetry[key], `$.telemetry.${key}`));
    }
}

function decodeMii(reader: Reader): JsonObject {
    const count = reader.u8();
    const entries: string[] = [];
    for (let i = 0; i < MII_COUNT; i++) {
        entries.push(reader.hex(MII_ENTRY_SIZE));
    }
    return {
        count,
        entries,
        padding: reader.hex(3),
    };
}

function encodeMii(writer: Writer, value: unknown): void {
    const mii = asObject(value, "$.mii");
    writer.u8(asByte(mii.count, "$.mii.count"));
    const entries = asArray(mii.entries, "$.mii.entries", MII_COUNT);
    entries.forEach((entry, index) =>
        writer.raw(asHex(entry, `$.mii.entries[${index}]`, MII_ENTRY_SIZE)),
    );
    writer.raw(asHex(mii.padding, "$.mii.padding", 3));
}

export function decodeSave(data: Buffer): JsonObject {
    if (data.length < SAVE_SIZE) {
        throw new Error(`save data is ${data.length} bytes; expected at least ${SAVE_SIZE}`);
    }
    const magic = data.toString("latin1", 0, 4);
    if (magic !== MAGIC) {
        throw new Error(`unexpected save magic: ${JSON.stringify(magic)}`);
    }
    const header = new Reader(data, 4);
    const version = [header.u8(), header.u8(), header.u8()];
    const lastUsedSlot = header.u8();
    const nsluNotified = header.u8();
    const buttonLayout = header.u8();
    const padding = header.hex(2);

    const mainSlots: JsonObject[] = [];
    for (let i = 0; i < SLOT_COUNT; i++) {
        mainSlots.push(decodeSaveSlot(new Reader(data, MAIN_OFFSET + i * SLOT_SIZE)));
    }
    const quickSlots: JsonObject[] = [];
    for (let i = 0; i < SLOT_COUNT; i++) {
        quickSlots.push(decodeSaveSlot(new Reader(data, QUICK_OFFSET + i * SLOT_SIZE)));
    }

    const challenge = decodeChallenge(new Reader(data, CHALLENGE_OFFSET));
    const boostRush = decodeBoostRush(new Reader(data, BOOST_OFFSET));
    const coinEdit = decodeCoinEdit(new Reader(data, COIN_OFFSET));
    const telemetry = decodeTelemetry(new Reader(data, TELEMETRY_OFFSET));
    const mii = decodeMii(new Reader(data, MII_OFFSET));

    const save: JsonObject = {
        magic,
        version,
        lastUsedSlot,
        nsluNotified,
        buttonLayout,
        padding,
        mainSlots,
        quickSlots,
        challenge,
        boostRush,
        coinEdit,
        telemetry,
        mii,
    };
    if (data.length > SAVE_SIZE) {
        save.trailing = data.toString("hex", SAVE_SIZE);
    }
    return save;
}

function writeBlockCrc(buf: Buffer, offset: number, size: number): void {
    buf.writeUInt32BE(crc32(buf.subarray(offset, offset + size)), offset + size);
}

function sealChallengeEntryCrcs(buf: Buffer): void {
    for (let i = 0; i < CHALLENGE_ENTRY_COUNT; i++) {
        const off = CHALLENGE_ENTRY_OFFSET + i * CHALLENGE_ENTRY_SIZE;
        writeBlockCrc(buf, off, CHALLENGE_ENTRY_CRC_AT);
    }
}

export function sealSavedataBlockCrcs(buf: Buffer): void {
    if (buf.length < SAVE_SIZE) {
        throw new Error(`save data is ${buf.length} bytes; expected at least ${SAVE_SIZE}`);
    }
    sealChallengeEntryCrcs(buf);
    writeBlockCrc(buf, 0, 0xc);
    for (let i = 0; i < SLOT_COUNT; i++) {
        writeBlockCrc(buf, MAIN_OFFSET + i * SLOT_SIZE, SLOT_DATA_SIZE);
        writeBlockCrc(buf, QUICK_OFFSET + i * SLOT_SIZE, SLOT_DATA_SIZE);
    }
    writeBlockCrc(buf, CHALLENGE_OFFSET, CHALLENGE_CRC_OFFSET);
    writeBlockCrc(buf, BOOST_OFFSET, BOOST_CRC_OFFSET);
    writeBlockCrc(buf, COIN_OFFSET, COIN_CRC_OFFSET);
    writeBlockCrc(buf, TELEMETRY_OFFSET, TELEMETRY_CRC_OFFSET);
    writeBlockCrc(buf, MII_OFFSET, MII_CRC_OFFSET);
}

function blockCrcOk(buf: Buffer, offset: number, payload: number): boolean {
    return buf.readUInt32BE(offset + payload) === crc32(buf.subarray(offset, offset + payload));
}

export function verifySavedataBlockCrcs(buf: Buffer): boolean {
    if (buf.length < SAVE_SIZE) {
        return false;
    }
    if (buf.toString("latin1", 0, 4) !== MAGIC) {
        return false;
    }
    if (buf[4] !== 1 || buf[5] !== 0) {
        return false;
    }
    if (!blockCrcOk(buf, 0, 0xc)) {
        return false;
    }
    for (let i = 0; i < SLOT_COUNT; i++) {
        if (!blockCrcOk(buf, MAIN_OFFSET + i * SLOT_SIZE, SLOT_DATA_SIZE)) {
            return false;
        }
        if (!blockCrcOk(buf, QUICK_OFFSET + i * SLOT_SIZE, SLOT_DATA_SIZE)) {
            return false;
        }
    }
    return (
        blockCrcOk(buf, CHALLENGE_OFFSET, CHALLENGE_CRC_OFFSET) &&
        blockCrcOk(buf, BOOST_OFFSET, BOOST_CRC_OFFSET) &&
        blockCrcOk(buf, COIN_OFFSET, COIN_CRC_OFFSET) &&
        blockCrcOk(buf, TELEMETRY_OFFSET, TELEMETRY_CRC_OFFSET) &&
        blockCrcOk(buf, MII_OFFSET, MII_CRC_OFFSET)
    );
}

export function encodeSave(value: unknown): Buffer {
    const save = asObject(value, "$");
    if (asMagic(save.magic) !== MAGIC) {
        throw new Error(`$.magic: expected ${JSON.stringify(MAGIC)}`);
    }
    const trailing = save.trailing === undefined ? Buffer.alloc(0) : asTrailing(save.trailing);
    const buf = Buffer.alloc(SAVE_SIZE + trailing.length);
    const writer = new Writer(buf, 0);

    writer.raw(Buffer.from(MAGIC, "latin1"));
    const version = asArray(save.version, "$.version", 3);
    version.forEach((entry, index) => writer.u8(asByte(entry, `$.version[${index}]`)));
    writer.u8(asByte(save.lastUsedSlot, "$.lastUsedSlot"));
    writer.u8(asByte(save.nsluNotified, "$.nsluNotified"));
    writer.u8(asByte(save.buttonLayout, "$.buttonLayout"));
    writer.raw(asHex(save.padding, "$.padding", 2));
    writer.pos = MAIN_OFFSET;

    const mainSlots = asArray(save.mainSlots, "$.mainSlots", SLOT_COUNT);
    mainSlots.forEach((slot, index) => {
        encodeSaveSlot(writer, slot, `$.mainSlots[${index}]`);
        writer.pos += 4;
    });
    const quickSlots = asArray(save.quickSlots, "$.quickSlots", SLOT_COUNT);
    quickSlots.forEach((slot, index) => {
        encodeSaveSlot(writer, slot, `$.quickSlots[${index}]`);
        writer.pos += 4;
    });

    writer.pos = CHALLENGE_OFFSET;
    encodeChallenge(writer, save.challenge);
    writer.pos += 4;

    writer.pos = BOOST_OFFSET;
    encodeBoostRush(writer, save.boostRush);
    writer.pos += 4;

    writer.pos = COIN_OFFSET;
    encodeCoinEdit(writer, save.coinEdit);
    writer.pos += 4;

    writer.pos = TELEMETRY_OFFSET;
    encodeTelemetry(writer, save.telemetry);
    writer.pos += 4;

    writer.pos = MII_OFFSET;
    encodeMii(writer, save.mii);
    writer.pos += 4;

    if (writer.pos !== SAVE_SIZE) {
        throw new Error(`internal error: wrote ${writer.pos} bytes; expected ${SAVE_SIZE}`);
    }
    if (trailing.length > 0) {
        writer.raw(trailing);
    }

    sealSavedataBlockCrcs(buf);
    if (!verifySavedataBlockCrcs(buf)) {
        throw new Error("internal error: save block CRC check failed after seal");
    }

    return buf;
}

function asTrailing(value: unknown): Buffer {
    if (typeof value !== "string" || value.length % 2 !== 0 || !/^[0-9a-fA-F]*$/.test(value)) {
        throw new Error("$.trailing: expected a hex string");
    }
    return Buffer.from(value, "hex");
}

export function defaultOutputPath(input: string): string {
    if (extname(input).toLowerCase() === ".json") {
        return input.slice(0, -extname(input).length) + ".dat";
    }
    if (extname(input) === "") {
        return `${input}.json`;
    }
    return input.slice(0, -extname(input).length) + ".json";
}

export function cmdSave(input: string, output?: string): void {
    const isJson = extname(input).toLowerCase() === ".json";
    const target = output ?? defaultOutputPath(input);
    if (isJson) {
        const source = readFileSync(input, "utf8");
        let json: unknown;
        try {
            json = JSON.parse(source);
        } catch (e) {
            const message = e instanceof Error ? e.message : String(e);
            throw new Error(`${input}: invalid JSON: ${message}`);
        }
        const data = encodeSave(json);
        writeFileSync(target, data);
        console.log(`${input} -> ${target} (${data.length} bytes)`);
        return;
    }
    const data = readFileSync(input);
    const json = decodeSave(data);
    writeFileSync(target, JSON.stringify(json, null, 2) + "\n");
    console.log(`${input} -> ${target}`);
}
