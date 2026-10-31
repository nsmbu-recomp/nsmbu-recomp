// On-screen controls over the game view, and the routing of every touch on it: the settings overlay
// while it is open, the on-screen buttons and stick, and the GamePad picture (GamePad touch screen).
// The buttons act as player 1's controller (input.mm merges ios_touch_values into it).
#include "core/env.h"
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cmath>
#include <mutex>

#include "gfx/display_modes.h"
#include "input/input.h"
#include "input/input_map.h"
#include "../../../overlay/hostui.h"
#include "../../../overlay/overlay.h"
#include "touch_controls.h"

namespace {
using namespace input_map;

std::mutex g_mu;
float g_values[kPadCount];

// Wii U face buttons by position, as the default controller layout maps a host pad's buttons
// (README: the bottom face button is Wii U B, the right one Wii U A)
enum Kind { kButton, kPill, kStick };
struct Control {
    const char* label;
    int pad;   // input_map pad index, kPadNone for host actions
    int action; // host action when pad == kPadNone
    Kind kind;
};
enum Action { kNoAction, kSettings, kView };
const Control kControls[] = {
    {"A", kPadB, kNoAction, kButton},  {"B", kPadA, kNoAction, kButton},     {"X", kPadY, kNoAction, kButton},
    {"Y", kPadX, kNoAction, kButton},  {"L", kPadLB, kNoAction, kPill},      {"R", kPadRB, kNoAction, kPill},
    {"ZL", kPadLT, kNoAction, kPill},  {"ZR", kPadRT, kNoAction, kPill},     {"+", kPadMenu, kNoAction, kButton},
    {"−", kPadOptions, kNoAction, kButton}, {"⚙", kPadNone, kSettings, kButton}, {"▣", kPadNone, kView, kButton},
    {"", kPadNone, kNoAction, kStick},
};
constexpr int kCount = sizeof kControls / sizeof *kControls;
constexpr int kStickIndex = kCount - 1;

enum Owner { kNone = -100, kOverlay = -1, kGamePadTouch = -2 };
}  // namespace

namespace gfx {
void ios_touch_values(float* v) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (int i = 0; i < kPadCount; i++) v[i] = std::max(v[i], g_values[i]);
}
}

@implementation NsmbuControlsView {
    CGRect _frames[kCount];
    CAShapeLayer* _shapes[kCount];
    CATextLayer* _labels[kCount];
    CAShapeLayer* _knob;
    NSMapTable<UITouch*, NSNumber*>* _owners;
    CGPoint _stickOrigin;
    UITouch* _stickTouch;
    UIImpactFeedbackGenerator* _haptics;
    bool _pressed[kCount];
    bool _padConnected;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.multipleTouchEnabled = YES;
    self.backgroundColor = UIColor.clearColor;
    _owners = [NSMapTable strongToStrongObjectsMapTable];
    _haptics = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    for (int i = 0; i < kCount; i++) {
        _shapes[i] = [CAShapeLayer layer];
        _shapes[i].fillColor = [UIColor colorWithWhite:1 alpha:0.18].CGColor;
        _shapes[i].strokeColor = [UIColor colorWithWhite:1 alpha:0.45].CGColor;
        _shapes[i].lineWidth = 1.5;
        [self.layer addSublayer:_shapes[i]];
        _labels[i] = [CATextLayer layer];
        _labels[i].string = @(kControls[i].label);
        _labels[i].alignmentMode = kCAAlignmentCenter;
        _labels[i].foregroundColor = [UIColor colorWithWhite:1 alpha:0.75].CGColor;
        _labels[i].contentsScale = UIScreen.mainScreen.scale;
        [self.layer addSublayer:_labels[i]];
    }
    _knob = [CAShapeLayer layer];
    _knob.fillColor = [UIColor colorWithWhite:1 alpha:0.35].CGColor;
    [self.layer addSublayer:_knob];
    for (NSNotificationName n in @[ GCControllerDidConnectNotification, GCControllerDidDisconnectNotification ])
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllersChanged) name:n object:nil];
    [self controllersChanged];
    return self;
}

// a connected controller hides the buttons and the stick; the settings and view buttons stay
- (void)controllersChanged {
    bool pad = false;
    for (GCController* c in GCController.controllers)
        if (c.extendedGamepad) pad = true;
    if (const char* e = env::TOUCH_CONTROLS()) pad = atoi(e) == 0;
    _padConnected = pad;
    [self setNeedsLayout];
}

static bool host_only(int i) { return kControls[i].kind != kStick && kControls[i].pad == kPadNone; }

- (void)layoutSubviews {
    [super layoutSubviews];
    const CGRect b = UIEdgeInsetsInsetRect(self.bounds, self.safeAreaInsets);
    const CGFloat u = std::min(self.bounds.size.width, self.bounds.size.height) / 9.0;
    const CGFloat r = u * 0.62, small = u * 0.36;
    const CGPoint face = CGPointMake(CGRectGetMaxX(b) - 2.2 * u, CGRectGetMaxY(b) - 2.2 * u);
    auto circle = [](CGPoint c, CGFloat rad) { return CGRectMake(c.x - rad, c.y - rad, 2 * rad, 2 * rad); };
    _frames[0] = circle(CGPointMake(face.x + 1.25 * u, face.y), r);         // A, right
    _frames[1] = circle(CGPointMake(face.x, face.y + 1.25 * u), r);         // B, bottom
    _frames[2] = circle(CGPointMake(face.x, face.y - 1.25 * u), r);         // X, top
    _frames[3] = circle(CGPointMake(face.x - 1.25 * u, face.y), r);         // Y, left
    // shoulder buttons side by side: across the top on the left, below the GamePad picture (top right
    // by default, display.mm) on the right
    const CGFloat pw = 1.5 * u, ph = 0.7 * u, gap = 0.2 * u, left = CGRectGetMinX(b) + 0.3 * u, right = CGRectGetMaxX(b) - 0.3 * u;
    const CGFloat top = CGRectGetMinY(b) + 0.3 * u, below = CGRectGetMinY(b) + 2.9 * u;
    _frames[6] = CGRectMake(left, top, pw, ph);                       // ZL
    _frames[4] = CGRectMake(left + pw + gap, top, pw, ph);            // L
    _frames[7] = CGRectMake(right - pw, below, pw, ph);               // ZR
    _frames[5] = CGRectMake(right - 2 * pw - gap, below, pw, ph);     // R
    const CGFloat mid = CGRectGetMidX(b), low = CGRectGetMaxY(b) - 0.6 * u;
    _frames[8] = circle(CGPointMake(mid + 0.9 * u, low), small);   // +
    _frames[9] = circle(CGPointMake(mid - 0.9 * u, low), small);   // -
    _frames[10] = circle(CGPointMake(mid + 0.5 * u, CGRectGetMinY(b) + 0.6 * u), small);  // settings overlay
    _frames[11] = circle(CGPointMake(mid - 0.5 * u, CGRectGetMinY(b) + 0.6 * u), small);  // GamePad picture
    _frames[kStickIndex] = circle(CGPointMake(CGRectGetMinX(b) + 2.4 * u, CGRectGetMaxY(b) - 2.2 * u), 1.3 * u);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int i = 0; i < kCount; i++) {
        const CGRect f = _frames[i];
        const bool pill = kControls[i].kind == kPill;
        _shapes[i].path = (pill ? [UIBezierPath bezierPathWithRoundedRect:f cornerRadius:f.size.height / 2]
                                : [UIBezierPath bezierPathWithOvalInRect:f]).CGPath;
        const CGFloat fs = kControls[i].kind == kButton && f.size.width < u ? f.size.height * 0.55 : std::min(f.size.height * 0.5, u * 0.55);
        _labels[i].fontSize = fs;
        _labels[i].frame = CGRectMake(f.origin.x, CGRectGetMidY(f) - fs * 0.62, f.size.width, fs * 1.3);
        const bool hidden = _padConnected && !host_only(i);
        _shapes[i].hidden = _labels[i].hidden = hidden;
    }
    [CATransaction commit];
    [self drawStick:CGPointZero active:false];
}

- (void)drawStick:(CGPoint)offset active:(bool)active {
    const CGRect base = _frames[kStickIndex];
    const CGFloat rad = base.size.width / 2;
    const CGPoint c = active ? _stickOrigin : CGPointMake(CGRectGetMidX(base), CGRectGetMidY(base));
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _shapes[kStickIndex].path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x - rad, c.y - rad, 2 * rad, 2 * rad)].CGPath;
    const CGFloat k = rad * 0.45;
    _knob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x + offset.x - k, c.y + offset.y - k, 2 * k, 2 * k)].CGPath;
    _knob.hidden = _shapes[kStickIndex].hidden;
    [CATransaction commit];
}

- (int)controlAt:(CGPoint)p {
    for (int i = 0; i < kStickIndex; i++) {
        if (_shapes[i].hidden) continue;
        // a little larger than drawn: thumbs land off-centre
        if (CGRectContainsPoint(CGRectInset(_frames[i], -_frames[i].size.height * 0.15, -_frames[i].size.height * 0.15), p)) return i;
    }
    return -1;
}

- (void)setValuesFromState {
    float v[kPadCount] = {};
    for (int i = 0; i < kStickIndex; i++)
        if (_pressed[i] && kControls[i].pad != kPadNone) v[kControls[i].pad] = 1.0f;
    if (_stickTouch) {
        const CGPoint p = [_stickTouch locationInView:self];
        const CGFloat rad = _frames[kStickIndex].size.width / 2;
        CGFloat dx = (p.x - _stickOrigin.x) / rad, dy = (p.y - _stickOrigin.y) / rad;
        const CGFloat len = std::hypot(dx, dy);
        if (len > 1) dx /= len, dy /= len;
        v[kPadLSRight] = (float)std::max<CGFloat>(dx, 0);
        v[kPadLSLeft] = (float)std::max<CGFloat>(-dx, 0);
        v[kPadLSDown] = (float)std::max<CGFloat>(dy, 0);
        v[kPadLSUp] = (float)std::max<CGFloat>(-dy, 0);
        [self drawStick:CGPointMake(dx * rad, dy * rad) active:true];
    }
    std::lock_guard<std::mutex> lk(g_mu);
    std::copy(v, v + kPadCount, g_values);
}

- (void)setPressed:(int)i on:(bool)on {
    if (i < 0 || _pressed[i] == on) return;
    _pressed[i] = on;
    _shapes[i].fillColor = [UIColor colorWithWhite:1 alpha:on ? 0.45 : 0.18].CGColor;
    if (on) [_haptics impactOccurred];
}

- (void)refreshButtons {
    bool held[kCount] = {};
    for (UITouch* t in _owners) {
        const int o = [_owners objectForKey:t].intValue;
        if (o >= 0 && o < kStickIndex) held[o] = true;
    }
    for (int i = 0; i < kStickIndex; i++) [self setPressed:i on:held[i]];
    [self setValuesFromState];
}

- (bool)gamePadPoint:(CGPoint)p tx:(float*)tx ty:(float*)ty clamp:(bool)clamp {
    const CGSize sz = self.bounds.size;
    if (sz.width <= 0 || sz.height <= 0) return false;
    return gfx::overlay_hit((float)(p.x / sz.width), (float)(p.y / sz.height), tx, ty, clamp);
}

- (void)overlayMouse:(CGPoint)p {
    const CGSize sz = self.bounds.size;
    if (sz.width > 0 && sz.height > 0) overlay::mouse_move((float)(p.x / sz.width), (float)(p.y / sz.height));
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    for (UITouch* t in touches) {
        const CGPoint p = [t locationInView:self];
        const int c = [self controlAt:p];
        if (c >= 0 && host_only(c)) {
            if (kControls[c].action == kSettings) overlay::set_open(!overlay::is_open());
            else if (kControls[c].action == kView) hostui::set_drc_mode(gfx::next_view());
            [_haptics impactOccurred];
            continue;
        }
        if (overlay::captures()) {
            // the settings overlay: one finger is its mouse
            bool busy = false;
            for (UITouch* o in _owners) busy |= [_owners objectForKey:o].intValue == kOverlay;
            if (busy) continue;
            [_owners setObject:@(kOverlay) forKey:t];
            [self overlayMouse:p];
            overlay::mouse_button(0, true);
            continue;
        }
        if (c >= 0) {
            [_owners setObject:@(c) forKey:t];
            continue;
        }
        // the drawn stick wins over the GamePad picture: with the GamePad screen full size (the world
        // map in Auto, GamePad only) every point is on the picture and the stick never got a finger
        const CGRect stick = _frames[kStickIndex];
        const bool onStick = CGRectContainsPoint(CGRectInset(stick, -stick.size.height * 0.15, -stick.size.height * 0.15), p);
        float tx, ty;
        if (!(onStick && !_stickTouch && !_shapes[kStickIndex].hidden) && !input::pro_controller() &&
            [self gamePadPoint:p tx:&tx ty:&ty clamp:false]) {
            [_owners setObject:@(kGamePadTouch) forKey:t];
            input::set_touch(true, tx, ty);
            gfx::display_touched();
            continue;
        }
        if (!_stickTouch && !_shapes[kStickIndex].hidden && p.x < self.bounds.size.width * 0.45) {
            _stickTouch = t;
            _stickOrigin = p;
            [_owners setObject:@(kStickIndex) forKey:t];
        }
    }
    [self refreshButtons];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    for (UITouch* t in touches) {
        NSNumber* o = [_owners objectForKey:t];
        if (!o) continue;
        const CGPoint p = [t locationInView:self];
        const int owner = o.intValue;
        if (owner == kOverlay) {
            [self overlayMouse:p];
        } else if (owner == kGamePadTouch) {
            float tx, ty;
            if ([self gamePadPoint:p tx:&tx ty:&ty clamp:true]) input::set_touch(true, tx, ty);
        } else if (owner >= 0 && owner < kStickIndex) {
            // sliding between buttons (B to Y and back) keeps one finger on both actions it touches
            const int c = [self controlAt:p];
            if (c >= 0 && !host_only(c)) [_owners setObject:@(c) forKey:t];
        }
    }
    [self refreshButtons];
}

- (void)endTouches:(NSSet<UITouch*>*)touches {
    for (UITouch* t in touches) {
        NSNumber* o = [_owners objectForKey:t];
        if (!o) continue;
        const int owner = o.intValue;
        if (owner == kOverlay) {
            [self overlayMouse:[t locationInView:self]];
            overlay::mouse_button(0, false);
        } else if (owner == kGamePadTouch) {
            float tx = 0, ty = 0;
            [self gamePadPoint:[t locationInView:self] tx:&tx ty:&ty clamp:true];
            input::set_touch(false, tx, ty);
            gfx::display_touched();
        } else if (t == _stickTouch) {
            _stickTouch = nil;
            [self drawStick:CGPointZero active:false];
        }
        [_owners removeObjectForKey:t];
    }
    [self refreshButtons];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches]; }

// the scene went inactive: every finger is lifted
- (void)releaseAll {
    [self endTouches:[NSSet setWithArray:_owners.keyEnumerator.allObjects]];
}

@end
