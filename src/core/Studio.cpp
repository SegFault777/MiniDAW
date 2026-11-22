// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Session: v0.12 features borrowed from other DAWs - Logic Pro (adapt tempo, groove template), Ableton Live (Capture MIDI),
// FL Studio (Riff Machine), Cubase (Logical Editor, MixConsole snapshots) and Pro Tools (Tab to Transient, Beat Detective).
#include <algorithm>
#include <chrono>
#include <cmath>
#include "../dsp/AudioAnalysis.h"
#include "../dsp/TempoDetect.h"
#include "Internal.h"
#include "Session.h"

namespace core {

namespace {
double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}  // namespace

// ------------------------------------------------------------------------------------------------ multi-region moves

std::vector<std::pair<int, int>> Session::moveRegions(const std::vector<std::pair<int, int>>& selIn, double beatDelta, int trackDelta) {
    std::vector<std::pair<int, int>> out;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        const int nt = int(m_tracks.size());
        std::vector<std::pair<int, int>> sel;   // valid, unique
        for (const auto& s : selIn) {
            if (s.first < 0 || s.first >= nt || s.second < 0 || s.second >= int(m_tracks[size_t(s.first)].regions.size())) continue;
            if (std::find(sel.begin(), sel.end(), s) == sel.end()) sel.push_back(s);
        }
        if (sel.empty()) return {};
        double minStart = 1e300;
        for (const auto& s : sel) minStart = std::min(minStart, m_tracks[size_t(s.first)].regions[size_t(s.second)].start);
        const double delta = std::max(beatDelta, -minStart);
        struct Item { Region r; int target; };
        std::vector<Item> items;
        for (const auto& s : sel) {
            Region r = m_tracks[size_t(s.first)].regions[size_t(s.second)];
            int target = s.first;
            const int want = s.first + trackDelta;
            if (want >= 0 && want < nt && want != s.first) {
                const TrackType ty = m_tracks[size_t(want)].type;
                const bool fits = r.midi ? (r.drummer ? ty == TrackType::Drummer : (ty == TrackType::Instrument || ty == TrackType::Drummer))
                                         : ty == TrackType::Audio;
                if (fits) target = want;
            }
            r.start = std::max(0.0, r.start + delta);
            items.push_back({std::move(r), target});
        }
        // erase the originals (highest index first so the others keep their place), then append the moved copies
        auto order = sel;
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; });
        for (const auto& s : order) { auto& regs = m_tracks[size_t(s.first)].regions; regs.erase(regs.begin() + s.second); }
        for (auto& it : items) {
            auto& regs = m_tracks[size_t(it.target)].regions;
            regs.push_back(std::move(it.r));
            out.emplace_back(it.target, int(regs.size()) - 1);
        }
    }
    changed(false);
    return out;
}

namespace {
bool regionFits(const Region& r, TrackType ty) {
    return r.midi ? (r.drummer ? ty == TrackType::Drummer : (ty == TrackType::Instrument || ty == TrackType::Drummer)) : ty == TrackType::Audio;
}
}  // namespace

bool Session::copyRegions(const std::vector<std::pair<int, int>>& sel) {
    bool loading = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        std::vector<Region> regs;
        std::vector<int> tracks;
        double minStart = 1e300;
        int firstTrack = -1;
        for (const auto& s : sel) {
            if (s.first < 0 || s.first >= int(m_tracks.size()) || s.second < 0 || s.second >= int(m_tracks[size_t(s.first)].regions.size())) continue;
            const Region& rg = m_tracks[size_t(s.first)].regions[size_t(s.second)];
            if (!rg.midi && !rg.audio) { loading = true; continue; }
            if (firstTrack < 0) firstTrack = s.first;
            minStart = std::min(minStart, rg.start);
            regs.push_back(rg);
            tracks.push_back(s.first - firstTrack);
        }
        if (!regs.empty()) {
            for (auto& r : regs) r.start -= minStart;
            m_clip = regs.front();
            m_clip.start = minStart;   // the single-region clipboard keeps its place like copyRegion does
            m_clipGroup = std::move(regs);
            m_clipGroupTrack = std::move(tracks);
            m_hasClip = true;
        }
    }
    if (loading) message(tr("This audio is still loading."));
    return m_hasClip && !loading;
}

std::vector<std::pair<int, int>> Session::pasteRegions(int baseTrack, double beat) {
    std::vector<std::pair<int, int>> out;
    std::vector<Region> group;
    std::vector<int> offs;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (m_clipGroup.empty() || baseTrack < 0 || baseTrack >= int(m_tracks.size())) return out;
        group = m_clipGroup;
        offs = m_clipGroupTrack;
    }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (size_t i = 0; i < group.size(); ++i) {
            const int t = baseTrack + offs[i];
            if (t < 0 || t >= int(m_tracks.size()) || !regionFits(group[i], m_tracks[size_t(t)].type)) continue;
            Region c = group[i];
            c.start = std::max(0.0, beat + c.start);
            auto& regs = m_tracks[size_t(t)].regions;
            regs.push_back(std::move(c));
            out.emplace_back(t, int(regs.size()) - 1);
        }
    }
    if (out.empty()) message(tr("Nothing fits the tracks at this place."));
    changed(false);
    return out;
}

int Session::muteRegions(const std::vector<std::pair<int, int>>& sel, bool mute) {
    int n = 0;
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const auto& s : sel) {
            if (s.first < 0 || s.first >= int(m_tracks.size()) || s.second < 0 || s.second >= int(m_tracks[size_t(s.first)].regions.size())) continue;
            m_tracks[size_t(s.first)].regions[size_t(s.second)].mute = mute;
            ++n;
        }
    }
    changed(false);
    return n;
}

double Session::rippleDeleteRegions(const std::vector<std::pair<int, int>>& selIn) {
    double total = 0;
    std::vector<std::pair<int, int>> sel;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const auto& s : selIn)
            if (s.first >= 0 && s.first < int(m_tracks.size()) && s.second >= 0 && s.second < int(m_tracks[size_t(s.first)].regions.size()) &&
                std::find(sel.begin(), sel.end(), s) == sel.end()) sel.push_back(s);
    }
    if (sel.empty()) return 0;
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (int t = 0; t < int(m_tracks.size()); ++t) {
            auto& regs = m_tracks[size_t(t)].regions;
            std::vector<std::pair<double, double>> gone;   // [start, end) of the removed regions
            std::vector<int> idx;
            for (const auto& s : sel) if (s.first == t) { idx.push_back(s.second); gone.emplace_back(regs[size_t(s.second)].start, regs[size_t(s.second)].start + regionBeats(regs[size_t(s.second)])); }
            if (idx.empty()) continue;
            for (const auto& g : gone) total += g.second - g.first;
            std::sort(idx.begin(), idx.end(), std::greater<int>());
            for (int i : idx) regs.erase(regs.begin() + i);
            for (auto& rg : regs) {
                double shift = 0;
                for (const auto& g : gone) if (g.second <= rg.start + 1e-6) shift += g.second - g.first;
                if (shift > 0) rg.start = std::max(0.0, rg.start - shift);
            }
        }
    }
    changed(false);
    return total;
}

double Session::regionStartBeat(int t, int r) const {
    std::lock_guard<std::mutex> l(m_mutex);
    if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return -1;
    return m_tracks[size_t(t)].regions[size_t(r)].start;
}

int Session::removeRegions(const std::vector<std::pair<int, int>>& selIn) {
    auto sel = selIn;
    std::sort(sel.begin(), sel.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; });
    sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
    int n = 0;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const auto& s : sel)
            if (s.first >= 0 && s.first < int(m_tracks.size()) && s.second >= 0 && s.second < int(m_tracks[size_t(s.first)].regions.size())) ++n;
    }
    if (n == 0) return 0;
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const auto& s : sel) {
            if (s.first < 0 || s.first >= int(m_tracks.size())) continue;
            auto& regs = m_tracks[size_t(s.first)].regions;
            if (s.second >= 0 && s.second < int(regs.size())) regs.erase(regs.begin() + s.second);
        }
    }
    changed(false);
    return n;
}

std::vector<std::pair<int, int>> Session::duplicateRegions(const std::vector<std::pair<int, int>>& sel) {
    std::vector<std::pair<int, int>> out;
    bool any = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const auto& s : sel)
            if (s.first >= 0 && s.first < int(m_tracks.size()) && s.second >= 0 && s.second < int(m_tracks[size_t(s.first)].regions.size())) any = true;
    }
    if (!any) return out;
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const auto& s : sel) {
            if (s.first < 0 || s.first >= int(m_tracks.size())) continue;
            auto& regs = m_tracks[size_t(s.first)].regions;
            if (s.second < 0 || s.second >= int(regs.size())) continue;
            Region c = regs[size_t(s.second)];
            c.start += regionBeats(c);
            regs.push_back(std::move(c));
            out.emplace_back(s.first, int(regs.size()) - 1);
        }
    }
    changed(false);
    return out;
}

// ---------------------------------------------------------------------------------------------------------- Logic Pro

double Session::adaptTempoToRegion(int t, int r, double beats) {
    double seconds = 0, detected = 0;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return 0;
        const Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (rg.midi) return 0;
        const i64 n = rg.source ? rg.srcFrames : rg.frames;   // the length of the recording itself, not of its stretched copy
        seconds = double(n) / double(kSampleRate);
    }
    if (seconds < 0.2) { message(tr("This audio is still loading.")); return 0; }
    if (beats <= 0) {
        detected = detectRegionTempo(t, r);
        if (detected <= 0) { message(tr("Could not find a tempo in this region.")); return 0; }
        const double raw = seconds * detected / 60.0;
        beats = std::round(raw / 4.0) * 4.0;   // loops and takes are almost always whole bars
        if (beats < 1) beats = std::max(1.0, std::round(raw));
    }
    const double bpm = std::clamp(beats * 60.0 / seconds, 20.0, 400.0);
    setBpm(bpm);
    message(tr("Project tempo set to %1 BPM.").arg(bpm, 0, 'f', 1));
    return bpm;
}

bool Session::makeGrooveTemplate(int t, int r, double grid) {
    std::vector<Note> notes;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return false;
        const Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (!rg.midi) return false;
        notes = rg.notes;
    }
    Groove g = makeGroove(notes, grid);
    if (!g.valid()) { message(tr("This region has no notes to take a groove from.")); return false; }
    m_groove = std::move(g);
    message(tr("Groove template made."));
    return true;
}

bool Session::applyGrooveTemplate(int t, int r, double strength) {
    if (!m_groove.valid()) { message(tr("Make a groove template from a MIDI region first.")); return false; }
    const Groove g = m_groove;
    return editNotes(t, r, [&](const std::vector<Note>& n, double) { return applyGroove(n, g, strength); });
}

// ------------------------------------------------------------------------------------------------------- Ableton Live

void Session::captureEvent(int trackId, int pitch, int velocity, bool on) {
    const double now = nowSeconds();
    std::lock_guard<std::mutex> l(m_capMutex);
    if (on) {
        m_capture.push_back({trackId, pitch, velocity, now, -1.0});
        if (m_capture.size() > 4096) m_capture.erase(m_capture.begin(), m_capture.begin() + 512);
    } else {
        for (auto it = m_capture.rbegin(); it != m_capture.rend(); ++it)
            if (it->trackId == trackId && it->pitch == pitch && it->off < 0) { it->off = now; break; }
    }
}

void Session::injectCapture(int trackId, int pitch, int velocity, double onSeconds, double offSeconds) {
    std::lock_guard<std::mutex> l(m_capMutex);
    m_capture.push_back({trackId, pitch, velocity, onSeconds, offSeconds});
}

int Session::capturedNotes(int track) const {
    int id = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return 0;
        id = m_tracks[size_t(track)].id;
    }
    std::lock_guard<std::mutex> l(const_cast<std::mutex&>(m_capMutex));
    return int(std::count_if(m_capture.begin(), m_capture.end(), [&](const CapNote& c) { return c.trackId == id; }));
}

int Session::captureMidi(int track, double maxSeconds) {
    int id = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return -1;
        const Track& t = m_tracks[size_t(track)];
        if (t.type != TrackType::Instrument) { message(tr("Select an instrument track to capture MIDI.")); return -1; }
        id = t.id;
    }
    std::vector<CapNote> got;
    {
        std::lock_guard<std::mutex> l(m_capMutex);
        for (const CapNote& c : m_capture)
            if (c.trackId == id && true) got.push_back(c);
    }
    // 'maxSeconds' counts back from the newest note, so injected (test) times and real times behave the same
    double newest = 0;
    for (const CapNote& c : got) newest = std::max(newest, c.off >= 0 ? c.off : c.on);
    got.erase(std::remove_if(got.begin(), got.end(), [&](const CapNote& c) { return c.on < newest - maxSeconds; }), got.end());
    if (got.empty()) { message(tr("Nothing to capture. Play some notes on this track first.")); return -1; }
    std::sort(got.begin(), got.end(), [](const CapNote& a, const CapNote& b) { return a.on < b.on; });
    const double t0 = got.front().on, bps = bpm() / 60.0;
    std::vector<Note> notes;
    double total = 0;
    for (const CapNote& c : got) {
        const double end = c.off >= 0 ? c.off : std::max(newest, c.on + 0.1);
        Note n;
        n.pitch = c.pitch;
        n.velocity = c.vel;
        n.start = (c.on - t0) * bps;
        n.length = std::max(0.05, (end - c.on) * bps);
        total = std::max(total, n.start + n.length);
        notes.push_back(n);
    }
    const double length = std::max(1.0, std::ceil(total / 4.0 - 1e-9) * 4.0);   // whole bars, like a Live clip
    const double start = std::floor(positionBeats() * 4.0 + 1e-6) / 4.0;
    checkpoint();
    int idx = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track >= int(m_tracks.size())) return -1;
        Region rg;
        rg.midi = true;
        rg.name = "Captured MIDI";
        rg.start = start;
        rg.lengthBeats = length;
        rg.notes = std::move(notes);
        m_tracks[size_t(track)].regions.push_back(std::move(rg));
        idx = int(m_tracks[size_t(track)].regions.size()) - 1;
    }
    {
        std::lock_guard<std::mutex> l(m_capMutex);
        m_capture.erase(std::remove_if(m_capture.begin(), m_capture.end(), [&](const CapNote& c) { return c.trackId == id; }), m_capture.end());
    }
    changed(false);
    message(tr("Captured %1 notes.").arg(int(got.size())));
    return idx;
}

// -------------------------------------------------------------------------------------------------------- FL Studio

bool Session::riffRegion(int t, int r, const RiffParams& p) {
    return editNotes(t, r, [&](const std::vector<Note>&, double len) { return generateRiff(p, len); });
}

// ---------------------------------------------------------------------------------------------------------- Cubase

int Session::logicalEditRegion(int t, int r, const LogicalParams& p) {
    int hits = 0;
    const bool ok = editNotes(t, r, [&](const std::vector<Note>& n, double) { return logicalEdit(n, p, &hits); });
    return ok ? hits : -1;
}

std::vector<MixSnapshot> Session::mixSnapshots() const { std::lock_guard<std::mutex> l(m_mutex); return m_snapshots; }

int Session::saveMixSnapshot(const std::string& name) {
    MixSnapshot s;
    s.master = m_master;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const Track& t : m_tracks) s.channels.push_back({t.id, t.volume, t.pan, t.sendA, t.sendB, t.mute, t.solo});
        s.name = name.empty() ? "Snapshot " + std::to_string(m_snapshots.size() + 1) : name;
        m_snapshots.push_back(s);
    }
    markDirty();
    return int(m_snapshots.size()) - 1;
}

bool Session::recallMixSnapshot(int index) {
    MixSnapshot s;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (index < 0 || index >= int(m_snapshots.size())) return false;
        s = m_snapshots[size_t(index)];
    }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (Track& t : m_tracks)
            for (const MixChannel& c : s.channels)
                if (c.trackId == t.id) { t.volume = c.volume; t.pan = c.pan; t.sendA = c.sendA; t.sendB = c.sendB; t.mute = c.mute; t.solo = c.solo; }
    }
    m_master = s.master;
    changed(false);
    return true;
}

void Session::removeMixSnapshot(int index) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (index < 0 || index >= int(m_snapshots.size())) return;
        m_snapshots.erase(m_snapshots.begin() + index);
    }
    markDirty();
}

void Session::renameMixSnapshot(int index, const std::string& name) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (index < 0 || index >= int(m_snapshots.size()) || name.empty()) return;
        m_snapshots[size_t(index)].name = name;
    }
    markDirty();
}

// -------------------------------------------------------------------------------------------------------- Pro Tools

double Session::nextTransient(int t, int r, double fromBeat, bool forward, double sensitivity) const {
    std::shared_ptr<const AudioData> audio;
    i64 off = 0, n = 0;
    double start = 0;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return -1;
        const Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (rg.midi || !rg.audio) return -1;
        audio = rg.audio;
        off = rg.offset;
        n = rg.frames;
        start = rg.start;
    }
    if (n <= 0 || off < 0 || off + n > audio->frames()) return -1;
    const auto tm = tempoMap();
    const double base = tm->beatToFrame(start);
    const auto marks = dsp::findTransients(audio->pcm.data() + off * 2, size_t(n), sensitivity);
    double best = -1;
    for (size_t p : marks) {
        const double beat = tm->frameToBeat(base + double(p));
        if (forward) { if (beat > fromBeat + 1e-4) { best = beat; break; } }
        else if (beat < fromBeat - 1e-4) best = beat;
    }
    return best;
}

int Session::separateAtTransients(int t, int r, double sensitivity, double conformGrid, double strength) {
    Region a;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return 0;
        a = m_tracks[size_t(t)].regions[size_t(r)];
    }
    if (a.midi) return 0;
    if (!a.audio) { message(tr("This audio is still loading.")); return 0; }
    if (a.flexed()) { message(tr("Beat Detective needs a region that is not time-stretched or transposed.")); return 0; }
    if (a.offset < 0 || a.frames <= 0 || a.offset + a.frames > a.audio->frames()) return 0;
    auto marks = dsp::findTransients(a.audio->pcm.data() + a.offset * 2, size_t(a.frames), sensitivity, 1323);   // 30 ms apart at least
    if (marks.empty()) { message(tr("No transients found. Try a higher sensitivity.")); return 1; }

    constexpr i64 kPreRoll = 88;   // 2 ms before the hit so the attack is not clipped
    std::vector<i64> cuts;
    if (marks.front() < 441) marks.front() = 0;   // a hit at the very start needs no separate lead-in piece
    else cuts.push_back(0);
    for (size_t m : marks) cuts.push_back(m == 0 ? 0 : std::max<i64>(cuts.empty() ? 0 : cuts.back() + 1, i64(m) - kPreRoll));
    const size_t firstHit = marks.front() == 0 ? 0 : 1;   // pieces before this index are lead-ins that are not conformed

    const auto tm = tempoMap();
    const double startFrame = tm->beatToFrame(a.start);
    std::vector<Region> parts;
    for (size_t k = 0; k < cuts.size(); ++k) {
        const i64 b = cuts[k], e = k + 1 < cuts.size() ? cuts[k + 1] : a.frames;
        if (e - b < 2) continue;
        Region x = a;
        x.offset = a.offset + b;
        x.frames = e - b;
        x.srcOffset = a.srcOffset + b;
        x.srcFrames = e - b;
        x.start = tm->frameToBeat(startFrame + double(b));
        const i64 cut = std::min<i64>(220, x.frames / 2);
        x.fadeIn = k == 0 ? a.fadeIn : cut;
        x.fadeOut = k + 1 == cuts.size() ? a.fadeOut : cut;
        if (conformGrid > 0 && k >= firstHit) {
            const double target = std::round(x.start / conformGrid) * conformGrid;
            x.start += (target - x.start) * std::clamp(strength, 0.0, 1.0);
        }
        parts.push_back(std::move(x));
    }
    if (parts.empty()) return 0;
    if (conformGrid > 0) {   // keep the pieces gapless: trim a piece that now overlaps the next one, extend one that leaves a hole
        for (size_t k = 0; k + 1 < parts.size(); ++k) {
            Region& p = parts[k];
            const double pf = tm->beatToFrame(p.start), nf = tm->beatToFrame(parts[k + 1].start);
            const i64 want = i64(std::llround(nf - pf));
            const i64 room = p.audio->frames() - p.offset;
            const i64 frames = std::clamp<i64>(want, 2, room);
            p.frames = p.srcFrames = frames;
            p.fadeOut = std::min<i64>(220, frames / 2);
        }
    }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return 0;
        auto& regs = m_tracks[size_t(t)].regions;
        regs[size_t(r)] = parts[0];
        regs.insert(regs.begin() + r + 1, parts.begin() + 1, parts.end());
    }
    changed(false);
    return int(parts.size());
}

}  // namespace core
