package org.nsmbrecomp.nsmbu;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Process;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

final class SaveFiles {
    private SaveFiles() {}

    static File saveDir(Context c) {
        File files = c.getExternalFilesDir(null);
        return new File(files, "save");
    }

    static String stamp() {
        return new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
    }

    static boolean hasSave(Context c) {
        return new File(saveDir(c), "user/rp_personal_savedata.dat").isFile();
    }

    static void zip(File dir, OutputStream out) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            add(zip, dir, "");
        }
    }

    private static void add(ZipOutputStream zip, File f, String path) throws IOException {
        File[] children = f.listFiles();
        if (children == null) return;
        for (File child : children) {
            String name = path.isEmpty() ? child.getName() : path + "/" + child.getName();
            if (child.isDirectory()) {
                add(zip, child, name);
            } else {
                zip.putNextEntry(new ZipEntry(name));
                try (InputStream in = new FileInputStream(child)) {
                    copy(in, zip);
                }
                zip.closeEntry();
            }
        }
    }

    static String saveRoot(InputStream in) throws IOException {
        try (ZipInputStream zip = new ZipInputStream(in)) {
            for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                String n = e.getName().replace('\\', '/');
                if (n.equals("user/rp_personal_savedata.dat")) return "";
                int i = n.indexOf("/user/rp_personal_savedata.dat");
                if (i > 0 && n.length() == i + "/user/rp_personal_savedata.dat".length() && n.indexOf('/') == i) return n.substring(0, i + 1);
            }
        }
        return null;
    }

    static int unzip(InputStream in, String root, File dir) throws IOException {
        int count = 0;
        String base = dir.getCanonicalPath() + File.separator;
        try (ZipInputStream zip = new ZipInputStream(in)) {
            for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                String n = e.getName().replace('\\', '/');
                if (!n.startsWith(root) || e.isDirectory()) continue;
                File out = new File(dir, n.substring(root.length()));
                if (!out.getCanonicalPath().startsWith(base)) throw new IOException("bad entry " + n);
                File parent = out.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("cannot create " + parent);
                try (OutputStream o = new FileOutputStream(out)) {
                    copy(zip, o);
                }
                count++;
            }
        }
        return count;
    }

    static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null) for (File c : children) deleteTree(c);
        f.delete();
    }

    static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[65536];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
    }

    static void stopGame(Context c) {
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null || am.getRunningAppProcesses() == null) return;
        for (ActivityManager.RunningAppProcessInfo p : am.getRunningAppProcesses())
            if (p.processName.equals(c.getPackageName()) && p.pid != Process.myPid()) {
                SystemExitReports.expectStop(c, p.pid);
                Process.killProcess(p.pid);
            }
    }
}
