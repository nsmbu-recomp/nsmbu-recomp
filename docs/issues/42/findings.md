# Findings

## Report

The reporter on Windows v0.7.2-devel cannot reach the title screen after first-run save creation.
The screen goes black after the baby Yoshi intro and the game crashes.
They run an Intel Core i3-6006U laptop with 4 GB RAM.
The crash prompt and logs point at `igvk64.dll`.
Choosing the dedicated GPU in settings did not stop the crash.
They attach two crash logs from 2026-10-30.

## Status

GitHub marks issue #42 closed with label `bug`.
IrishBruse closed it on 2026-10-31.
The issue has zero comments, so GitHub does not record a stated closure reason.
The same reporter (Somari34) later opened #60 with the same steps on v0.8.3.
Issue #60 comments show a devel build fixed the title-screen crash for that machine.

## CHANGELOG

CHANGELOG.md does not cite issue #42.
Version 0.8.2 notes a fix for Windows on older Intel graphics crashing after the intro at title-screen load.
A later entry cites the same class of bug under #60.

## Code and docs

`docs/developer/vulkan.md` documents `igvk64.dll` crashes when the title screen draws shadow maps on old Intel Windows drivers.
`runtime/src/gfx/vulkan/attachment_view_usage.h` enables `VkImageViewUsageCreateInfo` on Intel GPUs by default.
`runtime/src/gfx/vulkan/rendering_path.h` picks classic render passes when Intel Windows driver version is below 101.5000.
`runtime/src/gfx/vulkan/backend.cpp` wires both workarounds at startup.
`runtime/test/attachment_view_usage_test.cpp` covers the Intel vendor rule.