package org.nsmbrecomp.nsmbu;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

public final class SetupLogFilesTest {
    public static void main(String[] args) throws Exception {
        File job = Files.createTempDirectory("nsmbu-setup-logs").toFile();
        try {
            if (!SetupLogFiles.sources(null).isEmpty()) throw new AssertionError("null job");
            if (!SetupLogFiles.sources(job).isEmpty()) throw new AssertionError("empty job");
            Files.write(new File(job, "host.json").toPath(), "{\"state\":\"failed\"}\n".getBytes(StandardCharsets.UTF_8));
            Files.write(new File(job, "state.json").toPath(), "{\"error\":\"stopped\"}\n".getBytes(StandardCharsets.UTF_8));
            File input = new File(job, "input");
            if (!input.mkdirs()) throw new AssertionError("input");
            Files.write(new File(input, "disc.key").toPath(), "secret\n".getBytes(StandardCharsets.UTF_8));
            File oldStage = new File(job, "checkpoints/translation-old");
            File newStage = new File(job, "checkpoints/translation-new");
            File compile = new File(job, "checkpoints/compile-1");
            if (!oldStage.mkdirs() || !newStage.mkdirs() || !compile.mkdirs()) throw new AssertionError("stages");
            File older = new File(oldStage, "translation.log");
            File newer = new File(newStage, "translation.log");
            Files.write(older.toPath(), "old\n".getBytes(StandardCharsets.UTF_8));
            Files.write(newer.toPath(), "new translation\n".getBytes(StandardCharsets.UTF_8));
            if (!older.setLastModified(1_000) || !newer.setLastModified(2_000)) throw new AssertionError("mtime");
            Files.write(new File(compile, "compile.log").toPath(), "compile\n".getBytes(StandardCharsets.UTF_8));
            Files.write(new File(newStage, "notes.txt").toPath(), "skip\n".getBytes(StandardCharsets.UTF_8));
            List<SetupLogFiles.Piece> pieces = SetupLogFiles.sources(job);
            String[] names = new String[pieces.size()];
            File translation = null;
            for (int i = 0; i < pieces.size(); i++) {
                names[i] = pieces.get(i).shareName;
                if (names[i].equals("setup-translation.log")) translation = pieces.get(i).file;
                if (pieces.get(i).file.getPath().contains("disc.key")) throw new AssertionError("shared a key");
            }
            expect("setup-host.json,setup-state.json,setup-translation.log,setup-compile.log", String.join(",", names));
            if (translation == null || !translation.equals(newer)) throw new AssertionError("kept the older translation log");
            byte[] body = new byte[80];
            for (int i = 0; i < body.length; i++) body[i] = (byte) ('a' + (i % 26));
            File big = new File(job, "big.log");
            Files.write(big.toPath(), body);
            File tail = new File(job, "tail.log");
            SetupLogFiles.copyTail(big, tail, 10);
            String copied = new String(Files.readAllBytes(tail.toPath()), StandardCharsets.UTF_8);
            if (!copied.startsWith("... earlier log omitted (70 bytes) ...\n")) throw new AssertionError(copied);
            String end = copied.substring(copied.lastIndexOf('\n') + 1);
            if (!end.equals(new String(body, 70, 10, StandardCharsets.UTF_8))) throw new AssertionError(copied);
        } finally {
            delete(job);
        }
    }

    private static void expect(String expected, String actual) {
        if (!expected.equals(actual)) throw new AssertionError("Expected " + expected + ", got " + actual);
    }

    private static void delete(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) delete(child);
        file.delete();
    }
}
