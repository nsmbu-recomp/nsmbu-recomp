package org.nsmbrecomp.nsmbu;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/** The app-icon shortcut past the installer, stored as skipInstaller in settings.ini. */
public final class SkipInstaller {
    static final String KEY = "skipInstaller";

    private SkipInstaller() {}

    public static boolean enabled(java.io.File settings) {
        if (settings == null || !settings.isFile()) return false;
        try {
            for (String line : Files.readAllLines(settings.toPath(), StandardCharsets.UTF_8)) {
                int eq = line.indexOf('=');
                if (eq > 0 && line.substring(0, eq).equals(KEY))
                    return "1".equals(line.substring(eq + 1).trim());
            }
        } catch (IOException ignored) {
        }
        return false;
    }

    public static void set(java.io.File settings, boolean on) throws IOException {
        if (settings == null) throw new IOException("No settings file");
        java.io.File parent = settings.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create settings folder");
        List<String> lines = settings.isFile()
                ? new ArrayList<>(Files.readAllLines(settings.toPath(), StandardCharsets.UTF_8))
                : new ArrayList<>();
        String value = KEY + "=" + (on ? "1" : "0");
        boolean replaced = false;
        for (int i = 0; i < lines.size(); i++) {
            int eq = lines.get(i).indexOf('=');
            if (eq > 0 && lines.get(i).substring(0, eq).equals(KEY)) {
                lines.set(i, value);
                replaced = true;
            }
        }
        if (!replaced) lines.add(value);
        Path path = settings.toPath();
        Path tmp = path.resolveSibling(settings.getName() + ".tmp");
        Files.write(tmp, (String.join("\n", lines) + "\n").getBytes(StandardCharsets.UTF_8));
        try {
            Files.move(tmp, path, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (java.nio.file.AtomicMoveNotSupportedException unsupported) {
            Files.move(tmp, path, StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
