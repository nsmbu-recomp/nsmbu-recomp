"""Short [prof] digest for a profile log. The printed block is the result."""
import re

HEADER = re.compile(
    r"\[prof\] frame (\d+): [\d.]+ frames \([\d.]+ hold\), ([\d.]+) ms/frame, "
    r"([\d.]+) swaps/s, ([\d.]+) logic steps/s; render thread CPU ([\d.]+) ms/frame, "
    r"in ops [\d.]+ ms/frame, idle \(waiting for commands\) ([\d.]+) ms/frame"
)
WAITS = re.compile(
    r"\[prof\] render thread waits ms/frame: GPU ([\d.]+) \([\d.]+/frame\) acquire ([\d.]+) present ([\d.]+)"
)
PAIR = re.compile(r"([A-Za-z_][A-Za-z0-9_]*) ([0-9]+(?:\.[0-9]+)?)")


def _pairs(body):
    return [(name, float(ms)) for name, ms in PAIR.findall(body)]


def _top(items, limit=3):
    return sorted(items, key=lambda item: item[1], reverse=True)[:limit]


def _fmt_pairs(items):
    return ", ".join("%s %.2f" % (name, ms) for name, ms in items)


def windows(text):
    found = []
    current = None
    for line in text.splitlines():
        header = HEADER.search(line)
        if header:
            current = {
                "frame": int(header.group(1)),
                "frame_ms": float(header.group(2)),
                "swaps": float(header.group(3)),
                "logic": float(header.group(4)),
                "cpu": float(header.group(5)),
                "idle": float(header.group(6)),
                "ops": [],
                "phases": [],
                "gpu": None,
                "acquire": None,
                "present": None,
            }
            found.append(current)
            continue
        if current is None:
            continue
        if line.startswith("[prof] ops ms/frame:"):
            current["ops"] = _pairs(line.split(":", 1)[1])
            continue
        if line.startswith("[prof] draw phases"):
            body = line.split("):", 1)[-1]
            body = body.split("; untracked", 1)[0]
            current["phases"] = _pairs(body)
            continue
        waits = WAITS.search(line)
        if waits:
            current["gpu"] = float(waits.group(1))
            current["acquire"] = float(waits.group(2))
            current["present"] = float(waits.group(3))
    return found


def format_digest(text, log_path):
    found = windows(text)
    if not found:
        return "[profile] no [prof] windows in %s\n" % log_path
    last = found[-1]
    lines = [
        "[profile] log %s" % log_path,
        "[profile] last of %d windows, frame %d: %.2f ms/frame, %.1f swaps/s, %.1f logic steps/s"
        % (len(found), last["frame"], last["frame_ms"], last["swaps"], last["logic"]),
        "[profile] render cpu %.2f ms, idle %.2f ms" % (last["cpu"], last["idle"]),
    ]
    if last["gpu"] is not None:
        lines.append(
            "[profile] waits ms/frame: gpu %.2f, acquire %.2f, present %.2f"
            % (last["gpu"], last["acquire"], last["present"])
        )
    ops = _top(last["ops"])
    if ops:
        lines.append("[profile] top ops ms/frame: %s" % _fmt_pairs(ops))
    phases = _top(last["phases"])
    if phases:
        lines.append("[profile] top draw phases ms/frame: %s" % _fmt_pairs(phases))
    if len(found) > 1:
        sample = found[-3:]
        mean = sum(item["frame_ms"] for item in sample) / len(sample)
        lines.append("[profile] mean of last %d windows: %.2f ms/frame" % (len(sample), mean))
    return "\n".join(lines) + "\n"
