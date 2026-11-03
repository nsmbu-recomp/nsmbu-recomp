

#include "platform/keycodes.h"
#include <filesystem>
#include <chrono>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "input/input_map.h"
#include "platform/touch_face.h"

#include "core/env.h"
using namespace input_map;

static int g_fail = 0, g_pass = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (x) g_pass++;                                                      \
        else { g_fail++; fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #x); } \
    } while (0)

static input::PadState press(const Mapping& m, std::initializer_list<int> codes) {
    bool keys[256] = {};
    for (int c : codes) keys[c] = true;
    return keyboard_state(m, keys);
}

static void test_defaults() {
    Mapping m = Mapping::defaults();

    struct { int code; uint32_t bit; } old[] = {
        {kVK_ANSI_K, input::kA}, {kVK_Space, input::kA}, {kVK_ANSI_J, input::kB}, {kVK_ANSI_L, input::kX},
        {kVK_Shift, input::kY}, {kVK_ANSI_Q, input::kL}, {kVK_ANSI_E, input::kR},
        {kVK_Control, input::kZR}, {kVK_Return, input::kPlus}, {kVK_Tab, input::kMinus}, {kVK_ANSI_H, input::kHome},
        {kVK_ANSI_1, input::kUp}, {kVK_ANSI_2, input::kDown}, {kVK_ANSI_3, input::kLeft}, {kVK_ANSI_4, input::kRight},
        {kVK_ANSI_X, input::kStickL}, {kVK_ANSI_V, input::kStickR},
    };
    for (auto& o : old) CHECK(press(m, {o.code}).buttons == o.bit);
    auto s = press(m, {kVK_ANSI_W, kVK_ANSI_D});
    CHECK(std::fabs(s.lx - 0.7071f) < 1e-4 && std::fabs(s.ly - 0.7071f) < 1e-4);
    s = press(m, {kVK_ANSI_A});
    CHECK(s.lx == -1 && s.ly == 0);
    s = press(m, {kVK_ANSI_S});
    CHECK(s.ly == -1);
    s = press(m, {kVK_UpArrow, kVK_RightArrow});
    CHECK(s.rx == 1 && s.ry == 1);
    CHECK(press(m, {kVK_ANSI_Z}).buttons == 0);

    CHECK(m.keys[kScreenshot][0] == kVK_F10 && m.pad[kScreenshot] == kPadNone);
    CHECK(press(m, {kVK_F10}).buttons == 0 && press(m, {kVK_F10}).lx == 0);
    CHECK(action_bit(kScreenshot) == 0 && action_from_id("Screenshot") == kScreenshot);

    for (int a = 0; a < kActionCount; a++)
        for (int k : m.keys[a]) {
            CHECK(k == kNoKey || !reserved_key(k));
            CHECK(k == kNoKey || key_users(m, k, a).empty());
        }

    float v[kPadCount] = {};
    v[kPadA] = 1;
    CHECK(controller_state(m, v).buttons == input::kB);
    v[kPadA] = 0; v[kPadB] = 1;
    CHECK(controller_state(m, v).buttons == input::kA);
    v[kPadB] = 0; v[kPadLT] = 0.4f;
    CHECK(controller_state(m, v).buttons == 0);
    v[kPadLT] = 0.8f;
    CHECK(controller_state(m, v).buttons == input::kZL);
    v[kPadLT] = 0; v[kPadLSLeft] = 0.6f; v[kPadRSUp] = 0.3f;
    s = controller_state(m, v);
    CHECK(std::fabs(s.lx + 0.6f) < 1e-6 && std::fabs(s.ry - 0.3f) < 1e-6);
}

static void test_face_layout() {
    Mapping m = Mapping::defaults();

    CHECK(face_layout(m) == FaceLayout::kPosition);
    CHECK(m.pad[kA] == kPadB && m.pad[kB] == kPadA && m.pad[kX] == kPadY && m.pad[kY] == kPadX);
    CHECK(face_input(kA) == kPadB && face_input(kB) == kPadA);

    apply_face_layout(m, FaceLayout::kLabels);
    CHECK(face_layout(m) == FaceLayout::kLabels);
    CHECK(m.pad[kA] == kPadA && m.pad[kB] == kPadB && m.pad[kX] == kPadX && m.pad[kY] == kPadY);
    float v[kPadCount] = {};
    v[kPadA] = 1;
    CHECK(controller_state(m, v).buttons == input::kA);
    v[kPadA] = 0; v[kPadB] = 1;
    CHECK(controller_state(m, v).buttons == input::kB);
    v[kPadB] = 0; v[kPadX] = 1;
    CHECK(controller_state(m, v).buttons == input::kX);

    CHECK(m.keys[kA] == Mapping::defaults().keys[kA]);
    CHECK(m.keys[kB] == Mapping::defaults().keys[kB]);
    CHECK(m.keys[kX] == Mapping::defaults().keys[kX]);
    CHECK(m.keys[kY] == Mapping::defaults().keys[kY]);
    CHECK(m.pad[kL] == Mapping::defaults().pad[kL]);
    CHECK(m.pad[kZL] == Mapping::defaults().pad[kZL]);
    CHECK(m.pad[kPlus] == Mapping::defaults().pad[kPlus]);
    CHECK(m.pad[kStickLClick] == Mapping::defaults().pad[kStickLClick]);
    CHECK(m.pad[kLUp] == Mapping::defaults().pad[kLUp]);
    CHECK(m.pad[kScreenshot] == Mapping::defaults().pad[kScreenshot]);

    m.pad[kX] = kPadLB;
    CHECK(face_layout(m) == FaceLayout::kCustom);

    apply_face_layout(m, FaceLayout::kPosition);
    CHECK(face_layout(m) == FaceLayout::kPosition);
    apply_face_layout(m, FaceLayout::kCustom);
    CHECK(face_layout(m) == FaceLayout::kPosition);

    CHECK(face_layout_label(FaceLayout::kPosition) && *face_layout_label(FaceLayout::kPosition));
    CHECK(face_layout_label(FaceLayout::kLabels) && *face_layout_label(FaceLayout::kLabels));
    CHECK(std::string(face_layout_label(FaceLayout::kPosition)) != face_layout_label(FaceLayout::kLabels));
}

static void test_names() {
    for (int a = 0; a < kActionCount; a++) CHECK(action_from_id(action_id(a)) == a);
    for (int p = 1; p < kPadCount; p++) CHECK(pad_from_id(pad_id(p)) == p);
    for (int k = 0; k < 256; k++) CHECK(key_from_id(key_id(k)) == k);
    CHECK(key_from_id("LeftShift") == kVK_Shift);
    CHECK(key_from_id("Nonsense") == kNoKey);
    CHECK(action_from_id("Nonsense") == -1);
}

static void test_reserved() {
    for (int k : std::initializer_list<int>{kVK_ANSI_N, kVK_ANSI_6, kVK_ANSI_8, kVK_ANSI_9, kVK_ANSI_P,
                  kVK_ANSI_R, kVK_F12, kVK_Escape})
        CHECK(reserved_key(k) != nullptr);
    CHECK(reserved_key(kVK_ANSI_K) == nullptr);
    CHECK(reserved_key(kVK_ANSI_5) == nullptr);
}

static void test_conflicts() {
    Mapping m = Mapping::defaults();
    auto u = key_users(m, kVK_ANSI_K);
    CHECK(u.size() == 1 && u[0] == kA);
    CHECK(key_users(m, kVK_ANSI_K, kA).empty());
    m.keys[kB][1] = kVK_ANSI_K;
    u = key_users(m, kVK_ANSI_K, kB);
    CHECK(u.size() == 1 && u[0] == kA);
    CHECK(key_users(m, kVK_ANSI_K).size() == 2);
    CHECK(press(m, {kVK_ANSI_K}).buttons == (input::kA | input::kB));
    auto p = pad_users(m, kPadA);
    CHECK(p.size() == 1 && p[0] == kB);
    CHECK(pad_users(m, kPadNone).empty());
}

static void test_json_roundtrip() {
    Mapping m = Mapping::defaults();
    std::string err;
    Mapping r;
    CHECK(from_json(to_json(m), r, &err) && err.empty());
    CHECK(r == m);

    std::swap(m.keys[kA], m.keys[kB]);
    std::swap(m.pad[kA], m.pad[kB]);
    m.keys[kHome] = {kNoKey, kNoKey};
    m.pad[kHome] = kPadNone;
    m.keys[kZR] = {kVK_ANSI_C, 93};
    m.deadzone = 0.15f;
    m.invert_camera_y = true;
    std::string j = to_json(m);
    CHECK(j.find("\"Key93\"") != std::string::npos);
    CHECK(from_json(j, r, &err) && err.empty());
    CHECK(r == m);
    CHECK(press(r, {kVK_ANSI_K}).buttons == input::kB);
    CHECK(press(r, {kVK_ANSI_J}).buttons == input::kA);
    CHECK(press(r, {kVK_ANSI_H}).buttons == 0);
    CHECK(press(r, {kVK_UpArrow}).ry == -1);
    CHECK(press(r, {kVK_ANSI_W}).ly == 1);
}

static void test_json_partial_and_bad() {
    Mapping r;
    std::string err;

    CHECK(from_json(R"({"keyboard": {"A": "J", "B": ["K", "Space"]}, "controller": {"Home": null}})", r, &err));
    CHECK(err.empty());
    CHECK(r.keys[kA][0] == kVK_ANSI_J && r.keys[kA][1] == kNoKey);
    CHECK(r.keys[kB][0] == kVK_ANSI_K && r.keys[kB][1] == kVK_Space);
    CHECK(r.keys[kX] == Mapping::defaults().keys[kX]);
    CHECK(r.pad[kHome] == kPadNone && r.pad[kA] == kPadB);
    CHECK(r.deadzone == 0 && !r.invert_camera_y);
    // unknown names and app shortcuts are dropped with a warning, the rest still loads
    CHECK(from_json(R"({"keyboard": {"A": ["R", "K"], "Jump": ["J"], "B": ["Bogus"]}, "options": {"stick_deadzone": 5}})", r, &err));
    CHECK(!err.empty());
    CHECK(r.keys[kA][0] == kVK_ANSI_K && r.keys[kA][1] == kNoKey);
    CHECK(r.keys[kB][0] == kNoKey);
    CHECK(r.deadzone == 0.9f);  // clamped
    // syntax errors fail and leave the output alone
    Mapping before = r;
    CHECK(!from_json("{\"keyboard\": {\"A\": [\"K\"}", r, &err) && !err.empty());
    CHECK(!from_json("[1, 2]", r, &err));
    CHECK(!from_json("", r, &err));
    CHECK(!from_json("{} x", r, &err));
    CHECK(r == before);
    CHECK(from_json("{}", r, &err) && r == Mapping::defaults());
}

static void test_deadzone() {
    Mapping m = Mapping::defaults();
    m.deadzone = 0.2f;
    float v[kPadCount] = {};
    v[kPadLSRight] = 0.15f;
    CHECK(controller_state(m, v).lx == 0);
    v[kPadLSRight] = 1.0f;
    CHECK(std::fabs(controller_state(m, v).lx - 1.0f) < 1e-6);
    v[kPadLSRight] = 0.6f;
    CHECK(std::fabs(controller_state(m, v).lx - 0.5f) < 1e-6);  // (0.6 - 0.2) / 0.8
    v[kPadLSRight] = 0; v[kPadRSDown] = 0.6f;
    m.invert_camera_y = true;
    CHECK(std::fabs(controller_state(m, v).ry - 0.5f) < 1e-6);
}

static void test_config_json() {
    Config c = Config::defaults();
    CHECK(c.sync);
    for (int i = 1; i < kPlayers; i++) CHECK(c.players[i] == c.players[0]);

    c.sync = false;
    c.players[1].pad[kA] = kPadX;
    c.players[2].deadzone = 0.25f;
    std::string err;
    Config r;
    CHECK(config_from_json(config_to_json(c), r, &err) && err.empty());
    CHECK(r == c);
    CHECK(r.players[1].pad[kA] == kPadX && r.players[2].deadzone == 0.25f);

    Mapping m = Mapping::defaults();
    m.pad[kA] = kPadNone;
    CHECK(config_from_json(to_json(m), r, &err) && err.empty());
    CHECK(r.sync);
    for (int i = 0; i < kPlayers; i++) CHECK(r.players[i] == m);

    Config s = c;
    s.sync = true;
    CHECK(config_from_json(config_to_json(s), r, &err) && err.empty());
    for (int i = 1; i < kPlayers; i++) CHECK(r.players[i] == r.players[0]);
}

static void test_players() {
    set_config(Config::defaults(), false);
    CHECK(kPlayers == 4);
    CHECK(sync_players());
    Mapping m = player(2);
    m.pad[kA] = kPadNone;
    set_player(2, m, false);
    for (int i = 0; i < kPlayers; i++) CHECK(player(i) == m);

    set_sync_players(false, 0, false);
    CHECK(!sync_players());
    Mapping first = player(0);
    first.pad[kB] = kPadX;
    set_player(0, first, false);
    CHECK(player(0).pad[kB] == kPadX);
    CHECK(player(1).pad[kB] != kPadX);
    CHECK(player(3) == m);

    set_sync_players(true, 1, false);
    CHECK(sync_players());
    for (int i = 0; i < kPlayers; i++) CHECK(player(i) == player(1));
    set_config(Config::defaults(), false);
}

static void test_issue_9_multiplayer_bindings() {
    Config c = Config::defaults();
    c.sync = false;
    const int pads[4] = {kPadA, kPadB, kPadX, kPadY};
    for (int i = 0; i < kPlayers; i++) {
        c.players[i] = Mapping::defaults();
        c.players[i].pad[kA] = pads[i];
        c.players[i].deadzone = 0.1f * (float)(i + 1);
    }
    std::string err;
    Config r;
    CHECK(config_from_json(config_to_json(c), r, &err) && err.empty());
    CHECK(!r.sync);
    for (int i = 0; i < kPlayers; i++) {
        CHECK(r.players[i].pad[kA] == pads[i]);
        CHECK(std::fabs(r.players[i].deadzone - 0.1f * (float)(i + 1)) < 1e-6);
    }
    set_config(r, false);
    CHECK(!sync_players());
    for (int i = 0; i < kPlayers; i++) CHECK(player(i).pad[kA] == pads[i]);
    set_config(Config::defaults(), false);
}

static void test_files() {
    std::string dir=(std::filesystem::temp_directory_path()/("input_map_test."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    std::error_code ec;
    CHECK(std::filesystem::create_directory(dir,ec));
    std::string path = std::string(dir) + "/sub/dir/controls.json";  // directories are created
    Mapping m = Mapping::defaults();
    std::swap(m.keys[kA], m.keys[kB]);
    CHECK(save_file(path, m));
    Mapping r;
    std::string err;
    CHECK(load_file(path, r, &err) && r == m);
    CHECK(!load_file(std::string(dir) + "/missing.json", r, &err));
    std::string live = dir + "/controls.json";
    CHECK(save_file(live, m));
    // the live mapping follows NSMBU_USER_DIR
#ifdef _WIN32
    _putenv_s(env::USER_DIR.name(), dir.c_str());
#else
    setenv(env::USER_DIR.name(), dir.c_str(), 1);
#endif
    CHECK(default_path() == live);
    load_startup();
    CHECK(current() == m);
    uint32_t g = generation();
    set_current(Mapping::defaults());
    CHECK(generation() != g);
    CHECK(load_file(live, r, &err) && r == Mapping::defaults());  // saved
    Config c = Config::defaults();
    c.sync = false;
    c.players[1].pad[kA] = kPadX;
    c.players[3].invert_camera_y = true;
    CHECK(save_config_file(live, c));
    Config cr;
    CHECK(load_config_file(live, cr, &err) && cr == c);
    std::filesystem::remove_all(dir,ec);
}

static void test_conflict_helpers() {
    Mapping m = Mapping::defaults();
    CHECK(conflict_count(m) == 0);
    for (int a = 0; a < kActionCount; a++) CHECK(!has_conflict(m, a));
    m.keys[kZR][1] = kVK_ANSI_K;  // K is also A's key
    CHECK(has_conflict(m, kZR) && has_conflict(m, kA) && !has_conflict(m, kB));
    CHECK(conflict_count(m) == 1);
    m.pad[kX] = m.pad[kY];        // two actions on one controller button
    CHECK(has_conflict(m, kX) && has_conflict(m, kY));
    CHECK(conflict_count(m) == 2);
    m.pad[kX] = kPadNone;
    CHECK(!has_conflict(m, kX) && !has_conflict(m, kY) && conflict_count(m) == 1);
    CHECK(key_short_label(kVK_Shift) == "L ⇧");
    CHECK(key_short_label(kVK_ANSI_Keypad5) == "Num 5");
    CHECK(key_short_label(kVK_ANSI_K) == "K");
    CHECK(std::string(pad_short_label(kPadLT)) == "LT");
    CHECK(std::string(pad_short_label(kPadNone)).empty());
    for (int p = 1; p < kPadCount; p++) CHECK(*pad_short_label(p));
}

static void test_touch_face() {
    const uint32_t wiiu[4] = {input::kA, input::kB, input::kX, input::kY};
    for (FaceLayout layout : {FaceLayout::kPosition, FaceLayout::kLabels}) {
        Mapping m = Mapping::defaults();
        apply_face_layout(m, layout);
        for (int i = 0; i < 4; i++) {
            const uint32_t sdl = touch_face::buttons(1u << i, m);
            float v[kPadCount] = {};
            for (int p = 0; p < kPadCount; p++)
                if (touch_face::sdl_button(p) >= 0 && (sdl >> touch_face::sdl_button(p) & 1)) v[p] = 1;
            CHECK(controller_state(m, v).buttons == wiiu[i]);
        }
    }
    Mapping m = Mapping::defaults();
    CHECK(touch_face::buttons(1, m) == 1u << 1);
    apply_face_layout(m, FaceLayout::kLabels);
    CHECK(touch_face::buttons(1, m) == 1u << 0);
    m.pad[kA] = kPadLT;
    CHECK(touch_face::buttons(1, m) == 0);
    m.pad[kB] = kPadNone;
    CHECK(touch_face::buttons(2, m) == 0);
    CHECK(touch_face::buttons(0, Mapping::defaults()) == 0);
    CHECK(touch_face::sdl_button(kPadDRight) == 14 && touch_face::sdl_button(kPadLSUp) == -1);
}

int main() {
    test_conflict_helpers();
    test_touch_face();
    test_defaults();
    test_face_layout();
    test_names();
    test_reserved();
    test_conflicts();
    test_json_roundtrip();
    test_json_partial_and_bad();
    test_config_json();
    test_players();
    test_issue_9_multiplayer_bindings();
    test_deadzone();
    test_files();
    printf("input_map_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
