

#include "text_entry.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

#include "imgui.h"
#include "hostui.h"
#include "overlay.h"
#include "input/input.h"
#include "input/input_map.h"
#include "../mods/mods.h"
#include "../platform/keycodes.h"
#include "core/runtime.h"

namespace gfx { bool main_picture(float* x, float* y, float* w, float* h); }

namespace text_entry {
namespace {

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string utf8(const std::u16string& s, size_t from = 0, size_t to = std::u16string::npos) {
    std::string out;
    to = std::min(to, s.size());
    for (size_t i = from; i < to; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < to && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) out += (char)(0xC0 | c >> 6), out += (char)(0x80 | (c & 63));
        else if (c < 0x10000) out += (char)(0xE0 | c >> 12), out += (char)(0x80 | (c >> 6 & 63)), out += (char)(0x80 | (c & 63));
        else out += (char)(0xF0 | c >> 18), out += (char)(0x80 | (c >> 12 & 63)), out += (char)(0x80 | (c >> 6 & 63)), out += (char)(0x80 | (c & 63));
    }
    return out;
}

std::vector<uint32_t> code_points(const char* s) {
    std::vector<uint32_t> out;
    const unsigned char* p = (const unsigned char*)s;
    while (p && *p) {
        uint32_t c = *p++;
        int rest = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (c >= 0x80 && c < 0xC0) continue;
        c &= rest == 3 ? 7 : rest == 2 ? 15 : rest == 1 ? 31 : 127;
        while (rest-- > 0 && (*p & 0xC0) == 0x80) c = c << 6 | (*p++ & 63);
        out.push_back(c);
    }
    return out;
}
bool low_surrogate(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

uint32_t shifted(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c == 0x153) return 0x152;
    if (c >= 0x3041 && c <= 0x3096) return c + 0x60;
    return c;
}

struct Page {
    const char* name;
    std::vector<std::vector<const char*>> rows;
};
const Page kQwerty = {"ABC", {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", "'"},
    {"z", "x", "c", "v", "b", "n", "m", ",", ".", "-"}}};
const Page kAzerty = {"ABC", {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"a", "z", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"q", "s", "d", "f", "g", "h", "j", "k", "l", "m"},
    {"w", "x", "c", "v", "b", "n", "'", ",", ".", "-"}}};
const Page kQwertz = {"ABC", {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"q", "w", "e", "r", "t", "z", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", "'"},
    {"y", "x", "c", "v", "b", "n", "m", ",", ".", "-"}}};

const Page kAccents = {"ÀÉÑ", {
    {"à", "á", "â", "ä", "ã", "å", "æ", "ç", "è", "é"},
    {"ê", "ë", "ì", "í", "î", "ï", "ñ", "ò", "ó", "ô"},
    {"ö", "õ", "ø", "œ", "ù", "ú", "û", "ü", "ý", "ÿ"},
    {"ß", "ª", "º", "¡", "¿", "«", "»", "'", "-", "."}}};
const Page kSymbols = {"#+=", {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"!", "?", "&", "(", ")", ":", ";", "/", "+", "="},
    {"#", "%", "@", "*", "_", "\"", "'", "<", ">", "~"},
    {",", ".", "-", "[", "]", "$", "^", "|", "{", "}"}}};

const Page kKana = {"かな", {
    {"あ", "か", "さ", "た", "な", "は", "ま", "や", "ら", "わ"},
    {"い", "き", "し", "ち", "に", "ひ", "み", "", "り", "を"},
    {"う", "く", "す", "つ", "ぬ", "ふ", "む", "ゆ", "る", "ん"},
    {"え", "け", "せ", "て", "ね", "へ", "め", "", "れ", "ー"},
    {"お", "こ", "そ", "と", "の", "ほ", "も", "よ", "ろ", "・"}}};
const Page kKana2 = {"がぱ", {
    {"が", "ぎ", "ぐ", "げ", "ご", "ざ", "じ", "ず", "ぜ", "ぞ"},
    {"だ", "ぢ", "づ", "で", "ど", "ば", "び", "ぶ", "べ", "ぼ"},
    {"ぱ", "ぴ", "ぷ", "ぺ", "ぽ", "ぁ", "ぃ", "ぅ", "ぇ", "ぉ"},
    {"ゃ", "ゅ", "ょ", "っ", "ゎ", "ゔ", "！", "？", "〜", "ー"}}};
const Page kDigits = {"123", {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}}};

enum Special { kChar, kShift, kNextPage, kSpace, kDelete, kCancel, kOk };
struct Cell {
    Special kind = kChar;
    uint32_t ch = 0;
    int col = 0, span = 1;
};

struct HostEvent {
    enum Kind { Key, Text, Preedit } kind;
    int code = 0;
    bool down = false;
    std::string s;
};
std::mutex g_mu;
std::atomic<bool> g_active{false};
Request g_req;
Done g_done;
uint64_t g_serial = 0;
std::vector<HostEvent> g_events;

struct Ui {
    uint64_t serial = 0;
    Request req;
    std::u16string text;
    size_t caret = 0;
    std::string preedit;
    std::vector<const Page*> pages;
    int page = 0;
    int shift = 0;
    int row = 0, col = 0;
    std::vector<std::vector<Cell>> grid;
    float prev[input_map::kPadCount] = {};
    double next_repeat[input_map::kPadCount] = {};
    bool first = true;
    double full_since = -1;
    int result = 0;
    double last_draw = 0;
    std::shared_ptr<const game_font::Glyphs> glyphs;
    std::string note;
    double note_since = -1;
    std::u16string reported;
    float scale = 1.0f;
    float win_h = 0;
};
Ui U;

bool in_font(uint32_t c) { return !U.glyphs || U.glyphs->count(c); }

uint32_t key_char(uint32_t c) {
    const uint32_t s = U.shift ? shifted(c) : c;
    return in_font(s) ? s : c;
}

bool allowed(uint32_t c) {
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) return false;
    if (!in_font(c)) return false;
    switch (U.req.mode) {
    case 1: return c >= '0' && c <= '9';
    case 3: return (c < 0x80 && isalnum((int)c)) || c == '-' || c == '_' || c == '.';
    default: return true;
    }
}

void build_grid() {
    U.grid.clear();
    const Page& p = *U.pages[U.page];
    for (auto& r : p.rows) {
        std::vector<Cell> row;
        for (int i = 0; i < (int)r.size(); i++) {
            auto cp = code_points(r[i]);

            row.push_back({kChar, cp.empty() || !in_font(cp[0]) ? 0 : cp[0], i, 1});
        }
        U.grid.push_back(row);
    }
    if (U.req.mode == 1) {
        U.grid.push_back({{kDelete, 0, 0, 10}});
    } else {
        U.grid.push_back({{kShift, 0, 0, 2}, {kNextPage, 0, 2, 2}, {kSpace, 0, 4, 4}, {kDelete, 0, 8, 2}});
    }
    U.grid.push_back({{kCancel, 0, 0, 5}, {kOk, 0, 5, 5}});
    U.row = std::min(U.row, (int)U.grid.size() - 1);
}

void begin(const Request& r) {
    U.req = r;
    U.glyphs = r.glyphs;
    U.note.clear();
    U.reported = r.initial;
    U.text = r.initial;
    U.caret = U.text.size();
    U.preedit.clear();
    U.pages.clear();
    if (r.mode == 1) U.pages = {&kDigits};
    else {
        if (r.language == 0) U.pages = {&kKana, &kKana2};
        U.pages.push_back(r.language == 2 ? &kAzerty : r.language == 3 ? &kQwertz : &kQwerty);
        if (r.mode != 3) U.pages.push_back(&kAccents), U.pages.push_back(&kSymbols);
    }
    U.page = 0;
    U.shift = U.text.empty() && r.language != 0 && r.mode != 1 ? 1 : 0;
    U.row = 1, U.col = 0;
    if (U.pages[0]->rows.size() == 1) U.row = 0;
    build_grid();
    U.first = true;
    U.full_since = -1;
    U.result = 0;
}

void insert(uint32_t c) {
    if (c >= 0x20 && !in_font(c)) {
        U.note = "\"" + utf8(c > 0xFFFF ? std::u16string{(char16_t)(0xD800 + ((c - 0x10000) >> 10)), (char16_t)(0xDC00 + (c & 1023))}
                                         : std::u16string{(char16_t)c}) + "\" is not available in the game's font";
        U.note_since = now_s();
        return;
    }
    if (!allowed(c)) return;
    std::u16string units;
    if (c > 0xFFFF) c -= 0x10000, units = {(char16_t)(0xD800 + (c >> 10)), (char16_t)(0xDC00 + (c & 1023))};
    else units = {(char16_t)c};
    if ((int)(U.text.size() + units.size()) > U.req.max_len) {
        U.full_since = now_s();
        return;
    }
    U.text.insert(U.caret, units);
    U.caret += units.size();
}
void backspace() {
    if (!U.caret) return;
    size_t n = U.caret >= 2 && low_surrogate(U.text[U.caret - 1]) ? 2 : 1;
    U.text.erase(U.caret - n, n);
    U.caret -= n;
}
void delete_forward() {
    if (U.caret >= U.text.size()) return;
    size_t n = U.caret + 1 < U.text.size() && low_surrogate(U.text[U.caret + 1]) ? 2 : 1;
    U.text.erase(U.caret, n);
}
void caret_left() { if (U.caret) U.caret -= U.caret >= 2 && low_surrogate(U.text[U.caret - 1]) ? 2 : 1; }
void caret_right() { if (U.caret < U.text.size()) U.caret += U.caret + 1 < U.text.size() && low_surrogate(U.text[U.caret + 1]) ? 2 : 1; }

void next_page() {
    U.page = (U.page + 1) % (int)U.pages.size();
    build_grid();
}

void press(const Cell& c) {
    switch (c.kind) {
    case kChar:
        if (!c.ch) return;
        insert(key_char(c.ch));
        if (U.shift == 1 && shifted(c.ch) != c.ch) U.shift = 0;
        break;
    case kShift: U.shift = (U.shift + 1) % 3; break;
    case kNextPage: next_page(); break;
    case kSpace: insert(' '); break;
    case kDelete: backspace(); break;
    case kCancel: U.result = 2; break;
    case kOk: U.result = 1; break;
    }
}

void host_key(int code) {
    switch (code) {
    case kVK_Delete: backspace(); break;
    case kVK_ForwardDelete: delete_forward(); break;
    case kVK_LeftArrow: caret_left(); break;
    case kVK_RightArrow: caret_right(); break;
    case kVK_Home: case kVK_UpArrow: U.caret = 0; break;
    case kVK_End: case kVK_DownArrow: U.caret = U.text.size(); break;
    case kVK_Return: case kVK_ANSI_KeypadEnter: U.result = 1; break;
    case kVK_Escape: U.result = 2; break;
    default: break;
    }
}

bool pressed(const float* pad, int p, bool repeat) {
    const bool down = pad[p] > 0.5f, was = U.prev[p] > 0.5f;
    const double t = now_s();
    if (down && !was) {
        U.next_repeat[p] = t + 0.40;
        return true;
    }
    if (down && repeat && t >= U.next_repeat[p]) {
        U.next_repeat[p] = t + 0.085;
        return true;
    }
    return false;
}

int cell_at(int row, int col) {
    const auto& r = U.grid[row];
    int best = -1, dist = 1 << 20;
    for (int i = 0; i < (int)r.size(); i++) {
        if (r[i].kind == kChar && !r[i].ch) continue;
        int d = col < r[i].col ? r[i].col - col : col >= r[i].col + r[i].span ? col - (r[i].col + r[i].span - 1) : 0;
        if (d < dist) dist = d, best = i;
    }
    return best;
}
void move(int dx, int dy) {
    const int rows = (int)U.grid.size();
    if (dy) {
        for (int r = U.row + dy, n = 0; n < rows; r += dy, n++) {
            r = (r + rows) % rows;
            if (cell_at(r, U.col) >= 0) { U.row = r; break; }
        }
        return;
    }
    const auto& r = U.grid[U.row];
    int i = cell_at(U.row, U.col);
    for (int n = 0; n < (int)r.size(); n++) {
        i = (i + dx + (int)r.size()) % (int)r.size();
        if (r[i].kind != kChar || r[i].ch) break;
    }

    U.col = r[i].span > 1 ? r[i].col + r[i].span / 2 : r[i].col;
}

void controller(const float* pad) {
    using namespace input_map;
    if (U.first) {
        std::copy(pad, pad + kPadCount, U.prev);
        std::fill(std::begin(U.next_repeat), std::end(U.next_repeat), 1e300);
        return;
    }
    auto dir = [&](int dpad, int stick) {
        const bool a = pressed(pad, dpad, true), b = pressed(pad, stick, true);
        return a || b;
    };
    if (dir(kPadDUp, kPadLSUp)) move(0, -1);
    if (dir(kPadDDown, kPadLSDown)) move(0, 1);
    if (dir(kPadDLeft, kPadLSLeft)) move(-1, 0);
    if (dir(kPadDRight, kPadLSRight)) move(1, 0);
    if (pressed(pad, kPadA, false)) {
        int i = cell_at(U.row, U.col);
        if (i >= 0) press(U.grid[U.row][i]);
    }
    if (pressed(pad, kPadB, true)) backspace();
    if (pressed(pad, kPadX, false) && U.req.mode != 1) insert(' ');
    if (pressed(pad, kPadY, false) && U.req.mode != 1) U.shift = (U.shift + 1) % 3;
    if (pressed(pad, kPadLB, false) && U.pages.size() > 1) U.page = (U.page + (int)U.pages.size() - 1) % (int)U.pages.size(), build_grid();
    if (pressed(pad, kPadRB, false) && U.pages.size() > 1) next_page();
    if (pressed(pad, kPadLT, true)) caret_left();
    if (pressed(pad, kPadRT, true)) caret_right();
    if (pressed(pad, kPadMenu, false)) U.result = 1;
    std::copy(pad, pad + kPadCount, U.prev);
}

const ImVec4 kAccent(0.55f, 0.95f, 0.85f, 1.0f);
const ImVec4 kGold(1.0f, 0.85f, 0.35f, 1.0f);

void field(float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    const ImVec2 pad(fs * 0.5f, fs * 0.3f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, fs + pad.y * 2);
    ImGui::InvisibleButton("##field", size);
    const ImGuiStyle& st = ImGui::GetStyle();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), st.FrameRounding);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(kAccent), st.FrameRounding, 1.5f);
    const std::string before = utf8(U.text, 0, U.caret), after = utf8(U.text, U.caret);
    float x = p.x + pad.x;
    const float y = p.y + pad.y;

    if (ImGui::IsItemClicked() && U.preedit.empty()) {
        const float mx = ImGui::GetIO().MousePos.x - x;
        size_t best = 0;
        float bd = 1e9f;
        for (size_t i = 0; i <= U.text.size(); i++) {
            if (i < U.text.size() && low_surrogate(U.text[i])) continue;
            float w = ImGui::CalcTextSize(utf8(U.text, 0, i).c_str()).x;
            if (std::abs(w - mx) < bd) bd = std::abs(w - mx), best = i;
        }
        U.caret = best;
    }
    dl->PushClipRect(p, ImVec2(p.x + size.x - 4, p.y + size.y), true);
    const ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImVec2(x, y), text_col, before.c_str());
    x += ImGui::CalcTextSize(before.c_str()).x;
    if (!U.preedit.empty()) {
        const float w = ImGui::CalcTextSize(U.preedit.c_str()).x;
        dl->AddText(ImVec2(x, y), ImGui::GetColorU32(kGold), U.preedit.c_str());
        dl->AddLine(ImVec2(x, y + fs + 1), ImVec2(x + w, y + fs + 1), ImGui::GetColorU32(kGold), 1.5f);
        x += w;
    }
    if (fmod(ImGui::GetTime(), 1.0) < 0.6) dl->AddLine(ImVec2(x + 1, y - 1), ImVec2(x + 1, y + fs + 1), ImGui::GetColorU32(kAccent), 2.0f);
    dl->AddText(ImVec2(x + 3, y), text_col, after.c_str());
    dl->PopClipRect();
}

const char* special_label(const Cell& c, char* buf, size_t n) {
    switch (c.kind) {
    case kShift: return U.shift == 2 ? "CAPS" : U.shift == 1 ? "Shift (1)" : "Shift";
    case kNextPage: return U.pages[(U.page + 1) % U.pages.size()]->name;
    case kSpace: return "Space";
    case kDelete: return "Delete";
    case kCancel: return "Cancel";
    case kOk: return "OK";
    default: break;
    }
    uint32_t ch = key_char(c.ch);
    std::u16string s;
    if (ch > 0xFFFF) ch -= 0x10000, s = {(char16_t)(0xD800 + (ch >> 10)), (char16_t)(0xDC00 + (ch & 1023))};
    else s = {(char16_t)ch};
    snprintf(buf, n, "%s", utf8(s).c_str());
    return buf;
}

void keyboard(float unit, float key_h, float sp) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp, sp));
    const int sel = cell_at(U.row, U.col);
    const ImVec2 origin = ImGui::GetCursorPos();
    for (int r = 0; r < (int)U.grid.size(); r++) {

        const float y = origin.y + r * (key_h + sp) + (r >= (int)U.pages[U.page]->rows.size() ? sp * 2 : 0);
        for (int i = 0; i < (int)U.grid[r].size(); i++) {
            const Cell& c = U.grid[r][i];
            if (c.kind == kChar && !c.ch) continue;
            ImGui::SetCursorPos(ImVec2(origin.x + c.col * (unit + sp), y));
            const bool selected = r == U.row && i == sel;
            const bool usable = c.kind != kChar || allowed(c.ch);
            int colors = 0;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.48f, 0.12f, 1.0f)), colors++;
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.58f, 0.16f, 1.0f)), colors++;
            } else if (c.kind == kOk) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.46f, 0.36f, 0.95f)), colors++;
            } else if (c.kind != kChar) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.24f, 0.34f, 0.95f)), colors++;
            }
            if (c.kind == kShift && U.shift) ImGui::PushStyleColor(ImGuiCol_Text, kGold), colors++;
            char buf[16];
            ImGui::PushID(r * 64 + i);
            ImGui::BeginDisabled(!usable);
            if (ImGui::Button(special_label(c, buf, sizeof buf), ImVec2(unit * c.span + sp * (c.span - 1), key_h))) {
                U.row = r;
                U.col = c.span > 1 ? c.col + c.span / 2 : c.col;
                press(c);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::PopStyleColor(colors);
            if (selected)
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(kGold),
                                                    ImGui::GetStyle().FrameRounding, 2.5f);
        }
    }
    ImGui::PopStyleVar();
}

constexpr float kBelowField = 0.40f;

void window() {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    float px = 0, py = 0, pw = ds.x, ph = ds.y;
    if (float x, y, w, h; gfx::main_picture(&x, &y, &w, &h) && w > 0 && h > 0)
        px = x * ds.x, py = y * ds.y, pw = w * ds.x, ph = h * ds.y;
    const float k = U.scale, margin = 6.0f;
    const float base = ImGui::GetStyle().FontSizeBase;
    ImGui::PushFont(nullptr, base * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * k, 10 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9 * k, 5 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * k, 6 * k));
    const float sp = 6 * k;

    const float inner = std::min(620.0f * k, std::min(pw, ds.x) - 28 * k - 2 * margin);
    const float unit = (inner - sp * 9) / 10.0f;
    const float key_h = std::clamp(unit * 0.78f, 18.0f * k, 44.0f * k);
    const float top = py + ph * kBelowField, bottom = std::min(py + ph, ds.y) - margin;
    float y = top;
    if (U.win_h > 0 && y + U.win_h > bottom) y = std::max(margin, bottom - U.win_h);
    ImGui::SetNextWindowPos(ImVec2(std::clamp(px + pw * 0.5f, 0.0f, ds.x), y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowSize(ImVec2(inner + 28 * k, U.win_h), ImGuiCond_Always);
    if (U.first) ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##text_entry", nullptr, fl)) {
        const float fsz = ImGui::GetFontSize();
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        if (!U.req.hint.empty()) ImGui::TextWrapped("%s", utf8(U.req.hint).c_str());
        else if (U.req.max_len == 1) ImGui::TextUnformatted("Enter one character");
        else ImGui::Text("Enter text (up to %d characters)", U.req.max_len);
        ImGui::PopStyleColor();
        ImGui::PushFont(nullptr, fsz * 1.35f);
        field(inner);
        ImGui::PopFont();

        const float x0 = ImGui::GetCursorPosX();
        if (U.note_since >= 0 && now_s() - U.note_since < 2.5) {
            ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.45f, 1.0f), "%s", U.note.c_str());
            ImGui::SameLine();
        }
        const bool flash = U.full_since >= 0 && now_s() - U.full_since < 0.6;
        char count[32];
        snprintf(count, sizeof count, "%d / %d", (int)U.text.size(), U.req.max_len);
        ImGui::SetCursorPosX(x0 + inner - ImGui::CalcTextSize(count).x);
        ImGui::TextColored(flash ? ImVec4(1.0f, 0.45f, 0.40f, 1.0f) : ImVec4(0.70f, 0.78f, 0.84f, 1.0f), "%s", count);
        ImGui::PushFont(nullptr, fsz * 1.2f);
        keyboard(unit, key_h, sp);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.84f, 1.0f));
        ImGui::PushFont(nullptr, fsz * 0.9f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        ImGui::TextWrapped("Enter OK, Esc Cancel  |  A type, B delete, X space, Y shift, L / R pages, Start OK");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PopStyleColor();
        U.win_h = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().WindowPadding.y;
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopFont();

    if (U.win_h > 0) {
        const float want = std::clamp(k * (bottom - top) / U.win_h, 0.45f, 1.0f);
        if (std::fabs(want - k) > 0.015f) U.scale = want;
    }
}

}

bool start(const Request& r, Done done) {
    if (!overlay::alive()) return false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_req = r;
        if ((int)g_req.initial.size() > g_req.max_len) g_req.initial.resize(std::max(0, g_req.max_len));
        g_done = std::move(done);
        g_serial++;
        g_events.clear();
        g_active = true;
    }
    input::release_keys();
    hostui::post([] { mods::mouse_release(); });
    LOG("[text] the game asks for text (up to %d characters): on-screen text entry shown; type, or use the "
        "on-screen keyboard with a controller or the mouse", r.max_len);
    return true;
}

void dismiss() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_active) return;
    g_active = false;
    g_done = nullptr;
    LOG("[text] the game closed its keyboard");
}

bool active() { return g_active.load(std::memory_order_relaxed); }

void key(int code, bool down, bool repeat) {
    (void)repeat;
    if (!down || !active()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_events.size() < 1024) g_events.push_back({HostEvent::Key, code, down, {}});
}
void text(const char* s) {
    if (!s || !*s || !active()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_events.size() < 1024) g_events.push_back({HostEvent::Text, 0, false, s});
}
void preedit(const char* s) {
    if (!active()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_events.size() < 1024) g_events.push_back({HostEvent::Preedit, 0, false, s ? s : ""});
}

bool draw(const float* pad) {
    std::vector<HostEvent> events;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_active) return false;
        if (U.serial != g_serial) {
            U.serial = g_serial;
            begin(g_req);
        }
        events.swap(g_events);
    }
    for (const HostEvent& e : events) {
        if (U.result) break;
        switch (e.kind) {
        case HostEvent::Key: host_key(e.code); break;
        case HostEvent::Text:
            for (uint32_t c : code_points(e.s.c_str())) insert(c);
            U.preedit.clear();
            if (U.shift == 1 && !U.text.empty()) U.shift = 0;
            break;
        case HostEvent::Preedit: U.preedit = e.s; break;
        }
    }

    if (now_s() - U.last_draw > 0.25) U.first = true;
    U.last_draw = now_s();
    if (!U.result) controller(pad);
    window();
    U.first = false;
    if (U.text != U.reported && !U.result) {
        U.reported = U.text;
        if (U.req.changed) U.req.changed(U.text);
    }
    if (!U.result) return false;
    Done done;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_active || U.serial != g_serial) return false;
        g_active = false;
        done = std::move(g_done);
        g_done = nullptr;
    }
    const bool ok = U.result == 1;
    if (ok) LOG("[text] OK: \"%s\"", utf8(U.text).c_str());
    else LOG("[text] cancelled");
    if (done) done(ok, ok ? U.text : std::u16string());
    return true;
}

}
