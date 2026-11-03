package org.nsmbrecomp.nsmbu;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.Locale;

public final class GameFilesProvider extends DocumentsProvider {
    private static final String ROOT = "root";
    private static final String[] ROOT_COLUMNS = {Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID,
        Root.COLUMN_TITLE, Root.COLUMN_ICON, Root.COLUMN_FLAGS, Root.COLUMN_AVAILABLE_BYTES};
    private static final String[] DOCUMENT_COLUMNS = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS};

    private static final String[] READ_ONLY = {"game", "config/nsmbu/shadercache"};

    @Override public boolean onCreate() { return true; }

    private File base() throws FileNotFoundException {
        File files = getContext().getExternalFilesDir(null);
        if (files == null) throw new FileNotFoundException("The app's storage is not available");
        return files;
    }

    private static String canonical(File f) throws FileNotFoundException {
        try {
            return f.getCanonicalPath();
        } catch (IOException e) {
            throw new FileNotFoundException(e.getMessage());
        }
    }

    private String relative(File f) throws FileNotFoundException {
        String root = canonical(base()), path = canonical(f);
        if (path.equals(root)) return "";
        if (!path.startsWith(root + File.separator)) throw new FileNotFoundException("Outside the game folder");
        return path.substring(root.length() + 1);
    }

    private static boolean hidden(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        return n.endsWith(".key") || n.equals("keys.txt") || n.equals("otp.bin") || n.equals("seeprom.bin");
    }

    private File file(String id) throws FileNotFoundException {
        File base = base();
        if (id.equals(ROOT)) return base;
        if (!id.startsWith(ROOT + "/")) throw new FileNotFoundException("Unknown document " + id);
        File f = new File(base, id.substring(ROOT.length() + 1));
        String rel = relative(f);
        if (rel.isEmpty()) throw new FileNotFoundException("Unknown document " + id);
        for (String part : rel.split("/")) if (hidden(part)) throw new FileNotFoundException("Unknown document " + id);
        if (hidden(f.getName())) throw new FileNotFoundException("Unknown document " + id);
        return f;
    }

    private static boolean under(String rel, String prefix) {
        return rel.equals(prefix) || rel.startsWith(prefix + "/");
    }

    private boolean readOnly(File f) throws FileNotFoundException {
        String rel = relative(f);
        if (rel.isEmpty()) return false;
        for (String p : READ_ONLY) if (under(rel, p)) return true;
        return false;
    }

    private boolean pinned(File f) throws FileNotFoundException {
        String rel = relative(f);
        if (rel.isEmpty()) return true;
        for (String p : READ_ONLY) if (p.startsWith(rel + "/")) return true;
        return false;
    }

    private void writable(File f) throws FileNotFoundException {
        if (readOnly(f)) throw new FileNotFoundException("Read-only (the game and the shader caches): " + f.getName());
    }

    private void movable(File f) throws FileNotFoundException {
        writable(f);
        if (pinned(f)) throw new FileNotFoundException("This folder stays: " + f.getName());
    }

    private String id(File f) throws FileNotFoundException {
        String rel = relative(f);
        return rel.isEmpty() ? ROOT : ROOT + "/" + rel;
    }

    private static String mime(File f) {
        if (f.isDirectory()) return Document.MIME_TYPE_DIR;
        String name = f.getName();
        int dot = name.lastIndexOf('.');
        if (dot >= 0) {
            String type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase(Locale.ROOT));
            if (type != null) return type;
        }
        return "application/octet-stream";
    }

    private void row(MatrixCursor result, File f) throws FileNotFoundException {
        String id = id(f);
        int flags = 0;
        if (!readOnly(f)) {
            if (f.isDirectory()) flags |= Document.FLAG_DIR_SUPPORTS_CREATE;
            else flags |= Document.FLAG_SUPPORTS_WRITE;
            if (!pinned(f)) flags |= Document.FLAG_SUPPORTS_DELETE | Document.FLAG_SUPPORTS_RENAME | Document.FLAG_SUPPORTS_MOVE;
        }
        MatrixCursor.RowBuilder row = result.newRow();
        row.add(Document.COLUMN_DOCUMENT_ID, id);
        row.add(Document.COLUMN_DISPLAY_NAME, id.equals(ROOT) ? getContext().getString(R.string.app_name) : f.getName());
        row.add(Document.COLUMN_MIME_TYPE, mime(f));
        row.add(Document.COLUMN_SIZE, f.isDirectory() ? null : f.length());
        row.add(Document.COLUMN_LAST_MODIFIED, f.lastModified());
        row.add(Document.COLUMN_FLAGS, flags);
    }

    private Uri children(String parentId) {
        return DocumentsContract.buildChildDocumentsUri(getContext().getPackageName() + ".files", parentId);
    }

    private void changed(String parentId) {
        getContext().getContentResolver().notifyChange(children(parentId), null);
    }

    private static String parent(String id) {
        return id.substring(0, id.lastIndexOf('/'));
    }

    @Override public Cursor queryRoots(String[] projection) throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
        File base = getContext().getExternalFilesDir(null);
        if (base == null) return result;
        result.newRow()
            .add(Root.COLUMN_ROOT_ID, ROOT)
            .add(Root.COLUMN_DOCUMENT_ID, ROOT)
            .add(Root.COLUMN_TITLE, getContext().getString(R.string.app_name))
            .add(Root.COLUMN_ICON, getContext().getApplicationInfo().icon)
            .add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_IS_CHILD | Root.FLAG_LOCAL_ONLY)
            .add(Root.COLUMN_AVAILABLE_BYTES, base.getUsableSpace());
        return result;
    }

    @Override public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        File f = file(documentId);
        if (!f.exists()) throw new FileNotFoundException(documentId);
        row(result, f);
        return result;
    }

    @Override public Cursor queryChildDocuments(String parentDocumentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        File[] list = file(parentDocumentId).listFiles();
        if (list != null) for (File f : list) {
            if (hidden(f.getName())) continue;
            try { row(result, f); }
            catch (FileNotFoundException outside) { /* a symlink out of the folder: not shown */ }
        }
        result.setNotificationUri(getContext().getContentResolver(), children(parentDocumentId));
        return result;
    }

    @Override public boolean isChildDocument(String parentDocumentId, String documentId) {
        return parentDocumentId.equals(ROOT) ? documentId.startsWith(ROOT + "/")
                                             : documentId.startsWith(parentDocumentId + "/");
    }

    @Override public String getDocumentType(String documentId) throws FileNotFoundException {
        return mime(file(documentId));
    }

    @Override public ParcelFileDescriptor openDocument(String documentId, String mode, CancellationSignal signal)
            throws FileNotFoundException {
        File f = file(documentId);
        if (f.isDirectory()) throw new FileNotFoundException("A folder: " + documentId);
        final boolean write = !mode.equals("r");
        if (write) writable(f);
        ParcelFileDescriptor fd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.parseMode(mode));
        if (write) changed(parent(documentId));
        return fd;
    }

    private static File unique(File dir, String name) {
        File f = new File(dir, name);
        if (!f.exists()) return f;
        int dot = name.lastIndexOf('.');
        String stem = dot > 0 ? name.substring(0, dot) : name, ext = dot > 0 ? name.substring(dot) : "";
        for (int n = 1; ; n++) {
            f = new File(dir, stem + " (" + n + ")" + ext);
            if (!f.exists()) return f;
        }
    }

    private static String checkedName(String name) throws FileNotFoundException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") || name.contains("/")
                || name.indexOf('\0') >= 0 || hidden(name))
            throw new FileNotFoundException("Invalid name: " + name);
        return name;
    }

    @Override public String createDocument(String parentDocumentId, String mimeType, String displayName)
            throws FileNotFoundException {
        File dir = file(parentDocumentId);
        if (!dir.isDirectory()) throw new FileNotFoundException("Not a folder: " + parentDocumentId);
        writable(dir);
        File f = unique(dir, checkedName(displayName));
        writable(f);
        try {
            boolean made = Document.MIME_TYPE_DIR.equals(mimeType) ? f.mkdir() : f.createNewFile();
            if (!made) throw new FileNotFoundException("Cannot create " + f.getName());
        } catch (IOException e) {
            throw new FileNotFoundException(e.getMessage());
        }
        changed(parentDocumentId);
        return id(f);
    }

    private static boolean deleteTree(File f) {
        File[] list = f.isDirectory() && !isLink(f) ? f.listFiles() : null;
        if (list != null) for (File child : list) if (!deleteTree(child)) return false;
        return f.delete();
    }

    private static boolean isLink(File f) {
        return java.nio.file.Files.isSymbolicLink(f.toPath());
    }

    @Override public void deleteDocument(String documentId) throws FileNotFoundException {
        File f = file(documentId);
        movable(f);
        if (!deleteTree(f)) throw new FileNotFoundException("Cannot delete " + documentId);
        changed(parent(documentId));
    }

    @Override public void removeDocument(String documentId, String parentDocumentId) throws FileNotFoundException {
        deleteDocument(documentId);
    }

    @Override public String renameDocument(String documentId, String displayName) throws FileNotFoundException {
        File f = file(documentId);
        movable(f);
        File target = new File(f.getParentFile(), checkedName(displayName));
        writable(target);
        if (target.exists()) throw new FileNotFoundException(displayName + " exists already");
        if (!f.renameTo(target)) throw new FileNotFoundException("Cannot rename " + documentId);
        changed(parent(documentId));
        return id(target);
    }

    @Override public String moveDocument(String sourceDocumentId, String sourceParentDocumentId, String targetParentDocumentId)
            throws FileNotFoundException {
        File f = file(sourceDocumentId), dir = file(targetParentDocumentId);
        movable(f);
        if (!dir.isDirectory()) throw new FileNotFoundException("Not a folder: " + targetParentDocumentId);
        writable(dir);
        if (isChildDocument(sourceDocumentId, targetParentDocumentId) || sourceDocumentId.equals(targetParentDocumentId))
            throw new FileNotFoundException("Cannot move a folder into itself");
        File target = new File(dir, f.getName());
        writable(target);
        if (target.exists()) throw new FileNotFoundException(f.getName() + " exists already there");
        if (!f.renameTo(target)) throw new FileNotFoundException("Cannot move " + sourceDocumentId);
        changed(sourceParentDocumentId);
        changed(targetParentDocumentId);
        return id(target);
    }
}
