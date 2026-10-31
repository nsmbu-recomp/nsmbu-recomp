// The iOS app around the game: UIApplicationMain on the main thread (from the renderer's main loop),
// one scene with the game view (the TV picture's CAMetalLayer and the on-screen controls), and the
// messages the runtime shows (fatal errors, notices, text prompts).
#include "core/env.h"
#import <AVFAudio/AVFAudio.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#include <crt_externs.h>

#include <atomic>
#include <memory>
#include <string>

#include "core/runtime.h"
#include "touch_controls.h"

namespace gfx {
CAMetalLayer* display_layer(int i);
void display_view_changed(CGRect bounds, CGFloat scale, int hz);
void display_set_active(bool active);
void display_start_input();
void ios_load_prefs();
}

namespace {
std::atomic<bool> g_launched{false};
// a fatal error before UIKit ran (no game folder yet): the scene shows it instead of the game
NSString* g_early_title;
NSString* g_early_message;
UIWindow* g_window;
}

// ---------------------------------------------------------------- game view
@interface NsmbuGameViewController : UIViewController
@property(nonatomic, strong) NsmbuControlsView* controls;
@end

@implementation NsmbuGameViewController
- (void)loadView {
    UIView* v = [[UIView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    v.backgroundColor = UIColor.blackColor;
    if (CAMetalLayer* tv = gfx::display_layer(0)) [v.layer addSublayer:tv];
    self.controls = [[NsmbuControlsView alloc] initWithFrame:v.bounds];
    self.controls.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [v addSubview:self.controls];
    self.view = v;
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    UIScreen* screen = self.view.window.windowScene.screen;
    const CGFloat scale = self.view.window ? self.view.window.traitCollection.displayScale : 2.0;
    gfx::display_view_changed(self.view.bounds, scale > 0 ? scale : 2.0, screen ? (int)screen.maximumFramesPerSecond : 60);
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

// ---------------------------------------------------------------- start-up error (no game files yet)
@interface NsmbuErrorViewController : UIViewController
@end

@implementation NsmbuErrorViewController
- (void)loadView {
    UIView* v = [UIView new];
    v.backgroundColor = UIColor.systemBackgroundColor;
    UILabel* title = [UILabel new];
    title.text = g_early_title ?: @"NSMBU";
    title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    title.numberOfLines = 0;
    UITextView* body = [UITextView new];
    body.text = g_early_message;
    body.editable = NO;
    body.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    body.backgroundColor = UIColor.clearColor;
    UIButton* quit = [UIButton buttonWithType:UIButtonTypeSystem];
    [quit setTitle:@"Quit" forState:UIControlStateNormal];
    [quit addAction:[UIAction actionWithHandler:^(UIAction*) { std::_Exit(1); }] forControlEvents:UIControlEventTouchUpInside];
    UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[ title, body, quit ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:stack];
    UILayoutGuide* g = v.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-24],
        [stack.topAnchor constraintEqualToAnchor:g.topAnchor constant:24],
        [stack.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-24],
    ]];
    self.view = v;
}
@end

// ---------------------------------------------------------------- app and scene
@interface NsmbuAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation NsmbuAppDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)options {
    NSError* err = nil;
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategorySoloAmbient error:&err];
    [[AVAudioSession sharedInstance] setActive:YES error:&err];
    app.idleTimerDisabled = YES;
    return YES;
}
- (UISceneConfiguration*)application:(UIApplication*)app
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options {
    return [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
}
@end

@interface NsmbuSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation NsmbuSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    UIWindowScene* ws = (UIWindowScene*)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:ws];
    g_window = self.window;
    if (g_early_message) {
        self.window.rootViewController = [NsmbuErrorViewController new];
    } else {
        self.window.rootViewController = [NsmbuGameViewController new];
        gfx::display_start_input();
        gfx::ios_load_prefs();
    }
    [self.window makeKeyAndVisible];
    g_launched = true;
}
- (void)sceneDidBecomeActive:(UIScene*)scene { gfx::display_set_active(true); }
- (void)sceneWillResignActive:(UIScene*)scene {
    gfx::display_set_active(false);
    if (auto* vc = (NsmbuGameViewController*)self.window.rootViewController; [vc isKindOfClass:[NsmbuGameViewController class]])
        [vc.controls releaseAll];
}
@end

// ---------------------------------------------------------------- runtime entry points
namespace gfx {

// the renderer's main loop (display_ios.inc): UIKit's, for good
void ios_run_app() {
    @autoreleasepool {
        UIApplicationMain(*_NSGetArgc(), *_NSGetArgv(), nil, NSStringFromClass([NsmbuAppDelegate class]));
    }
    std::_Exit(0);
}

static UIViewController* top_controller() {
    UIViewController* vc = g_window.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

void ios_present_alert(UIAlertController* a) {
    if (UIViewController* vc = top_controller()) [vc presentViewController:a animated:YES completion:nil];
}

void ios_show_notice(const char* title, const char* text) {
    NSString* t = @(title), *m = @(text);
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController* a = [UIAlertController alertControllerWithTitle:t message:m preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        ios_present_alert(a);
    });
}

}  // namespace gfx

// a fatal error's message (core/user_error.cpp), returns once it was dismissed; the caller then exits
void ios_show_error(const char* title, const char* message) {
    NSString* t = @(title), *m = @(message);
    if (!g_launched) {
        if ([NSThread isMainThread]) {
            // before UIKit started (game folder check, guest memory): the scene shows the message, the app ends with Quit
            g_early_title = t;
            g_early_message = m;
            gfx::ios_run_app();
        }
        while (!g_launched) usleep(10000);
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto show = ^{
        UIAlertController* a = [UIAlertController alertControllerWithTitle:t message:m preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"Quit" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { *done = true; }]];
        gfx::ios_present_alert(a);
    };
    if ([NSThread isMainThread]) {
        show();
        while (!*done) [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    } else {
        dispatch_async(dispatch_get_main_queue(), show);
        while (!*done) usleep(20000);
    }
}

void ios_open_url(const char* url) {
    NSURL* u = [NSURL URLWithString:@(url)];
    if (!u) return;
    dispatch_async(dispatch_get_main_queue(), ^{ [UIApplication.sharedApplication openURL:u options:@{} completionHandler:nil]; });
}
