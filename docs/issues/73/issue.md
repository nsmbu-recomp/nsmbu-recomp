<!-- GitHub issue #73: My game Crash in NSMBU in (fr) defis de voll2: (OPEN) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/73 -->

### What happens

CRASH: Windows exception C0000005 at 00007FFE4578C61C in ig11icd64.dll+0x5FC61C (base 00007FFE45190000)
  module: C:\WINDOWS\System32\DriverStore\FileRepository\iigd_dch.inf_amd64_c3bafbe80a0ff678\ig11icd64.dll
  access violation: read of address 00000000000357B8
  host backtrace:
  #0  00007FFE4578C61C in ig11icd64.dll+0x5FC61C (base 00007FFE45190000)
  #1  00007FFE4576D6FD in ig11icd64.dll+0x5DD6FD (base 00007FFE45190000)
  #2  00007FFE4586738C in ig11icd64.dll+0x6D738C (base 00007FFE45190000)
  #3  00007FFE457CBFA8 in ig11icd64.dll+0x63BFA8 (base 00007FFE45190000)
  #4  00007FFE45863939 in ig11icd64.dll+0x6D3939 (base 00007FFE45190000)
  #5  00007FFE45768740 in ig11icd64.dll+0x5D8740 (base 00007FFE45190000)
  #6  00007FFE45768589 in ig11icd64.dll+0x5D8589 (base 00007FFE45190000)
  #7  00007FFE45718A0B in ig11icd64.dll+0x588A0B (base 00007FFE45190000)
  #8  00007FFEAACBEF6A in ntdll.dll+0x11EF6A (base 00007FFEAABA0000)
  #9  00007FFEAABBB2E3 in ntdll.dll+0x1B2E3 (base 00007FFEAABA0000)
  #10 00007FFEAAC2027F in ntdll.dll+0x8027F (base 00007FFEAABA0000)
  #11 00007FFEAAC1EF8E in ntdll.dll+0x7EF8E (base 00007FFEAABA0000)
  #12 00007FFEA90117AB in KERNEL32.DLL+0x417AB (base 00007FFEA8FD0000)
  #13 00007FFEA83A0813 in ucrtbase.dll+0xA0813 (base 00007FFEA8300000)
  #14 00007FF622B3510E in nsmbu.exe+0x19510E (base 00007FF6229A0000)
  #15 00007FF622A3D01D in nsmbu.exe+0x9D01D (base 00007FF6229A0000)
  #16 00007FF6229A10FB in nsmbu.exe+0x10FB (base 00007FF6229A0000)
  #17 00007FFEA8FFDDA7 in KERNEL32.DLL+0x2DDA7 (base 00007FFEA8FD0000)
  #18 00007FFEAAC1EE4C in ntdll.dll+0x7EE4C (base 00007FFEAABA0000)

--- crash context (latest game-thread snapshot) ---
renderer=Vulkan
resolution=0.5
frame_mode=60 fps
frame_target=60
controller=GamePad
NSMBU_STATE_DIR=E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/states
NSMBU_VK_REUSE_UNIFORM_SNAPSHOTS=1
NSMBU_VK_REUSE_FEEDBACK_IMAGES=1
NSMBU_VK_SKIP_REDUNDANT_BINDS=1
NSMBU_VK_DESCRIPTOR_RANKS=1
NSMBU_VK_PIPELINE_LOOKASIDE=1
NSMBU_VK_SHADER_ADDRESS_MEMO=1
NSMBU_VK_FETCH_MEMO=1
NSMBU_VK_SPECIALIZE_INDICES=1
NSMBU_VK_SHADER_STATE_MEMO=1
NSMBU_VK_SKIP_VERTEX_BINDS=1
NSMBU_VK_SAMPLER_MEMO=1
NSMBU_VK_SPARSE_HASH_MEMO=1
NSMBU_VK_SHADER_KEY_DIRTY=<redacted>
NSMBU_VK_REUSE_VERTEX_SNAPSHOTS=1
NSMBU_VK_VERTEX_HISTORY_REUSE=1
crash recovery: off (Save States > Crash Recovery to turn it on)

--- last log lines ---
[log] writing E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/logs/nsmbu.log
[boot] New Super Mario Bros. U v0.9.1 (f99ee83), Windows 11 26H2 (build 26300.9711)
[boot] game version USA base disc (USA_V0, title 0005000010101d00, title version 0)
[gfx] renderer: Vulkan (default)
[boot] loaded E:\Jeux\MARIO BROS\roms\New SUPER MARIO BROS U [Game] [0005000010101d00]/code/red-pro2.rpx (USA base disc / USA_V0, title 0005000010101d00 version 0): entry 02AA2E50 sda 10231120 sda2 10008000 data end 10229104
[boot] save dir E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/save (rp_savedata.dat 45364 bytes)
[boot] rp_personal_savedata.dat 8 bytes
[mem] default heap 1022A000-50000000
[vulkan] layers: VK_LAYER_VALVE_steam_overlay (1.3.207), VK_LAYER_VALVE_steam_fossilize (1.4.303)
[vulkan] GPU 0: Intel(R) HD Graphics Gen11 (integrated, 8086:4e61): usable
[vulkan] rendering path: render passes (driver rule: Intel Windows < 101.5000)
[vulkan] chosen GPU: Intel(R) HD Graphics Gen11 (setting)
[vulkan] device: Intel(R) HD Graphics Gen11 (Vulkan 1.3.215, driver 101.2130)
[vulkan] attachment views name their usage (NSMBU_VK_ATTACHMENT_VIEW_USAGE=0 turns it off)
[vulkan] TV present mode fifo (available: fifo, immediate)
[vulkan] TV swapchain 3 images, present mode fifo
[vulkan] GamePad present mode fifo (available: fifo, immediate)
[vulkan] GamePad swapchain 3 images, present mode fifo
[gfx] internal resolution 0.5x
[gfx] anisotropy off
[gfx] FXAA off
[aspect] 16:9 (original)
[aspect] stretch on
[vulkan] present mode fifo requested
[display] GamePad screen mode: off
[display] TV window starts in full screen (as it was left)
[gfx] Vulkan renderer started
[thread] start "" core 1 prio 16 affinity 2
[interp] display refresh rate 60 Hz: frame interpolation draws up to 60 fps
[display] mode 60.000 Hz (148520000/2475300), period 16666442 ns
[gx2] initialized (native GX2 -> Vulkan)
[gx2] vsync period 16666442 ns (display reported 16666442 ns)
[gx2] TV buffer format 1A
[gyro] controllers with motion sensors: 2
[thread] start "Prepare Thread" core 1 prio 20 affinity 2
[config] console language 2 (saved setting)
[thread] start "RenderObjParallelExecuterThread" core 1 prio 17 affinity 2
[thread] start "RenderObjParallelExecuterThread" core 0 prio 17 affinity 1
[thread] start "RenderObjParallelExecuterThread" core 2 prio 17 affinity 4
[test] origin at guest time 1.9296 s, logic step 0
[vulkan buffer cache] on: budget 256 MiB, device-local host-visible memory, hints on
[vulkan] upload arena memory: not host-cached; reuse checks keep CPU copies (NSMBU_VK_UPLOAD_READS=auto)
[vulkan] unbound sampled texture uses a 1x1 stand-in
[thread] start "AudioLaunchThread" core 0 prio 16 affinity 1
[thread] start "AudioDrcVsCtrlSubCoreThread" core 0 prio 16 affinity 1
[heap] frame allocator waits for its buffer
[audio] SDL output started (48 kHz stereo)
[ax] initialized, 96 voices
[thread] start "nw::snd::TaskThread" core 0 prio 3 affinity 1
[thread] start "nw::snd::SoundThread" core 0 prio 4 affinity 1
[gx2] frame 1, 375000000.0 swaps/s, swap interval 1
[input] keyboard/controller act as GamePad
[display] GamePad screen shown (off)
[vulkan] TV swapchain 3 images, present mode fifo
[thread] start "PtclParallelExecuter" core 1 prio 17 affinity 2
[thread] start "PtclParallelExecuter" core 2 prio 17 affinity 4
[save] fs open /vol/save/common/FFLResMiddle.dat (r) -> not found host E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/save/common/FFLResMiddle.dat
[thread] start "SaveMgrThread" core 1 prio 19 affinity 2
[thread] start "CommentMgrThread" core 1 prio 25 affinity 2
[save] startReadPersonalSavedata requested
[thread] start "BackgroundLoadMgrThread" core 1 prio 22 affinity 2
[save] load rp_personal_savedata.dat starting
[save] SAVEOpenFile slot=1 rp_personal_savedata.dat (r)
[save] fs open /vol/save/user/rp_personal_savedata.dat (r) -> ok host E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/save/user/rp_personal_savedata.dat (8 bytes)
[save] load rp_personal_savedata.dat: ok (0), host 8 bytes, dir E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/save
[save] startReadGame requested
[save] load rp_savedata.dat starting
[save] SAVEOpenFile slot=255 rp_savedata.dat (r)
[save] fs open /vol/save/common/rp_savedata.dat (r) -> ok host E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/save/common/rp_savedata.dat (45364 bytes)
[save] load rp_savedata.dat: ok (0), last_file=0, magic=52505344 ver=1.0.1, files 0- 1- 2-, dir E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/save
[save] host common/rp_savedata.dat 45364 bytes
[save] guest after load file 0 main exists=0 lives=0 map=0.0.0 flags=0 quick exists=0 lives=0 map=0.0.0 flags=0
[save] host file 0 main exists=0 lives=0 map=0.0.0 flags=0 quick exists=0 lives=0 map=0.0.0 flags=0
[gx2] frame 301, 54.0 swaps/s, swap interval 1
[ax] aux callback: device 0 index 0 bus 1 func 02A78B88
[scene] FileSelect (controller GamePad)
[course-reload] watching E:\Jeux\MARIO BROS\roms\New SUPER MARIO BROS U [Game] [0005000010101d00]\content\Common\course_res_pack\1-39.szs
[ax] aux callback: device 0 index 0 bus 1 func 00000000
[ax] aux callback: device 0 index 0 bus 1 func 02A78B88
[ax] aux callback: device 0 index 0 bus 1 func 00000000
[ax] aux callback: device 0 index 0 bus 1 func 02A78B88
[gx2] frame 601, 53.4 swaps/s, swap interval 1
[gfx] drop draw item with null model
[ax] aux callback: device 0 index 0 bus 1 func 00000000
[ax] aux callback: device 0 index 0 bus 1 func 02A78B88
[gx2] frame 901, 60.0 swaps/s, swap interval 1
[gx2] frame 1201, 60.0 swaps/s, swap interval 1
[overlay] opened
[overlay] font C:/Windows/Fonts/segoeui.ttf
[gx2] frame 1501, 60.0 swaps/s, swap interval 1
[gx2] frame 1801, 59.8 swaps/s, swap interval 1
[gx2] frame 2101, 59.8 swaps/s, swap interval 1
[gx2] frame 2401, 60.0 swaps/s, swap interval 1
[input] keyboard/controller act as Pro Controller
[display] GamePad screen hidden (off)
[input] keyboard/controller act as GamePad
[display] GamePad screen shown (off)
[gx2] frame 2701, 59.0 swaps/s, swap interval 1
[input] keyboard/controller act as Pro Controller
[display] GamePad screen hidden (off)
[input] keyboard/controller act as GamePad
[display] GamePad screen shown (off)
[input] keyboard/controller act as Pro Controller
[display] GamePad screen hidden (off)
[input] keyboard/controller act as GamePad
[display] GamePad screen shown (off)
[input] keyboard/controller act as Pro Controller
[display] GamePad screen hidden (off)
[gx2] frame 3001, 59.8 swaps/s, swap interval 1
[gx2] frame 3301, 60.0 swaps/s, swap interval 1
[gx2] frame 3601, 60.0 swaps/s, swap interval 1
[gx2] frame 3901, 60.0 swaps/s, swap interval 1
[gx2] frame 4201, 60.0 swaps/s, swap interval 1
[gx2] frame 4501, 58.6 swaps/s, swap interval 1
[gx2] frame 4801, 59.6 swaps/s, swap interval 1
[gx2] frame 5101, 59.6 swaps/s, swap interval 1
[gx2] frame 5401, 59.2 swaps/s, swap interval 1
[gx2] frame 5701, 58.3 swaps/s, swap interval 1
[gx2] frame 6001, 58.8 swaps/s, swap interval 1
[gx2] frame 6301, 60.0 swaps/s, swap interval 1
[overlay] closed
[gx2] frame 6601, 60.0 swaps/s, swap interval 1
[gx2] frame 6901, 60.0 swaps/s, swap interval 1
[gx2] frame 7201, 60.0 swaps/s, swap interval 1
[gx2] frame 7501, 60.0 swaps/s, swap interval 1
[gx2] frame 7801, 60.0 swaps/s, swap interval 1
[gx2] frame 8101, 60.0 swaps/s, swap interval 1
[overlay] opened
[input] keyboard/controller act as GamePad
[display] GamePad screen shown (off)
[gx2] frame 8401, 60.0 swaps/s, swap interval 1
[gx2] frame 8701, 59.8 swaps/s, swap interval 1
[gx2] frame 9001, 59.8 swaps/s, swap interval 1
[gx2] frame 9301, 57.7 swaps/s, swap interval 1
[gx2] frame 9601, 57.7 swaps/s, swap interval 1
[overlay] closed
[gx2] frame 9901, 60.0 swaps/s, swap interval 1
[gx2] frame 10201, 60.0 swaps/s, swap interval 1
[gx2] frame 10501, 60.0 swaps/s, swap interval 1
[gx2] frame 10801, 60.0 swaps/s, swap interval 1
[gx2] frame 11101, 60.0 swaps/s, swap interval 1
[gx2] frame 11401, 60.0 swaps/s, swap interval 1
[gx2] frame 11701, 60.0 swaps/s, swap interval 1
[gx2] frame 12001, 60.0 swaps/s, swap interval 1
[overlay] opened
[gx2] frame 12301, 60.0 swaps/s, swap interval 1
[gx2] frame 12601, 58.8 swaps/s, swap interval 1
[gx2] frame 12901, 59.6 swaps/s, swap interval 1


### Steps to reproduce

the next is fr am bad a anglish but look my captur : 

[

<img width="1919" height="1079" alt="Capture d&#39;écran 2026-11-12 100216" src="https://github.com/user-attachments/assets/9894a41f-8489-4deb-8640-4b6301a02566" />

](url)

### Where you were

in defis (my game is fr so....)

### Version

v0.9.1 (f99ee83)

### Game version

NSMBU + NSLU (USA)

### Platform

Windows

### Hardware

Intel(R) Celeron(R) N5100 @ 1.10GHz, 8 GB

### Logs

am go in the goombas and my game crash just crash why?????

### From the app

The game crashed.

A crash log was written to:
E:\Jeux\MARIO BROS\NSMBU\data/nsmbu/logs/crash-20261112-095311.log

Report crash opens GitHub and the logs folder; describe what happened yourself.
