package org.nsmbrecomp.nsmbu;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Setup status and stage logs that are safe to share. Key files and the imported dump stay out. */
public final class SetupLogFiles {
    public static final long TAIL = 1L << 20;
    private static final String[] STATUS = {"host.json", "state.json"};
    private static final String[] LOGS = {
        "extract.log", "output.log", "translation.log", "compile.log", "link.log"};

    private SetupLogFiles() {}

    public static final class Piece {
        public final File file;
        public final String shareName;
        Piece(File file, String shareName) {
            this.file = file;
            this.shareName = shareName;
        }
    }

    public static List<Piece> sources(File job) throws IOException {
        ArrayList<Piece> found = new ArrayList<>();
        if (job == null || !job.isDirectory()) return found;
        for (String name : STATUS) add(found, job, new File(job, name), "setup-" + name);
        File checkpoints = new File(job, "checkpoints");
        File[] stages = checkpoints.isDirectory() ? checkpoints.listFiles() : null;
        if (stages == null) return found;
        for (String name : LOGS) {
            File newest = null;
            for (File stage : stages) {
                if (!stage.isDirectory()) continue;
                File log = new File(stage, name);
                if (!log.isFile() || log.length() == 0) continue;
                if (newest == null || log.lastModified() >= newest.lastModified()) newest = log;
            }
            if (newest != null) add(found, job, newest, "setup-" + name);
        }
        return found;
    }

    public static void copyTail(File source, File dest, long limit) throws IOException {
        long length = source.length();
        long skip = length > limit ? length - limit : 0;
        try (FileInputStream in = new FileInputStream(source);
             FileOutputStream out = new FileOutputStream(dest)) {
            if (skip > 0) {
                out.write(("... earlier log omitted (" + skip + " bytes) ...\n").getBytes(StandardCharsets.UTF_8));
                long left = skip;
                while (left > 0) {
                    long moved = in.skip(left);
                    if (moved <= 0) {
                        if (in.read() < 0) break;
                        left--;
                        continue;
                    }
                    left -= moved;
                }
            }
            byte[] bytes = new byte[65536];
            int count;
            while ((count = in.read(bytes)) >= 0) out.write(bytes, 0, count);
        }
    }

    private static void add(List<Piece> found, File job, File file, String shareName) throws IOException {
        if (!file.isFile() || file.length() == 0 || !shareName.matches("setup-[A-Za-z0-9._-]+")) return;
        String root = job.getCanonicalPath() + File.separator;
        String path = file.getCanonicalPath();
        if (!path.startsWith(root)) return;
        String rest = path.substring(root.length());
        if (rest.equals("input") || rest.startsWith("input" + File.separator) || file.getName().endsWith(".key")) return;
        found.add(new Piece(file, shareName));
    }
}
