export const HELP = `nsmbu — NSMBU developer and agent CLI

Game flags use --name or --name=value. Drop the NSMBU_ prefix, lowercase, and
use hyphens instead of underscores (NSMBU_NO_AUDIO → --no-audio). Every flag is
listed in the game binary's --help output.

Usage:
  nsmbu [-s ID] <command> …
  nsmbu build [--release] [--sanitizer] [--verbose] [cmake args…]
  nsmbu launch [--no-debug] [--trace] [--gdb] [--lldb] [game flags…] [slot|course] […]
  nsmbu crash
  nsmbu run [--release] [game flags…] [slot|course] […]
  nsmbu game [id] [--extract] [--recomp]
  nsmbu game --list
  nsmbu game --import PATH --id ID [--extract] [--recomp]
  nsmbu game --create ID
  nsmbu extract
  nsmbu save <save.dat|save.json> [output] [--output PATH]
  nsmbu settings [list|get KEY|set KEY VALUE|unset KEY] [--file PATH]
  nsmbu profile [slot] [--cpu] [--seconds N] [--attach PID]
  nsmbu installer [--version VERSION] [--no-open]
  nsmbu apk [--abi arm64-v8a|x86_64] [--device SERIAL] [--build-only] [--install-only] [--yes]
  nsmbu recomp
  nsmbu recomplauncher
  nsmbu test [--list] [--show] [--jobs N] [name…]
  nsmbu scenario OUT [--game PATH] [--save PATH] [--align NAME] [--steps N] [--fixed-seed N] [--bin PATH] [--shader-cache PATH] [--boot-tries N] [--boot-limit N] [--run-limit N] [NSMBU_VAR=value …] [--flag …]
  nsmbu compare-runs RUN_A RUN_B [--scene NAME] [--steps N]
  nsmbu bench [name…] [--list]
  nsmbu completion fish [--install]

Agent control (one game per session; -s ID or --session ID only):
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
  nsmbu click close
  nsmbu actors [--limit N] [--radius N]
  nsmbu eval --list | <name> [json]
  nsmbu log [--tag a,b] [--around] [--lines N]
  nsmbu picture [--frames N] [--rect x,y,w,h]
  nsmbu restart
  nsmbu quit | close
`;