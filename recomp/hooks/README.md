# Runtime hook lists

Each `.jsonc` file lists guest addresses the recompiler replaces.
`hooks` is an array of USA 1.3.0 function entries.
`sites` is an array of single instructions.
`address` is eight hex digits.
A `//` comment names that entry.
`"same": true` on a hook is a whole-function wrap that was checked on builds that compile the body differently.
`"builds": ["NAME", ...]` limits the file to those builds.

- `aspect.jsonc` — widescreen camera, projections, and layout panes
- `base_hud.jsonc` — base-disc course stock HUD (`USA_V0`, `EU_V0`)
- `course.jsonc` — course open, the world map, and the course HUD
- `draw.jsonc` — missing model and Mii draws
- `heap.jsonc` — frame allocator wait
- `interp.jsonc` — frame interpolation and course-layer camera edges
- `map_menu.jsonc` — world-map settings button
- `save.jsonc` — save log and quick-record sites
