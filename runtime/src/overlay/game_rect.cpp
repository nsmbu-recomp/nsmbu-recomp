#include "game_rect.h"

#include "gfx/aspect.h"
#include "gfx/display.h"
#include "gfx/display_modes.h"
#include "gfx/renderer.h"

namespace overlay {

bool game_content_rect(float display_w, float display_h, float& ox, float& oy, float& ow, float& oh) {
    float px = 0, py = 0, pw = 1, ph = 1;
    if (gfx::main_picture(&px, &py, &pw, &ph) && pw > 0 && ph > 0) {
        ox = px * display_w;
        oy = py * display_h;
        ow = pw * display_w;
        oh = ph * display_h;
    } else {
        const gfx::Box b = gfx::layout(display_w, display_h, 1280.0f, 720.0f, 0, 0, false).tv;
        if (b.w > 0 && b.h > 0) {
            ox = b.x;
            oy = b.y;
            ow = b.w;
            oh = b.h;
        } else {
            ox = 0;
            oy = 0;
            ow = display_w;
            oh = display_h;
        }
    }
    float kx = 1, ky = 1;
    if (aspect::content_clip() && render::target_aspect_factors(1280, 720, kx, ky) && kx > 0 && ky > 0) {
        const float ix = ox + ow * (1.0f - kx) * 0.5f;
        const float iy = oy + oh * (1.0f - ky) * 0.5f;
        ox = ix;
        oy = iy;
        ow *= kx;
        oh *= ky;
    }
    return ow > 1 && oh > 1;
}

}
