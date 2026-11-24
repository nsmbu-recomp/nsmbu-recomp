# Findings

## Report

The reporter plays on 21:9 on Windows with Intel and Nvidia hardware.
They see the TV picture stretched to fill the wide screen.
They want Hor+ behavior that widens horizontal field of view with extra space on the sides.
They attached `nsmbu.log` and comparison screenshots.
Steps are enable 21:9 and play in World 1.

## Issue status

GitHub issue #24 is **closed**.
`kolomeets80` filed it on 2026-10-26.
`IrishBruse` replied the same day that **0.7.0** would ship a partial fix.
The reporter thanked the maintainer.
The issue metadata shows no further discussion before close on 2026-11-07.

## Changelog

`CHANGELOG.md` does not cite issue #24.
**0.7.0** (2026-10-26) says wider windows show more scene instead of stretching a 16:9 picture.
**0.7.1** and **0.7.2** list many 21:9 layout and pop-in fixes.
**0.7.1** notes ultrawide no longer stretches the GamePad picture.
**0.9.0** adds **Stretch picture instead of widening the view**, on by default, which restores stretch when enabled.

## Code

Aspect behavior lives under `runtime/src/hooks/aspect.cpp` and `runtime/src/gfx/aspect.h`.
`aspect::stretch_image()` and `aspect::set_stretch_image()` choose stretch versus Hor+ game aspect.
`aspect::on_swap()` sets game aspect to 16:9 when stretch is on and uses the requested ratio when off.
`factors()` and `adjust_projection_args()` widen horizontal projection for ultrawide ratios.
`runtime/src/gfx/display_modes.cpp` passes the stretch flag into `tv_picture()` for TV layout.
Settings UI and persistence sit in `runtime/src/overlay/overlay.cpp` and `runtime/src/gfx/vulkan/overlay_sdl.cpp` (`aspectStretch`, `NSMBU_ASPECT_STRETCH`).

## Assessment

The report matches pre-0.7.0 stretch-on-ultrawide behavior.
The changelog and aspect code show Hor+ support shipped around 0.7.0, matching the maintainer comment.
Later releases refined 21:9 HUD, menus, and scenery, then reintroduced stretch as an explicit default-on setting in 0.9.0.
Users who still see stretching should turn off **Stretch picture instead of widening the view** in Settings → Display.