// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiFile.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace dsp {
namespace {

struct Reader {
    const std::vector<std::uint8_t>& d;
    size_t p = 0;
    bool ok() const { return p <= d.size(); }
    std::uint32_t u(int bytes) {
        std::uint32_t v = 0;
        for (int i = 0; i < bytes && p < d.size(); ++i) v = (v << 8) | d[p++];
        return v;
    }
    std::uint32_t var() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4 && p < d.size(); ++i) {
            const std::uint8_t b = d[p++];
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    }
};

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(std::uint8_t(v >> s)); }
void put16(std::vector<std::uint8_t>& o, std::uint16_t v) { o.push_back(std::uint8_t(v >> 8)); o.push_back(std::uint8_t(v)); }
void putVar(std::vector<std::uint8_t>& o, std::uint32_t v) {
    std::uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7) > 0) buf[n++] = std::uint8_t((v & 0x7F) | 0x80);
    while (n > 0) o.push_back(buf[--n]);
}

}  // namespace

bool readMidiFile(const std::vector<std::uint8_t>& data, MidiSong& out, std::string& error) {
    Reader r{data};
    if (data.size() < 14 || r.u(4) != 0x4D546864) { error = "Not a MIDI file."; return false; }
    r.u(4);
    r.u(2);                                   // format (0 or 1; both are read the same way)
    const int ntrks = int(r.u(2));
    const int div = int(r.u(2));
    if (div & 0x8000 || div == 0) { error = "SMPTE time division is not supported."; return false; }
    out = MidiSong{};
    bool gotTempo = false, gotSig = false;

    for (int t = 0; t < ntrks && r.p + 8 <= data.size(); ++t) {
        if (r.u(4) != 0x4D54726B) { error = "Corrupt track header."; return false; }
        const size_t len = r.u(4), end = std::min(data.size(), r.p + len);
        MidiTrack track;
        std::uint64_t tick = 0;
        int running = 0;
        std::map<int, std::pair<std::uint64_t, int>> open;   // (channel << 8 | pitch) -> start tick, velocity
        auto closeNote = [&](int ch, int pitch) {
            auto it = open.find(ch << 8 | pitch);
            if (it == open.end()) return;
            MidiNote n;
            n.channel = ch;
            n.pitch = pitch;
            n.velocity = it->second.second;
            n.startBeat = double(it->second.first) / div;
            n.lengthBeats = std::max(1.0 / 64, double(tick - it->second.first) / div);
            track.notes.push_back(n);
            open.erase(it);
        };
        while (r.p < end) {
            tick += r.var();
            int status = data[r.p];
            if (status & 0x80) ++r.p; else status = running;   // running status reuses the previous status byte
            if (status == 0xFF) {
                const int type = data[r.p++];
                const size_t mlen = r.var();
                if (type == 0x51 && mlen == 3 && !gotTempo) {
                    out.bpm = 60000000.0 / double((data[r.p] << 16) | (data[r.p + 1] << 8) | data[r.p + 2]);
                    gotTempo = true;
                } else if (type == 0x58 && mlen >= 2 && !gotSig) {
                    out.num = data[r.p];
                    out.den = 1 << std::min<int>(data[r.p + 1], 5);
                    gotSig = true;
                } else if (type == 0x03) {
                    track.name.assign(reinterpret_cast<const char*>(&data[r.p]), std::min(mlen, end - r.p));
                }
                r.p += mlen;
                if (type == 0x2F) break;
            } else if (status == 0xF0 || status == 0xF7) {
                r.p += r.var();
            } else {
                running = status;
                const int kind = status & 0xF0, ch = status & 0x0F;
                const int d1 = r.p < end ? data[r.p++] : 0;
                const int d2 = (kind != 0xC0 && kind != 0xD0 && r.p < end) ? data[r.p++] : 0;
                if (kind == 0x90 && d2 > 0) {
                    closeNote(ch, d1);   // a repeated note-on ends the previous one
                    open[ch << 8 | d1] = {tick, d2};
                } else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
                    closeNote(ch, d1);
                }
            }
        }
        r.p = end;
        const std::uint64_t finalTick = tick;
        tick = finalTick;
        for (auto it = open.begin(); it != open.end();) { closeNote(it->first >> 8, it->first & 0xFF); it = open.begin(); }
        if (!track.notes.empty()) {
            std::sort(track.notes.begin(), track.notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.startBeat < b.startBeat; });
            out.tracks.push_back(std::move(track));
        }
    }
    if (out.tracks.empty()) { error = "The file contains no notes."; return false; }
    return true;
}

std::vector<std::uint8_t> writeMidiFile(const MidiSong& song) {
    constexpr int kPpq = 480;
    std::vector<std::uint8_t> o;
    put32(o, 0x4D546864); put32(o, 6); put16(o, 1); put16(o, std::uint16_t(song.tracks.size() + 1)); put16(o, kPpq);

    auto chunk = [&](const std::vector<std::uint8_t>& body) {
        put32(o, 0x4D54726B);
        put32(o, std::uint32_t(body.size()));
        o.insert(o.end(), body.begin(), body.end());
    };
    {   // conductor track: tempo and time signature
        std::vector<std::uint8_t> b;
        const std::uint32_t mpq = std::uint32_t(std::llround(60000000.0 / song.bpm));
        putVar(b, 0); b.insert(b.end(), {0xFF, 0x51, 0x03, std::uint8_t(mpq >> 16), std::uint8_t(mpq >> 8), std::uint8_t(mpq)});
        int dd = 0;
        while ((1 << dd) < song.den) ++dd;
        putVar(b, 0); b.insert(b.end(), {0xFF, 0x58, 0x04, std::uint8_t(song.num), std::uint8_t(dd), 24, 8});
        putVar(b, 0); b.insert(b.end(), {0xFF, 0x2F, 0x00});
        chunk(b);
    }
    for (const MidiTrack& t : song.tracks) {
        struct Ev { std::uint64_t tick; int order; std::uint8_t status, d1, d2; };
        std::vector<Ev> evs;
        for (const MidiNote& n : t.notes) {
            const auto on = std::uint64_t(std::llround(n.startBeat * kPpq));
            const auto off = on + std::max<std::uint64_t>(1, std::uint64_t(std::llround(n.lengthBeats * kPpq)));
            evs.push_back({on, 1, std::uint8_t(0x90 | (n.channel & 15)), std::uint8_t(n.pitch & 127), std::uint8_t(std::clamp(n.velocity, 1, 127))});
            evs.push_back({off, 0, std::uint8_t(0x80 | (n.channel & 15)), std::uint8_t(n.pitch & 127), 0});
        }
        std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
        std::vector<std::uint8_t> b;
        putVar(b, 0);
        b.insert(b.end(), {0xFF, 0x03, std::uint8_t(std::min<size_t>(t.name.size(), 127))});
        b.insert(b.end(), t.name.begin(), t.name.begin() + std::ptrdiff_t(std::min<size_t>(t.name.size(), 127)));
        std::uint64_t last = 0;
        for (const Ev& e : evs) {
            putVar(b, std::uint32_t(e.tick - last));
            last = e.tick;
            b.insert(b.end(), {e.status, e.d1, e.d2});
        }
        putVar(b, 0); b.insert(b.end(), {0xFF, 0x2F, 0x00});
        chunk(b);
    }
    return o;
}

}  // namespace dsp
