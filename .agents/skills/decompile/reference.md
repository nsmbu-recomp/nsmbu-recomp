# Decomp domain reference

USA v1.3.0 `red-pro2.rpx` unless a note says Europe.

## Tools

- `python3 tools/rpx/ppcdis.py ADDR [COUNT] [RPX]` — `.text` disassembly; annotates `headers_names.txt` callees and imports.
- `recomp/headers_names.txt` — class and symbol for game addresses (~5k symbols).
- `game/active` — which dump `ppcdis.py` uses by default.

## TODO workflow

`vendor/decomp/docs/TODO.md` — one checkbox per open question; answer in the linked markdown under `vendor/decomp/docs/`.

Example open theme: soft-restart — bootstrap order between audio init (`0x029E8324`) and first `startSound`; tables `.rodata 0x1017C1A4`, `0x1017C284`, `0x1018D9E0`, `0x1018DA04`; see `docs/soft-restart-tls.md` (TLS table `0x1017C814`, not `0x1018C814`).

## Dispatch tables

8-byte `{pad, fn}` convention; function pointer at **+4**:

- App dispatch `0x10185280`
- Base table `0x101906D4`
- `DualScreenMethodTreeMgr` vtable `0x1018FEB8`
- `SndAudioMgr` method table `0x1017C814` (at object `+0x3D0`)

`AudAudioMgr` is not polymorphic (ctor `0x029B1AAC` sets no vptr); `SndAudioMgr`'s only method table is at `+0x3D0`.

## Singletons (data)

| Symbol | Address |
|--------|---------|
| `SndAudioMgr::sInstance` | `0x101E7C84` |
| `SndSpeakerMgr::sInstance` | `0x101E7D0C` |
| `MainGame::sInstance` | `0x101E7D7C` |
| `LayerMgr::sInstance` | `0x101D2BE4` |
| `CourseSelectLayerMgr` (inferred) | `0x101E8A2C` |

## agl / rendering

- `agl::RenderBuffer` size `0x50`: `mColorTarget[8]` at `+0x2C`, `mDepthTarget` at `+0x4C`.
- `clear(u32 target_index, u32 clr_flag, const Color4f&, f32 depth, u32 stencil)` — flag bits `1` colour, `2` depth, `4` stencil.

`RenderObjLayer::RenderStep`: `cRenderStep_CalcView=0`, `UpdateGPUBuffer`, `Opa`, `Xlu`, `Particle`, `PostFx`, `cRenderStep_Num=6`.

### ReflectionRenderObjLayer (observed)

| Field | Offset |
|-------|--------|
| `mpRenderMgr` | `+0x420` |
| `mViewIndex` | `+0x424` |
| `mpCameraProjection` | `+0x42C` (camera `+0x44`, projection `+0x48`, renderer `+0x420`) |
| `mpColorTarget` | `+0x478` |
| `mpDepthTarget` | `+0x47C` |
| colour target object | `+0x480` |
| depth target object | `+0x5D4` |
| `agl::RenderBuffer` | `+0x734` |
| render-target state | `+0x784` |
| bool | `+0x86C` |
| `CullViewFrustum` | `+0x924` |
| projection scale | `+0x9E4` |
| bool | `+0x9FC` |

## SndAudioMgr header note

`SndAudioMgr.h` uses filler `_1C[(0x3D0-0x1C)/4]` and `const void* mpSoundStartable; // +0x3D0`.

## sead naming trees

When present, use:

- `packages/agl/include/common/aglRenderBuffer.h`
- `packages/agl/include/layer/aglRenderInfo.h`
- `engine/library/include/gfx/seadFrameBuffer.h`

under `.tmp/nsmbu-sead/`.

## Docs map (completed themes)

- `docs/frame-loop.md` — sead helpers, 11-node `DualScreenMethodTreeMgr` layout.
- `docs/map-shadow-interp.md` — TV DepthShadow, `0x024F9E24` as mid-function label.
- `docs/soft-restart-tls.md` — TLS / audio bootstrap.

## Parent repo

Runtime hooks and `f_XXXXXXXX` live in the parent repo.
Read `docs/developer/how-it-works.md` for `build/gen` and guest memory, `docs/developer/names.md` for hooks and symbol names, `docs/developer/builds.md` for regional maps, and `docs/user/logs.md` to map a crash log's guest PCs to USA ids.
This skill is for REDPRO2 documentation C++ under `vendor/decomp`.
