# Findings

## Report

The reporter on Windows 10 cannot finish first-time setup on v0.8.1.
They pick an extracted folder named `New SUPER MARIO BROS. U [ARPE01]`.
The launcher stops on the step labeled "Translating the game code to C".
They attach `setup.log` and a screenshot but do not quote an error in the issue body.
Hardware is an Intel Core i5-8365U laptop with 8 GB RAM and Intel UHD Graphics 620.

## Status

GitHub marks issue #54 closed (`bug`).
Alaenawawi7 opened it on 2026-11-01.
Somari34 confirms the same stall on fresh install and on a title-version-64 copy.
IrishBruse closed it on 2026-11-02 with: "Fixed in `0.8.2`."
Alaenawawi7 then updated and reported setup worked.

## Changelog

No entry cites issue #54.
v0.8.2 (2026-11-01) lists: "Setup finishes translating a USA or Europe base disc."
The same release also lists Android setup finishing translation instead of stopping.
The folder tag `ARPE01` is the Europe Wii U product id for this title.

## Code

Setup enters the `translate` step in `installer/setup.py` and calls `recompile()`.
That function runs `recomp/recomp.py` on `code/red-pro2.rpx` and raises `SetupError` on a nonzero exit.
The launcher label comes from the `translate` entry in `LAUNCHER_STAGES`.
Build detection and per-disc hook rules live in `recomp/builds.py` and `recomp/hooks*.txt`.
Issue #53 on the same date reports a base-disc hook mismatch at this step; v0.8.2 fixed that class of failure.
This issue does not state the log error, so the exact subprocess failure is not confirmed here.