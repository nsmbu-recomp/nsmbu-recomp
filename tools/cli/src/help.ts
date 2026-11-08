export const HELP = `nsmbu — NSMBU developer and agent CLI

Usage:
  nsmbu build [--release] [--sanitizer] [cmake args…]
  nsmbu launch [--no-debug] [--trace] [--gdb] [--lldb] [slot|course] […]
  nsmbu crash
  nsmbu wine [--no-debug] [--trace] [slot|course] […]
  nsmbu run [--release] [slot|course] […]
  nsmbu game [id] [--extract] [--recomp]
  nsmbu game --list
  nsmbu game --import PATH --id ID [--extract] [--recomp]
  nsmbu game --create ID
  nsmbu extract
  nsmbu save <save.dat|save.json> [output] [--output PATH]
  nsmbu profile […]
  nsmbu perf […]
  nsmbu installer [--version VERSION] [--no-open]
  nsmbu apk [--abi arm64-v8a|x86_64] [--device SERIAL] [--build-only] [--install-only] [--yes]
  nsmbu recomp
  nsmbu scripted <name> […]
  nsmbu test [--list] [--show] [name…]
  nsmbu scenario OUT [VAR=value …]
  nsmbu compare-runs RUN_A RUN_B [--scene NAME] [--steps N]
  nsmbu bench [name…] [--list]
  nsmbu completion fish [--install]

Agent control (single game instance):
  nsmbu open [slot|course|course-file] […]
  nsmbu status
  nsmbu leave
  nsmbu press <button|hex> [--frames N]
  nsmbu stick <x> <y> [--frames N]
  nsmbu rstick <x> <y> [--frames N]
  nsmbu screenshot [path]
  nsmbu wait --frame N | --step N | --scene NAME | --cursor NAME [--timeout SEC]
  nsmbu goto <title|file|map|W-C> [--timeout SEC]
  nsmbu ui
  nsmbu actors [--limit N] [--radius N]
  nsmbu eval --list | <name> [json]
  nsmbu log [--tag a,b] [--lines N]
  nsmbu picture [--frames N] [--rect x,y,w,h]
  nsmbu restart
  nsmbu quit | close
`;
