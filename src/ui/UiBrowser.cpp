// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Browser panel of the Qt-free UI: loops, instruments and effects in one searchable list, with drag and drop onto
// the arrangement (an idea taken from Ableton Live's browser, kept next to the Logic-style inspector).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../dsp/Effects.h"
#include "Ui.h"

namespace ui {

namespace {
constexpr Rgba kPanel = rgba(32, 35, 42), kPanel2 = rgba(41, 45, 54), kPanel3 = rgba(52, 57, 68), kLine = rgba(14, 15, 18);
constexpr Rgba kText = rgba(226, 230, 238), kDim = rgba(137, 146, 163), kAccent = rgba(38, 222, 196), kAmber = rgba(255, 176, 46);
constexpr int kRowH = 22;
const char* const kSectionNames[3] = {"Loops", "Instruments", "Effects"};
const Rgba kKindColors[3] = {rgba(70, 130, 240), rgba(95, 190, 100), rgba(235, 150, 60)};

std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return s; }
}  // namespace

std::vector<NativeUi::BrowserItem> NativeUi::browserItems() {
    if (!m_loopsLoaded) reloadLoops();
    if (m_effectNames.empty()) for (const std::string& id : dsp::insertEffectIds()) { auto fx = dsp::createEffect(id); m_effectNames.emplace_back(id, fx ? fx->displayName() : id); }
    std::vector<BrowserItem> all;
    for (const LoopEntry& l : m_loops) all.push_back({0, "", l.name, l.bpm > 0 ? std::to_string(int(l.bpm)) + " BPM" : "", l.path, l.bpm});
    all.push_back({1, "synth", "Synth", "built in", "", 0});
    all.push_back({1, "drumkit", "Drum Kit", "built in", "", 0});
    const auto plugins = m_host.plugins ? m_host.plugins() : std::vector<PluginEntry>{};
    for (const PluginEntry& p : plugins) if (p.instrument) all.push_back({1, p.id, p.name, p.vendor, "", 0});
    for (const auto& e : m_effectNames) all.push_back({2, e.first, e.second, "built in", "", 0});
    for (const PluginEntry& p : plugins) if (!p.instrument) all.push_back({2, p.id, p.name, p.vendor, "", 0});
    const std::string q = lower(m_search);
    if (q.empty()) return all;
    std::vector<BrowserItem> out;
    for (const BrowserItem& it : all) if (lower(it.name).find(q) != std::string::npos || lower(it.sub).find(q) != std::string::npos) out.push_back(it);
    return out;
}

void NativeUi::drawBrowser(Canvas& c) {
    if (!m_browser) return;
    const int top = kControlH + kToolbarH;
    const Rect r{m_winW - kBrowserW, top, kBrowserW, m_h - top - kStatusH};
    c.fillRect(r, kPanel);
    c.vline(r.x, r.y, r.bottom() - 1, kLine);
    c.text(r.x + 14, r.y + 10, "Browser", kDim, Font::Bold11);
    // search field
    const Rect sf{r.x + 12, r.y + 30, r.w - 24, 24};
    c.fillRoundRect(sf, 12, rgba(20, 22, 27));
    c.strokeRoundRect(sf, 12, m_searchFocus ? kAccent : rgba(70, 76, 90));
    c.textIn(sf.inset(12, 0), m_search.empty() && !m_searchFocus ? "Search" : m_search + (m_searchFocus ? "|" : ""), m_search.empty() && !m_searchFocus ? kDim : kText, Font::Regular11);
    addHit(sf, Hit::BrowserSearch);

    const auto items = browserItems();
    const Rect list{r.x, r.y + 62, r.w, r.h - 62};
    c.pushClip(list);
    int y = list.y - int(m_browserScroll);
    m_browserRows.assign(items.size(), Rect{});
    int lastKind = -1;
    for (int i = 0; i < int(items.size()); ++i) {
        const BrowserItem& it = items[size_t(i)];
        if (it.kind != lastKind) {
            lastKind = it.kind;
            int count = 0;
            for (const BrowserItem& o : items) count += o.kind == it.kind;
            const Rect hdr{r.x, y, r.w, kRowH + 2};
            c.fillRect(hdr, rgba(36, 39, 47));
            c.text(r.x + 14, y + 5, std::string(m_sectionClosed[it.kind] ? "> " : "v ") + kSectionNames[it.kind] + "  (" + std::to_string(count) + ")", kText, Font::Bold11);
            addHit(hdr.intersect(list), Hit::BrowserSection, it.kind);
            y += kRowH + 2;
        }
        if (m_sectionClosed[it.kind]) continue;
        const Rect row{r.x + 6, y, r.w - 12, kRowH - 1};
        if (row.bottom() >= list.y && row.y <= list.bottom()) {
            const bool sel = i == m_browserSel, hover = row.contains(m_hoverX, m_hoverY);
            if (sel || hover) c.fillRoundRect(row, 5, sel ? rgba(52, 110, 150) : kPanel2);
            c.fillCircle(row.x + 10, row.y + row.h / 2.0, 4, kKindColors[it.kind]);
            const int subW = it.sub.empty() ? 0 : std::min(80, c.textWidth(it.sub) + 8);
            c.textIn({row.x + 22, row.y, row.w - 28 - subW, row.h}, it.name, kText, Font::Regular11);
            if (subW) c.textIn({row.right() - subW - 4, row.y, subW, row.h}, it.sub, it.kind == 0 ? kAmber : kDim, Font::Regular11, Align::Right);
            addHit(row.intersect(list), Hit::BrowserItem, i);
            m_browserRows[size_t(i)] = row.intersect(list);
        }
        y += kRowH;
    }
    m_browserContentH = y + int(m_browserScroll) - list.y;
    m_browserViewH = list.h;
    if (items.empty()) c.textIn({list.x, list.y + 20, list.w, 20}, "Nothing matches.", kDim, Font::Regular11, Align::Center);
    c.popClip();
    // drag ghost
    if (m_payActive) {
        const int w = std::min(220, c.textWidth(m_payName, Font::Bold11) + 24);
        const Rect g{m_hoverX + 12, m_hoverY + 8, w, 22};
        c.fillRoundRect(g, 11, withAlpha(kKindColors[std::max(0, m_payKind)], 230));
        c.textIn(g.inset(10, 0), m_payName, rgba(14, 16, 20), Font::Bold11);
    }
}

void NativeUi::dropPayload(int x, int y) {
    const HitRect* target = nullptr;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
        if (it->r.contains(x, y) && (it->kind == Hit::Lane || it->kind == Hit::Region || it->kind == Hit::TrackHeader || it->kind == Hit::MixerStrip)) { target = &*it; break; }
    const Rect area = arrangeArea();
    const bool overArrange = area.contains(x, y);
    const int track = target ? target->a : -1;
    const bool trackOk = track >= 0 && track < int(m_tracks.size());
    const double beat = x >= kHeaderW && overArrange ? snapBeat(xToBeat(x)) : m_s.positionBeats();
    if (m_payKind == 0) {   // a loop
        if (!overArrange && !trackOk) return;
        const int dest = trackOk && m_tracks[size_t(track)].type == core::TrackType::Audio ? track : -1;
        m_s.importAudio(m_payPath, dest, beat);
        message(msg("Added %1", m_payName));
    } else if (m_payKind == 1) {   // an instrument
        if (trackOk && m_tracks[size_t(track)].instrument) { m_s.setInstrument(track, m_payId); m_selTrack = track; message(msg("Instrument: %1", m_payName)); }
        else if (overArrange && !trackOk) { const int nt = m_s.addTrack(core::TrackType::Instrument, m_payName); m_s.setInstrument(nt, m_payId); m_selTrack = nt; message(msg("New track with %1", m_payName)); }
        else message("Drop an instrument on a Software Instrument track, or on an empty part of the arrangement.");
    } else if (m_payKind == 2) {   // an effect
        if (trackOk) { m_s.addInsert(track, m_payId); m_selTrack = track; message(msg("%1 added as an insert.", m_payName)); }
        else message("Drop an effect on a track.");
    }
    m_dirty = true;
}

}  // namespace ui
