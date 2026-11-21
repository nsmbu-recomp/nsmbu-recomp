import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
    getSetting,
    listSettings,
    readSettingsText,
    settingsCommand,
    SETTINGS_HEADER,
    setSetting,
    writeSettingsText,
} from "../../tools/cli/src/settings.ts";

const root = join(fileURLToPath(new URL(".", import.meta.url)), "../..");

function tempFile(): string {
    const parent = join(root, ".tmp");
    mkdirSync(parent, { recursive: true });
    const dir = mkdtempSync(join(parent, "settings-"));
    return join(dir, "settings.ini");
}

test("fps 165 writes the overlay frame-rate keys", () => {
    const values = new Map<string, string>();
    setSetting(values, "fps", "165");
    assert.equal(getSetting(values, "fps"), "165");
    assert.equal(
        writeSettingsText(values),
        SETTINGS_HEADER + "fps60=1\nfpsCustom=0\nfpsUnlocked=0\ninterpFps=165\n",
    );
    assert.doesNotMatch(listSettings(values), /^fps60=/m);
    assert.match(listSettings(values), /^fps=165\s+30, 60/m);
});

test("listed rates, custom caps, and unlocked", () => {
    const values = new Map<string, string>();
    setSetting(values, "fps", "30");
    assert.equal(values.get("fps60"), "1");
    assert.equal(values.get("fpsCustom"), "0");
    assert.equal(values.get("interpFps"), "30");
    setSetting(values, "fps", "90");
    assert.equal(getSetting(values, "fps"), "90");
    assert.equal(values.get("fpsCustom"), "1");
    setSetting(values, "fps", "unlocked");
    assert.equal(getSetting(values, "fps"), "unlocked");
    assert.equal(values.get("interpFps"), "60");
    setSetting(values, "fps", "165");
    setSetting(values, "fps", "unlocked");
    assert.equal(values.get("interpFps"), "165");
    assert.throws(() => setSetting(values, "fps", "45"), /fps is 30, 60/);
});

test("known values are stored the way the overlay saves them", () => {
    const values = new Map<string, string>();
    setSetting(values, "aniso", "on");
    setSetting(values, "resScale", "1440p");
    setSetting(values, "audioVolume", "80%");
    setSetting(values, "vkPresentMode", "fifo");
    setSetting(values, "language", "Chinese (Taiwan)");
    setSetting(values, "aspect", "match");
    setSetting(values, "kept.extra", "yes");
    assert.equal(values.get("aniso"), "1");
    assert.equal(values.get("resScale"), "2");
    assert.equal(values.get("audioVolume"), "0.8");
    assert.equal(values.get("vkPresentMode"), "0");
    assert.equal(values.get("vkPresentChosen"), "1");
    assert.equal(values.get("language"), "11");
    assert.equal(values.get("aspect"), "window");
    assert.equal(values.get("kept.extra"), "yes");
    assert.throws(() => setSetting(values, "aniso", "maybe"), /aniso is 0 or 1/);
    assert.equal(values.get("aniso"), "1");
});

test("settings file round trip keeps unknown keys and drops the virtual fps key", () => {
    const file = tempFile();
    try {
        assert.match(settingsCommand(file, ["set", "audioMute", "off"]), /^audioMute=0$/);
        assert.equal(settingsCommand(file, ["set", "note.custom", "keep"]), "note.custom=keep");
        assert.equal(settingsCommand(file, ["get", "audioMute"]), "0");
        const text = readFileSync(file, "utf8");
        assert.equal(text.startsWith(SETTINGS_HEADER), true);
        assert.match(text, /audioMute=0\n/);
        assert.match(text, /note.custom=keep\n/);
        assert.equal(settingsCommand(file, ["unset", "audioMute"]), "unset audioMute");
        assert.throws(() => settingsCommand(file, ["get", "audioMute"]), /not set: audioMute/);
        assert.equal(readSettingsText("fps=165\nfxaa=1\n").get("fxaa"), "1");
        assert.equal(readSettingsText("fps=165\nfxaa=1\n").has("fps"), false);
        const listed = settingsCommand(file, ["list"]);
        assert.match(listed, /note.custom=keep/);
        assert.match(listed, /audioMute=-\s+0 or 1/);
    } finally {
        rmSync(join(file, ".."), { recursive: true, force: true });
    }
});
