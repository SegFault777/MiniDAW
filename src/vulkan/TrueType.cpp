// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrueType.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vkui {

namespace {

struct Rd {
    const std::uint8_t* d; std::size_t n;
    bool ok(std::size_t o, std::size_t len) const { return o <= n && len <= n - o; }
    std::uint32_t u8(std::size_t o) const { return ok(o, 1) ? d[o] : 0; }
    std::uint32_t u16(std::size_t o) const { return ok(o, 2) ? std::uint32_t(d[o]) << 8 | d[o + 1] : 0; }
    std::int32_t s16(std::size_t o) const { return std::int16_t(u16(o)); }
    std::uint32_t u32(std::size_t o) const { return ok(o, 4) ? std::uint32_t(d[o]) << 24 | std::uint32_t(d[o + 1]) << 16 | std::uint32_t(d[o + 2]) << 8 | d[o + 3] : 0; }
};

struct P2 { float x, y; };

// Exact-area accumulation rasteriser (the "signed area" method): lines add their coverage into 'acc', a running sum per row gives
// the coverage of every pixel, |sum| clamped to 1 (non-zero winding for outlines that overlap).
struct Canvas {
    int w, h;                 // bitmap size
    std::vector<float> acc;   // (w + 2) * h
    Canvas(int W, int H) : w(W), h(H), acc(std::size_t(W + 2) * std::size_t(H), 0.f) {}
    void line(P2 p0, P2 p1) {
        if (p0.y == p1.y) return;
        float dir = 1.f;
        if (p0.y > p1.y) { std::swap(p0, p1); dir = -1.f; }
        const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
        float x = p0.x;
        int y0 = 0;
        if (p0.y > 0) y0 = int(p0.y); else x -= p0.y * dxdy;
        const int y1 = std::min(h, int(std::ceil(p1.y)));
        for (int y = y0; y < y1; ++y) {
            float* row = &acc[std::size_t(y) * std::size_t(w + 2)];
            const float dy = std::min(float(y + 1), p1.y) - std::max(float(y), p0.y);
            const float xnext = x + dxdy * dy;
            const float d = dy * dir;
            float xa = x, xb = xnext;
            if (xa > xb) std::swap(xa, xb);
            xa = std::clamp(xa, 0.f, float(w)); xb = std::clamp(xb, 0.f, float(w));
            const float x0floor = std::floor(xa);
            const int x0i = int(x0floor);
            const float x1ceil = std::ceil(xb);
            const int x1i = int(x1ceil);
            if (x1i <= x0i + 1) {
                const float xmf = 0.5f * (xa + xb) - x0floor;
                row[x0i] += d - d * xmf;
                row[x0i + 1] += d * xmf;
            } else {
                const float s = 1.f / (xb - xa);
                const float x0f = xa - x0floor;
                const float a0 = 0.5f * s * (1.f - x0f) * (1.f - x0f);
                const float x1f = xb - x1ceil + 1.f;
                const float am = 0.5f * s * x1f * x1f;
                row[x0i] += d * a0;
                if (x1i == x0i + 2) {
                    row[x0i + 1] += d * (1.f - a0 - am);
                } else {
                    const float a1 = s * (1.5f - x0f);
                    row[x0i + 1] += d * (a1 - a0);
                    for (int xi = x0i + 2; xi < x1i - 1; ++xi) row[xi] += d * s;
                    const float a2 = a1 + float(x1i - x0i - 3) * s;
                    row[x1i - 1] += d * (1.f - a2 - am);
                }
                row[x1i] += d * am;
            }
            x = xnext;
        }
    }
};

void quad(Canvas& cv, P2 a, P2 c, P2 b) {   // quadratic Bezier a -> b with control point c, flattened
    const float dx = a.x - 2 * c.x + b.x, dy = a.y - 2 * c.y + b.y;
    const float dd = std::sqrt(dx * dx + dy * dy);
    const int n = std::clamp(int(std::ceil(std::sqrt(dd * 0.75f / 0.1f))), 1, 64);
    P2 prev = a;
    for (int i = 1; i <= n; ++i) {
        const float t = float(i) / float(n), u = 1 - t;
        const P2 p{u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y};
        cv.line(prev, p);
        prev = p;
    }
}

}  // namespace

bool TrueTypeFont::load(std::vector<std::uint8_t> data, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; m_upem = 0; return false; };
    *this = TrueTypeFont();
    m_data = std::move(data);
    const Rd r{m_data.data(), m_data.size()};
    if (m_data.size() < 12) return fail("The font file is too short.");
    const std::uint32_t tag = r.u32(0);
    if (tag != 0x00010000 && tag != 0x74727565) return fail("Not a TrueType font (OpenType/CFF fonts are not supported).");
    const int numTables = int(r.u16(4));
    std::size_t head = 0, maxp = 0;
    for (int i = 0; i < numTables; ++i) {
        const std::size_t e = 12 + std::size_t(i) * 16;
        if (!r.ok(e, 16)) return fail("Damaged table directory.");
        const std::uint32_t t = r.u32(e), off = r.u32(e + 8), len = r.u32(e + 12);
        if (!r.ok(off, len)) continue;
        if (t == 0x68656164) head = off;              // head
        else if (t == 0x6D617870) maxp = off;         // maxp
        else if (t == 0x6C6F6361) m_loca = off;       // loca
        else if (t == 0x676C7966) { m_glyf = off; m_glyfLen = len; }   // glyf
        else if (t == 0x636D6170) m_cmap = off;       // cmap
    }
    if (!head || !maxp || !m_loca || !m_glyf || !m_cmap) return fail("The font has no TrueType outlines (glyf / loca / cmap missing).");
    m_upem = int(r.u16(head + 18));
    m_locaLong = int(r.s16(head + 50));
    m_numGlyphs = int(r.u16(maxp + 4));
    if (m_upem < 16 || m_upem > 16384 || m_numGlyphs <= 0) return fail("Unusable font header.");
    // choose a cmap subtable: format 12 (full Unicode) first, then format 4
    const int nSub = int(r.u16(m_cmap + 2));
    std::size_t best4 = 0, best12 = 0;
    for (int i = 0; i < nSub; ++i) {
        const std::size_t e = m_cmap + 4 + std::size_t(i) * 8;
        const std::uint32_t pid = r.u16(e), eid = r.u16(e + 2), off = r.u32(e + 4);
        const std::size_t sub = m_cmap + off;
        if (!r.ok(sub, 4)) continue;
        const std::uint32_t fmt = r.u16(sub);
        if (fmt == 12 && ((pid == 3 && eid == 10) || pid == 0)) best12 = sub;
        else if (fmt == 4 && ((pid == 3 && (eid == 1 || eid == 0)) || pid == 0)) best4 = sub;
    }
    // glyph names (post format 2): glyphNameIndex[numGlyphs] u16, then Pascal strings for the indices from 258 on
    for (int i = 0; i < numTables; ++i) {
        const std::size_t e = 12 + std::size_t(i) * 16;
        if (r.u32(e) != 0x706F7374) continue;   // post
        const std::uint32_t off = r.u32(e + 8), len = r.u32(e + 12);
        if (!r.ok(off, len) || len < 34 || r.u32(off) != 0x00020000) break;
        const int n = int(r.u16(off + 32));
        std::vector<std::string> strs;
        std::size_t p = off + 34 + std::size_t(n) * 2;
        while (p < std::size_t(off) + len) { const std::size_t l = r.u8(p); if (p + 1 + l > std::size_t(off) + len) break; strs.emplace_back(reinterpret_cast<const char*>(m_data.data() + p + 1), l); p += 1 + l; }
        for (int g = 0; g < n && g < m_numGlyphs; ++g) {
            const std::size_t idx = r.u16(off + 34 + std::size_t(g) * 2);
            if (idx >= 258 && idx - 258 < strs.size()) m_names.emplace_back(strs[idx - 258], g);
        }
        std::sort(m_names.begin(), m_names.end());
        break;
    }
    m_sub = best12 ? best12 : best4;
    m_subFormat = best12 ? 12 : best4 ? 4 : 0;
    if (!m_sub) return fail("The font has no usable character map.");
    return true;
}

int TrueTypeFont::glyphByName(const std::string& name) const {
    const auto it = std::lower_bound(m_names.begin(), m_names.end(), std::make_pair(name, -1));
    return it != m_names.end() && it->first == name ? it->second : 0;
}

int TrueTypeFont::glyphIndex(std::uint32_t cp) const {
    const Rd r{m_data.data(), m_data.size()};
    if (m_subFormat == 12) {
        const std::uint32_t groups = std::min<std::uint32_t>(r.u32(m_sub + 12), std::uint32_t((m_data.size() - std::min(m_data.size(), m_sub + 16)) / 12));
        std::uint32_t lo = 0, hi = groups;
        while (lo < hi) {   // binary search over the sorted groups
            const std::uint32_t mid = lo + (hi - lo) / 2;
            const std::size_t g = m_sub + 16 + std::size_t(mid) * 12;
            const std::uint32_t s = r.u32(g), e = r.u32(g + 4);
            if (cp < s) hi = mid; else if (cp > e) lo = mid + 1; else return int(r.u32(g + 8) + (cp - s));
        }
        return 0;
    }
    if (m_subFormat == 4 && cp <= 0xFFFF) {
        const int segX2 = int(r.u16(m_sub + 6));
        const std::size_t endC = m_sub + 14, startC = endC + std::size_t(segX2) + 2, delta = startC + std::size_t(segX2), range = delta + std::size_t(segX2);
        for (int i = 0; i < segX2 / 2; ++i) {
            const std::uint32_t e = r.u16(endC + std::size_t(i) * 2);
            if (cp > e) continue;
            const std::uint32_t s = r.u16(startC + std::size_t(i) * 2);
            if (cp < s) return 0;
            const std::uint32_t ro = r.u16(range + std::size_t(i) * 2), dl = r.u16(delta + std::size_t(i) * 2);
            if (ro == 0) return int((cp + dl) & 0xFFFF);
            const std::size_t gp = range + std::size_t(i) * 2 + ro + std::size_t(cp - s) * 2;
            const std::uint32_t g = r.u16(gp);
            return g == 0 ? 0 : int((g + dl) & 0xFFFF);
        }
    }
    return 0;
}

bool TrueTypeFont::glyphRange(int g, std::size_t& begin, std::size_t& end) const {
    if (g < 0 || g >= m_numGlyphs) return false;
    const Rd r{m_data.data(), m_data.size()};
    std::uint32_t a, b;
    if (m_locaLong) { a = r.u32(m_loca + std::size_t(g) * 4); b = r.u32(m_loca + std::size_t(g + 1) * 4); }
    else { a = r.u16(m_loca + std::size_t(g) * 2) * 2; b = r.u16(m_loca + std::size_t(g + 1) * 2) * 2; }
    if (b < a || b > m_glyfLen) return false;
    begin = m_glyf + a; end = m_glyf + b;
    return true;
}

// Collects the contours of a glyph (transformed by [a b; c d] + (dx, dy), font units, y up).
bool TrueTypeFont::glyphPoints(int g, std::vector<std::vector<Pt>>& out, int depth, float ma, float mb, float mc, float md, float dx, float dy) const {
    if (depth > 6 || --m_budget < 0) return false;
    std::size_t b, e;
    if (!glyphRange(g, b, e)) return false;
    if (b == e) return true;   // empty glyph
    const Rd r{m_data.data(), m_data.size()};
    if (!r.ok(b, 10)) return false;
    const int nContours = int(r.s16(b));
    std::size_t p = b + 10;
    if (nContours >= 0) {
        if (!r.ok(p, std::size_t(nContours) * 2 + 2)) return false;
        std::vector<int> ends(static_cast<std::size_t>(nContours));
        for (int i = 0; i < nContours; ++i) ends[std::size_t(i)] = int(r.u16(p + std::size_t(i) * 2));
        p += std::size_t(nContours) * 2;
        for (int i = 1; i < nContours; ++i) if (ends[std::size_t(i)] < ends[std::size_t(i) - 1]) return false;   // end points must not go back
        const int nPts = nContours ? ends.back() + 1 : 0;
        const std::size_t insLen = r.u16(p);
        p += 2 + insLen;
        if (nPts <= 0 || nPts > 20000) return nPts == 0;
        std::vector<std::uint8_t> flags(static_cast<std::size_t>(nPts));
        for (int i = 0; i < nPts;) {
            if (!r.ok(p, 1)) return false;
            const std::uint8_t f = std::uint8_t(r.u8(p)); ++p;
            flags[std::size_t(i++)] = f;
            if (f & 8) { if (!r.ok(p, 1)) return false; int rep = int(r.u8(p)); ++p; while (rep-- > 0 && i < nPts) flags[std::size_t(i++)] = f; }
        }
        std::vector<float> xs(static_cast<std::size_t>(nPts)), ys(static_cast<std::size_t>(nPts));
        int v = 0;
        for (int i = 0; i < nPts; ++i) {
            const std::uint8_t f = flags[std::size_t(i)];
            if (f & 2) { if (!r.ok(p, 1)) return false; const int dv = int(r.u8(p)); ++p; v += (f & 16) ? dv : -dv; }
            else if (!(f & 16)) { if (!r.ok(p, 2)) return false; v += r.s16(p); p += 2; }
            xs[std::size_t(i)] = float(v);
        }
        v = 0;
        for (int i = 0; i < nPts; ++i) {
            const std::uint8_t f = flags[std::size_t(i)];
            if (f & 4) { if (!r.ok(p, 1)) return false; const int dv = int(r.u8(p)); ++p; v += (f & 32) ? dv : -dv; }
            else if (!(f & 32)) { if (!r.ok(p, 2)) return false; v += r.s16(p); p += 2; }
            ys[std::size_t(i)] = float(v);
        }
        int start = 0;
        for (int c = 0; c < nContours; ++c) {
            std::vector<Pt> pts;
            for (int i = start; i <= ends[std::size_t(c)]; ++i) {
                const float x = xs[std::size_t(i)], y = ys[std::size_t(i)];
                pts.push_back({ma * x + mc * y + dx, mb * x + md * y + dy, (flags[std::size_t(i)] & 1) != 0});
            }
            start = ends[std::size_t(c)] + 1;
            if (pts.size() >= 2) out.push_back(std::move(pts));
        }
        return true;
    }
    // composite glyph
    for (int guard = 0; guard < 64; ++guard) {
        if (!r.ok(p, 4)) return false;
        const std::uint32_t flags = r.u16(p), gi = r.u16(p + 2);
        p += 4;
        float ox, oy;
        if (flags & 1) { if (!r.ok(p, 4)) return false; ox = float(r.s16(p)); oy = float(r.s16(p + 2)); p += 4; }
        else { if (!r.ok(p, 2)) return false; ox = float(std::int8_t(r.u8(p))); oy = float(std::int8_t(r.u8(p + 1))); p += 2; }
        if (!(flags & 2)) { ox = oy = 0; }   // point matching is not supported: place at the origin
        float a = 1, bb = 0, c = 0, d = 1;
        auto f2 = [&](std::size_t o) { return float(r.s16(o)) / 16384.f; };
        if (flags & 8) { if (!r.ok(p, 2)) return false; a = d = f2(p); p += 2; }
        else if (flags & 0x40) { if (!r.ok(p, 4)) return false; a = f2(p); d = f2(p + 2); p += 4; }
        else if (flags & 0x80) { if (!r.ok(p, 8)) return false; a = f2(p); bb = f2(p + 2); c = f2(p + 4); d = f2(p + 6); p += 8; }
        // child transform, then the parent's
        const float na = ma * a + mc * bb, nb = mb * a + md * bb, nc = ma * c + mc * d, nd = mb * c + md * d;
        const float ndx = ma * ox + mc * oy + dx, ndy = mb * ox + md * oy + dy;
        if (!glyphPoints(int(gi), out, depth + 1, na, nb, nc, nd, ndx, ndy)) return false;
        if (!(flags & 0x20)) break;   // no MORE_COMPONENTS
    }
    return true;
}

bool TrueTypeFont::rasterize(int glyph, int px, GlyphBitmap& out) const {
    out = GlyphBitmap();
    out.advance = float(px);
    if (!loaded() || glyph <= 0 || px < 1) return false;
    std::vector<std::vector<Pt>> contours;
    m_budget = 64;
    if (!glyphPoints(glyph, contours, 0, 1, 0, 0, 1, 0, 0)) return false;
    const float s = float(px) / float(m_upem);
    // to pixel space: x right, y down, origin at the top left corner of the em square
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (auto& c : contours) for (Pt& q : c) {
        q.x = q.x * s;
        q.y = (float(m_upem) - q.y) * s;
        minx = std::min(minx, q.x); maxx = std::max(maxx, q.x); miny = std::min(miny, q.y); maxy = std::max(maxy, q.y);
    }
    if (contours.empty() || minx > maxx) return true;   // empty glyph
    const int ix0 = int(std::floor(minx)), iy0 = int(std::floor(miny));
    const int w = int(std::ceil(maxx)) - ix0, h = int(std::ceil(maxy)) - iy0;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return true;
    Canvas cv(w, h);
    auto at = [&](const Pt& q) { return P2{q.x - float(ix0), q.y - float(iy0)}; };
    for (const auto& c : contours) {
        const std::size_t n = c.size();
        // start on an on-curve point (or the midpoint of the first two off-curve points)
        std::size_t first = 0;
        bool found = false;
        for (std::size_t i = 0; i < n; ++i) if (c[i].on) { first = i; found = true; break; }
        P2 start;
        if (found) start = at(c[first]);
        else { const P2 a = at(c[0]), b = at(c[1 % n]); start = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; first = 0; }
        P2 cur = start;
        const std::size_t begin = found ? first + 1 : 1;
        bool haveCtrl = false;
        P2 ctrl{0, 0};
        const std::size_t count = n;   // every point once; with no on-curve point the last one visited is the first (start = midpoint of the first two)
        for (std::size_t k = 0; k < count; ++k) {
            const std::size_t idx = (begin + k) % n;
            const Pt& q = c[idx];
            const P2 pq = at(q);
            if (q.on) {
                if (haveCtrl) { quad(cv, cur, ctrl, pq); haveCtrl = false; } else cv.line(cur, pq);
                cur = pq;
            } else {
                if (haveCtrl) { const P2 mid{(ctrl.x + pq.x) * 0.5f, (ctrl.y + pq.y) * 0.5f}; quad(cv, cur, ctrl, mid); cur = mid; }
                ctrl = pq; haveCtrl = true;
            }
        }
        if (haveCtrl) quad(cv, cur, ctrl, start); else cv.line(cur, start);
    }
    out.w = w; out.h = h;
    out.bearingX = ix0; out.bearingY = iy0;
    out.a8.assign(std::size_t(w) * std::size_t(h), 0);
    for (int y = 0; y < h; ++y) {
        float sum = 0;
        const float* row = &cv.acc[std::size_t(y) * std::size_t(w + 2)];
        for (int x = 0; x < w; ++x) {
            sum += row[x];
            out.a8[std::size_t(y) * std::size_t(w) + std::size_t(x)] = std::uint8_t(std::lround(std::min(1.f, std::fabs(sum)) * 255.f));
        }
    }
    return true;
}

}  // namespace vkui
