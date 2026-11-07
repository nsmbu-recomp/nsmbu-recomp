#include "save/save_preview.h"
#include "save/portable_state.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static std::vector<uint8_t> blank() {
    std::vector<uint8_t> image(pstate::kNsmbuSaveDataSize, 0);
    image[0] = 'R';
    image[1] = 'P';
    image[2] = 'S';
    image[3] = 'D';
    image[4] = 1;
    image[5] = 2;
    image[6] = 3;
    image[7] = 0;
    return image;
}

static uint8_t* main_rec(std::vector<uint8_t>& image, size_t file) {
    return image.data() + pstate::kNsmbuMainOff + file * pstate::kNsmbuSlotSize;
}

static uint8_t* quick_rec(std::vector<uint8_t>& image, size_t file) {
    return image.data() + pstate::kNsmbuQuickOff + file * pstate::kNsmbuSlotSize;
}

int main() {
    {
        auto info = save_preview::inspect_bytes(nullptr, 0);
        assert(!info.present && !info.readable);
        assert(save_preview::summary(info) == "No game save yet.");
    }
    {
        std::vector<uint8_t> short_buf(16, 0);
        auto info = save_preview::inspect_bytes(short_buf.data(), short_buf.size());
        assert(!info.present);
    }
    {
        auto image = blank();
        image[0] = 'X';
        auto info = save_preview::inspect_bytes(image.data(), image.size());
        assert(info.present && !info.readable);
        assert(save_preview::summary(info) == "Game save could not be read.");
    }
    {
        auto image = blank();
        auto info = save_preview::inspect_bytes(image.data(), image.size());
        assert(info.readable && info.used_files == 0 && info.last_file == 0 && !info.current.used);
        assert(info.version[0] == 1 && info.version[1] == 2 && info.version[2] == 3);
        assert(save_preview::summary(info).find("empty") != std::string::npos);
    }
    {
        auto image = blank();
        image[7] = 1;
        uint8_t* rec = main_rec(image, 1);
        rec[0] = 1;
        rec[2] = 42;
        rec[4] = 2;
        rec[5] = 1;
        rec[6] = 7;
        rec[0x0C] = 8;
        put_be32(rec + 0x30, 3661);
        rec[0x6B] = 1;
        rec[0x6B + 1] = 2;
        rec[0x6B + 2] = 0;
        auto info = save_preview::inspect_bytes(image.data(), image.size());
        assert(info.readable && info.used_files == 1 && info.last_file == 1);
        assert(info.current.used && !info.current.from_quick);
        assert(info.current.lives == 8 && info.current.coins == 42);
        assert(info.current.world == 2 && info.current.section == 1 && info.current.node == 7);
        assert(info.current.courses == 2 && info.current.play_seconds == 3661);
        std::string line = save_preview::summary(info);
        assert(line.find("last file 2") != std::string::npos);
        assert(line.find("World 3") != std::string::npos);
        assert(line.find("8 lives") != std::string::npos);
        assert(line.find("42 coins") != std::string::npos);
        assert(line.find("2 courses") != std::string::npos);
        assert(line.find("1h 1m") != std::string::npos);
    }
    {
        auto image = blank();
        uint8_t* main = main_rec(image, 0);
        main[0] = 1;
        main[0x0C] = 3;
        uint8_t* quick = quick_rec(image, 0);
        quick[0] = 1;
        quick[0x0C] = 0;
        quick[4] = 4;
        auto info = save_preview::inspect_bytes(image.data(), image.size());
        assert(info.current.from_quick && info.current.lives == 5 && info.current.world == 4);
    }
    {
        auto image = blank();
        main_rec(image, 0)[0] = 1;
        main_rec(image, 2)[0] = 1;
        image[7] = 2;
        auto info = save_preview::inspect_bytes(image.data(), image.size());
        assert(info.used_files == 2 && info.last_file == 2 && info.current.used);
    }
    assert(save_preview::inspect_dir("").present == false);
    assert(save_preview::inspect_file("/no/such/rp_savedata.dat").present == false);
    return 0;
}
