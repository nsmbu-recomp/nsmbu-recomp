package org.nsmbrecomp.nsmbu;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;
import androidx.core.content.FileProvider;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.ArrayList;

/** Share the game's run logs and the current setup job's stage logs.
 * Game logs are already filtered by the native crash redactor.
 * Setup sharing copies status and stage logs only, never the dump, saves, or key files. */
final class LogShare {
    private static final long LIMIT = (64L << 20) + 65536;
    static void share(Activity activity) {
        new Thread(() -> {
            try {
                Intent share = prepare(activity);
                activity.runOnUiThread(() -> {
                    if (share == null) {
                        Toast.makeText(activity, "No log yet. Run setup or start the game, then share its log.", Toast.LENGTH_LONG).show();
                        return;
                    }
                    try { activity.startActivity(Intent.createChooser(share, "Share log")); }
                    catch (Exception error) { Toast.makeText(activity, "Cannot open the share sheet", Toast.LENGTH_LONG).show(); }
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, "Cannot share logs", Toast.LENGTH_LONG).show());
            }
        }, "share-game-log").start();
    }
    static synchronized Intent prepare(android.content.Context activity) throws java.io.IOException {
        File root = activity.getExternalFilesDir(null);
        if (root == null) throw new java.io.IOException("Game storage is unavailable");
        File folder = new File(activity.getCacheDir(), "crash-share");
        if (!folder.isDirectory() && !folder.mkdirs()) throw new java.io.IOException("Cannot create share folder");
        ArrayList<Uri> logs = new ArrayList<>();
        for (String name : new String[] {"nsmbu.log", "nsmbu-previous.log"}) {
            File source = new File(new File(AndroidUserDirectories.config(root), "logs"), name);
            if (!source.isFile() || source.length() == 0) continue;
            File copy = new File(folder, name);
            try (FileInputStream in = new FileInputStream(source);
                 FileOutputStream out = new FileOutputStream(copy)) {
                long remaining = Math.min(source.length(), LIMIT);
                byte[] bytes = new byte[65536];
                while (remaining > 0) {
                    int count = in.read(bytes, 0, (int)Math.min(bytes.length, remaining));
                    if (count < 0) break;
                    out.write(bytes, 0, count);
                    remaining -= count;
                }
            }
            logs.add(FileProvider.getUriForFile(activity, activity.getPackageName() + ".crashlogs", copy));
        }
        File job = null;
        try { job = SetupStore.current(activity); }
        catch (Exception ignored) { }
        for (SetupLogFiles.Piece piece : SetupLogFiles.sources(job)) {
            File copy = new File(folder, piece.shareName);
            SetupLogFiles.copyTail(piece.file, copy, SetupLogFiles.TAIL);
            logs.add(FileProvider.getUriForFile(activity, activity.getPackageName() + ".crashlogs", copy));
        }
        if (logs.isEmpty()) return null;
        Intent share = new Intent(Intent.ACTION_SEND_MULTIPLE).setType("text/plain")
            .putParcelableArrayListExtra(Intent.EXTRA_STREAM, logs)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        ClipData clip = ClipData.newRawUri("Game log", logs.get(0));
        for (int i = 1; i < logs.size(); ++i) clip.addItem(new ClipData.Item(logs.get(i)));
        share.setClipData(clip);
        return share;
    }
}
