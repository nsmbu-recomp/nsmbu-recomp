package org.nsmbrecomp.nsmbu;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public final class SkipInstallerTest {
    public static void main(String[] args) throws Exception {
        Path dir = Files.createTempDirectory("skip-installer");
        try {
            java.io.File settings = dir.resolve("settings.ini").toFile();
            if (SkipInstaller.enabled(settings)) throw new AssertionError("missing file is off");
            SkipInstaller.set(settings, true);
            if (!SkipInstaller.enabled(settings)) throw new AssertionError("enabled");
            String body = Files.readString(settings.toPath(), StandardCharsets.UTF_8);
            if (!body.contains("skipInstaller=1\n")) throw new AssertionError(body);
            Files.writeString(settings.toPath(),
                    "# New Super Mario Bros. U settings\nfps60=1\n", StandardCharsets.UTF_8);
            SkipInstaller.set(settings, true);
            body = Files.readString(settings.toPath(), StandardCharsets.UTF_8);
            if (!body.contains("# New Super Mario Bros. U settings\n")) throw new AssertionError(body);
            if (!body.contains("fps60=1\n")) throw new AssertionError(body);
            if (!body.contains("skipInstaller=1\n")) throw new AssertionError(body);
            SkipInstaller.set(settings, false);
            if (SkipInstaller.enabled(settings)) throw new AssertionError("turned off");
            if (!Files.readString(settings.toPath(), StandardCharsets.UTF_8).contains("fps60=1\n"))
                throw new AssertionError("other keys lost");
        } finally {
            Files.walk(dir).sorted(java.util.Comparator.reverseOrder()).forEach(p -> {
                try { Files.deleteIfExists(p); } catch (Exception ignored) {}
            });
        }
    }
}
