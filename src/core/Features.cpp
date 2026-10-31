// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Session: Logic Pro style editing features (v0.7).
#include <algorithm>
#include "../dsp/TempoDetect.h"
#include "Internal.h"
#include "Session.h"

namespace core {

int Session::bounceTrackInPlace(int track) {
    Track src;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return -1;
        src = m_tracks[size_t(track)];
    }
    if (src.regions.empty() && !(src.type == TrackType::Drums)) {
        message(tr("This track has no regions to bounce."));
        return -1;
    }
    const auto tempo = tempoMap();
    double startBeat = 1e18, endBeat = 0;
    for (const Region& r : src.regions) {
        startBeat = std::min(startBeat, r.start);
        endBeat = std::max(endBeat, r.start + regionBeats(r));
    }
    if (src.type == TrackType::Drums) {   // the pattern loops: bounce four bars
        startBeat = std::min(startBeat, 0.0);
        endBeat = std::max(endBeat, tempo->barStart(5));
    }
    if (!(endBeat > startBeat)) {
        message(tr("This track has no regions to bounce."));
        return -1;
    }
    const bool tail = src.type != TrackType::Audio || !src.inserts.empty();   // instrument releases, reverb and echo tails
    const i64 first = i64(std::llround(tempo->beatToFrame(startBeat)));
    const i64 end = i64(std::llround(tempo->beatToFrame(endBeat))) + (tail ? i64(kSampleRate) * 2 : 0);

    auto data = std::make_shared<AudioData>();
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tm = tempo.get();
        constexpr int kBlock = 4096;
        ensureScratch(size_t(kBlock) * 2);
        m_only = track;   // solo this one track, without sends and master processing
        flushDsp();
        m_expected = -1;
        data->pcm.reserve(size_t(end - first) * 2);
        for (i64 p = first; p < end; p += kBlock) {
            const int n = int(std::min<i64>(kBlock, end - p));
            renderBlock(p, n, m_scratch.data(), true);
            data->pcm.insert(data->pcm.end(), m_scratch.begin(), m_scratch.begin() + std::ptrdiff_t(n) * 2);
        }
        m_only = -1;
        flushDsp();
        m_expected = -1;
    }
    if (dsp::peakOf(data->pcm.data(), data->pcm.size() / 2) < 1e-6f) {
        message(tr("The track is silent, nothing was bounced."));
        return -1;
    }

    // The bounced audio already contains fader and pan, so the new track passes it through unchanged.
    checkpoint();
    data->name = tr("%1 (Bounce)").arg(src.name).str();
    computePeaks(*data);
    int idx;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track >= int(m_tracks.size())) return -1;
        Track t = makeTrack(TrackType::Audio, data->name);
        t.color = m_tracks[size_t(track)].color;
        t.volume = 1.f;
        Region r;
        r.name = data->name;
        r.start = startBeat;
        r.source = r.audio = data;
        r.frames = r.srcFrames = data->frames();
        t.regions.push_back(std::move(r));
        m_tracks[size_t(track)].mute = true;   // the original stays for editing but no longer sounds
        idx = track + 1;
        m_tracks.insert(m_tracks.begin() + idx, std::move(t));
    }
    changed(true);
    return idx;
}

/// Runs a note transformation on a MIDI region (one undo step). False when the region is not MIDI.
bool Session::editNotes(int t, int r, const std::function<std::vector<Note>(const std::vector<Note>&, double)>& edit) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return false;
        if (!m_tracks[size_t(t)].regions[size_t(r)].midi) return false;
    }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return false;
        Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        rg.notes = edit(rg.notes, rg.lengthBeats);
        rg.drummer = false;   // hand-edited notes detach from the generator
    }
    changed(false);
    return true;
}

bool Session::arpeggiateRegion(int t, int r, const ArpParams& p) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double len) { return arpeggiate(n, len, p); });
}

bool Session::quantizeRegion(int t, int r, const QuantizeParams& p) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double) { return quantizeNotes(n, p); });
}

bool Session::scaleQuantizeRegion(int t, int r, const ScaleParams& p) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double) { return scaleQuantize(n, p); });
}

bool Session::humanizeRegion(int t, int r, const HumanizeParams& p) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double) { return humanize(n, p); });
}

bool Session::chordTriggerRegion(int t, int r, const ChordParams& p) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double) { return chordTrigger(n, p); });
}

bool Session::legatoRegion(int t, int r) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double len) { return legato(n, len); });
}

bool Session::transposeRegion(int t, int r, int semitones) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double) { return transposeNotes(n, semitones); });
}

bool Session::velocityRampRegion(int t, int r, int fromVelocity, int toVelocity) {
    return editNotes(t, r, [&](const std::vector<Note>& n, double len) { return velocityRamp(n, len, fromVelocity, toVelocity); });
}

int Session::repeatRegion(int t, int r, int copies) {
    copies = std::clamp(copies, 1, 64);
    Region c;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return 0;
        c = m_tracks[size_t(t)].regions[size_t(r)];
    }
    if (!c.midi && !c.audio) return 0;   // audio that is still being decoded
    const double len = regionBeats(c);
    if (len <= 0) return 0;
    checkpoint();
    const auto tm = tempoMap();
    std::vector<Region> made;
    for (int k = 1; k <= copies; ++k) {
        Region x = c;
        x.start = c.start + k * len;
        if (!x.midi && x.source && x.follow) applyFlex(x, tm->bpmAt(x.start));   // stretch for the tempo at the new place
        made.push_back(std::move(x));
    }
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t >= int(m_tracks.size())) return 0;
        auto& regs = m_tracks[size_t(t)].regions;
        for (Region& x : made) regs.push_back(std::move(x));
    }
    changed(false);
    return int(made.size());
}

bool Session::joinRegions(int t, int r) {
    Region a, b;
    int bi = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return false;
        const auto& regs = m_tracks[size_t(t)].regions;
        a = regs[size_t(r)];
        for (int i = 0; i < int(regs.size()); ++i)   // the region that starts next
            if (i != r && regs[size_t(i)].start > a.start + 1e-9 && (bi < 0 || regs[size_t(i)].start < regs[size_t(bi)].start)) bi = i;
        if (bi >= 0) b = regs[size_t(bi)];
    }
    if (bi < 0) { message(tr("There is no region after this one to join.")); return false; }
    if (a.midi != b.midi) { message(tr("Only regions of the same kind can be joined.")); return false; }
    const auto tm = tempoMap();
    Region m = a;
    if (a.midi) {
        const double end = std::max(a.start + a.lengthBeats, b.start + b.lengthBeats);
        const double shift = b.start - a.start;
        for (Note n : b.notes) { n.start += shift; m.notes.push_back(n); }
        std::stable_sort(m.notes.begin(), m.notes.end(), [](const Note& x, const Note& y) { return x.start < y.start; });
        m.lengthBeats = end - a.start;
        m.drummer = false;
        m.mute = a.mute && b.mute;
    } else {
        const bool sameSource = a.source && a.source == b.source;
        const bool contiguous = sameSource && b.srcOffset == a.srcOffset + a.srcFrames;
        const bool sameFlex = a.follow == b.follow && a.sourceBpm == b.sourceBpm && a.semitones == b.semitones;
        const bool neighbours = std::fabs(b.start - (a.start + regionBeats(a))) < 1e-3;
        if (!sameSource || !contiguous || !sameFlex || !neighbours) {
            message(tr("These audio regions cannot be joined: they must be neighbours that play the same recording one after the other."));
            return false;
        }
        m.srcFrames = a.srcFrames + b.srcFrames;
        m.fadeOut = b.fadeOut;
        m.mute = a.mute && b.mute;
        if (m.flexed()) applyFlex(m, tm->bpmAt(m.start));   // stretch the joined source part again
        else m.frames = a.frames + b.frames;
    }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        auto& regs = m_tracks[size_t(t)].regions;
        if (r >= int(regs.size()) || bi >= int(regs.size())) return false;
        regs[size_t(r)] = std::move(m);
        regs.erase(regs.begin() + bi);
    }
    changed(false);
    return true;
}

double Session::detectRegionTempo(int t, int r) const {
    std::shared_ptr<const AudioData> src;
    i64 off = 0, n = 0;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return 0;
        const Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (rg.midi || !rg.source) return 0;
        src = rg.source;
        off = rg.srcOffset;
        n = rg.srcFrames;
    }
    if (n <= 0 || off < 0 || off + n > src->frames()) return 0;
    return dsp::detectTempo(src->pcm.data() + off * 2, size_t(n));
}

int Session::convertPatternToMidi(int track, int bars) {
    bars = std::clamp(bars, 1, 64);
    Track src;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || m_tracks[size_t(track)].type != TrackType::Drums) return -1;
        src = m_tracks[size_t(track)];
    }
    static const int kRowPitch[kDrumRows] = {36, 38, 42, 39};   // General MIDI: kick, snare, closed hat, clap
    Region r;
    r.midi = true;
    r.start = 0;
    r.lengthBeats = bars * 4.0;   // a pattern has 16 sixteenth-note steps: 4 beats
    for (int bar = 0; bar < bars; ++bar)
        for (int step = 0; step < kSteps; ++step)
            for (int row = 0; row < kDrumRows; ++row)
                if (src.steps[size_t(row)][size_t(step)]) r.notes.push_back(Note{kRowPitch[row], bar * 4.0 + step * 0.25, 0.2, 100});
    if (r.notes.empty()) {
        message(tr("The pattern is empty."));
        return -1;
    }
    checkpoint();
    int idx;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track >= int(m_tracks.size())) return -1;
        Track t = makeTrack(TrackType::Drummer, tr("%1 (MIDI)").arg(src.name).str());
        t.color = src.color;
        r.name = t.name;
        t.regions.push_back(std::move(r));
        idx = track + 1;
        m_tracks.insert(m_tracks.begin() + idx, std::move(t));
    }
    changed(true);
    return idx;
}

}  // namespace core
