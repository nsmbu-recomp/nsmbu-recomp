package org.nsmbrecomp.nsmbu;

import android.os.Bundle;
import android.content.Intent;
import android.app.AlertDialog;
import java.io.FileOutputStream;
import java.io.InputStream;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;

public class NsmbuActivity extends SDLActivity {
    private AndroidGame.Selection selection;
    private volatile PhoneRumble phoneRumble;

    public static void phoneRumble(int level) {
        if (mSingleton instanceof NsmbuActivity) {
            PhoneRumble output = ((NsmbuActivity) mSingleton).phoneRumble;
            if (output != null) output.update(level);
        }
    }

    /** Leave the game and open the installer. The preference survives the process exit that follows. */
    public static void openInstaller() {
        if (!(mSingleton instanceof NsmbuActivity)) return;
        NsmbuActivity activity = (NsmbuActivity) mSingleton;
        activity.runOnUiThread(() -> {
            activity.getSharedPreferences("setup", MODE_PRIVATE).edit().putBoolean("show_setup", true).commit();
            Intent intent = new Intent(activity, SetupActivity.class);
            intent.putExtra(SetupActivity.EXTRA_SHOW_SETUP, true);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            activity.startActivity(intent);
            activity.finish();
        });
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override public void loadLibraries() {
        selection = AndroidGame.selected(this);
        if (selection == null) {
            if (!new File(getApplicationInfo().nativeLibraryDir, "libmain.so").isFile())
                throw new UnsatisfiedLinkError("The game is not set up yet. Open the app from its icon to set it up.");
            super.loadLibraries();
            return;
        }
        System.loadLibrary("SDL3");
        System.load(selection.library.getAbsolutePath());
        Log.i("nsmbu", "Loaded on-device game " + selection.library.getAbsolutePath());
        if (selection.game != null) Log.i("nsmbu", "Selected game assets " + selection.game.getAbsolutePath());
    }

    @Override protected String getMainSharedObject() {
        return selection != null ? selection.library.getAbsolutePath() : super.getMainSharedObject();
    }

    @Override protected String[] getArguments() {
        if (selection == null || selection.game == null) return new String[0];
        return new String[] { "--game", selection.game.getAbsolutePath() };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {

        File files = getExternalFilesDir(null);
        if (files != null) {
            new File(files, "game").mkdirs();
            new File(files, "save").mkdirs();
            Log.i("nsmbu", "game folder: " + new File(files, "game"));
        }
        super.onCreate(savedInstanceState);
        phoneRumble = new PhoneRumble(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (!mBrokenLibraries) {
            try {
                int heat = HeatGuardDevice.publish(android.os.Build.MANUFACTURER, android.os.Build.BRAND, android.os.Build.MODEL);
                Log.i("nsmbu", "heat guard device: " + (heat == HeatGuardDevice.FAN_HANDHELD ? "handheld (fan)" : "phone"));
            } catch (UnsatisfiedLinkError e) {
                Log.w("nsmbu", "heat guard device class not passed: " + e.getMessage());
            }
        }
        if (mLayout != null && !mBrokenLibraries)
            mLayout.addView(new TouchControls(this), new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (savedInstanceState == null) CrashReports.offer(this);
    }

    static void syncSystemBars(Window w) {
        WindowInsetsController c = w != null ? w.getInsetsController() : null;
        if (c == null) return;
        if (mFullscreenModeActive) {
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        } else {
            c.show(WindowInsets.Type.systemBars());
        }
    }

    private void applyFullScreen() {
        Window w = getWindow();
        if (w == null) return;
        w.setDecorFitsSystemWindows(!mFullscreenModeActive);
        if (mFullscreenModeActive) {
            WindowManager.LayoutParams lp = w.getAttributes();
            if (lp.layoutInDisplayCutoutMode != WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES &&
                lp.layoutInDisplayCutoutMode != WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS) {
                lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                w.setAttributes(lp);
            }
        }
        syncSystemBars(w);
    }

    private final Runnable reapplyFullScreen = this::applyFullScreen;

    @Override protected void onResume() {
        super.onResume();
        if (phoneRumble != null) phoneRumble.setActive(true);
        applyFullScreen();
    }

    @Override protected void onPause() {
        if (phoneRumble != null) phoneRumble.setActive(false);
        super.onPause();
    }

    @Override protected void onDestroy() {
        if (phoneRumble != null) phoneRumble.setActive(false);
        super.onDestroy();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) applyFullScreen();
    }

    @Override public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        final boolean barsShown = (visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0;
        View decor = getWindow().getDecorView();
        decor.removeCallbacks(reapplyFullScreen);
        decor.postDelayed(reapplyFullScreen, mFullscreenModeActive && barsShown && hasWindowFocus() ? 2000 : 0);
    }

    private static final int GPU_DRIVER_REQUEST = 4972;
    private static native String installGpuDriver(String path, boolean select);

    private boolean driverAfterFailure;
    public void chooseGpuDriver(boolean afterFailure) {
        driverAfterFailure = afterFailure;
        runOnUiThread(() -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), GPU_DRIVER_REQUEST));
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != GPU_DRIVER_REQUEST) return;
        final boolean afterFailure = driverAfterFailure;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            if (afterFailure) endApp();
            return;
        }
        final android.net.Uri uri = data.getData();

        new Thread(() -> {
            File zip = null;
            String error = "";
            try {
                zip = File.createTempFile("gpu-driver-", ".zip", getCacheDir());
                try (InputStream in = getContentResolver().openInputStream(uri);
                     FileOutputStream out = new FileOutputStream(zip)) {
                    if (in == null) throw new java.io.IOException("Cannot read selected file");
                    byte[] bytes = new byte[65536]; long total = 0; int count;
                    while ((count = in.read(bytes)) != -1) {
                        total += count;
                        if (total > 512L * 1024 * 1024) throw new java.io.IOException("Driver ZIP exceeds 512 MiB");
                        out.write(bytes, 0, count);
                    }
                }
                error = installGpuDriver(zip.getAbsolutePath(), afterFailure);
            } catch (Exception e) { error = e.getMessage(); }
            finally { if (zip != null) zip.delete(); }
            final String problem = error;
            final boolean ok = problem == null || problem.isEmpty();
            runOnUiThread(() -> new AlertDialog.Builder(this).setTitle(ok ? "Driver installed" : "Driver installation failed")
                .setMessage(!ok ? problem : afterFailure ? "The game uses it from the next start: open the game again."
                                                         : "Select the driver in Graphics, then restart the game.")
                .setCancelable(!afterFailure)
                .setPositiveButton("OK", afterFailure ? (dialog, which) -> endApp() : null).show());
        }, "gpu-driver-install").start();
    }

    private void endApp() {
        finishAndRemoveTask();
        System.exit(0);
    }

    public static float displayRefreshRate() {
        Display d = mSingleton != null ? mSingleton.getDisplay() : null;
        return d != null ? d.getRefreshRate() : 0;
    }

    public static float maxRefreshRate() {
        Display d = mSingleton != null ? mSingleton.getDisplay() : null;
        if (d == null) return 0;
        Display.Mode cur = d.getMode();
        float best = d.getRefreshRate();
        for (Display.Mode m : d.getSupportedModes())
            if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight())
                best = Math.max(best, m.getRefreshRate());
        return best;
    }

    public static void requestRefreshRate(final float hz) {
        final SDLActivity a = mSingleton;
        if (a == null) return;
        a.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Window w = a.getWindow();
                Display d = a.getDisplay();
                if (w == null || d == null) return;
                int id = 0;
                if (hz > 0) {
                    Display.Mode cur = d.getMode(), pick = null, highest = null;
                    for (Display.Mode m : d.getSupportedModes()) {
                        if (m.getPhysicalWidth() != cur.getPhysicalWidth() || m.getPhysicalHeight() != cur.getPhysicalHeight()) continue;
                        if (highest == null || m.getRefreshRate() > highest.getRefreshRate()) highest = m;
                        if (m.getRefreshRate() >= hz - 1 && (pick == null || m.getRefreshRate() < pick.getRefreshRate())) pick = m;
                    }
                    if (pick == null) pick = highest;
                    if (pick != null) id = pick.getModeId();
                }
                WindowManager.LayoutParams lp = w.getAttributes();
                if (lp.preferredDisplayModeId == id) return;
                lp.preferredDisplayModeId = id;
                w.setAttributes(lp);
                Log.i("nsmbu", "display mode " + id + " requested for " + hz + " fps");
            }
        });
    }
}
