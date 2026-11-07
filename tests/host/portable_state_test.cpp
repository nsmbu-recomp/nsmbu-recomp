

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "save/portable_state.h"
#include "save/full_state_header.h"

using namespace pstate;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);     \
            failures++;                                                 \
        }                                                               \
    } while (0)

static State sample() {
    State s;
    s.title_id = "0005000010101d00";
    s.title_version = 0;
    s.game_hash = "0123456789abcdef";
    s.runtime = "v0.2.6 (abc1234)";
    s.created = "2026-10-08 14:03:11";
    s.file_slot = 1;
    s.controller = 2;
    s.player_name = "Mario";
    s.stage = "1-1";
    s.start_point = 0;
    s.start_room = 44;
    s.layer = -1;
    s.room = 44;
    s.pos[0] = -199836.5f;
    s.pos[1] = 0.25f;
    s.pos[2] = 315424.125f;
    s.angle_y = -16384;
    s.player_proc = 0x88;
    s.on_ship = true;
    s.has_ship = true;
    s.ship_pos[0] = -199830.25f;
    s.ship_pos[1] = -12.5f;
    s.ship_pos[2] = 315420.0f;
    s.ship_angle_y = 12345;
    s.time_of_day = 187.5f;
    s.date = 12;
    s.savedata.assign(kSaveDataSize, 0);
    for (size_t i = 0; i < kSaveDataUsed; i++) s.savedata[i] = (uint8_t)(i * 7 + 3);
    seal_savedata(s.savedata);
    s.hd_player.assign(kHdPlayerSize, 1);
    s.hd_status.assign(kHdStatusSize, 2);
    s.hd_event.assign(kHdEventSize, 3);
    s.hd_map.assign(kHdMapSize, 4);
    return s;
}

static std::string replace(std::string s, const std::string& a, const std::string& b) {
    size_t p = s.find(a);
    assert(p != std::string::npos);
    return s.replace(p, a.size(), b);
}

static std::string reseal(std::string text) {
    size_t p = text.find("checksum = ");
    assert(p != std::string::npos);
    std::string body = text.substr(0, p);
    char ck[48];
    snprintf(ck, sizeof ck, "checksum = crc32:%08X\n", crc32(body.data(), body.size()));
    return body + ck;
}

int main() {
    std::string why;

    State s = sample();
    std::string text = write(s, why);
    CHECK(!text.empty());
    CHECK(text.size() < 8 * 1024);
    printf("sample file: %zu bytes\n", text.size());
    State r;
    CHECK(read(text, r, why));
    CHECK(r.title_id == s.title_id && r.runtime == s.runtime && r.created == s.created);
    CHECK(r.controller == 2);
    bool pro = false;
    pro = ss::restored_pro_controller(r.controller, pro);
    CHECK(pro);
    State legacy;
    CHECK(read(reseal(replace(text, "controller = 2\n", "")), legacy, why));
    CHECK(legacy.controller == 0);
    CHECK(!ss::restored_pro_controller(legacy.controller, false));
    CHECK(ss::restored_pro_controller(legacy.controller, true));
    for (const char* invalid : {"-1", "3", "Pro", "999999999999999999999"}) {
        State x;
        CHECK(!read(reseal(replace(text, "controller = 2", std::string("controller = ") + invalid)), x, why));
    }
    State bad_mode = s;
    bad_mode.controller = 3;
    CHECK(write(bad_mode, why).empty());
    CHECK(r.file_slot == 1 && r.player_name == "Mario" && r.stage == "1-1");
    CHECK(r.start_point == 0 && r.start_room == 44 && r.layer == -1 && r.room == 44);
    CHECK(r.pos[0] == s.pos[0] && r.pos[1] == s.pos[1] && r.pos[2] == s.pos[2]);
    CHECK(r.angle_y == -16384 && r.player_proc == 0x88 && r.on_ship);
    CHECK(r.has_ship && r.ship_angle_y == 12345 && r.ship_pos[0] == s.ship_pos[0] && r.ship_pos[1] == s.ship_pos[1] &&
          r.ship_pos[2] == s.ship_pos[2]);
    {
        State x;
        CHECK(!read(reseal(replace(text, "has_ship = 1", "has_ship = 0")), x, why));
    }
    CHECK(r.time_of_day == 187.5f && r.date == 12);
    CHECK(r.savedata == s.savedata && r.hd_player == s.hd_player && r.hd_status == s.hd_status &&
          r.hd_event == s.hd_event && r.hd_map == s.hd_map);

    {
        std::string crlf;
        for (char c : text) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }

        State x;
        CHECK(!read(crlf, x, why));
        CHECK(why.find("checksum") != std::string::npos);
    }

    {
        State x;
        CHECK(!read(reseal(replace(text, "format = 1", "format = 2")), x, why));
        CHECK(why.find("format version 2") != std::string::npos);
    }

    {
        State x;
        CHECK(!read(replace(text, "room = 44", "room = 45"), x, why));
        CHECK(why.find("checksum") != std::string::npos);
        CHECK(!read(text.substr(0, text.size() / 2), x, why));
        CHECK(!read(text + "extra = 1\n", x, why));
    }

    {
        State bad = sample();
        bad.savedata[0x10] ^= 0xFF;
        std::string t = write(bad, why);
        CHECK(!t.empty());
        State x;
        CHECK(!read(t, x, why));
        CHECK(why.find("save data checksum") != std::string::npos);
    }

    {
        State bad = sample();
        bad.savedata.resize(0x10000);
        CHECK(write(bad, why).empty());
        CHECK(why.find("savedata") != std::string::npos);
        bad = sample();
        bad.hd_map.push_back(0);
        CHECK(write(bad, why).empty());
    }

    {
        CHECK(blob_check(text, why));
        std::string big(40000, 'a');
        std::string t = reseal(replace(text, "date = 12\n", "date = 12\nmemory = " + big + "\n"));
        State x;
        CHECK(!blob_check(t, why));
        CHECK(!read(t, x, why));
        t = reseal(replace(text, "player_name = Mario", "player_name = " + std::string(200, 'x')));
        CHECK(!blob_check(t, why));

        std::string huge = text;
        while (huge.size() <= kMaxFileSize) huge = "# padding " + std::string(1000, '-') + "\n" + huge;
        CHECK(!blob_check(huge, why));
        CHECK(why.find("larger") != std::string::npos);
        CHECK(!read(huge, x, why));

        size_t p = text.find("hd_map = ");
        std::string line = text.substr(p, text.find('\n', p) - p + 1);
        t = reseal(replace(text, line, line + line));
        CHECK(!read(t, x, why));
    }

    {
        State x = sample();
        x.player_name = "Ma\nrio\x01";
        std::string t = write(x, why);
        State y;
        CHECK(read(t, y, why) && y.player_name == "Mario");
    }

    auto nsmbu_image = [](int slot) {
        std::vector<uint8_t> image(kNsmbuSaveDataSize, 0);
        image[0] = 'R';
        image[1] = 'P';
        image[2] = 'S';
        image[3] = 'D';
        image[4] = 1;
        image[5] = 0;
        image[6] = 0;
        image[7] = (uint8_t)slot;
        for (size_t i = 8; i < 32; i++) image[i] = (uint8_t)(i + 1);
        seal_nsmbu_savedata(image);
        return image;
    };
    auto header = [](State& s) {
        s.title_id = "0005000010101d00";
        s.title_version = 0;
        s.game_hash = "0123456789abcdef";
        s.runtime = "v0.6.2 (abc1234)";
        s.created = "2026-10-25 03:00:00";
        s.controller = 1;
        s.player_name = "Mario";
    };

    {
        State course;
        header(course);
        course.place = "course";
        course.file_slot = 2;
        course.world = 0;
        course.course = 1;
        course.stage = "1-2";
        course.pos[0] = 1.5f;
        course.pos[1] = 2.25f;
        course.pos[2] = -3.5f;
        course.savedata = nsmbu_image(2);
        std::string t = write(course, why);
        CHECK(!t.empty());
        CHECK(t.find("place = course") != std::string::npos);
        CHECK(t.find("player_pos = ") != std::string::npos);
        CHECK(t.find("on_ship") == std::string::npos);
        CHECK(t.find("time_of_day") == std::string::npos);
        CHECK(t.find("ship_pos") == std::string::npos);
        CHECK(t.find("\nroom = ") == std::string::npos);
        State back;
        CHECK(read(t, back, why));
        CHECK(back.place == "course" && back.world == 0 && back.course == 1 && back.file_slot == 2);
        CHECK(back.stage == "1-2" && back.pos[0] == course.pos[0] && back.pos[1] == course.pos[1] && back.pos[2] == course.pos[2]);
        CHECK(back.savedata == course.savedata && back.node == -1);
        CHECK(!read(reseal(replace(t, "player_pos = 1.5 2.25 -3.5\n", "")), back, why));
    }

    {
        State map;
        header(map);
        map.place = "map";
        map.file_slot = 0;
        map.world = 0;
        map.section = 1;
        map.node = 1;
        map.course = 0;
        map.stage = "W1 n1";
        map.savedata = nsmbu_image(0);
        std::string t = write(map, why);
        CHECK(!t.empty());
        CHECK(t.find("place = map") != std::string::npos);
        CHECK(t.find("node = 1") != std::string::npos);
        CHECK(t.find("player_pos") == std::string::npos);
        State back;
        CHECK(read(t, back, why));
        CHECK(back.place == "map" && back.world == 0 && back.section == 1 && back.node == 1 && back.course == 0);
        CHECK(back.stage == "W1 n1" && back.savedata == map.savedata && back.file_slot == 0);
        map.course = -1;
        map.stage = "W1 n1";
        t = write(map, why);
        CHECK(read(t, back, why) && back.course == -1 && back.node == 1);
        CHECK(!read(reseal(replace(t, "node = 1\n", "")), back, why));
    }

    {
        State bad;
        header(bad);
        bad.place = "course";
        bad.file_slot = 0;
        bad.world = 1;
        bad.course = 0;
        bad.stage = "2-1";
        bad.pos[0] = 1;
        bad.savedata = nsmbu_image(1);
        std::string t = write(bad, why);
        CHECK(!t.empty());
        State back;
        CHECK(!read(t, back, why));
        CHECK(why.find("save file does not match") != std::string::npos);
        bad.world = 9;
        bad.savedata = nsmbu_image(0);
        bad.file_slot = 0;
        CHECK(write(bad, why).empty());
    }

    {
        State course;
        header(course);
        course.place = "course";
        course.file_slot = 1;
        course.world = 0;
        course.course = 0;
        course.stage = "1-1";
        course.pos[0] = 0;
        course.savedata = nsmbu_image(1);
        CHECK(nsmbu_savedata_ok(course.savedata));
        course.savedata[0x20] ^= 0xFF;
        CHECK(!nsmbu_savedata_ok(course.savedata));
        std::string t = write(course, why);
        CHECK(!t.empty());
        State back;
        CHECK(!read(t, back, why));
        CHECK(why.find("save data checksum") != std::string::npos);
        course.savedata = nsmbu_image(1);
        course.savedata[0] = 'X';
        seal_nsmbu_savedata(course.savedata);
        t = write(course, why);
        CHECK(!t.empty());
        CHECK(!read(t, back, why));
        CHECK(why.find("save data checksum") != std::string::npos);
    }

    CHECK(crc32("123456789", 9) == 0xCBF43926u);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("portable_state_test: all passed\n");
    return 0;
}
