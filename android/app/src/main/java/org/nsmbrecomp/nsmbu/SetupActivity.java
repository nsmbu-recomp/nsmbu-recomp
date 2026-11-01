package org.nsmbrecomp.nsmbu;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.UUID;
import org.json.JSONObject;

/** Progress and controls for a durable phone setup job. */
public class SetupActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private TextView stage;
    private TextView detail;
    private TextView updateStatus;
    private android.widget.ProgressBar bar;
    private Button choose;
    private Button details;
    private Button resume;
    private Button pause;
    private Button continueAnyway;
    private String continueAction;
    private Button play;
    private CheckBox skipInstaller;
    private boolean skipSync;
    private Button disc;
    private Button common;
    private Button update;
    private Button restore;
    private final ArrayList<Button> pickers = new ArrayList<>();
    private boolean phoneSetup;
    private boolean launchingGame;
    private String actionMessage;
    private TextView keysHint;
    private TextView folderHint;
    /** A chosen folder is being checked for the game (off the UI thread). */
    private volatile boolean checkingFolder;
    private String pickerJob;
    private static final int FOLDER = 10, ARCHIVE = 11, IMAGE = 12, DISC = 13, COMMON = 14, RESTORE = 15, UPDATE = 16, PC_FOLDER = 20;
    static final String EXTRA_SHOW_SETUP = "org.nsmbrecomp.nsmbu.SHOW_SETUP";

    private boolean pcGame() {
        return PcGameFolder.ready(this);
    }

    private File settingsFile() {
        File root = getExternalFilesDir(null);
        if (root == null) return null;
        return new File(AndroidUserDirectories.config(root), "settings.ini");
    }

    /** A game the activity can launch now: the phone build AndroidGame.selected accepts, or the PC route's folder. */
    private boolean launchableGame() {
        try {
            if (!phoneSetup) return pcGame();
            return AndroidGame.selected(this) != null;
        } catch (Exception unreadable) { return false; }
    }

    private boolean setupBusy() {
        try {
            File job = SetupStore.current(this);
            File host = job == null ? null : new File(job, "host.json");
            if (host == null || !host.isFile()) return false;
            String name = SetupStore.read(host).optString("state");
            return name.equals("activating") || name.equals("importing") || name.equals("preparing") || name.equals("running");
        } catch (Exception unreadable) { return false; }
    }

    /** Skip is on, a game will start, and this open was not an explicit request to show setup. */
    private boolean shouldAutoPlay() {
        return !getIntent().getBooleanExtra(EXTRA_SHOW_SETUP, false) &&
            SkipInstaller.enabled(settingsFile()) && launchableGame() && !setupBusy() && !checkingFolder;
    }

    private void playNow() {
        if (launchingGame || isFinishing()) return;
        launchingGame = true;
        startActivity(new Intent(this, NsmbuActivity.class));
        finish();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) pickerJob = state.getString("picker_job");
        try (java.io.InputStream python = getAssets().open("python-build.json");
             java.io.InputStream compiler = getAssets().open("toolchain-apk.json");
             java.io.InputStream sdk = getAssets().open("runtime-sdk.json")) { phoneSetup = true; }
        catch (Exception unavailable) { phoneSetup = false; }
        if (phoneSetup && state == null) cleanJobs();
        if ((state != null && state.getBoolean("launching_game", false)) ||
                (state == null && shouldAutoPlay())) {
            playNow();
            return;
        }
        android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
        int padding = (int)(24 * metrics.density), gap = (int)(12 * metrics.density);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(28);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        title.setTextColor(0xFFFFFFFF);
        layout.addView(title);
        updateStatus = new TextView(this);
        updateStatus.setText(SetupPresentation.UPDATING);
        updateStatus.setTextSize(16);
        updateStatus.setVisibility(android.view.View.GONE);
        layout.addView(updateStatus);
        stage = new TextView(this);
        stage.setTextSize(20);
        stage.setTextColor(0xFFE8EEF2);
        stage.setPadding(0, gap, 0, 0);
        layout.addView(stage);
        bar = new android.widget.ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setPadding(0, gap, 0, 0);
        layout.addView(bar);
        detail = new TextView(this);
        detail.setTextSize(16);
        detail.setPadding(0, gap / 2, 0, gap);
        layout.addView(detail);
        play = button(layout, getString(R.string.setup_play), () -> startActivity(new Intent(this, NsmbuActivity.class)));
        skipInstaller = new CheckBox(this);
        skipInstaller.setText(R.string.setup_skip_installer);
        skipInstaller.setTextSize(16);
        skipInstaller.setTextColor(0xFFE8EEF2);
        skipInstaller.setPadding(0, gap, 0, 0);
        skipInstaller.setOnCheckedChangeListener((button, checked) -> {
            if (skipSync) return;
            File settings = settingsFile();
            if (settings == null) return;
            try {
                SkipInstaller.set(settings, checked);
            } catch (Exception e) {
                actionMessage = e.getMessage();
            }
        });
        layout.addView(skipInstaller);
        button(layout, getString(R.string.setup_share_log), () -> LogShare.share(this));
        play.setTextSize(20);
        play.setTextColor(0xFFFFFFFF);
        play.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF0D5C8A));
        choose = button(layout, getString(R.string.setup_choose), this::chooseKind);
        pickers.add(choose);
        folderHint = new TextView(this);
        folderHint.setText(R.string.setup_folder_hint);
        folderHint.setPadding(0, 0, 0, gap);
        layout.addView(folderHint);
        restore = button(layout, getString(R.string.setup_restore), () -> pick(RESTORE));
        keysHint = new TextView(this);
        keysHint.setText(R.string.setup_keys_detail);
        layout.addView(keysHint);
        disc = button(layout, getString(R.string.setup_disc_key), () -> pick(DISC));
        common = button(layout, getString(R.string.setup_common_key), () -> pick(COMMON));
        update = button(layout, getString(R.string.setup_update), () -> pick(UPDATE));
        resume = button(layout, getString(R.string.setup_start), () -> control(SetupService.RESUME));
        pause = button(layout, getString(R.string.setup_pause), () -> control(SetupService.PAUSE));
        continueAnyway = button(layout, getString(R.string.setup_continue_anyway), () -> { if (continueAction != null) control(continueAction); });
        continueAnyway.setVisibility(android.view.View.GONE);
        details = new Button(this, null, android.R.attr.borderlessButtonStyle);
        details.setText(R.string.setup_details_show);
        layout.addView(details);
        details.setOnClickListener(view -> {
            boolean show = status.getVisibility() != android.view.View.VISIBLE;
            status.setVisibility(show ? android.view.View.VISIBLE : android.view.View.GONE);
            details.setText(show ? R.string.setup_details_hide : R.string.setup_details_show);
        });
        status = new TextView(this);
        status.setTextSize(14);
        status.setVisibility(android.view.View.GONE);
        layout.addView(status);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);
        setContentView(scroll);
    }

    private void chooseKind() {
        new android.app.AlertDialog.Builder(this).setTitle(R.string.setup_choose)
            .setItems(new CharSequence[] {getString(R.string.setup_kind_folder), getString(R.string.setup_kind_wua),
                getString(R.string.setup_kind_image)}, (dialog, which) -> pick(which == 0 ? FOLDER : which == 1 ? ARCHIVE : IMAGE))
            .setNegativeButton(android.R.string.cancel, null).show();
    }

    private static void show(android.view.View view, boolean visible) {
        int value = visible ? android.view.View.VISIBLE : android.view.View.GONE;
        if (view.getVisibility() != value) view.setVisibility(value);
    }

    /** The plain-words line, the detail line and the bar (0..1000, -1 moving, -2 hidden). */
    private void phase(String stageText, String detailText, int progress) {
        setTextIfChanged(stage, stageText);
        setTextIfChanged(detail, detailText == null ? "" : detailText);
        show(detail, detailText != null);
        show(bar, progress > -2);
        if (progress == -1) bar.setIndeterminate(true);
        else if (progress >= 0) { bar.setIndeterminate(false); bar.setProgress(progress); }
    }

    private boolean replaceable(File job) throws Exception {
        if (job == null) return true;
        File host = new File(job, "host.json");
        if (!host.isFile()) return true;
        String state = SetupStore.read(host).optString("state");
        return state.equals("selected") || state.equals("failed") || state.equals("complete") ||
            (state.equals("paused") && new File(job, "manual-pause.json").isFile());
    }

    private void pick(int request) {
        try {
            actionMessage = null;
            if (request == PC_FOLDER) {
                if (checkingFolder) throw new Exception("Still checking the chosen folder");
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                for (java.util.Map.Entry<String, Object> extra : SetupPicker.extras(true).entrySet()) {
                    if (extra.getValue() instanceof Boolean) intent.putExtra(extra.getKey(), (Boolean)extra.getValue());
                    else intent.putExtra(extra.getKey(), Uri.parse((String)extra.getValue()));
                }
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                startActivityForResult(intent, request);
                return;
            }
            File job = SetupStore.current(this);
            if (!replaceable(job)) throw new Exception("Pause setup and wait for its checkpoint before changing input");
            if (checkingFolder) throw new Exception("Still checking the chosen folder");
            pickerJob = job == null ? null : job.getName();
            boolean folder = request == FOLDER || request == UPDATE;
            if (request == UPDATE && (job == null || new File(job, "job.json").exists()))
                throw new Exception("Choose the update folder before setup copies the disc image");
            if (request == RESTORE) {
                if (job == null || new File(job, "job.json").exists()) throw new Exception("This job already uses a private input copy");
                folder = SetupStore.read(new File(job, "source.json")).getString("kind").equals("folder");
            }
            Intent intent = new Intent(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT);
            if (!folder) intent.addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            for (java.util.Map.Entry<String, Object> extra : SetupPicker.extras(folder).entrySet()) {
                if (extra.getValue() instanceof Boolean) intent.putExtra(extra.getKey(), (Boolean)extra.getValue());
                else intent.putExtra(extra.getKey(), Uri.parse((String)extra.getValue()));
            }
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, request);
        } catch (Exception failure) { actionMessage = "Cannot choose input: " + failure.getMessage(); }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PC_FOLDER) {
            if (result == RESULT_OK && data != null && data.getData() != null) importPcFolder(data.getData());
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        try {
            actionMessage = null;
            File current = SetupStore.current(this);
            if (!replaceable(current)) throw new Exception("Pause setup before changing input");
            if ((request == RESTORE || request == DISC || request == COMMON) &&
                (current == null || !current.getName().equals(pickerJob)))
                throw new Exception("The selected job changed. Open its picker again.");
            Uri uri = data.getData();
            if (request == RESTORE) {
                if (new File(current, "job.json").exists()) throw new Exception("The input has already been copied privately");
                JSONObject source = SetupStore.read(new File(current, "source.json"));
                if (!uri.toString().equals(source.getString("uri")))
                    throw new Exception("Choose the same previously selected file or folder to restore access. Use the dump-selection buttons for a different input.");
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                actionMessage = "Access restored. Tap Start / resume to retry using completed private copies.";
            } else if (request == DISC || request == COMMON) {
                if (current == null) throw new Exception("Choose a disc image first");
                JSONObject source = SetupImport.keySource(current);
                if (!source.getString("kind").equals("image")) throw new Exception("Keys are only needed for disc images");
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                SetupImport.selectKey(this, current, request == DISC ? "disc" : "common", uri);
                if (new File(current, "job.json").isFile())
                    actionMessage = "Replacement key file selected. Tap Start / resume to read it and retry. Your dump copy is retained.";
            } else if (request == UPDATE) {
                if (current == null || !current.getName().equals(pickerJob)) throw new Exception("The selected job changed. Open its picker again.");
                checkUpdate(current, uri);
            } else if (request == FOLDER) checkFolder(uri);
            else if (request == ARCHIVE || request == IMAGE) {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                newJob(this, request == ARCHIVE ? "archive" : "image", uri, null);
                cleanJobs();
            }
        } catch (Exception failure) { actionMessage = "Cannot keep selected input: " + failure.getMessage(); }
        finally { pickerJob = null; }
    }

    /** A new setup job for the chosen game becomes the selected one. */
    private static void newJob(android.content.Context context, String kind, Uri uri, String document) throws Exception {
        synchronized (SetupJobCleanup.CREATION) {
            String id = UUID.randomUUID().toString().replace("-", "");
            File job = SetupStore.job(context, id);
            JSONObject source = new JSONObject().put("schema", 1).put("kind", kind).put("uri", uri.toString());
            if (document != null) source.put("document", document);
            SetupStore.write(new File(job, "source.json"), source);
            SetupStore.write(new File(job, "manual-pause.json"), new JSONObject().put("schema", 1));
            SetupStore.write(new File(job, "host.json"), new JSONObject().put("schema", 1).put("state", "selected"));
            SetupStore.select(context, id);
        }
    }

    /** Removes the private copies of games no longer chosen (never the selected game, the installed build
     *  or a job the setup service holds), off the UI thread. */
    private void cleanJobs() {
        android.content.Context context = getApplicationContext();
        new Thread(() -> {
            SetupJobCleanup.Report report = SetupJobCleanup.clean(AndroidGame.storage(context));
            if (!report.removed.isEmpty() || !report.failed.isEmpty() || report.skipped != null)
                android.util.Log.i("nsmbu-setup", report.summary());
        }, "nsmbu-setup-cleanup").start();
    }

    /** Checks the chosen folder for the game before anything is copied; a wrong folder is refused with a
     *  message that says which folder to choose, and nothing is kept of it. */
    private void checkFolder(Uri uri) {
        checkingFolder = true;
        android.content.Context context = getApplicationContext();
        new Thread(() -> {
            String message = null, refusal = null;
            try {
                SetupFolderCheck.Result found = SetupImport.checkFolder(context,
                    uri, android.provider.DocumentsContract.getTreeDocumentId(uri));
                android.util.Log.i("nsmbu-setup", "chosen folder: " + found.kind + " " + found.path);
                if (!found.usable()) refusal = folderMessage(found);
                else {
                    if (!replaceable(SetupStore.current(context))) throw new Exception("Pause setup before changing input");
                    context.getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    newJob(context, "folder", uri, found.kind == SetupFolderCheck.Kind.SUBFOLDER ? found.document : null);
                    if (found.kind == SetupFolderCheck.Kind.SUBFOLDER) message = folderMessage(found);
                    SetupJobCleanup.Report report = SetupJobCleanup.clean(AndroidGame.storage(context));
                    android.util.Log.i("nsmbu-setup", report.summary());
                }
            } catch (Exception failure) {
                refusal = getString(R.string.setup_folder_unreadable, String.valueOf(failure.getMessage()));
            }
            final String chosen = message, refused = refusal;
            runOnUiThread(() -> {
                checkingFolder = false;
                actionMessage = refused != null ? refused : chosen;
                if (refused == null && chosen != null)
                    android.widget.Toast.makeText(this, chosen, android.widget.Toast.LENGTH_LONG).show();
                if (refused != null && !isFinishing() && !isDestroyed())
                    new android.app.AlertDialog.Builder(this).setTitle(R.string.setup_folder_wrong)
                        .setMessage(refused).setPositiveButton(R.string.setup_folder_again, (dialog, which) -> pick(FOLDER))
                        .setNegativeButton(android.R.string.cancel, null).show();
            });
        }, "nsmbu-setup-folder-check").start();
    }

    private void importPcFolder(Uri uri) {
        checkingFolder = true;
        android.content.Context context = getApplicationContext();
        new Thread(() -> {
            String refusal = null;
            try {
                SetupFolderCheck.Result found = SetupImport.checkFolder(context,
                    uri, android.provider.DocumentsContract.getTreeDocumentId(uri));
                android.util.Log.i("nsmbu-pc", "chosen folder: " + found.kind + " " + found.path);
                if (!found.usable()) refusal = folderMessage(found);
                else {
                    PcGameFolder.saveSource(context, uri,
                        found.kind == SetupFolderCheck.Kind.SUBFOLDER ? found.document : null);
                    PcGameFolder.copySaved(context, new SetupImport.Control() {
                        @Override public void checkpoint() { }
                        @Override public void progress(long copied, long total, int complete, int files) throws Exception {
                            final String title = context.getString(R.string.setup_copying);
                            final String detail = total > 0 ?
                                context.getString(R.string.setup_copying_detail, complete, files) : title;
                            final int progress = total > 0 ? (int)(1000L * complete / Math.max(1, files)) : -1;
                            runOnUiThread(() -> phase(title, detail, progress));
                        }
                    });
                }
            } catch (Exception failure) {
                refusal = getString(R.string.setup_folder_unreadable, String.valueOf(failure.getMessage()));
            }
            final String refused = refusal;
            runOnUiThread(() -> {
                checkingFolder = false;
                actionMessage = refused;
                if (refused != null && !isFinishing() && !isDestroyed())
                    new android.app.AlertDialog.Builder(this).setTitle(R.string.setup_folder_wrong)
                        .setMessage(refused).setPositiveButton(R.string.setup_folder_again, (dialog, which) -> pick(PC_FOLDER))
                        .setNegativeButton(android.R.string.cancel, null).show();
            });
        }, "nsmbu-pc-folder-import").start();
    }

    private void checkUpdate(File job, Uri uri) {
        checkingFolder = true;
        android.content.Context context = getApplicationContext();
        new Thread(() -> {
            String refusal = null;
            try {
                SetupFolderCheck.Result found = SetupImport.checkFolder(context,
                    uri, android.provider.DocumentsContract.getTreeDocumentId(uri));
                android.util.Log.i("nsmbu-setup", "chosen update folder: " + found.kind + " " + found.path);
                if (!found.usable()) refusal = folderMessage(found);
                else {
                    context.getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    SetupImport.selectUpdate(job, uri, found.document);
                }
            } catch (Exception failure) {
                refusal = getString(R.string.setup_folder_unreadable, String.valueOf(failure.getMessage()));
            }
            final String refused = refusal;
            runOnUiThread(() -> {
                checkingFolder = false;
                actionMessage = refused;
                if (refused != null && !isFinishing() && !isDestroyed())
                    new android.app.AlertDialog.Builder(this).setTitle(R.string.setup_update_wrong)
                        .setMessage(refused).setPositiveButton(R.string.setup_folder_again, (dialog, which) -> pick(UPDATE))
                        .setNegativeButton(android.R.string.cancel, null).show();
            });
        }, "nsmbu-setup-update-check").start();
    }

    private String folderMessage(SetupFolderCheck.Result found) {
        switch (found.kind) {
            case SUBFOLDER: return getString(R.string.setup_folder_subfolder, found.path);
            case INSIDE_GAME: return getString(R.string.setup_folder_inside, found.path);
            case NOT_THIS_GAME: return found.path.isEmpty() ? getString(R.string.setup_folder_not_game_here) :
                getString(R.string.setup_folder_not_game, found.path);
            case SEVERAL: return getString(R.string.setup_folder_several, found.path);
            default: return getString(R.string.setup_folder_none);
        }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("picker_job", pickerJob);
        state.putBoolean("launching_game", launchingGame);
        super.onSaveInstanceState(state);
    }

    private Button button(LinearLayout layout, String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(view -> action.run());
        layout.addView(button);
        return button;
    }

    private void control(String action) {
        try {
            actionMessage = null;
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
            SetupService.start(this, action);
        } catch (Exception failure) { actionMessage = "Cannot start setup: " + failure.getMessage(); }
    }

    private static void setTextIfChanged(TextView view, String text) {
        if (!text.contentEquals(view.getText())) view.setText(text);
    }

    private boolean keyReady(File job, JSONObject source, String key, boolean correcting, java.util.Set<String> grants) throws Exception {
        if (source == null) return false;
        if (SetupKeyAccess.failed(job, key)) return false;
        if (correcting) {
            File pending = SetupImport.keyReplacement(job, key);
            if (pending.isFile()) return grants.contains(SetupStore.read(pending).getString("uri"));
            File configuration = new File(job, "job.json");
            if (configuration.isFile() && SetupKeyAccess.legacyFile(job, SetupStore.read(configuration).getJSONObject("input"), key) != null) return true;
            return grants.contains(source.optString(key + "_uri"));
        }
        return grants.contains(source.optString(key + "_uri"));
    }

    private static final String CHECKPOINTS = "Setup keeps a private copy of your input. Leave room for that copy, extracted files, compiled output and at least 1 GiB working reserve. Selected key files are read only. Earlier private key copies are retained until setup completes. Choosing a different game deletes the private copy of the earlier choice; the game you can already play is kept.\n\nYou can close this screen while setup runs. Import resumes from verified complete files; an interrupted file copy or extraction restarts. Python and compiler resource preparation pause during unpacking and restart the incomplete stage. Checksum validation and translation respond to pause requests; an interrupted translation restarts. Compilation and linking also respond to pause requests. An interrupted command restarts; verified objects are kept. Saves stay in their current folder.\n\nA PC-built APK can also copy your extracted game folder from anywhere on the device after you choose it.";

    /** The top of the screen: one plain sentence, a detail line and the bar. */
    private void plainPhase(File job, JSONObject host, String state, boolean needsKeys, boolean canStart) throws Exception {
        if (checkingFolder) { phase(getString(R.string.setup_folder_checking), null, -1); return; }
        if (!phoneSetup) {
            phase(getString(play.isEnabled() ? R.string.setup_ready : R.string.setup_pc_route),
                getString(play.isEnabled() ? R.string.setup_pc_ready_detail : R.string.setup_pc_route_detail), -2);
            return;
        }
        if (job == null) {
            phase(getString(play.isEnabled() ? R.string.setup_ready : R.string.setup_welcome), getString(R.string.setup_welcome_detail), -2);
            return;
        }
        switch (state) {
            case "selected":
                if (needsKeys && !canStart) phase(getString(R.string.setup_keys), getString(R.string.setup_keys_detail), -2);
                else phase(getString(R.string.setup_selected), getString(R.string.setup_selected_detail), -2);
                return;
            case "importing": {
                File imported = new File(job, "import-progress.json");
                if (imported.isFile()) {
                    JSONObject progress = SetupStore.read(imported);
                    int files = Math.max(1, progress.getInt("files"));
                    phase(getString(R.string.setup_copying), getString(R.string.setup_copying_detail,
                        progress.getInt("complete"), progress.getInt("files")), (int)(1000L * progress.getInt("complete") / files));
                } else phase(getString(R.string.setup_copying), null, -1);
                return;
            }
            case "preparing": phase(getString(R.string.setup_preparing), getString(R.string.setup_leave_detail), -1); return;
            case "running": {
                File record = new File(job, "state.json");
                JSONObject event = record.isFile() ? SetupStore.read(record).optJSONObject("last_event") : null;
                String step = event == null ? "" : event.optString("stage");
                if (step.equals("compile") && event.optInt("total") > 0) {
                    int total = event.optInt("total"), done = Math.min(total, event.optInt("compiled") + event.optInt("reused"));
                    long eta = event.optLong("eta_seconds", -1);
                    String left = eta < 0 ? "" : eta < 90 ? getString(R.string.setup_minute_left) :
                        getString(R.string.setup_minutes_left, (eta + 59) / 60);
                    phase(getString(R.string.setup_compiling), getString(R.string.setup_compiling_detail, done, total) + left,
                        (int)(1000L * done / total));
                } else if (step.equals("translate")) phase(getString(R.string.setup_translating), getString(R.string.setup_leave_detail), -1);
                else if (step.equals("link") || step.equals("activate")) phase(getString(R.string.setup_finishing), null, -1);
                else phase(getString(R.string.setup_extracting), getString(R.string.setup_leave_detail), -1);
                return;
            }
            case "paused": {
                String reason = host == null ? "" : host.optString("reason");
                int text = reason.equals("heat") ? R.string.setup_paused_heat :
                    reason.equals("battery") || reason.equals("battery_unknown") ? R.string.setup_paused_battery : R.string.setup_paused;
                phase(getString(text), getString(reason.equals("manual") || reason.isEmpty() ?
                    R.string.setup_paused_detail : R.string.setup_paused_auto_detail), -2);
                return;
            }
            case "failed": phase(getString(R.string.setup_failed), getString(R.string.setup_failed_detail), -2); return;
            case "complete": phase(getString(R.string.setup_ready), getString(R.string.setup_ready_detail), 1000); return;
            default: phase(getString(R.string.setup_working), null, -1);
        }
    }

    /** Current {thermal status, battery temperature in tenths of °C}; -1 where unavailable. */
    private int[] sensorReadings() {
        int thermal = -1, temperature = -1;
        try {
            android.os.PowerManager power = getSystemService(android.os.PowerManager.class);
            if (power != null) thermal = power.getCurrentThermalStatus();
        } catch (RuntimeException ignored) { /* shown as n/a */ }
        try {
            Intent battery = registerReceiver(null, new android.content.IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (battery != null) temperature = battery.getIntExtra(android.os.BatteryManager.EXTRA_TEMPERATURE, -1);
        } catch (RuntimeException ignored) { /* shown as n/a */ }
        return new int[] {thermal, temperature};
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            try {
                File job = SetupStore.current(SetupActivity.this);
                JSONObject source = job != null && new File(job, "source.json").isFile() ? SetupStore.read(new File(job, "source.json")) : null;
                if (source == null && job != null && new File(job, "job.json").isFile() &&
                    SetupStore.read(new File(job, "job.json")).getJSONObject("input").optString("kind").equals("image"))
                    source = SetupImport.keySource(job);
                boolean importing = phoneSetup && source != null && !new File(job, "job.json").isFile();
                JSONObject hostState = job != null && new File(job, "host.json").isFile() ? SetupStore.read(new File(job, "host.json")) : null;
                boolean correctingKeys = !importing && phoneSetup && source != null && source.optString("kind").equals("image") &&
                    hostState != null && (hostState.optString("state").equals("failed") ||
                    (hostState.optString("state").equals("paused") && new File(job, "manual-pause.json").isFile()));
                boolean needsKeys = (importing && source.optString("kind").equals("image")) || correctingKeys;
                java.util.HashSet<String> grants = new java.util.HashSet<>();
                if (importing || correctingKeys) for (android.content.UriPermission permission : getContentResolver().getPersistedUriPermissions())
                    if (permission.isReadPermission()) grants.add(permission.getUri().toString());
                boolean discReady = keyReady(job, source, "disc", correctingKeys, grants);
                boolean commonReady = keyReady(job, source, "common", correctingKeys, grants);
                boolean accessMissing = importing && !grants.contains(source.optString("uri"));
                restore.setVisibility(accessMissing ? android.view.View.VISIBLE : android.view.View.GONE);
                restore.setEnabled(accessMissing && replaceable(job));
                keysHint.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                disc.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                common.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                boolean updatable = importing && source.optString("kind").equals("image");
                show(update, updatable);
                update.setEnabled(updatable && replaceable(job) && !checkingFolder);
                setTextIfChanged(update, getString(source != null && source.has("update_uri") ? R.string.setup_update_ready : R.string.setup_update));
                setTextIfChanged(disc, getString(discReady ? R.string.setup_disc_key_ready : source != null && source.has("disc_uri") ? R.string.setup_disc_key_again : R.string.setup_disc_key));
                setTextIfChanged(common, getString(commonReady ? R.string.setup_common_key_ready : source != null && source.has("common_uri") ? R.string.setup_common_key_again : R.string.setup_common_key));
                disc.setEnabled(needsKeys && replaceable(job));
                common.setEnabled(needsKeys && replaceable(job));
                resume.setEnabled(phoneSetup && job != null && (!needsKeys || (discReady && commonReady)));
                pause.setEnabled(phoneSetup && job != null);
                for (Button picker : pickers)
                    picker.setEnabled(!checkingFolder && (phoneSetup ? replaceable(job) : true));
                play.setEnabled(new File(AndroidGame.storage(SetupActivity.this), "active.json").isFile() ||
                    pcGame());
                String hostName = hostState == null ? (job == null ? "" : "selected") : hostState.optString("state");
                boolean working = hostName.equals("activating") || hostName.equals("importing") || hostName.equals("preparing") || hostName.equals("running");
                show(play, play.isEnabled() && !working && !checkingFolder);
                boolean skip = SkipInstaller.enabled(settingsFile());
                if (skipInstaller.isChecked() != skip) {
                    skipSync = true;
                    skipInstaller.setChecked(skip);
                    skipSync = false;
                }
                show(choose, (phoneSetup && !working) || (!phoneSetup && !checkingFolder));
                show(folderHint, phoneSetup && !working);
                if (!phoneSetup) {
                    setTextIfChanged(choose, getString(play.isEnabled() ? R.string.setup_choose_other : R.string.setup_choose));
                    choose.setOnClickListener(view -> pick(PC_FOLDER));
                } else setTextIfChanged(choose, getString(job == null ? R.string.setup_choose : R.string.setup_choose_other));
                show(pause, phoneSetup && working);
                show(resume, phoneSetup && job != null && !working && !hostName.equals("complete"));
                setTextIfChanged(resume, getString(hostName.equals("selected") ? R.string.setup_start : R.string.setup_continue));
                show(updateStatus, hostState != null && SetupPresentation.showUpdate(hostState.optBoolean("updating"), hostName));
                plainPhase(job, hostState, hostName, needsKeys, resume.isEnabled());
                String text;
                if (!phoneSetup) {
                    text = play.isEnabled() ? getString(R.string.setup_pc_ready) : getString(R.string.setup_pc_route);
                    if (!play.isEnabled()) text += "\n" + getString(R.string.setup_pc_route_detail);
                }
                else if (job == null) text = "Choose your game dump to set up on this Android device.";
                else {
                    File host = new File(job, "host.json");
                    JSONObject value = host.isFile() ? SetupStore.read(host) : new JSONObject().put("state", "ready to start");
                    text = "Setup: " + value.getString("state");
                    if (SetupPresentation.showUpdate(value.optBoolean("updating"), value.optString("state")))
                        text = SetupPresentation.UPDATING + "\n" + text;
                    String reason = value.optString("state").equals("paused") ? value.optString("reason", null) : null;
                    if (value.has("reason")) {
                        int[] sensors = sensorReadings();
                        text += "\n" + SetupPolicy.description(value.getString("reason"), sensors[0], sensors[1]);
                    }
                    if ("heat".equals(reason))
                        text += "\nIf the Android device does not feel hot, tap Continue anyway. Setup then ignores heat until this run ends; Android's critical heat level still pauses it.";
                    else if (SetupPolicy.overridable(reason))
                        text += "\nTap Continue anyway to run on battery until this run ends.";
                    continueAction = !SetupPolicy.overridable(reason) ? null :
                        reason.equals("heat") ? SetupService.CONTINUE_HEAT : SetupService.CONTINUE_BATTERY;
                    if (value.has("error")) text += "\n" + value.getString("error");
                    if (importing && !grants.contains(source.optString("uri")))
                        text += "\nDump access is missing. Restore access to the same selected dump if another file copy is needed. Completed private copies are retained.";
                    if (needsKeys) text += "\n" + (resume.isEnabled() ? correctingKeys ?
                        "Private dump retained. Change either key if needed, then tap Start / resume to retry." :
                        getString(R.string.setup_keys_ready) : getString(R.string.setup_keys_detail));
                    if (value.optString("state").equals("importing")) {
                        File imported = new File(job, "import-progress.json");
                        if (imported.isFile()) {
                            JSONObject progress = SetupStore.read(imported);
                            text += "\nCopied " + progress.getLong("copied") / (1024 * 1024) + " MiB; " + progress.getInt("complete") + "/" + progress.getInt("files") + " files";
                        }
                    }
                    File progress = new File(job, "state.json");
                    if (value.optString("state").equals("running") && progress.isFile()) {
                        JSONObject worker = SetupStore.read(progress);
                        org.json.JSONArray keySources = worker.optJSONArray("key_sources");
                        if (keySources != null) for (int i = 0; i < keySources.length(); i++) text += "\n" + keySources.getString(i);
                        JSONObject event = worker.optJSONObject("last_event");
                        String compiling = event == null || !event.optString("stage").equals("compile") ? null :
                            SetupPolicy.compileProgress(event.optString("state"), event.optInt("compiled"), event.optInt("reused"),
                                event.optInt("total"), event.optLong("eta_seconds", -1));
                        if (compiling != null) text += "\n" + compiling;
                        else if (event != null) text += "\n" + event.optString("stage", "setup") + ": " + event.optString("state");
                    }
                }
                if (job == null || !phoneSetup) continueAction = null;
                continueAnyway.setVisibility(continueAction != null ? android.view.View.VISIBLE : android.view.View.GONE);
                if (actionMessage != null) text += "\n" + actionMessage;
                setTextIfChanged(status, text + "\n\n" + CHECKPOINTS);
            } catch (Exception failure) { setTextIfChanged(status, "Cannot read setup progress: " + failure.getMessage()); }
            if (!isFinishing()) handler.postDelayed(this, 500);
        }
    };

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (shouldAutoPlay()) playNow();
    }

    @Override protected void onResume() {
        super.onResume();
        if (status == null) return;
        refresh.run();
        new Thread(() -> {
            try {
                File job = SetupStore.current(this);
                if (job == null || new File(job, "manual-pause.json").exists()) return;
                File host = new File(job, "host.json");
                if (!host.isFile()) return;
                String state = SetupStore.read(host).optString("state");
                boolean changed = state.equals("complete") && AndroidGame.needsRebuild(this);
                if (SetupRecoveryPolicy.shouldStart(state, false, true, changed))
                    runOnUiThread(() -> {
                        try { SetupService.start(this, null); }
                        catch (Exception failure) { status.setText("Tap Resume to continue setup: " + failure.getMessage()); }
                    });
            } catch (Exception ignored) { /* refresh shows a readable record error */ }
        }, "nsmbu-setup-check").start();
    }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
}
