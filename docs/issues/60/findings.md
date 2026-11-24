# Findings

## Report

Somari34 on Windows v0.8.3 reports a repeat of an earlier title-screen crash on an Intel Core i3-6006U laptop.
The baby Yoshi control screen appears, the picture goes black, and the game crashes before the title screen loads.
Crash logs name Intel's Vulkan driver module `igvk64.dll`.
The reporter asks for an optional DirectX 11 or 12 renderer for older Intel laptops.
The attached crash log path names a v0.8.2 install, not v0.8.3.

## Status

GitHub marks issue #60 closed with label `bug`.
IrishBruse offered a devel build with a second Intel workaround on 2026-11-02.
Somari34 confirmed the title screen loads and play reached the first miniboss on that build.
They noted glitchy graphics in the start Toad bonus house and uneven performance on low-end hardware.
They said the 30 fps lock helps enough for their machine.
IrishBruse closed the thread on performance follow-up the same day.

## CHANGELOG

Version 0.8.4 credits issue #60 for fixing title-screen crashes on older Intel Windows graphics after the intro.
Version 0.8.2 records the same title-screen fix without citing #60.
Neither entry mentions a DirectX renderer option.

## Code and docs

`docs/developer/vulkan.md` ties `igvk64.dll` crashes to title-screen shadow maps on old Intel Windows Vulkan drivers.
`runtime/src/gfx/vulkan/rendering_path.h` selects classic render passes when Intel Windows driver version is below 101.5000.
`runtime/src/gfx/vulkan/attachment_view_usage.h` and `runtime/src/gfx/vulkan/backend.cpp` apply the related Intel Vulkan workarounds at startup.
`runtime/test/rendering_path_test.cpp` covers the Intel driver version rule.