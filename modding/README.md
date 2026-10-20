# Modding

| Path | Contents |
| --- | --- |
| [sdk/](sdk/) | TypeScript definitions for the global `nsmbu` API (`nsmbu.d.ts` declares the global) |
| [examples/](examples/) | Example mod packages: TypeScript source plus the Lua the runtime loads |

Mod sources are TypeScript, compiled to Lua with [TypeScriptToLua](https://typescripttolua.github.io/).
The runtime loads only Lua: `manifest.json` points its `entry` at the compiled `main.lua`, and hand-written Lua packages still work.
Install the toolchain once, then build:

```bash
npm --prefix modding ci
npm --prefix modding run build
```

`tstl` writes `main.lua` next to each `main.ts`.
The Lua target is 5.1 with inlined helpers, so a package loads in the runtime LuaJIT without extra files.

A mod starts from one `main.ts` entry with the manifest beside it:

```ts
nsmbu.Hooks.onLogicStep = (step) => {
    if (step % 30 !== 0) {
        return;
    }
    const [player] = nsmbu.Player.get_player(0);
    if (player) {
        const [lives] = player.lives();
        nsmbu.status(`step ${step} lives ${lives ?? 0}`);
    }
};

export {};
```

End the entry file with `export {};` so top-level variables stay local to the package.
Host calls that can fail return a `LuaMultiReturn` pair; destructure it as `const [value, err] = ...`.

Format the generated Lua with [StyLua](https://github.com/JohnnyMorganz/StyLua) (`cargo install stylua`).
`nsmbu lint` installs the mod dependencies when needed, compiles every example, and formats the Lua.

```bash
stylua --config-path modding/.stylua.toml modding/examples
```

Config: [`tsconfig.json`](tsconfig.json) (compiler and `tstl` options), [`.stylua.toml`](.stylua.toml).
Use `npm --prefix modding run watch` while editing.
Editor setup: `.vscode/settings.json` formats Lua with the StyLua extension.

Docs: [docs/modding/](../docs/modding/).
