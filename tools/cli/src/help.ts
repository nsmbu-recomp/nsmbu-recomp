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
  nsmbu record <course> [name] […]
  nsmbu replay [--release] [--uncapped] [--no-exit-on-input] [--exit-at N] [--log PATH] <name> […]
  nsmbu profile […]
  nsmbu perf […]
  nsmbu installer [--version VERSION] [--no-open]
  nsmbu apk [--abi arm64-v8a|x86_64] [--device SERIAL] [--build-only] [--install-only] [--yes]
  nsmbu lint
  nsmbu recomp
  nsmbu scripted <name> […]
  nsmbu test [name…] [--list]
  nsmbu bench [name…] [--list]
  nsmbu completion fish [--install]

Agent control (single game instance):
  nsmbu open [slot|course|course-file] […]
  nsmbu status
  nsmbu press <button|hex> [--frames N]
  nsmbu stick <x> <y> [--frames N]
  nsmbu rstick <x> <y> [--frames N]
  nsmbu screenshot [path]
  nsmbu wait --frame N | --step N | --scene NAME [--timeout SEC]
  nsmbu quit | close
`;
