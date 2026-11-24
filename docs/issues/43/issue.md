<!-- GitHub issue #43: Blocks / assets fail to load and render invisible (macOS, Apple Silicon) (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/43 -->

### What happens

Some blocks and other level assets fail to load and render invisible (fully transparent). The game logic still treats them as solid — Mario can stand on blocks that are not drawn. Where a real, visible block should be shown, there is only the background.

- World 1-1: Mario can step and stand on blocks that render transparent instead of showing the real block (screenshot 1 — he is standing on top of a column of blocks that aren't drawn).
- Missing assets appear in several worlds, not just 1-1.
- The rest renders correctly (terrain, characters, HUD, background layers) and performance is unaffected (120-144 fps, 60 logic steps/s), so this looks like a purely visual / asset-loading issue.

**Expected:** all blocks and assets render as on the original game.

**Actual:** blocks/assets are missing (transparent) while still being solid.

### Steps to reproduce

1. Launch the game on macOS (Apple Silicon, Metal renderer, v0.8.0).
2. Enter World 1-1 (Acorn Plains).
3. Reach the point where Mario can stand on top of the blocks (screenshot 1): the blocks under him are not drawn, though he stands on them.
4. Similar missing assets show up in several other worlds during normal play.

The artifacts are visible as soon as the area is on screen; no special actions required.

### Where you were

World 1-1 (Acorn Plains). I also noticed missing assets in several other worlds while playing normally — I can capture more examples on request.

### Version

v0.8.0 (macOS app bundle). Boot log reports: `New Super Mario Bros. U v0.8.0 (32af149)`.

### Game version

NSMBU + NSLU (USA)

### Platform

macOS (Apple Silicon, arm64) — macOS app

### Hardware

Apple M5 (8-core GPU, Metal 4), 16 GB unified memory, external 1080p / 165 Hz display.

### Screenshots

- **Screenshot 1** — World 1-1: Mario standing on a column of blocks that render transparent (a real block should show).
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/b64931be-5357-41d6-a7eb-63af93d45a72" />

- **Screenshot 2** — World 1-1 opening area, same session.
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/2ea56e6e-f73e-48ee-a8cc-1b9aa72f43c4" />

- **Screenshot 3** — Acorn Plains course, same session.
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/0f010c3d-ca27-4d06-b242-c21a11e3ade5" />


Renderer info from the log: `Metal (default)`, `native GX2 -> Metal`, `Metal device: Apple M5`, internal resolution 1.5x, anisotropic filtering on, FXAA on.

### Additional details

- Game dump: `New SUPER MARIO BROS. U + New SUPER LUIGI U (USA).wua` (Minerva), title id `000500001014b700`.
- From the in-game debug overlay during capture: 120 fps in-level, 60.0 logic steps/s, GPU interval ~6.3 ms avg — no performance impact, purely visual.
