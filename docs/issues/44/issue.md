<!-- GitHub issue #44: World map shadows flicker on/off at 144 fps (macOS, Apple Silicon) (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/44 -->

### What happens

On the world map, the object shadows flicker on and off between frames. Two captures taken moments apart on the Acorn Plains map show shadows rendered in one frame and absent in the next, alternating continuously.

A detail that may help: in the shadows-on frame the in-game overlay shows `Game CPU: 1.58 ms / Render CPU: 0.12 ms`; in the shadows-off frame it shows `Game CPU: 0.07 ms / Render CPU: 3.11 ms`. The near-zero game-CPU frame looks like an interpolated frame (frame interpolation runs at 144 fps on this setup), which suggests the shadow layer is missing on interpolated frames rather than being randomly dropped.

Possibly related to #20 ("At over 60fps, the shadows on the world map flicker.", closed) — this is on v0.8.0 at 144 fps.

**Expected:** shadows render consistently on every presented frame.

**Actual:** shadows appear and disappear frame to frame (visible flicker).

### Steps to reproduce

1. Launch the game on macOS (Apple Silicon, Metal renderer, v0.8.0) with frame interpolation at 144 fps (165 Hz display).
2. Go to the Acorn Plains world map.
3. Watch the map objects' shadows: they appear and disappear between frames.

### Where you were

World map (Acorn Plains), standing/moving on the path — no special action required, the flicker is constant.

### Version

v0.8.0 (macOS app bundle). Boot log reports: `New Super Mario Bros. U v0.8.0 (32af149)`.

### Game version

NSMBU + NSLU (USA)

### Platform

macOS (Apple Silicon, arm64) — macOS app

### Hardware

Apple M5 (8-core GPU, Metal 4), 16 GB unified memory, external 1080p / 165 Hz display.

### Screenshots

Two captures (shadows on / shadows off). While playing the game keeps flickering between these two states.
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/3948f89b-57c1-4170-ac4c-30ad2409d493" />
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/0294a8ec-126b-4ba2-a98a-d2f355a911b0" />

### Logs

Full `nsmbu.log` (portable install: `data/nsmbu/logs/`, same session as #43):

<details><summary>nsmbu.log</summary>

```
[log] writing /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/logs/nsmbu.log
[boot] New Super Mario Bros. U v0.8.0 (32af149), macOS 27.0.1 (26A434)
[boot] game version NSMBU + NSLU (USA) (NSMBU_NSLU_USA, title 000500001014b700, title version 0)
[gfx] renderer: Metal (default)
[boot] loaded /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/game/code/red-pro2.rpx (NSMBU + NSLU (USA) / NSMBU_NSLU_USA, title 000500001014b700 version 0): entry 02B68940 sda 1061F320 sda2 10008000 data end 10617308
[boot] save dir /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/save (rp_savedata.dat 45364 bytes)
[boot] rp_personal_savedata.dat 8 bytes
[mem] default heap 10618000-50000000
[interp] display refresh rate 165 Hz: frame interpolation draws up to 165 fps
[display] TV window starts in full screen (as it was left)
[gfx] Metal device: Apple M5
[gfx] Metal renderer started
[thread] start "" core 1 prio 16 affinity 2
[gfx] internal resolution 1.5x
[gfx] anisotropic filtering on
[gfx] edge smoothing (FXAA) on
[interp] frame interpolation 144 fps
[aspect] 16:9 (original)
[gx2] initialized (native GX2 -> Metal)
[gx2] TV buffer format 1A
[thread] start "Prepare Thread" core 1 prio 20 affinity 2
[thread] start "RenderObjParallelExecuterThread" core 1 prio 17 affinity 2
[thread] start "RenderObjParallelExecuterThread" core 0 prio 17 affinity 1
[thread] start "RenderObjParallelExecuterThread" core 2 prio 17 affinity 4
[test] origin at guest time 0.3425 s, logic step 1
[gfx] 100 shaders translated
[gfx] 200 shaders translated
[gfx] 300 shaders translated
[gfx] 400 shaders translated
[gfx] 500 shaders translated
[gfx] 600 shaders translated
[gfx] 700 shaders translated
[gfx] 800 shaders translated
[gfx] 900 shaders translated
[gfx] 1000 shaders translated
[gfx] 1100 shaders translated
[gfx] 1200 shaders translated
[gfx] 1300 shaders translated
[gfx] 1400 shaders translated
[gfx] 1500 shaders translated
[gfx] 1600 shaders translated
[gfx] 1700 shaders translated
[gfx] 1800 shaders translated
[gfx] 1900 shaders translated
[gfx] 2000 shaders translated
[gfx] 2100 shaders translated
[gfx] 2200 shaders translated
[gfx] 2300 shaders translated
[gfx] 2400 shaders translated
[gfx] 2500 shaders translated
[gfx] 2600 shaders translated
[gfx] 2700 shaders translated
[gfx] 2800 shaders translated
[gfx] 2900 shaders translated
[gfx] 3000 shaders translated
[gfx] 3100 shaders translated
[gfx] 3200 shaders translated
[gfx] 3300 shaders translated
[gfx] 3400 shaders translated
[gfx] 3500 shaders translated
[gfx] 3600 shaders translated
[gfx] 3700 shaders translated
[gfx] 3800 shaders translated
[gfx] 3900 shaders translated
[gfx] 4000 shaders translated
[gfx] 4100 shaders translated
[gfx] 4200 shaders translated
[gfx] 4300 shaders translated
[gfx] 4400 shaders translated
[gfx] 4500 shaders translated
[gfx] 4600 shaders translated
[gfx] 4700 shaders translated
[gfx] 4800 shaders translated
[gfx] 4900 shaders translated
[gfx] 5000 shaders translated
[gfx] 5100 shaders translated
[gfx] 5200 shaders translated
[gfx] 5300 shaders translated
[gfx] 5400 shaders translated
[gfx] 5500 shaders translated
[gfx] 5600 shaders translated
[gfx] 5700 shaders translated
[gfx] 5800 shaders translated
[gfx] 5900 shaders translated
[gfx] 6000 shaders translated
[gfx] 6100 shaders translated
[gfx] 6200 shaders translated
[gfx] 6300 shaders translated
[gfx] 6400 shaders translated
[gfx] 6500 shaders translated
[gfx] 6600 shaders translated
[gfx] 6700 shaders translated
[gfx] 6800 shaders translated
[gfx] 6900 shaders translated
[gfx] 7000 shaders translated
[gfx] 7100 shaders translated
[gfx] 7200 shaders translated
[gfx] 7300 shaders translated
[gfx] 7400 shaders translated
[gfx] 7500 shaders translated
[gfx] 7600 shaders translated
[gfx] 7700 shaders translated
[gfx] 7800 shaders translated
[gfx] 7900 shaders translated
[gfx] 8000 shaders translated
[gfx] 8100 shaders translated
[gfx] 8200 shaders translated
[gfx] 8300 shaders translated
[gfx] 8400 shaders translated
[gfx] 8500 shaders translated
[gfx] 8600 shaders translated
[gfx] 8700 shaders translated
[gfx] 8800 shaders translated
[gfx] 8900 shaders translated
[gfx] 9000 shaders translated
[gfx] 9100 shaders translated
[gfx] 9200 shaders translated
[gfx] 9300 shaders translated
[gfx] 9400 shaders translated
[gfx] 9500 shaders translated
[gfx] 9600 shaders translated
[gfx] 9700 shaders translated
[gfx] 9800 shaders translated
[gfx] 9900 shaders translated
[gfx] 10000 shaders translated
[gfx] 10100 shaders translated
[gfx] 10200 shaders translated
[gfx] 10300 shaders translated
[gfx] 10400 shaders translated
[gfx] 10500 shaders translated
[gfx] 10600 shaders translated
[gfx] 10700 shaders translated
[gfx] 10800 shaders translated
[gfx] 10900 shaders translated
[gfx] 11000 shaders translated
[gfx] 11100 shaders translated
[gfx] 11200 shaders translated
[gfx] 11300 shaders translated
[gfx] 11400 shaders translated
[gfx] 11500 shaders translated
[gfx] 11600 shaders translated
[gfx] 11700 shaders translated
[gfx] 11800 shaders translated
[gfx] 11900 shaders translated
[gfx] 12000 shaders translated
[gfx] 12100 shaders translated
[gfx] 12200 shaders translated
[gfx] 12300 shaders translated
[gfx] 12400 shaders translated
[gfx] 12500 shaders translated
[gfx] 12600 shaders translated
[gfx] 12700 shaders translated
[gfx] 12800 shaders translated
[gfx] 12900 shaders translated
[gfx] 13000 shaders translated
[gfx] 13100 shaders translated
[gfx] 13200 shaders translated
[gfx] 13300 shaders translated
[gfx] 13400 shaders translated
[gfx] 13500 shaders translated
[gfx] 13600 shaders translated
[gfx] 13700 shaders translated
[gfx] 13800 shaders translated
[gfx] 13900 shaders translated
[gfx] 14000 shaders translated
[gfx] 14100 shaders translated
[gfx] 14200 shaders translated
[gfx] 14300 shaders translated
[gfx] 14400 shaders translated
[gfx] 14500 shaders translated
[gfx] 14600 shaders translated
[gfx] 14700 shaders translated
[gfx] 14800 shaders translated
[gfx] 14900 shaders translated
[gfx] 15000 shaders translated
[gfx] 15100 shaders translated
[gfx] 15200 shaders translated
[gfx] 15300 shaders translated
[gfx] 15400 shaders translated
[gfx] 15500 shaders translated
[gfx] 15600 shaders translated
[gfx] 15700 shaders translated
[gfx] 15800 shaders translated
[gfx] 15900 shaders translated
[gfx] 16000 shaders translated
[gfx] 16100 shaders translated
[gfx] 16200 shaders translated
[gfx] 16300 shaders translated
[gfx] 16400 shaders translated
[gfx] 16500 shaders translated
[gfx] 16600 shaders translated
[gfx] 16700 shaders translated
[gfx] 16800 shaders translated
[gfx] 16900 shaders translated
[gfx] 17000 shaders translated
[gfx] 17100 shaders translated
[gfx] 17200 shaders translated
[gfx] 17300 shaders translated
[gfx] 17400 shaders translated
[gfx] 17500 shaders translated
[gfx] 17600 shaders translated
[gfx] 17700 shaders translated
[gfx] 17800 shaders translated
[gfx] 17900 shaders translated
[gfx] 18000 shaders translated
[gfx] 18100 shaders translated
[gfx] 18200 shaders translated
[gfx] 18300 shaders translated
[gfx] 18400 shaders translated
[gfx] 18500 shaders translated
[gfx] 18600 shaders translated
[gfx] 18700 shaders translated
[gfx] 18800 shaders translated
[gfx] 18900 shaders translated
[gfx] 19000 shaders translated
[gfx] 19100 shaders translated
[gfx] 19200 shaders translated
[gfx] 19300 shaders translated
[gfx] 19400 shaders translated
[gfx] 19500 shaders translated
[gfx] 19600 shaders translated
[gfx] 19700 shaders translated
[gfx] 19800 shaders translated
[gfx] 19900 shaders translated
[gfx] 20000 shaders translated
[gfx] 20100 shaders translated
[gfx] 20200 shaders translated
[gfx] 20300 shaders translated
[gfx] 20400 shaders translated
[gfx] 20500 shaders translated
[gfx] 20600 shaders translated
[gfx] 20700 shaders translated
[gfx] 20800 shaders translated
[gfx] 20900 shaders translated
[gfx] 21000 shaders translated
[gfx] 21100 shaders translated
[gfx] 21200 shaders translated
[gfx] 21300 shaders translated
[gfx] 21400 shaders translated
[gfx] 21500 shaders translated
[gfx] 21600 shaders translated
[gfx] 21700 shaders translated
[gfx] 21800 shaders translated
[gfx] 21900 shaders translated
[gfx] 22000 shaders translated
[gfx] 22100 shaders translated
[gfx] 22200 shaders translated
[gfx] 22300 shaders translated
[gfx] 22400 shaders translated
[gfx] 22500 shaders translated
[gfx] 22600 shaders translated
[gfx] 22700 shaders translated
[gfx] 22800 shaders translated
[gfx] 22900 shaders translated
[gfx] 23000 shaders translated
[gfx] 23100 shaders translated
[gfx] 23200 shaders translated
[gfx] 23300 shaders translated
[gfx] 23400 shaders translated
[gfx] 23500 shaders translated
[gfx] 23600 shaders translated
[gfx] 23700 shaders translated
[gfx] 23800 shaders translated
[gfx] 23900 shaders translated
[gfx] 24000 shaders translated
[gfx] 24100 shaders translated
[gfx] 24200 shaders translated
[gfx] 24300 shaders translated
[gfx] 24400 shaders translated
[gfx] 24500 shaders translated
[gfx] 24600 shaders translated
[gfx] 24700 shaders translated
[gfx] 24800 shaders translated
[gfx] 24900 shaders translated
[gfx] 25000 shaders translated
[gfx] 25100 shaders translated
[gfx] 25200 shaders translated
[gfx] 25300 shaders translated
[gfx] 25400 shaders translated
[gfx] 25500 shaders translated
[gfx] 25600 shaders translated
[gfx] 25700 shaders translated
[gfx] 25800 shaders translated
[gfx] 25900 shaders translated
[gfx] 26000 shaders translated
[gfx] 26100 shaders translated
[gfx] 26200 shaders translated
[gfx] 26300 shaders translated
[gfx] 26400 shaders translated
[gfx] 26500 shaders translated
[gfx] 26600 shaders translated
[gfx] 26700 shaders translated
[gfx] 26800 shaders translated
[gfx] 26900 shaders translated
[gfx] 27000 shaders translated
[gfx] 27100 shaders translated
[gfx] 27200 shaders translated
[gfx] 27300 shaders translated
[gfx] 27400 shaders translated
[gfx] 27500 shaders translated
[gfx] 27600 shaders translated
[gfx] 27700 shaders translated
[gfx] 27800 shaders translated
[gfx] 27900 shaders translated
[gfx] 28000 shaders translated
[gfx] 28100 shaders translated
[gfx] 28200 shaders translated
[gfx] 28300 shaders translated
[gfx] 28400 shaders translated
[gfx] 28500 shaders translated
[gfx] 28600 shaders translated
[gfx] 28700 shaders translated
[gfx] 28800 shaders translated
[gfx] 28900 shaders translated
[gfx] 29000 shaders translated
[gfx] 29100 shaders translated
[gfx] 29200 shaders translated
[gfx] 29300 shaders translated
[gfx] 29400 shaders translated
[gfx] 29500 shaders translated
[gfx] 29600 shaders translated
[gfx] 29700 shaders translated
[gfx] 29800 shaders translated
[gfx] 29900 shaders translated
[gfx] 30000 shaders translated
[gfx] 30100 shaders translated
[gfx] 30200 shaders translated
[gfx] 30300 shaders translated
[gfx] 30400 shaders translated
[gfx] 30500 shaders translated
[gfx] 30600 shaders translated
[gfx] 30700 shaders translated
[gfx] 30800 shaders translated
[gfx] 30900 shaders translated
[gfx] shader cache: replayed 30951 shaders, 19834 pipelines queued (971 ms) from /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/shaders.bin
[thread] start "AudioLaunchThread" core 0 prio 16 affinity 1
[thread] start "AudioDrcVsCtrlSubCoreThread" core 0 prio 16 affinity 1
[heap] frame allocator waits for its buffer
[audio] CoreAudio output started (48 kHz stereo)
[ax] initialized, 96 voices
[thread] start "nw::snd::SoundThread" core 0 prio 4 affinity 1
[thread] start "nw::snd::TaskThread" core 0 prio 3 affinity 1
[gx2] frame 1, inf swaps/s, swap interval 1
[input] keyboard/controller act as GamePad
[display] GamePad screen shown (off)
[overlay] font /System/Library/Fonts/Supplemental/Arial Unicode.ttf
[gfx] frame 1, 12 draws so far, GPU 0.5 ms/frame, render thread CPU 0.00 ms/frame
[gfx] 30951 shaders, 15 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 3, 0 ms in all
[gfx] last 300 frames: 3 full texture checks (6.7 MiB hashed), 6 uploads, 214 invalidates marking 0 surfaces, 0 texture page write faults, 4 pages protected
[thread] start "PtclParallelExecuter" core 1 prio 17 affinity 2
[thread] start "PtclParallelExecuter" core 2 prio 17 affinity 4
[save] fs open /vol/save/common/FFLResMiddle.dat (r) -> not found host /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/save/common/FFLResMiddle.dat
[thread] start "SaveMgrThread" core 1 prio 19 affinity 2
[thread] start "OliveThread" core 1 prio 24 affinity 2
[thread] start "CommentMgrThread" core 1 prio 25 affinity 2
[save] startReadPersonalSavedata requested
[save] load rp_personal_savedata.dat starting
[thread] start "BackgroundLoadMgrThread" core 1 prio 22 affinity 2
[save] SAVEOpenFile slot=1 rp_personal_savedata.dat (r)
[save] fs open /vol/save/user/rp_personal_savedata.dat (r) -> ok host /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/save/user/rp_personal_savedata.dat (8 bytes)
[save] load rp_personal_savedata.dat: ok (0), host 8 bytes, dir /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/save
[save] startReadGame requested
[save] load rp_savedata.dat starting
[save] SAVEOpenFile slot=255 rp_savedata.dat (r)
[save] fs open /vol/save/common/rp_savedata.dat (r) -> ok host /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/save/common/rp_savedata.dat (45364 bytes)
[save] load rp_savedata.dat: ok (0), last_file=1, magic=52505344 ver=1.0.2, files 0- 1+ 2-, dir /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/nsmbu/save
[save] host common/rp_savedata.dat 45364 bytes
[save] guest after load file 1 main exists=1 lives=5 map=0.0.10 flags=1 quick exists=0 lives=0 map=0.0.0 flags=0
[save] host file 1 main exists=1 lives=5 map=0.0.10 flags=1 quick exists=0 lives=0 map=0.0.0 flags=0
[interp] present cap 144 fps: 57% holds (2.34 presents per logic)
[gx2] frame 301, 138.9 swaps/s, swap interval 1
[gfx] frame 301, 11610 draws so far, GPU 2.9 ms/frame, render thread CPU 0.92 ms/frame
[gfx] 30953 shaders, 2367 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 11, 1 ms in all
[gfx] last 300 frames: 40 full texture checks (12.9 MiB hashed), 40 uploads, 1530 invalidates marking 0 surfaces, 0 texture page write faults, 851 pages protected
[interp] present cap 144 fps: 58% holds (2.40 presents per logic)
[gx2] frame 601, 144.0 swaps/s, swap interval 1
[gfx] frame 601, 23673 draws so far, GPU 2.5 ms/frame, render thread CPU 1.07 ms/frame
[gfx] 30953 shaders, 5386 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 11, 1 ms in all
[gfx] last 300 frames: 0 full texture checks (0.0 MiB hashed), 0 uploads, 1500 invalidates marking 0 surfaces, 0 texture page write faults, 0 pages protected
[ax] aux callback: device 0 index 0 bus 1 func 02B3E0D4
[scene] FileSelect (controller GamePad)
[interp] 48.9 logic steps/s (144 fps, 2.37 presents per logic)
[ax] aux callback: device 0 index 0 bus 1 func 00000000
[ax] aux callback: device 0 index 0 bus 1 func 02B3E0D4
[ax] aux callback: device 0 index 0 bus 1 func 00000000
[ax] aux callback: device 0 index 0 bus 1 func 02B3E0D4
[interp] in-between frames paused: logic pass 21.8 ms
[course-reload] watching /Applications/Apps/NewSuperMarioBrosU-NsmbuRecomp/data/game/content/Common/course_res_pack/1-39.szs
[interp] in-between frames resumed: logic pass 1.2 ms
[gx2] ignored display list 12D97A80 of 0 bytes
[gx2] ignored display list 0AD97A80 of 0 bytes
[interp] present cap 144 fps: 56% holds (2.27 presents per logic)
[gx2] frame 901, 131.2 swaps/s, swap interval 1
[gfx] frame 901, 60419 draws so far, GPU 4.8 ms/frame, render thread CPU 1.65 ms/frame
[gfx] skipped draws: dropped-depth=736
[gfx] 30953 shaders, 8465 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 258 full texture checks (749.9 MiB hashed), 74 uploads, 21063 invalidates marking 183 surfaces, 2 texture page write faults, 1178 pages protected
[gx2] ignored display list 10D97A80 of 0 bytes
[gx2] ignored display list 12D97A80 of 0 bytes
[interp] present cap 144 fps: 58% holds (2.40 presents per logic)
[gx2] frame 1201, 144.0 swaps/s, swap interval 1
[gfx] frame 1201, 116390 draws so far, GPU 6.4 ms/frame, render thread CPU 1.91 ms/frame
[gfx] skipped draws: dropped-depth=1936
[gfx] 30953 shaders, 11806 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 316 full texture checks (1201.5 MiB hashed), 16 uploads, 34047 invalidates marking 300 surfaces, 0 texture page write faults, 107 pages protected
[gx2] ignored display list 145E2EC0 of 0 bytes
[interp] models blended 0 fresh 5370 cut 0 still 0
[interp] 59.1 logic steps/s (144 fps, 2.34 presents per logic)
[gx2] ignored display list 1A101200 of 0 bytes
[interp] present cap 144 fps: 58% holds (2.38 presents per logic)
[gx2] frame 1501, 144.0 swaps/s, swap interval 1
[gfx] frame 1501, 168257 draws so far, GPU 6.2 ms/frame, render thread CPU 1.94 ms/frame
[gfx] skipped draws: dropped-depth=3136
[gfx] 30953 shaders, 15744 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 302 full texture checks (1202.5 MiB hashed), 2 uploads, 33677 invalidates marking 300 surfaces, 0 texture page write faults, 161 pages protected
[gx2] ignored display list 1B197A80 of 0 bytes
[gx2] ignored display list 15997A80 of 0 bytes
[interp] present cap 144 fps: 59% holds (2.42 presents per logic)
[gx2] frame 1801, 144.0 swaps/s, swap interval 1
[gfx] frame 1801, 221495 draws so far, GPU 5.8 ms/frame, render thread CPU 1.93 ms/frame
[gfx] skipped draws: dropped-depth=4336
[gfx] 30953 shaders, 18863 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 306 full texture checks (1201.5 MiB hashed), 6 uploads, 33243 invalidates marking 300 surfaces, 0 texture page write faults, 93 pages protected
[gfx] background pipelines done by frame 1898: 19834 built, 0 unresolved
[gx2] ignored display list 122AB800 of 0 bytes
[gx2] ignored display list 1A80F500 of 0 bytes
[interp] present cap 144 fps: 58% holds (2.40 presents per logic)
[gx2] frame 2101, 144.0 swaps/s, swap interval 1
[gfx] frame 2101, 278005 draws so far, GPU 6.0 ms/frame, render thread CPU 1.55 ms/frame
[gfx] skipped draws: dropped-depth=5536
[gfx] 30953 shaders, 19835 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 304 full texture checks (1200.6 MiB hashed), 4 uploads, 33425 invalidates marking 300 surfaces, 0 texture page write faults, 37 pages protected
[interp] models blended 0 fresh 4969 cut 0 still 0
[interp] 60.0 logic steps/s (144 fps, 2.40 presents per logic)
[gx2] ignored display list 12684240 of 0 bytes
[gx2] ignored display list 16D70520 of 0 bytes
[interp] present cap 144 fps: 58% holds (2.40 presents per logic)
[gx2] frame 2401, 144.0 swaps/s, swap interval 1
[gfx] frame 2401, 336302 draws so far, GPU 6.2 ms/frame, render thread CPU 1.22 ms/frame
[gfx] skipped draws: dropped-depth=6736
[gfx] 30953 shaders, 19835 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 301 full texture checks (1200.2 MiB hashed), 1 uploads, 33425 invalidates marking 300 surfaces, 0 texture page write faults, 11 pages protected
[gx2] ignored display list FECA1340 of 1053208576 bytes
[gx2] ignored display list 16D97A80 of 0 bytes
[interp] present cap 144 fps: 58% holds (2.38 presents per logic)
[gx2] frame 2701, 143.5 swaps/s, swap interval 1
[gfx] frame 2701, 393090 draws so far, GPU 6.0 ms/frame, render thread CPU 1.21 ms/frame
[gfx] skipped draws: dropped-depth=7936
[gfx] 30953 shaders, 19835 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 301 full texture checks (1200.1 MiB hashed), 1 uploads, 33425 invalidates marking 300 surfaces, 0 texture page write faults, 5 pages protected
[gx2] ignored display list 184AD180 of 0 bytes
[interp] models blended 0 fresh 5028 cut 0 still 0
[interp] 60.0 logic steps/s (144 fps, 2.40 presents per logic)
[gx2] ignored display list 14997A80 of 0 bytes
[interp] present cap 144 fps: 58% holds (2.40 presents per logic)
[gx2] frame 3001, 144.0 swaps/s, swap interval 1
[gfx] frame 3001, 449888 draws so far, GPU 5.9 ms/frame, render thread CPU 1.37 ms/frame
[gfx] skipped draws: dropped-depth=9136
[gfx] 30953 shaders, 19835 pipelines; ms decompile 664, msl 2, pipeline 0
[gfx] draws that waited for their compile (targets not redrawn every frame): 16, 1 ms in all
[gfx] last 300 frames: 300 full texture checks (1200.0 MiB hashed), 0 uploads, 33425 invalidates marking 300 surfaces, 0 texture page write faults, 0 pages protected
[gx2] ignored display list 13197A80 of 0 bytes
[gx2] ignored display list 149E5C80 of 0 bytes
[quit] TV window closed: quitting (no game in progress)
[quit] Quit: quitting (no game in progress)
```

</details>

Renderer info from the log: `Metal (default)`, `native GX2 -> Metal`, `Metal device: Apple M5`, internal resolution 1.5x, anisotropic filtering on, FXAA on.

### Additional details

- Game dump: `New SUPER MARIO BROS. U + New SUPER LUIGI U (USA).wua` (Minerva), title id `000500001014b700`.
- Overlay during capture: 144 fps, 60.0 logic steps/s — no performance impact, purely visual.
