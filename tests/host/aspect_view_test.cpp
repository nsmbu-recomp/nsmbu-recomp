#include "gfx/aspect_view.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok) {
    if (!ok) {
        std::fprintf(stderr, "aspect view check failed\n");
        std::exit(1);
    }
}

static bool same(float a, float b) { return std::fabs(a - b) <= 1e-4f * std::fmax(1.0f, std::fabs(a)); }

int main() {
    using namespace aspect::view;
    const float wide = 64.0f / 27.0f, tall = 16.0f / 10.0f;
    Ortho screen = expand_ortho(360, -360, -640, 640, kBase);
    check(screen.top == 360 && screen.bottom == -360 && screen.left == -640 && screen.right == 640);

    Ortho screen_wide = expand_ortho(360, -360, -640, 640, wide);
    check(screen_wide.top == 360 && screen_wide.bottom == -360 && screen_wide.left == -640 && screen_wide.right == 640);
    Ortho wider = expand_ortho(3780, -3780, -6720, 6720, wide);
    check(same(wider.top, 3780) && same(wider.bottom, -3780));
    check(same(wider.left, -6720.0f * (wide / kBase)) && same(wider.right, 6720.0f * (wide / kBase)));
    Ortho again = expand_ortho(wider.top, wider.bottom, wider.left, wider.right, wide);
    check(again.top == wider.top && again.bottom == wider.bottom && again.left == wider.left && again.right == wider.right);

    Ortho higher = expand_ortho(3780, -3780, -6720, 6720, tall);
    check(same(higher.left, -6720) && same(higher.right, 6720));
    check(same(higher.top, 3780.0f * (kBase / tall)) && same(higher.bottom, -3780.0f * (kBase / tall)));
    Ortho screen_tall = expand_ortho(360, -360, -640, 640, tall);
    check(screen_tall.top == 360 && screen_tall.bottom == -360 && screen_tall.left == -640 && screen_tall.right == 640);

    Ortho coin = expand_ortho(512, 0, 0, 2048, wide);
    check(coin.top == 512 && coin.bottom == 0 && coin.left == 0 && coin.right == 2048);
    Ortho small = expand_ortho(18, -18, -32, 32, wide);
    check(small.top == 18 && small.left == -32);

    Persp level = expand_perspective(0.7f, kBase, kBase);
    check(level.fovy == 0.7f && level.aspect == kBase);
    Persp ultra = expand_perspective(0.7f, kBase, wide);
    check(ultra.fovy == 0.7f && ultra.aspect == wide);
    Persp second = expand_perspective(ultra.fovy, ultra.aspect, wide);
    check(second.fovy == ultra.fovy && second.aspect == ultra.aspect);
    float ky = kBase / tall;
    float want = 2.0f * std::atan(std::tan(0.7f * 0.5f) * ky);
    Persp ten = expand_perspective(0.7f, kBase, tall);
    check(same(ten.fovy, want) && ten.aspect == tall);
    Persp other = expand_perspective(0.7f, 2.0f, wide);
    check(other.fovy == 0.7f && other.aspect == 2.0f);

    check(gamepad_target(854, 480));
    check(gamepad_target(427, 240));
    check(gamepad_target(855, 480));
    check(!gamepad_target(1280, 720));
    check(!gamepad_target(640, 360));
    check(!gamepad_target(1920, 1080));
    check(framebuffer_ortho(360, -360, -640, 640));
    check(framebuffer_ortho(0, 720, 0, 1280));
    check(framebuffer_ortho(240, -240, -427, 427));
    check(!framebuffer_ortho(3780, -3780, -6720, 6720));

    float matrix[8] = {2.0f / 1280.0f, 0, 0, 0, 0, 2.0f / 720.0f, 0, 0};
    check(screen_ortho_scale(matrix, 8, 1280, 720));
    matrix[0] = 2.0f / (1280.0f * (wide / kBase));
    check(!screen_ortho_scale(matrix, 8, 1280, 720));
    float zoom = 1.07f;
    matrix[0] = 2.0f / 1280.0f * zoom;
    matrix[5] = -2.0f / 720.0f * zoom;
    check(screen_ortho_scale(matrix, 8, 1280, 720));
    matrix[5] = 2.0f / 720.0f;
    check(!screen_ortho_scale(matrix, 8, 1280, 720));
    matrix[0] = 1.358f;
    matrix[5] = 2.414f;
    check(!screen_ortho_scale(matrix, 8, 1280, 720));

    float background[] = {1316, 740, -658, 370, 255, 255, 255, 40};
    check(fullscreen_quad(background, 8, 1280, 720));
    float band[] = {1280, 120, -640, 120, 255, 255, 255, 0};
    check(fullscreen_quad(band, 8, 1280, 720));
    float plate[] = {1280, 0, 0, 0, 0, 720, 0, 0, 0, 0, 1, 0};
    check(fullscreen_quad(plate, 12, 1280, 720));
    float sprite[] = {1280, 720, -640, 360, 160, 121, -80, 60.5f, 255, 255, 255, 255};
    check(!fullscreen_quad(sprite, 12, 1280, 720));
    float card[] = {800, 338, -400, 169, 255, 255, 255, 255};
    check(!fullscreen_quad(card, 8, 1280, 720));

    float rows[16] = {};
    rows[0] = 2.0f / 1280.0f;
    rows[5] = 2.0f / 720.0f;
    uint32_t span_begin[4] = {0, 4, 8, 12};
    uint32_t span_end[4] = {4, 8, 12, 16};
    check(screen_ortho_ranges(rows, span_begin, span_end, 4, 1280, 720));
    check(!screen_ortho_ranges(rows, span_begin, span_end, 1, 1280, 720));
    rows[5] = -2.0f / 720.0f;
    check(screen_ortho_ranges(rows, span_begin, span_end, 4, 1280, 720));

    float ax = wide / kBase;
    float extent = 1280.0f * ax;
    OrthoPlace full = place_ortho(640, 640, 360, 360, extent, 720, 1280, 720, extent / 1280.0f, 1, ax, 1, false);
    check(same(full.x, 0) && same(full.w, extent));
    OrthoPlace centered = place_ortho(640, 640, 360, 360, extent, 720, 1280, 720, extent / 1280.0f, 1, ax, 1, true);
    check(same(centered.w, 1280) && same(centered.x, (extent - 1280.0f) * 0.5f));
    check(same(centered.y, 0) && same(centered.h, 720));

    float sx = 1707.0f / 1280.0f;
    check(cover_max(1280.0f * sx, 1707) == 1707);
    check(cover_min(0.0f, 1707) == 0);
    check(cover_max(720.0f, 720) == 720);
    check(cover_min(10.0f, 720) == 10);

    float guest[8] = {2.0f / 1280.0f, 0, 0, 0, 0, 2.0f / 720.0f, -1, 1};
    scale_guest_ortho(guest, 8, 1280, 720, ax, 1);
    check(same(guest[0], 2.0f / (1280.0f * ax)));
    check(same(guest[5], 2.0f / 720.0f));
    float divided[8] = {2.0f / (1280.0f * ax), 0, 0, 0, 0, -2.0f / 720.0f, 0, 0};
    scale_guest_ortho(divided, 8, 1280, 720, ax, 1);
    check(same(divided[0], 2.0f / (1280.0f * ax)));
    check(same(divided[5], -2.0f / 720.0f));

    check(center_framebuffer_draw(true, true, false));
    check(!center_framebuffer_draw(true, true, true));
    check(!center_framebuffer_draw(false, true, false));
    check(!center_framebuffer_draw(true, false, false));

    std::puts("aspect view: ortho and perspective passed");
}
