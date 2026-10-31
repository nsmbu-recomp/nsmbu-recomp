// iOS part of display.mm (included there in place of the AppKit windows).
// One window (ios/app_ios.mm) whose game view hosts the TV layer. The GamePad picture never gets a
// window of its own: it is shown over the TV picture (picture-in-picture, automatic, or GamePad only)
// and touches on it are GamePad touches.

namespace gfx {

static CAMetalLayer* g_layers[2];

bool tv_fullscreen() { return true; }

bool drc_window_available() { return g_mode == kDrcPip || g_mode == kDrcAuto; }
bool drc_window_shown() { return drc_screen_shown(false); }

void show_drc_window(bool on) {
    display_show_drc(on);
    LOG("[display] GamePad screen %s (%s)", on ? "shown" : "hidden", kModeNames[g_mode]);
}
void toggle_drc_screen() { show_drc_window(!drc_window_shown()); }

void set_drc_mode(int m) {
    display_set_mode(m);
    save_options();
    LOG("[display] GamePad screen mode: %s", kModeNames[m]);
}

// settings only: the window comes with the scene (app_ios.mm), after UIApplicationMain started
static void create_windows() {
    static bool done = false;
    if (done) return;
    done = true;
    load_settings();
    load_options();
    hostui::load_saved_options();
    g_shown = !input::pro_controller();
}

static CAMetalLayer* attach_layer(int i) {
    if (i != 0) return nil;
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    layer.opaque = YES;
    g_layers[i] = layer;
    return layer;
}

static void attach_metal_layers() {
    for (int i = 0; i < 2; i++) {
        Screen& scr = i ? R.drc : R.tv;
        CAMetalLayer* layer = attach_layer(i);
        scr.layer = layer;
        if (!layer) continue;

        CGColorSpaceRef outputColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        layer.colorspace = outputColorSpace;
        CGColorSpaceRelease(outputColorSpace);
        layer.device = R.device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.maximumDrawableCount = 3;
        if (hidden_windows()) scr.visible = false;
    }
}

// the game view's size (points), its screen's scale and refresh rate (main thread, app_ios.mm)
void display_view_changed(CGRect bounds, CGFloat scale, int hz) {
    CAMetalLayer* layer = g_layers[0];
    if (!layer) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.frame = bounds;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(std::max<CGFloat>(1, bounds.size.width * scale), std::max<CGFloat>(1, bounds.size.height * scale));
    [CATransaction commit];
    interp::set_display_hz(hz);
    overlay::set_density((float)scale);
}

// scene in the foreground and active: present; otherwise the game waits at its next vsync (gx2_core.cpp)
void display_set_active(bool active) {
    hostui::set_app_focused(active);
    R.tv.visible = active && !hidden_windows();
}

void display_set_tv_fullscreen(bool) {}
void* display_tv_window() { return nullptr; }
bool drc_overlay_hit(void*, double, double) { return false; }

void ios_run_app();
void run_appkit_loop() {
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillTerminateNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification*) {
        render::shutdown();
        fflush(stdout);
        fflush(stderr);
        std::_Exit(0);
    }];
    ios_run_app();
}

void ios_show_notice(const char* title, const char* text);
void show_startup_notice(const std::string& title, const std::string& text) {
    LOG("[display] %s: %s", title.c_str(), text.c_str());
    if (env::NO_HOST_INPUT()) return;
    ios_show_notice(title.c_str(), text.c_str());
}
