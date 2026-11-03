package org.nsmbrecomp.nsmbu;

import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.File;
import org.json.JSONObject;

/** PC-built APK: player-chosen dump copied into the app game folder for native --game. */
public final class PcGameFolder {
    private PcGameFolder() {}

    private static File selectionFile(Context context) {
        File external = context.getExternalFilesDir(null);
        return external == null ? null : new File(external, "pc-game-source.json");
    }

    public static File root(Context context) {
        File external = context.getExternalFilesDir(null);
        return external == null ? null : new File(external, "game");
    }

    public static boolean ready(Context context) {
        File game = root(context);
        return game != null && new File(game, "code/red-pro2.rpx").isFile();
    }

    public static void saveSource(Context context, Uri tree, String document) throws Exception {
        File file = selectionFile(context);
        if (file == null) throw new java.io.IOException("App storage is unavailable");
        JSONObject value = new JSONObject().put("schema", 1).put("uri", tree.toString());
        if (document != null && !document.isEmpty()) value.put("document", document);
        SetupStore.write(file, value);
    }

    public static void copySaved(Context context, SetupImport.Control control) throws Exception {
        File file = selectionFile(context);
        if (file == null || !file.isFile()) throw new java.io.IOException("Choose your game folder first");
        JSONObject value = SetupStore.read(file);
        if (value.getInt("schema") != 1) throw new java.io.IOException("Unsupported game selection version");
        Uri tree = Uri.parse(value.getString("uri"));
        String document = value.optString("document", DocumentsContract.getTreeDocumentId(tree));
        File destination = root(context);
        if (destination == null) throw new java.io.IOException("App storage is unavailable");
        SetupImport.copyGameFolder(context, tree, document, destination, control);
    }
}
