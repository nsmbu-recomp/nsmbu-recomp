# Findings

## Report (translated)

The reporter uses Windows 10 on an Intel Core i5-3210M laptop with 4 GB RAM.
They believe the port should run on this third-generation CPU.
They ask for legacy OpenGL support or DirectX 10/11 because those APIs exist on the machine.
They tested NSMBU v0.9.1 with the USA base disc.
Setup completed recompilation and linked `nsmbu.exe` twice successfully.
Shader precompile then failed with exit code 1 before it finished.
The app log shows `renderer: Vulkan (default)`.
Startup stops with a fatal error loading `vulkan-1.dll`.
Windows reports the module cannot be found (Spanish: *No se puede encontrar el módulo especificado*).

## Metadata

Issue #70 is open, labeled `bug`, filed 2026-11-11 by `mantoto2326`.
The title asks for old OpenGL support.
The only comment tells the reporter to update graphics drivers.

## Code and changelog

The Windows build uses Vulkan only; there is no OpenGL renderer in `runtime/src/gfx/`.
`docs/user/windows/README.md` states the port uses Vulkan.
`renderer.h` exposes only Metal and Vulkan backends.
This failure happens before any GPU feature check.
`runtime/src/gfx/vulkan/backend.cpp` throws when `SDL_Vulkan_LoadLibrary` cannot load `vulkan-1.dll`.
Issue #63 fixed Vulkan 1.1/1.2 drivers that lack dynamic rendering (#63).
That fix does not apply here because the Vulkan runtime itself is missing.