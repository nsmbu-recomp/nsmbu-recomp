# Findings

## Report

The reporter on Windows v0.7.2 cannot pass the blue baby Yoshi intro animation.
The game shows a generic crash prompt and exits.
They list a Europe 1.3.0 ROM and attach crash-20261027-214724.log.
Hardware is an Intel Core i5-10300H laptop with 16 GB RAM and a GTX 1660 Ti.

## Status

GitHub marks issue #34 closed.
IrishBruse could not reproduce it on either reported game version.
Log analysis pins the fault in Intel `igvk64.dll` during an indexed draw at address `0x2A8`.
The game had reached `FileSelect` on Intel UHD Graphics with driver 30.0.101.1994.
The log shows a Europe base disc (title version 0), not the 1.3.0 update.
The reporter fixed it in the NVIDIA control panel by forcing High performance.
Windows Graphics settings alone did not work.
They still see brief freezes but call the port playable.

## CHANGELOG

No entry cites issue #34.

## What the comments and repo show

This matches other closed Intel Vulkan reports, such as issue #42.
`docs/developer/vulkan.md` documents `igvk64.dll` crashes on old Intel Windows drivers.
The intro ends near boot `FileSelect`, which lines up with the log scene name.
The user-facing fault sits in the GPU stack, not in ROM patching or port logic on its own.