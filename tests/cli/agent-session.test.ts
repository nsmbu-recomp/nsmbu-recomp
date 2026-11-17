import assert from "node:assert/strict";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { describe, test } from "node:test";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { agentDir, agentSession, agentSocketPath, agentUserDir, setAgentSession } from "../../tools/cli/src/agent/paths.ts";
import { liveAgents } from "../../tools/cli/src/agent/process.ts";
import { applySession, takeSessionFlag } from "../../tools/cli/src/agent/session.ts";
import { ensureLiveAgentIdle } from "../../tools/cli/src/playtest/session.ts";

const repoRoot = join(fileURLToPath(new URL(".", import.meta.url)), "..", "..");

function restoreEnv(prev: string | undefined): void {
    if (prev === undefined) delete process.env.NSMBU_SESSION;
    else process.env.NSMBU_SESSION = prev;
}

describe("agent session", { concurrency: false }, () => {
test("takeSessionFlag accepts -s and --session anywhere", () => {
    assert.deepEqual(takeSessionFlag(["-s", "agent-1", "open", "1-1"]), {
        session: "agent-1",
        rest: ["open", "1-1"],
    });
    assert.deepEqual(takeSessionFlag(["status", "--session", "b"]), {
        session: "b",
        rest: ["status"],
    });
    assert.deepEqual(takeSessionFlag(["--session=c", "press", "A"]), {
        session: "c",
        rest: ["press", "A"],
    });
});

test("a missing or repeated session id is rejected", () => {
    assert.throws(() => takeSessionFlag(["-s"]), /usage: nsmbu -s <id>/);
    assert.throws(() => takeSessionFlag(["-s", "a", "--session", "b", "status"]), /more than once/);
    assert.throws(() => takeSessionFlag(["-s", "../x", "open"]), /invalid session id/);
});

test("a named session has its own socket and user dir", () => {
    setAgentSession(null);
    try {
        assert.equal(agentDir("/repo"), join("/repo", ".tmp", "agent"));
        assert.equal(agentUserDir("/repo"), null);
        setAgentSession("alpha");
        assert.equal(agentDir("/repo"), join("/repo", ".tmp", "agent", "alpha"));
        assert.equal(agentUserDir("/repo"), join("/repo", ".tmp", "agent", "alpha", "user"));
        assert.equal(agentDir("/repo", null), join("/repo", ".tmp", "agent"));
        if (process.platform !== "win32") {
            assert.equal(agentSocketPath("/repo"), "/repo/.tmp/agent/alpha/ctl.sock");
            assert.equal(agentSocketPath("/repo", null), "/repo/.tmp/agent/ctl.sock");
            const longRepo = join("/home", "a".repeat(90));
            const sock = agentSocketPath(longRepo);
            assert.ok(sock.length < 108);
            assert.match(sock, /nsmbu-agent/);
        }
    } finally {
        setAgentSession(null);
    }
});

test("NSMBU_SESSION applies when -s is absent and the flag wins", () => {
    const prev = process.env.NSMBU_SESSION;
    try {
        process.env.NSMBU_SESSION = "from-env";
        assert.deepEqual(applySession(["status"]), ["status"]);
        assert.equal(agentSession(), "from-env");
        assert.deepEqual(applySession(["-s", "flag", "open"]), ["open"]);
        assert.equal(agentSession(), "flag");
    } finally {
        restoreEnv(prev);
        setAgentSession(null);
    }
});

test("a live named session does not count as the default session", async () => {
    const repo = join(repoRoot, ".tmp", "cli-session-test");
    rmSync(repo, { recursive: true, force: true });
    const pidPath = join(repo, ".tmp", "agent", "beta", "pid");
    mkdirSync(join(pidPath, ".."), { recursive: true });
    writeFileSync(pidPath, `${process.pid}\n`);
    setAgentSession(null);
    try {
        const live = await liveAgents(repo);
        assert.deepEqual(
            live.map((agent) => agent.id),
            ["beta"],
        );
        await assert.rejects(ensureLiveAgentIdle(repo), /session beta/);
        assert.equal(agentDir(repo), join(repo, ".tmp", "agent"));
    } finally {
        setAgentSession(null);
        rmSync(repo, { recursive: true, force: true });
    }
});
});
