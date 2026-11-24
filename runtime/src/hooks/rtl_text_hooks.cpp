

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "overlay/game_font.h"
#include "text/rtl_text.h"
#include "core/runtime.h"

#include "core/env.h"
extern "C" {
void f_02871328_orig(Cpu* c);
void f_028709E0_orig(Cpu* c);
void f_0286E8B4_orig(Cpu* c);
void f_028785C8_orig(Cpu* c);
}

namespace rtl_text {

namespace {

std::atomic<bool> g_on{false};
std::shared_ptr<const game_font::Glyphs> g_font;
std::mutex g_mu;

bool has_glyph(uint32_t cp) { return g_font && g_font->count(cp); }

struct Rec {
    float pen, advance;
    uint32_t quad;
    int line;
    int32_t index;
};
struct Print {
    uint32_t writer = 0, text = 0, n = 0;
    uint32_t frame = 0;
    int line = 0;
    int32_t tag = -1;
    rtl::Plan plan;
    std::vector<Rec> recs;
};
thread_local std::vector<std::unique_ptr<Print>> t_prints;
thread_local float t_box_width = -1;
thread_local int t_logged = 0;

struct Break {
    uint32_t frame = 0, at = 0;
};
thread_local Break t_breaks[8];
thread_local unsigned t_break_next = 0;
uint32_t& last_break(uint32_t frame) {
    for (Break& b : t_breaks)
        if (b.frame == frame) return b.at;
    Break& b = t_breaks[t_break_next++ % 8];
    b = {frame, 0};
    return b.at;
}
bool breaks_here(uint32_t code) { return code == ' ' || code == 0x3000; }

Print* top() { return t_prints.empty() ? nullptr : t_prints.back().get(); }

bool rtl_unit(uint16_t u) { return (u >= 0x0590 && u <= 0x08FF) || (u >= 0xFB1D && u <= 0xFEFC); }

uint32_t shape_loose(uint32_t pos, bool* skip) {
    const uint16_t c = ld16(pos);
    *skip = false;
    if (c < 0x0600 || c > 0x06FF) return c;
    uint16_t buf[65];
    size_t before = 0;
    while (before < 32) {
        const uint16_t u = ld16(pos - 2 * (before + 1));
        if (u < 0x20) break;
        before++;
    }
    size_t n = 0;
    for (size_t k = before; k > 0; k--) buf[n++] = ld16(pos - 2 * k);
    const size_t at = n;
    buf[n++] = c;
    for (size_t k = 1; k <= 32; k++) {
        const uint16_t u = ld16(pos + 2 * k);
        if (u < 0x20) break;
        buf[n++] = u;
    }
    const rtl::Shaped r = rtl::shape_at([&](size_t k) { return (uint32_t)buf[k]; }, at, n, has_glyph);
    *skip = r.skip;
    return r.code;
}

void finish(Print& p) {
    if (p.recs.empty()) return;

    const uint32_t flags = ld32(p.writer + 0x44);
    const uint32_t align = flags & 3, origin = flags & 0x30;
    std::vector<std::vector<size_t>> lines;
    for (size_t i = 0; i < p.recs.size(); i++) {
        const int l = p.recs[i].line;
        if (l >= (int)lines.size()) lines.resize(l + 1);
        lines[l].push_back(i);
    }
    float block_right = 0;
    bool any = false;
    for (const auto& l : lines) {
        if (l.empty()) continue;
        const Rec& last = p.recs[l.back()];
        const float r = last.pen + last.advance;
        block_right = any ? std::max(block_right, r) : r;
        any = true;
    }
    for (const auto& l : lines) {
        if (l.empty()) continue;
        std::vector<rtl::Glyph> g;
        for (size_t i : l) {
            const Rec& r = p.recs[i];
            const bool known = r.index >= 0 && (uint32_t)r.index < p.n;
            g.push_back({r.pen, r.advance, known ? p.plan.level[r.index] : (uint8_t)1, known && p.plan.ws[r.index]});
        }
        const std::vector<float> x = rtl::reorder_line(g, 1);

        float shift = 0;
        if (align == 0) {
            const Rec& last = p.recs[l.back()];
            const float right = (origin == 0 && t_box_width >= 0 && t_prints.size() == 1) ? t_box_width : block_right;
            shift = right - (last.pen + last.advance);
        }
        if (env::RTL_TRACE() && t_logged < 400) {
            std::string d;
            for (size_t k = 0; k < l.size(); k++) {
                const Rec& r = p.recs[l[k]];
                char b[64];
                snprintf(b, sizeof b, "[%d L%d %.1f+%.1f->%.1f %s] ", r.index, g[k].level, r.pen, r.advance, x[k] + shift, r.quad ? "q" : "-");
                d += b;
            }
            LOG("[rtl]   line: %s", d.c_str());
        }
        for (size_t k = 0; k < l.size(); k++) {
            const Rec& r = p.recs[l[k]];
            if (!r.quad) continue;
            stf32(r.quad, ldf32(r.quad) + (x[k] - r.pen) + shift);
        }
    }
}

}

void language_pack_opened(const std::string& host_path) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_on.load()) return;
    const char* env = env::RTL();
    if (env && !strcmp(env, "0")) {
        LOG("[rtl] right-to-left text off (NSMBU_RTL=0)");
        return;
    }
    std::string why;
    auto font = game_font::pack_font(host_path, &why);
    if (!font) {
        LOG("[rtl] the language pack's message font can't be read (%s): right-to-left text off", why.c_str());
        return;
    }
    size_t arabic = 0, hebrew = 0;
    for (uint32_t c : *font) {
        if (c >= 0x0621 && c <= 0x064A) arabic++;
        if (c >= 0x05D0 && c <= 0x05EA) hebrew++;
    }
    const bool force = env && !strcmp(env, "1");
    const bool on = force || arabic >= 20 || hebrew >= 20;
    if (!on) return;
    g_font = font;
    size_t forms = 0, lig = 0, marks = 0;
    for (uint32_t c : *font) {
        if (c >= 0xFE80 && c <= 0xFEF4) forms++;
        if (c >= 0xFEF5 && c <= 0xFEFC) lig++;
        if (c >= 0x064B && c <= 0x0652) marks++;
    }
    LOG("[rtl] right-to-left text on%s: the message font has %zu Arabic and %zu Hebrew letters, %zu Arabic letter forms, "
        "%zu lam-alef ligatures, %zu harakat (missing ones are not drawn)",
        force ? " (NSMBU_RTL=1)" : "", arabic, hebrew, forms, lig, marks);
    g_on.store(true, std::memory_order_release);
}

}

using namespace rtl_text;

extern "C" void hook_02871328(Cpu* c) {
    if (!g_on.load(std::memory_order_relaxed)) return f_02871328_orig(c);
    const uint32_t reader = c->r[3];
    uint32_t pos = ld32(reader);
    Print* p = top();
    if (p && pos >= p->text && pos < p->text + 2 * p->n) {
        size_t i = (pos - p->text) / 2;
        while (i < p->n && p->plan.skip[i]) i++;
        if (i < p->n) {
            st32(reader, p->text + 2 * (uint32_t)(i + 1));
            c->r[3] = p->plan.code[i];
            return;
        }
        pos = p->text + 2 * p->n;
    }
    for (int guard = 0; guard < 64; guard++) {
        bool skip;
        const uint32_t code = shape_loose(pos, &skip);
        pos += 2;
        if (skip) continue;
        st32(reader, pos);
        c->r[3] = code;
        return;
    }
    st32(reader, pos);
    c->r[3] = ld16(pos - 2);
}

extern "C" void hook_028709E0(Cpu* c) {
    if (!g_on.load(std::memory_order_acquire)) return f_028709E0_orig(c);
    const uint32_t writer = c->r[3], text = c->r[4];
    const int32_t len = (int32_t)c->r[5];
    if (len <= 0 || len > 0x10000 || !text) return f_028709E0_orig(c);
    bool any = false;
    for (int32_t i = 0; i < len && !any; i++) any = rtl_unit(ld16(text + 2 * i));
    if (!any) return f_028709E0_orig(c);
    std::vector<uint16_t> units(len);
    for (int32_t i = 0; i < len; i++) units[i] = ld16(text + 2 * i);
    auto p = std::make_unique<Print>();
    p->plan = rtl::plan(units.data(), units.size(), has_glyph);
    if (!p->plan.rtl) return f_028709E0_orig(c);
    p->writer = writer;
    p->text = text;
    p->n = (uint32_t)len;
    Print* raw = p.get();
    t_prints.push_back(std::move(p));
    f_028709E0_orig(c);

    finish(*raw);
    if (env::RTL_TRACE() && t_logged < 400) {
        t_logged++;
        std::string s;
        for (int32_t i = 0; i < len && i < 400; i++) {
            char b[8];
            snprintf(b, sizeof b, "%04X ", units[i]);
            s += b;
        }
        LOG("[rtl] print writer %08X len %d flags %X box %.1f limit %g glyphs %zu lines %d: %s", writer, len, ld32(writer + 0x44),
            t_box_width, ldf32(writer + 0x34), raw->recs.size(), raw->line + 1, s.c_str());
    }
    t_prints.pop_back();
}

extern "C" void site_02870A50(Cpu* c) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    last_break(c->r[1]) = 0;
    Print* p = top();
    if (p && !p->frame) p->frame = c->r[1];
}

extern "C" void site_02870CC8(Cpu* c) {
    Print* p = top();
    if (!p || c->r[1] != p->frame) return;
    if (c->r[22] == 0x0A) p->line++;
    const uint32_t after = ld32(c->r[1] + 0x24);
    p->tag = after > p->text ? (int32_t)((after - p->text) / 2) - 1 : -1;
}

extern "C" void site_02870CCC(Cpu* c) {
    Print* p = top();
    if (p && c->r[1] == p->frame) p->tag = -1;
}

extern "C" void site_02870FC8(Cpu* c) {
    if (!g_on.load(std::memory_order_relaxed) || !c->r[24]) return;
    uint32_t& at = last_break(c->r[1]);
    if (breaks_here(c->r[22])) {
        at = c->r[25];
        return;
    }
    c->r[25] = at > c->r[26] ? at : c->r[26];
}

extern "C" void site_02870028(Cpu* c) {
    if (g_on.load(std::memory_order_relaxed)) last_break(c->r[1]) = 0;
}

extern "C" void site_028704CC(Cpu* c) {
    if (!g_on.load(std::memory_order_relaxed) || !c->r[22]) return;
    uint32_t& at = last_break(c->r[1]);
    if (breaks_here(c->r[20])) at = c->r[24];
    else c->r[24] = at;
}

extern "C" void hook_0286E8B4(Cpu* c) {
    Print* p = g_on.load(std::memory_order_relaxed) ? top() : nullptr;
    if (!p || c->r[3] != p->writer) return f_0286E8B4_orig(c);
    const uint32_t w = c->r[3];
    const float pen = (float)ldf32(w + 0x14);
    const uint32_t list = ld32(w + 0x2C);
    const uint32_t count = list ? ld16(list + 4) : 0;
    f_0286E8B4_orig(c);
    Rec r;
    r.pen = pen;
    r.advance = (float)ldf32(w + 0x14) - pen;
    r.quad = list && ld16(list + 4) == count + 1 ? list + 0xA4 + count * 0x30 + 8 : 0;
    r.line = p->line;
    if (p->tag >= 0) r.index = p->tag;
    else if (p->frame) {
        const uint32_t pos = ld32(p->frame + 0xD4);
        r.index = pos > p->text ? (int32_t)((pos - p->text) / 2) - 1 : -1;
    } else r.index = -1;

    while (r.index > 0 && (uint32_t)r.index < p->n && p->plan.skip[r.index]) r.index--;
    p->recs.push_back(r);
}

extern "C" void hook_028785C8(Cpu* c) {
    if (!g_on.load(std::memory_order_relaxed)) return f_028785C8_orig(c);
    const float saved = t_box_width;
    t_box_width = (float)ldf32(c->r[3] + 0x3C);
    f_028785C8_orig(c);
    t_box_width = saved;
}
