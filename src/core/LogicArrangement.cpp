// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LogicArrangement.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace core {

namespace {

constexpr std::uint32_t kRegionOrigin = 34560;   // placement positions: 34560 + tick (960 per quarter note)
constexpr std::uint32_t kEventOrigin = 38400;    // tempo / meter / marker / note positions: 38400 + tick
constexpr double kPpq = 960.0;

struct View {
    const std::uint8_t* d; std::size_t n;
    bool ok(std::size_t o, std::size_t len) const { return o <= n && len <= n - o; }
    std::uint32_t u32(std::size_t o) const { return ok(o, 4) ? std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 | std::uint32_t(d[o + 2]) << 16 | std::uint32_t(d[o + 3]) << 24 : 0; }
    std::uint32_t u16(std::size_t o) const { return ok(o, 2) ? std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 : 0; }
    std::uint8_t u8(std::size_t o) const { return ok(o, 1) ? d[o] : 0; }
};

struct Rec {
    std::size_t off = 0;        // start of the record header
    std::uint32_t size = 0;     // payload size
    char tag[4] = {0, 0, 0, 0};
    bool is(const char* t) const { return std::memcmp(tag, t, 4) == 0; }
    std::size_t pay() const { return off + 36; }
};

std::string utf8FromUtf16(const View& v, std::size_t o, std::size_t chars) {
    std::string s;
    for (std::size_t i = 0; i < chars; ++i) {
        const std::uint32_t cp = v.u16(o + 2 * i);
        if (cp < 0x80) s += char(cp);
        else if (cp < 0x800) { s += char(0xC0 | cp >> 6); s += char(0x80 | (cp & 0x3F)); }
        else if (cp >= 0xD800 && cp < 0xE000) s += '?';
        else { s += char(0xE0 | cp >> 12); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
    }
    return s;
}

struct Placement {
    int track = 0;
    long long tick = 0;
    std::uint32_t regionIndex = 0;   // audio: file index; MIDI: content cluster
    std::uint32_t regionK = 0;
    std::uint32_t seq = 0;           // sequence the placement lives in
    std::uint32_t child = 0xFFFFFFFFu;
};

bool isPlacement(const View& v, std::size_t o, std::size_t end) {
    return o + 0x50 <= end && (v.u8(o) == 0x24 || v.u8(o) == 0x20) && v.u8(o + 1) == 0 && v.u8(o + 2) == 0 && v.u8(o + 3) == 0 &&
           v.u32(o + 4) >= kRegionOrigin && v.u8(o + 0x16) == 0 && v.u8(o + 0x17) == 0x89 && v.u8(o + 0x37) == 0x8A && v.u16(o + 0x14) >= 1;
}

}  // namespace

namespace {
// UTF-8 without control characters (Logic's names are plain text)
bool readableName(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) continue;
        std::size_t extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 99;
        if (extra == 99 || i + extra >= s.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        if (extra == 1 && c == 0xC2 && static_cast<unsigned char>(s[i + 1]) < 0xA0) return false;   // C1 control characters
        i += extra;
    }
    return true;
}
}  // namespace

std::vector<std::string> readLogicTrackNames(const std::uint8_t* data, std::size_t size, int hint) {
    const View v{data, size};
    std::vector<Rec> recs;
    for (std::size_t o = 24; o + 36 <= size;) {
        Rec r;
        r.off = o;
        r.size = v.u32(o + 0x1c);
        if (std::uint64_t(o) + 36 + r.size > size) break;
        std::memcpy(r.tag, data + o, 4);
        recs.push_back(r);
        o += 36 + r.size;
    }
    // object id -> name, from the Environment objects
    std::map<std::uint32_t, std::string> names;
    for (const Rec& r : recs) {
        if (!r.is("ivnE") || r.size < 162) continue;
        const std::uint32_t oid = v.u32(r.pay() + 16);
        const std::size_t n = v.u16(r.pay() + 158);
        if (n == 0 || 160 + n > r.size) continue;
        const std::string s(reinterpret_cast<const char*>(data + r.pay() + 160), n);
        if (readableName(s)) names.emplace(oid, s);
    }
    // the karT runs, split by zero-size markers
    std::vector<std::vector<std::uint32_t>> runs;   // object ids per run
    std::vector<std::uint32_t> cur;
    for (const Rec& r : recs) {
        if (!r.is("karT")) continue;
        if (r.size == 0) { if (!cur.empty()) runs.push_back(std::move(cur)); cur.clear(); }
        else if (r.size >= 12) cur.push_back(v.u32(r.pay() + 8));
    }
    if (!cur.empty()) runs.push_back(std::move(cur));
    if (runs.empty()) return {};
    std::size_t pick = 0;
    if (hint > 0) {   // the run nearest to NumberOfTracks + 1 rows
        long best = -1;
        for (std::size_t i = 0; i < runs.size(); ++i) {
            const long diff = std::labs(long(runs[i].size()) - long(hint + 1));
            if (best < 0 || diff < best) { best = diff; pick = i; }
        }
    } else {          // else the first run whose objects all sit in a longer run, else the longest
        bool found = false;
        for (std::size_t i = 0; i < runs.size() && !found; ++i) {
            if (runs[i].size() <= 1) continue;
            for (std::size_t j = 0; j < runs.size() && !found; ++j) {
                if (j == i || runs[j].size() <= runs[i].size()) continue;
                bool all = true;
                for (std::uint32_t id : runs[i]) if (std::find(runs[j].begin(), runs[j].end(), id) == runs[j].end()) { all = false; break; }
                if (all) { pick = i; found = true; }
            }
        }
        if (!found) for (std::size_t i = 1; i < runs.size(); ++i) if (runs[i].size() > runs[pick].size()) pick = i;
    }
    std::vector<std::string> out;
    for (std::uint32_t id : runs[pick]) {
        const auto it = names.find(id);
        out.push_back(it == names.end() ? std::string() : it->second);
    }
    return out;
}

LogicArrangementStats decodeLogicArrangement(const std::uint8_t* data, std::size_t size, LogicProject& out) {
    LogicArrangementStats st;
    const View v{data, size};
    std::vector<Rec> recs;
    for (std::size_t o = 24; o + 36 <= size;) {
        Rec r;
        r.off = o;
        r.size = v.u32(o + 0x1c);
        if (std::uint64_t(o) + 36 + r.size > size) break;
        std::memcpy(r.tag, data + o, 4);
        recs.push_back(r);
        o += 36 + r.size;
    }
    const std::uint8_t kTail[16] = {0xF1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0};
    (void)kTail;

    // ---- tempo map: the qSvE whose payload starts 60 00 00 00, 32-byte events
    for (const Rec& r : recs) {
        if (!r.is("qSvE") || r.size < 32 || v.u32(r.pay()) != 0x60) continue;
        for (std::size_t i = 0; i + 32 <= r.size && v.u32(r.pay() + i) == 0x60; i += 32) {
            const std::uint32_t pos = v.u32(r.pay() + i + 4), raw = v.u32(r.pay() + i + 0x10);
            const double bpm = raw / 10000.0;
            if (pos < kEventOrigin || bpm < 20 || bpm > 999) continue;
            const double beat = double(pos - kEventOrigin) / kPpq;
            if (!out.tempo.empty() && std::fabs(out.tempo.back().beat - beat) < 1e-9) out.tempo.back().bpm = bpm;
            else if (out.tempo.empty() || std::fabs(out.tempo.back().bpm - bpm) > 1e-9) out.tempo.push_back({beat, bpm, false});
        }
        break;
    }
    if (!out.tempo.empty()) out.tempo.front().beat = 0;
    st.tempoPoints = int(out.tempo.size());

    // ---- meter map: payload starts 30 00 00 00; 80-byte header (initial signature inside) + 48-byte changes
    for (const Rec& r : recs) {
        if (!r.is("qSvE") || r.size < 80 || v.u32(r.pay()) != 0x30) continue;
        auto valid = [](int num, int den) { return num >= 1 && num <= 64 && den >= 1 && den <= 64; };
        int num = v.u8(r.pay() + 0x0c), exp = v.u8(r.pay() + 0x0b);
        int den = exp < 7 ? 1 << exp : 0;
        if (!valid(num, den)) break;
        out.meters.push_back({1, num, den});
        long long prevTick = 0, bar = 1;
        for (std::size_t i = 80; i + 48 <= r.size && v.u32(r.pay() + i) == 0x30; i += 48) {
            const std::uint32_t pos = v.u32(r.pay() + i + 4);
            const int n2 = v.u8(r.pay() + i + 0x0c), e2 = v.u8(r.pay() + i + 0x0b), d2 = e2 < 7 ? 1 << e2 : 0;
            if (pos < kEventOrigin || !valid(n2, d2)) continue;
            const long long tick = pos - kEventOrigin;
            const double perBar = 3840.0 * num / den;
            bar += std::llround(double(tick - prevTick) / perBar);
            prevTick = tick; num = n2; den = d2;
            if (out.meters.back().bar == int(bar)) out.meters.back() = {int(bar), num, den};
            else out.meters.push_back({int(bar), num, den});
        }
        break;
    }
    st.meterPoints = int(out.meters.size());

    // ---- markers: payload starts 12 00 00 00, 48-byte events, names in qSxT records (RTF "\cf2 NAME}") keyed by a link id
    std::map<std::uint32_t, std::string> markerNames;
    for (const Rec& r : recs) {
        if (!r.is("qSxT") || r.size < 8) continue;
        const std::string body(reinterpret_cast<const char*>(data + r.off), 36 + r.size);
        const std::size_t a = body.find("\\cf2 ");
        if (a == std::string::npos) continue;
        const std::size_t b = body.find('}', a);
        if (b == std::string::npos) continue;
        std::string nm = body.substr(a + 5, b - a - 5);
        while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\n' || nm.back() == '\r')) nm.pop_back();
        markerNames[v.u16(r.off + 0x0a)] = nm;
    }
    for (const Rec& r : recs) {
        if (!r.is("qSvE") || r.size < 48 || v.u32(r.pay()) != 0x12) continue;
        for (std::size_t i = 0; i + 48 <= r.size && v.u32(r.pay() + i) == 0x12; i += 48) {
            const std::uint32_t pos = v.u32(r.pay() + i + 4), link = v.u32(r.pay() + i + 0x10);
            if (pos < kEventOrigin) continue;
            const auto it = markerNames.find(link);
            out.markers.push_back({double(pos - kEventOrigin) / kPpq, it == markerNames.end() ? std::string() : it->second});
        }
        break;
    }
    st.markers = int(out.markers.size());

    // ---- audio pool: lFuA (file name, UTF-16LE) by file index; gRuA (region parameters) by (file index, region number)
    struct GRegion { long long trim = 0, length = 0; std::string name; };
    std::map<std::uint32_t, std::string> fileOf;
    std::map<std::pair<std::uint32_t, std::uint32_t>, GRegion> regionOf;
    for (const Rec& r : recs) {
        const bool lf = r.is("lFuA"), gr = r.is("gRuA");
        if (!lf && !gr) continue;
        const std::uint32_t id = v.u32(r.off + 8), ord = v.u32(r.off + 0x0c);
        if (id % 0x40000 != 0) continue;
        const std::uint32_t fi = id / 0x40000;
        if (lf) {
            if (fileOf.count(fi)) continue;
            const std::size_t chars = v.u16(r.pay() + 8);
            if (10 + 2 * chars <= r.size) fileOf[fi] = utf8FromUtf16(v, r.pay() + 10, chars);
        } else if (ord % 0x10000 == 0 && r.size >= 0x4c) {
            GRegion g;
            g.trim = v.u32(r.pay() + 0x06);
            g.length = v.u32(r.pay() + 0x16);
            const std::size_t nl = v.u16(r.pay() + 0x4a);
            if (nl < 200 && 0x4c + nl <= r.size) g.name.assign(reinterpret_cast<const char*>(data + r.pay() + 0x4c), nl);
            regionOf.emplace(std::make_pair(fi, ord >> 16), g);
        }
    }

    // ---- notes of every note sequence (role 0x17, payload starting with a note event), by sequence number
    std::map<std::uint32_t, std::vector<Note>> notesOf;
    std::map<std::uint32_t, std::string> seqName;
    std::map<std::uint32_t, std::size_t> seqRec;   // first qeSM record of a sequence (offset)
    for (const Rec& r : recs) {
        if (r.is("qeSM") && r.size >= 0x12 && !seqName.count(v.u32(r.off + 8) >> 18)) {   // the first qeSM of a sequence names it
            const std::size_t nl = v.u8(r.off + 0x34);
            seqName[v.u32(r.off + 8) >> 18] = r.off + 0x36 + nl <= r.off + 36 + r.size ? std::string(reinterpret_cast<const char*>(data + r.off + 0x36), nl) : std::string();
            seqRec[v.u32(r.off + 8) >> 18] = r.off;
        }
        if (!r.is("qSvE") || v.u16(r.off + 6) != 0x17 || r.size < 32 || v.u32(r.pay()) != 0x90) continue;
        std::vector<Note> notes;
        for (std::size_t i = 0; i + 32 <= r.size && v.u32(r.pay() + i) == 0x90; i += 32) {
            const std::uint32_t pos = v.u32(r.pay() + i + 4), len = v.u32(r.pay() + i + 0x1c);
            if (pos < kEventOrigin) continue;
            Note n;
            n.pitch = v.u8(r.pay() + i + 0x0c);
            n.velocity = std::max<int>(1, v.u8(r.pay() + i + 0x0b));
            n.start = double(pos - kEventOrigin) / kPpq;
            n.length = std::max(1.0, double(len)) / kPpq;
            notes.push_back(n);
        }
        if (!notes.empty()) notesOf[v.u32(r.off + 8) >> 18] = std::move(notes);
    }

    // ---- placements: 80-byte events of every qSvE (audio 0x24, MIDI 0x20)
    std::vector<Placement> audioP, midiP;
    for (const Rec& r : recs) {
        if (!r.is("qSvE")) continue;
        const std::size_t base = r.pay(), end = r.pay() + r.size;
        const std::uint32_t seq = v.u32(r.off + 8) >> 18;
        for (std::size_t o = base; o + 0x50 <= end;) {
            if (isPlacement(v, o, end)) {
                const bool audio = v.u8(o) == 0x24;
                Placement p;
                p.track = int(v.u16(o + 0x14));
                p.tick = static_cast<long long>(v.u32(o + 4)) - kRegionOrigin;
                p.regionIndex = v.u32(o + (audio ? 0x2c : 0x08)) / 4;
                p.regionK = audio ? v.u32(o + 0x28) : 0;
                p.seq = seq;
                const std::uint32_t ch = v.u32(o + 0x20);
                p.child = ch == 0xFFFFFFFFu ? ch : ch / 4;
                (audio ? audioP : midiP).push_back(p);
                o += 0x50;
            } else o += 4;
        }
    }

    // ---- build tracks: one per (track number, audio / MIDI)
    std::map<std::pair<int, bool>, LogicTrack> tracks;
    auto trackFor = [&](int no, bool midi) -> LogicTrack& {
        auto it = tracks.find({no, midi});
        if (it == tracks.end()) {
            LogicTrack t;
            t.number = no;
            t.name = "Track " + std::to_string(no);
            t.kind = midi ? LogicTrackKind::Software : LogicTrackKind::Audio;
            it = tracks.emplace(std::make_pair(no, midi), std::move(t)).first;
        }
        return it->second;
    };
    for (const Placement& p : audioP) {
        const auto f = fileOf.find(p.regionIndex);
        auto g = regionOf.find({p.regionIndex, p.regionK});
        if (g == regionOf.end()) g = regionOf.find({p.regionIndex, 0});
        if (f == fileOf.end()) { ++st.unresolvedAudio; continue; }
        LogicAudioRegion a;
        a.file = f->second;
        a.name = g != regionOf.end() ? g->second.name : f->second;
        a.startBeat = double(std::max<long long>(0, p.tick)) / kPpq;
        if (g != regionOf.end()) { a.trimInSamples = g->second.trim; a.lengthSamples = g->second.length; }
        trackFor(p.track, false).audio.push_back(a);
        ++st.audioRegions;
    }
    for (const Placement& p : midiP) {
        std::uint32_t seq = p.child != 0xFFFFFFFFu ? p.child : p.regionIndex;
        if (p.child != 0xFFFFFFFFu) {   // an automation holder is not a MIDI region
            const auto nm = seqName.find(p.child);
            if (nm != seqName.end() && nm->second.rfind("*Automation", 0) == 0) continue;
        }
        auto it = notesOf.find(seq);
        if (it == notesOf.end()) it = notesOf.find(p.regionIndex);
        LogicMidiRegion m;   // a region without notes is still a region (an empty MIDI region on the timeline)
        m.startBeat = double(std::max<long long>(0, p.tick)) / kPpq;
        if (it != notesOf.end()) m.notes = it->second;
        double end = 0;
        for (const Note& n : m.notes) end = std::max(end, n.start + n.length);
        m.lengthBeats = std::max(4.0, std::ceil(end / 4.0) * 4.0);   // fallback: whole bars (4/4)
        {   // exact length: u32 (960 PPQ ticks) in the region's qeSM at 0x78, moved by the even-rounded name length (name field sits before it)
            auto rec = seqRec.find(seq);
            if (rec == seqRec.end()) rec = seqRec.find(p.regionIndex);
            if (rec != seqRec.end()) {
                const std::size_t ro = rec->second, rsize = 36 + v.u32(ro + 0x1c);
                const std::size_t nl = v.u8(ro + 0x34), off = 0x78 + ((nl + (nl & 1)) - 6);
                {
                    if (nl + (nl & 1) >= 6 && off + 4 <= rsize) {
                        const std::uint32_t L = v.u32(ro + off);
                        if (L >= 240 && L % 240 == 0 && L <= 960u * 4 * 2000) { m.lengthBeats = double(L) / kPpq; ++st.exactMidiLengths; }
                    }
                }
            }
        }
        st.notes += int(m.notes.size());
        trackFor(p.track, true).midi.push_back(std::move(m));
        ++st.midiRegions;
    }
    for (auto& kv : tracks) {
        std::sort(kv.second.audio.begin(), kv.second.audio.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        std::sort(kv.second.midi.begin(), kv.second.midi.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        out.tracks.push_back(std::move(kv.second));
    }
    return st;
}

}  // namespace core
