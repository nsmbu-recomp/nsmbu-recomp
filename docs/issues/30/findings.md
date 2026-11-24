# Findings

## Status

Issue #30 is **closed**.
IrishBruse closed it on 2026-11-08.
The maintainer said newer builds mostly fixed it, but ultrawide fixes remain incomplete and they lack an ultrawide monitor.

## Report

The reporter used NSMBU **v0.7.0** on Windows (Europe 1.3.0, Ryzen 7 5800X3D).
They set non-16:9 aspect ratios (16:9, 16:10, 21:9, 32:9).
They saw dark side lines on the world map and a cropped world-selection image.
In 21:9 levels, scenery and enemies popped in at the old 16:9 edge.

## Changelog

The report targets v0.7.0.
**v0.7.1** (2026-10-27) lists many 21:9 fixes, including world-map dark bars outside the 16:9 frame and file-select art.
**v0.7.2** (same date) says course enemies, blocks, bushes, and scenery no longer pop in at the 16:9 edge on 21:9.
CHANGELOG does not cite `#30`.

## Likely code

Aspect handling lives under `runtime/src/gfx/aspect.h`, `aspect_view.h`, and `hooks/aspect.cpp`.
Viewport expansion uses `aspect::view::expand_ortho` and `factors` in `aspect_view.h`.
GX2 render hooks call into aspect code from `runtime/src/gx2/gx2_core.cpp`.
Background widening tests point at `bg_scroll_wide` and `aspect::scroll::widen`.
Settings UI exposes aspect choice in `runtime/src/overlay/overlay.cpp`.

## Assessment

The pop-in and world-map bar symptoms match changelog entries added in v0.7.1 and v0.7.2, right after the reported build.
Closure reflects partial fixes without full ultrawide verification.
Residual edge-case risk may remain for ratios the maintainer did not test.