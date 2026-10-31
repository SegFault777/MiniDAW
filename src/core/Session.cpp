// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Session.h"

#include <algorithm>
#include <cmath>
#include "../dsp/MidiFile.h"
#include "../dsp/Stretch.h"
#include "../dsp/Synth.h"
#include "Files.h"
#include "Internal.h"

namespace core {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr i64 kClickLen = 1400;   // metronome click length in frames
constexpr int kToneFrames = kSampleRate * 6 / 10;
const int kRowPiece[kDrumRows] = {dsp::Kick, dsp::Snare, dsp::HatClosed, dsp::Clap};

}  // namespace

// ------------------------------------------------------------------ model
Track Session::makeTrack(TrackType type, const std::string& name) {
    Track t;
    t.id = m_nextId++;
    t.type = type;
    t.name = name;
    switch (type) {
    case TrackType::Audio: t.color = Color{60, 130, 235}; break;
    case TrackType::Instrument:
        t.color = Color{100, 190, 100};
        t.instrumentId = "synth";
        t.instrument = makeInstrument("synth");
        break;
    case TrackType::Drums: t.color = Color{240, 150, 50}; break;
    case TrackType::Drummer:
        t.color = Color{235, 200, 70};
        t.instrumentId = "drumkit";
        t.instrument = makeInstrument("drumkit");
        break;
    }
    return t;
}

void Session::changed(bool structural) {
    m_dirty = true;
    if (m_hooks.changed) m_hooks.changed(structural);
}

std::vector<Track> Session::snapshot() const { std::lock_guard<std::mutex> l(m_mutex); return m_tracks; }
int Session::trackCount() const { std::lock_guard<std::mutex> l(m_mutex); return int(m_tracks.size()); }

DrumPattern Session::drumPattern(int track) const {
    std::lock_guard<std::mutex> l(m_mutex);
    if (track < 0 || track >= int(m_tracks.size())) return {};
    return m_tracks[size_t(track)].steps;
}

double Session::regionBeats(const Region& r) const {
    if (r.midi) return r.lengthBeats;
    const auto tm = tempoMap();
    return tm->frameToBeat(tm->beatToFrame(r.start) + double(r.frames)) - r.start;
}

double Session::projectEndBeats() const {
    std::lock_guard<std::mutex> l(m_mutex);
    double e = 0;
    for (const Track& t : m_tracks)
        for (const Region& r : t.regions) e = std::max(e, r.start + regionBeats(r));
    return e;
}

void Session::newProject() {
    stopRecording();
    m_playing = false;
    m_countRemain = 0;
    {
        std::lock_guard<std::mutex> l(m_tempoMutex);
        m_tempo = std::make_shared<const dsp::TempoMap>();
    }
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_tracks.clear();
        m_markers.clear();
        m_undo.clear();
        m_redo.clear();
        m_nextId = 1;
        Track d = makeTrack(TrackType::Drums, "Drum Machine 1");
        for (int s : {0, 4, 8, 12}) d.steps[0][size_t(s)] = true;
        for (int s : {4, 12}) d.steps[1][size_t(s)] = true;
        for (int s : {2, 6, 10, 14}) d.steps[2][size_t(s)] = true;
        m_tracks.push_back(d);
        m_tracks.push_back(makeTrack(TrackType::Instrument, "Software Instrument 1"));
        m_pos = 0;
        m_audition.reset();
    }
    m_loopOn = false;
    m_loopA = 0.0;
    m_loopB = 16.0;
    m_path.clear();
    m_flush |= 2;
    m_dirty = false;
    if (m_hooks.changed) m_hooks.changed(true);
    if (m_hooks.tempoChanged) m_hooks.tempoChanged();
    if (m_hooks.loopChanged) m_hooks.loopChanged();
}

int Session::addTrack(TrackType type, const std::string& name) {
    checkpoint();
    int idx;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        std::string n = name;
        if (n.empty()) {
            int count = 0;
            for (const Track& t : m_tracks) count += (t.type == type) ? 1 : 0;
            const char* base = type == TrackType::Audio ? "Audio" : type == TrackType::Instrument ? "Software Instrument"
                             : type == TrackType::Drums ? "Drum Machine" : "Drummer";
            n = Text("%1 %2").arg(base).arg(count + 1).str();
        }
        m_tracks.push_back(makeTrack(type, n));
        idx = int(m_tracks.size()) - 1;
    }
    changed(true);
    return idx;
}

void Session::removeTrack(int i) {
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (i < 0 || i >= int(m_tracks.size())) return;
        m_tracks.erase(m_tracks.begin() + i);
    }
    changed(true);
}

void Session::renameTrack(int i, const std::string& name) {
    { std::lock_guard<std::mutex> l(m_mutex); if (i < 0 || i >= int(m_tracks.size())) return; m_tracks[size_t(i)].name = name; }
    changed(true);
}

#define TRACK_SET(...) { std::lock_guard<std::mutex> l(m_mutex); if (i < 0 || i >= int(m_tracks.size())) return; Track& t = m_tracks[size_t(i)]; __VA_ARGS__; } m_dirty = true;
void Session::setVolume(int i, float v) { TRACK_SET(t.volume = std::clamp(v, 0.f, 1.5f)) }
void Session::setPan(int i, float p) { TRACK_SET(t.pan = std::clamp(p, -1.f, 1.f)) }
void Session::setSendA(int i, float v) { TRACK_SET(t.sendA = std::clamp(v, 0.f, 1.f)) }
void Session::setSendB(int i, float v) { TRACK_SET(t.sendB = std::clamp(v, 0.f, 1.f)) }
void Session::setMute(int i, bool on) { TRACK_SET(t.mute = on) }
void Session::setSolo(int i, bool on) { TRACK_SET(t.solo = on) }
#undef TRACK_SET

void Session::setArmed(int i, bool on) {
    std::lock_guard<std::mutex> l(m_mutex);
    for (size_t k = 0; k < m_tracks.size(); ++k) m_tracks[k].armed = on && int(k) == i;   // one armed track at a time
}

void Session::addInsert(int track, const std::string& effectId) {
    auto fx = makeEffect(effectId);
    if (!fx) { message(tr("Could not create the effect.")); return; }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        m_tracks[size_t(track)].inserts.push_back(fx);
    }
    changed(true);
}

void Session::removeInsert(int track, int slot) {
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        auto& v = m_tracks[size_t(track)].inserts;
        if (slot < 0 || slot >= int(v.size())) return;
        v.erase(v.begin() + slot);
    }
    changed(true);
}

void Session::setInsertBypass(int track, int slot, bool bypass) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (track < 0 || track >= int(m_tracks.size())) return;
    auto& v = m_tracks[size_t(track)].inserts;
    if (slot >= 0 && slot < int(v.size())) v[size_t(slot)]->bypass = bypass;
    m_dirty = true;
}

void Session::setInstrument(int track, const std::string& instrumentId) {
    auto inst = makeInstrument(instrumentId);
    if (!inst) { message(tr("Could not load the instrument.")); return; }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || !m_tracks[size_t(track)].instrument) return;
        m_tracks[size_t(track)].instrument = inst;
        m_tracks[size_t(track)].instrumentId = instrumentId;
        m_tracks[size_t(track)].preset = 0;
    }
    changed(true);
}

void Session::setInstrumentPreset(int track, int preset) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (track < 0 || track >= int(m_tracks.size()) || !m_tracks[size_t(track)].instrument) return;
    m_tracks[size_t(track)].instrument->setPreset(preset);
    m_tracks[size_t(track)].preset = preset;
    m_dirty = true;
}

void Session::regenDrummer(Region& r, const dsp::TempoMap& tm) const {
    const dsp::MeterPoint mp = tm.meterAtBeat(r.start);
    r.notes.clear();
    for (const dsp::DrumHit& h : dsp::generateDrummer(r.drum, mp.num, mp.den)) r.notes.push_back({h.pitch, h.beat, h.length, h.velocity});
    r.lengthBeats = r.drum.bars * mp.num * 4.0 / mp.den;
}

int Session::addMidiRegion(int track, double startBeat, double lengthBeats) {
    checkpoint();
    int idx = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return -1;
        Track& t = m_tracks[size_t(track)];
        if (t.type != TrackType::Instrument && t.type != TrackType::Drummer) return -1;
        Region r;
        r.midi = true;
        r.name = "MIDI Region";
        r.start = std::max(0.0, startBeat);
        r.lengthBeats = std::max(0.25, lengthBeats);
        t.regions.push_back(r);
        idx = int(t.regions.size()) - 1;
    }
    changed(false);
    return idx;
}

int Session::addDrummerRegion(int track, double startBeat, int bars) {
    checkpoint();
    const auto tm = tempoMap();
    int idx = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || m_tracks[size_t(track)].type != TrackType::Drummer) return -1;
        Region r;
        r.midi = true;
        r.drummer = true;
        r.name = "Drummer";
        r.start = std::max(0.0, startBeat);
        r.drum.bars = std::clamp(bars, 1, 64);
        regenDrummer(r, *tm);
        m_tracks[size_t(track)].regions.push_back(r);
        idx = int(m_tracks[size_t(track)].regions.size()) - 1;
    }
    changed(false);
    return idx;
}

void Session::setDrummerParams(int track, int region, const dsp::DrummerParams& p) {
    const auto tm = tempoMap();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || region < 0 || region >= int(m_tracks[size_t(track)].regions.size())) return;
        Region& r = m_tracks[size_t(track)].regions[size_t(region)];
        if (!r.drummer) return;
        r.drum = p;
        regenDrummer(r, *tm);
    }
    changed(false);
}

#define REGION_OP(...) { std::lock_guard<std::mutex> l(m_mutex); \
    if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return; \
    auto& regs = m_tracks[size_t(t)].regions; Region& rg = regs[size_t(r)]; __VA_ARGS__ }

void Session::moveRegion(int t, int r, double startBeat) {
    REGION_OP(rg.start = std::max(0.0, startBeat);)
    changed(false);
}

void Session::resizeRegion(int t, int r, double lengthBeats) {
    const auto tm = tempoMap();
    REGION_OP(
        if (rg.midi) rg.lengthBeats = std::max(0.25, lengthBeats);
        else if (rg.audio && !rg.flexed()) {   // trimming the end of an audio region
            const double sf = tm->beatToFrame(rg.start);
            const i64 maxFrames = rg.audio->frames() - rg.offset;
            rg.frames = std::clamp<i64>(i64(std::llround(tm->beatToFrame(rg.start + lengthBeats) - sf)), 1, maxFrames);
            rg.srcFrames = rg.frames;
        } else if (rg.audio) {   // stretched audio: shorten the played buffer, keep the source segment proportional
            const double sf = tm->beatToFrame(rg.start);
            const i64 maxFrames = rg.audio->frames() - rg.offset;
            const i64 nf = std::clamp<i64>(i64(std::llround(tm->beatToFrame(rg.start + lengthBeats) - sf)), 1, maxFrames);
            rg.srcFrames = std::max<i64>(1, i64(double(rg.srcFrames) * double(nf) / double(std::max<i64>(1, rg.frames))));
            rg.frames = nf;
        })
    changed(false);
}

void Session::removeRegion(int t, int r) {
    checkpoint();
    REGION_OP(regs.erase(regs.begin() + r);)
    changed(false);
}

void Session::splitRegion(int t, int r, double atBeat) {
    checkpoint();
    const auto tm = tempoMap();
    Region a, b;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return;
        a = m_tracks[size_t(t)].regions[size_t(r)];
    }
    const double len = regionBeats(a), rel = atBeat - a.start;
    if (rel <= 0.01 || rel >= len - 0.01) return;
    const Region orig = a;
    b = a;
    b.start = atBeat;
    if (orig.midi) {
        a.lengthBeats = rel;
        b.lengthBeats = len - rel;
        a.drummer = b.drummer = false;   // split regions are no longer regenerated
        a.notes.clear();
        b.notes.clear();
        for (const Note& n : orig.notes) {
            if (n.start >= rel) { Note m = n; m.start -= rel; b.notes.push_back(m); }
            else a.notes.push_back(n);
        }
    } else {
        const i64 rf = i64(std::llround(tm->beatToFrame(atBeat) - tm->beatToFrame(orig.start)));
        if (rf <= 0 || rf >= orig.frames) return;
        if (orig.flexed()) {   // split the source proportionally and stretch both halves again
            const i64 sf = orig.srcOffset + i64(std::llround(double(rf) / double(orig.frames) * double(orig.srcFrames)));
            a.srcFrames = sf - orig.srcOffset;
            b.srcOffset = sf;
            b.srcFrames = orig.srcOffset + orig.srcFrames - sf;
            applyFlex(a, tm->bpmAt(a.start));
            applyFlex(b, tm->bpmAt(b.start));
        } else {
            a.frames = rf;
            a.srcFrames = rf;
            b.offset += rf;
            b.frames -= rf;
            b.srcOffset += rf;
            b.srcFrames -= rf;
        }
        a.fadeOut = 0;   // fades stay on the outer edges of the two halves
        b.fadeIn = 0;
    }
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return;
        auto& regs = m_tracks[size_t(t)].regions;
        regs[size_t(r)] = a;
        regs.insert(regs.begin() + r + 1, b);
    }
    changed(false);
}

int Session::duplicateRegion(int t, int r) {
    checkpoint();
    int idx = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return -1;
        auto& regs = m_tracks[size_t(t)].regions;
        Region c = regs[size_t(r)];
        c.start = c.start + regionBeats(c);   // place the copy directly after the original
        regs.push_back(c);
        idx = int(regs.size()) - 1;
    }
    changed(false);
    return idx;
}

bool Session::copyRegion(int t, int r) {
    bool loading = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return false;
        const Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (!rg.midi && !rg.audio) loading = true;
        else { m_clip = rg; m_hasClip = true; }
    }
    if (loading) message(tr("This audio is still loading."));
    return !loading;
}

int Session::pasteRegion(int track, double beat) {
    Region c;
    TrackType type = TrackType::Audio;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (!m_hasClip || track < 0 || track >= int(m_tracks.size())) return -1;
        c = m_clip;
        type = m_tracks[size_t(track)].type;
    }
    const bool fits = c.midi ? (c.drummer ? type == TrackType::Drummer : (type == TrackType::Instrument || type == TrackType::Drummer))
                             : type == TrackType::Audio;
    if (!fits) {
        message(c.midi ? tr("MIDI regions can be pasted on Software Instrument and Drummer tracks.")
                            : tr("Audio regions can be pasted on Audio tracks."));
        return -1;
    }
    checkpoint();
    c.start = std::max(0.0, beat);
    if (!c.midi && c.source && c.follow) applyFlex(c, tempoMap()->bpmAt(c.start));   // stretch for the tempo at the new place
    int idx = -1;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track >= int(m_tracks.size())) return -1;
        auto& regs = m_tracks[size_t(track)].regions;
        regs.push_back(std::move(c));
        idx = int(regs.size()) - 1;
    }
    changed(false);
    return idx;
}

int Session::duplicateTrack(int i) {
    Track src;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (i < 0 || i >= int(m_tracks.size())) return -1;
        src = m_tracks[size_t(i)];
    }
    checkpoint();
    Track t = src;   // regions share their audio data; everything else is copied below
    t.name = tr("%1 copy").arg(src.name);
    t.armed = false;
    t.peakL = t.peakR = 0;
    if (src.instrument) {   // every track needs its own instrument and effect instances
        t.instrument = makeInstrument(src.instrumentId);
        if (t.instrument) {
            copyParams(*src.instrument, *t.instrument);
            if (startsWith(src.instrumentId, "vst3:") && m_hooks.saveState && m_hooks.loadState)
                m_hooks.loadState(*t.instrument, m_hooks.saveState(*src.instrument));
        } else {
            message(tr("Instrument plug-in not available: %1").arg(src.instrumentId));
        }
    }
    t.inserts.clear();
    for (const auto& fx : src.inserts) {
        const std::string fxId = fx->id();
        auto c = makeEffect(fxId);
        if (!c) continue;
        copyParams(*fx, *c);
        c->bypass = fx->bypass.load();
        if (startsWith(fxId, "vst3:") && m_hooks.saveState && m_hooks.loadState) m_hooks.loadState(*c, m_hooks.saveState(*fx));
        t.inserts.push_back(c);
    }
    int idx;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        t.id = m_nextId++;
        idx = std::min(i + 1, int(m_tracks.size()));
        m_tracks.insert(m_tracks.begin() + idx, std::move(t));
    }
    changed(true);
    return idx;
}

void Session::setRegionAudio(int t, int r, float gainDb, i64 fadeInFrames, i64 fadeOutFrames) {
    checkpoint();
    REGION_OP(
        if (rg.midi) return;
        rg.gainDb = std::clamp(gainDb, -60.f, 24.f);
        rg.fadeIn = std::max<i64>(0, fadeInFrames);
        rg.fadeOut = std::max<i64>(0, fadeOutFrames);)
    changed(false);
}

void Session::setRegionMute(int t, int r, bool mute) {
    checkpoint();
    REGION_OP(rg.mute = mute;)
    changed(false);
}

void Session::moveTrack(int from, int to) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        const int n = int(m_tracks.size());
        if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    }
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        const int n = int(m_tracks.size());
        if (from >= n || to >= n) return;
        Track t = std::move(m_tracks[size_t(from)]);
        m_tracks.erase(m_tracks.begin() + from);
        m_tracks.insert(m_tracks.begin() + to, std::move(t));
    }
    changed(true);
}

void Session::setTrackColor(int i, Color color) {
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (i < 0 || i >= int(m_tracks.size())) return;
        m_tracks[size_t(i)].color = color;
    }
    changed(true);
}

void Session::reverseRegion(int t, int r) {
    Region c;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return;
        c = m_tracks[size_t(t)].regions[size_t(r)];
    }
    if (c.midi || !c.source || c.srcFrames <= 0) return;
    checkpoint();
    auto d = std::make_shared<AudioData>();   // no path yet: it is written next to the project when saved
    d->name = c.source->name;
    d->pcm = dsp::reverseFrames(c.source->pcm.data() + c.srcOffset * 2, size_t(c.srcFrames));   // reverse the source so tempo / pitch settings still apply
    computePeaks(*d);
    c.source = c.audio = d;
    c.srcOffset = c.offset = 0;
    c.srcFrames = c.frames = d->frames();
    c.pendingPath.clear();
    static const std::string kTag = " (reversed)";
    c.name = endsWith(c.name, kTag) ? c.name.substr(0, c.name.size() - kTag.size()) : c.name + kTag;
    if (c.flexed()) applyFlex(c, tempoMap()->bpmAt(c.start));   // slow part runs outside the lock
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return;
        m_tracks[size_t(t)].regions[size_t(r)] = std::move(c);
    }
    changed(false);
}

void Session::normalizeRegion(int t, int r, float targetDb) {
    checkpoint();
    REGION_OP(
        if (rg.midi || !rg.audio || rg.frames <= 0) return;
        const float pk = dsp::peakOf(rg.audio->pcm.data() + rg.offset * 2, size_t(rg.frames));
        rg.gainDb = std::clamp(dsp::linearToDb(dsp::normalizeGain(pk, targetDb)), -60.f, 24.f);)
    changed(false);
}

void Session::setRegionNotes(int t, int r, const std::vector<Note>& notes) {
    REGION_OP(if (!rg.midi) return; rg.notes = notes; rg.drummer = false;)   // hand-edited notes detach from the generator
    changed(false);
}

void Session::setRegionFlex(int t, int r, bool follow, double sourceBpm, double semitones) {
    checkpoint();
    const auto tm = tempoMap();
    Region copy;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return;
        copy = m_tracks[size_t(t)].regions[size_t(r)];
    }
    if (copy.midi || !copy.source) return;
    copy.follow = follow;
    copy.sourceBpm = sourceBpm;
    copy.semitones = semitones;
    applyFlex(copy, tm->bpmAt(copy.start));   // the slow part runs outside the lock
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return;
        Region& dst = m_tracks[size_t(t)].regions[size_t(r)];
        dst.follow = copy.follow; dst.sourceBpm = copy.sourceBpm; dst.semitones = copy.semitones;
        dst.audio = copy.audio; dst.offset = copy.offset; dst.frames = copy.frames; dst.flexBpm = copy.flexBpm;
    }
    changed(false);
}
#undef REGION_OP

void Session::setStep(int track, int row, int step, bool on) {
    if (row < 0 || row >= kDrumRows || step < 0 || step >= kSteps) return;
    std::lock_guard<std::mutex> l(m_mutex);
    if (track < 0 || track >= int(m_tracks.size())) return;
    m_tracks[size_t(track)].steps[size_t(row)][size_t(step)] = on;
    m_dirty = true;
}

void Session::setDrumPattern(int track, const DrumPattern& p) {
    checkpoint();
    { std::lock_guard<std::mutex> l(m_mutex); if (track < 0 || track >= int(m_tracks.size())) return; m_tracks[size_t(track)].steps = p; }
    changed(false);
}

// ------------------------------------------------------------------ markers
std::vector<Marker> Session::markers() const {
    std::lock_guard<std::mutex> l(m_mutex);
    return m_markers;
}

int Session::addMarker(double beat, const std::string& name) {
    checkpoint();
    int idx;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        Marker m;
        m.beat = std::max(0.0, beat);
        m.name = name.empty() ? tr("Marker %1").arg(m_markers.size() + 1).str() : name;
        const auto it = std::upper_bound(m_markers.begin(), m_markers.end(), m.beat, [](double b, const Marker& x) { return b < x.beat; });
        idx = int(it - m_markers.begin());
        m_markers.insert(it, m);
    }
    changed(false);
    return idx;
}

void Session::removeMarker(int i) {
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (i < 0 || i >= int(m_markers.size())) return;
        m_markers.erase(m_markers.begin() + i);
    }
    changed(false);
}

void Session::renameMarker(int i, const std::string& name) {
    checkpoint();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (i < 0 || i >= int(m_markers.size()) || trimmed(name).empty()) return;
        m_markers[size_t(i)].name = trimmed(name);
    }
    changed(false);
}

int Session::moveMarker(int i, double beat) {   // no checkpoint: the caller sets one when a drag starts
    int idx = i;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (i < 0 || i >= int(m_markers.size())) return i;
        Marker m = m_markers[size_t(i)];
        m.beat = std::max(0.0, beat);
        m_markers.erase(m_markers.begin() + i);
        const auto it = std::upper_bound(m_markers.begin(), m_markers.end(), m.beat, [](double b, const Marker& x) { return b < x.beat; });
        idx = int(it - m_markers.begin());
        m_markers.insert(it, m);
    }
    changed(false);
    return idx;
}

void Session::setVolAuto(int track, const std::vector<AutoPoint>& pts) { setAuto(track, AutoParam::Volume, pts); }

void Session::setAuto(int track, AutoParam param, const std::vector<AutoPoint>& pts) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        auto sorted = pts;
        const auto [lo, hi] = autoRange(param);
        for (AutoPoint& a : sorted) a.value = std::clamp(a.value, lo, hi);
        std::sort(sorted.begin(), sorted.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.beat < b.beat; });
        m_tracks[size_t(track)].autoOf(param) = sorted;
    }
    changed(false);
}

// ------------------------------------------------------------------ tempo and meter
std::shared_ptr<const dsp::TempoMap> Session::tempoMap() const {
    std::lock_guard<std::mutex> l(m_tempoMutex);
    return m_tempo;
}

void Session::setTempoMap(std::shared_ptr<const dsp::TempoMap> map, bool commit) {
    const double beat = positionBeats();   // keep the playhead at the same musical position
    { std::lock_guard<std::mutex> l(m_tempoMutex); m_tempo = map; }
    { std::lock_guard<std::mutex> l(m_mutex); m_pos = i64(std::llround(map->beatToFrame(beat))); }
    m_dirty = true;
    if (m_hooks.tempoChanged) m_hooks.tempoChanged();
    if (commit) commitTempo();
}

void Session::setTempoPoints(const std::vector<dsp::TempoPoint>& pts, bool commit) {
    auto tm = std::make_shared<dsp::TempoMap>(*tempoMap());
    tm->setTempo(pts);
    setTempoMap(tm, commit);
}

void Session::setMeterPoints(const std::vector<dsp::MeterPoint>& pts) {
    checkpoint();
    auto tm = std::make_shared<dsp::TempoMap>(*tempoMap());
    tm->setMeter(pts);
    setTempoMap(tm, false);
    changed(false);
}

double Session::bpm() const { return tempoMap()->bpmAt(positionBeats()); }

void Session::setBpm(double b) {
    checkpoint();
    auto pts = tempoMap()->tempoPoints();
    const double beat = positionBeats();
    size_t idx = 0;
    for (size_t k = 0; k < pts.size(); ++k)
        if (pts[k].beat <= beat + 1e-6) idx = k;
    pts[idx].bpm = b;
    setTempoPoints(pts, true);
}

void Session::commitTempo() {
    struct Job { int trackId; size_t ri; Region copy; double bpm; };
    std::vector<Job> jobs;
    const auto tm = tempoMap();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const Track& t : m_tracks)
            for (size_t i = 0; i < t.regions.size(); ++i) {
                const Region& r = t.regions[i];
                const double bpm = tm->bpmAt(r.start);
                if (!r.midi && r.source && r.follow && std::fabs(r.flexBpm - bpm) > 1e-6) jobs.push_back({t.id, i, r, bpm});
            }
    }
    for (Job& j : jobs) applyFlex(j.copy, j.bpm);   // stretching runs outside the lock
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const Job& j : jobs)
            for (Track& t : m_tracks) {
                if (t.id != j.trackId || j.ri >= t.regions.size() || t.regions[j.ri].source != j.copy.source) continue;
                Region& dst = t.regions[j.ri];
                dst.audio = j.copy.audio; dst.offset = j.copy.offset; dst.frames = j.copy.frames; dst.flexBpm = j.copy.flexBpm;
            }
    }
    if (m_hooks.changed) m_hooks.changed(false);
}

// ------------------------------------------------------------------ undo / redo
void Session::pushUndo() {
    UndoState s{{}, tempoMap(), {}};
    std::lock_guard<std::mutex> l(m_mutex);
    s.tracks = m_tracks;
    s.markers = m_markers;
    m_undo.push_back(std::move(s));
    if (m_undo.size() > 60) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_dirty = true;
}

void Session::restore(UndoState s) {
    {
        std::lock_guard<std::mutex> l(m_tempoMutex);
        m_tempo = s.tempo;
    }
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_tracks = std::move(s.tracks);
        m_markers = std::move(s.markers);
    }
    if (m_hooks.tempoChanged) m_hooks.tempoChanged();
    changed(true);
}

void Session::undo() {
    UndoState cur{{}, tempoMap(), {}};
    UndoState prev;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (m_undo.empty()) return;
        cur.tracks = m_tracks;
        cur.markers = m_markers;
        prev = std::move(m_undo.back());
        m_undo.pop_back();
        m_redo.push_back(std::move(cur));
    }
    restore(std::move(prev));
}

void Session::redo() {
    UndoState cur{{}, tempoMap(), {}};
    UndoState next;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (m_redo.empty()) return;
        cur.tracks = m_tracks;
        cur.markers = m_markers;
        next = std::move(m_redo.back());
        m_redo.pop_back();
        m_undo.push_back(std::move(cur));
    }
    restore(std::move(next));
}

// ------------------------------------------------------------------ transport
void Session::startPlayback() {
    m_expected = -1;   // forces a discontinuity: instrument voices are re-synchronised
    m_flush |= 2;
    m_playing = true;
}

void Session::play() {
    if (!m_playing) startPlayback();
}

void Session::pause() {
    if (!m_playing) return;
    stopRecording();
    m_playing = false;
    m_countRemain = 0;
    m_flush |= 1;
    m_idle = i64(kSampleRate) * 4;  // let reverb / echo tails ring out
}

void Session::stop() {
    if (m_playing) pause();
    else seekBeats(m_loopOn ? double(m_loopA) : 0.0);
}

void Session::seekBeats(double beat) {
    const double f = beatToFrame(std::max(0.0, beat));
    std::lock_guard<std::mutex> l(m_mutex);   // keeps the audio thread from overwriting the new position
    m_pos = i64(std::llround(f));
}

void Session::setLoop(bool on, double a, double b) {
    if (a >= 0 && b > a) { m_loopA = a; m_loopB = b; }
    m_loopOn = on;
    m_dirty = true;
    if (m_hooks.loopChanged) m_hooks.loopChanged();
}

void Session::masterPeaks(float& l, float& r) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    l = m_masterL;
    r = m_masterR;
}

// ------------------------------------------------------------------ live notes and recording
void Session::noteOn(int track, int pitch, int velocity) {
    bool onRecTrack = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        Track& t = m_tracks[size_t(track)];
        if (!t.instrument) return;
        t.instrument->noteOn(pitch, velocity);
        m_idle = i64(kSampleRate) * 6;
        onRecTrack = (t.id == m_recTrackId.load());
    }
    if (onRecTrack && m_recording && m_playing && m_countRemain == 0 && !m_recAudio)
        m_recPending[pitch] = {positionBeats(), velocity};
}

void Session::noteOff(int track, int pitch) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        Track& t = m_tracks[size_t(track)];
        if (t.instrument) t.instrument->noteOff(pitch);
    }
    auto it = m_recPending.find(pitch);
    if (it != m_recPending.end()) {
        const double end = positionBeats();
        m_recNotes.push_back(Note{pitch, it->second.start, std::max(0.05, end - it->second.start), it->second.vel});
        m_recPending.erase(it);
    }
}

bool Session::startRecording() {
    if (m_recording) return true;
    int tid = -1;
    bool audio = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const Track& t : m_tracks)
            if (t.armed && t.type != TrackType::Drums) { tid = t.id; audio = (t.type == TrackType::Audio); break; }
    }
    if (tid < 0) {
        message(tr("Arm an audio or instrument track first (R button on the track header)."));
        return false;
    }
    m_recStartBeat = positionBeats();
    m_recPending.clear();
    m_recNotes.clear();
    { std::lock_guard<std::mutex> r(m_recMutex); m_recBuf.clear(); }
    m_recTrackId = tid;
    m_recAudio = audio;
    m_recording = true;
    if (!m_playing) {
        if (m_countIn) {   // one bar of clicks in the meter at the playhead
            const auto tm = tempoMap();
            const double beat = m_recStartBeat;
            const dsp::MeterPoint mp = tm->meterAtBeat(beat);
            const i64 click = i64(std::llround(kSampleRate * 60.0 / tm->bpmAt(beat) * (4.0 / mp.den)));
            m_countClick = click;
            m_countPerBar = mp.num;
            m_countTotal = click * mp.num;
            m_countRemain = m_countTotal.load();
        }
        startPlayback();
    }
    if (audio && m_countRemain == 0 && m_hooks.startInput) m_hooks.startInput();
    return true;
}

void Session::stopRecording() {
    if (!m_recording.exchange(false)) return;
    const bool audio = m_recAudio;
    if (audio && m_hooks.stopInput) m_hooks.stopInput();
    const double endBeat = positionBeats();
    std::vector<float> take;
    { std::lock_guard<std::mutex> r(m_recMutex); take.swap(m_recBuf); }
    if (audio) dsp::applyRecordingOffset(take, long(std::lround(double(m_recOffsetMs.load()) * kSampleRate / 1000.0)));
    for (const auto& [pitch, p] : m_recPending)
        m_recNotes.push_back(Note{pitch, p.start, std::max(0.05, endBeat - p.start), p.vel});
    m_recPending.clear();
    std::vector<Note> notes;
    notes.swap(m_recNotes);
    if (audio ? take.size() < size_t(kSampleRate / 10) : notes.empty()) return;   // ignore accidental taps

    pushUndo();
    const auto tm = tempoMap();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        int ti = -1;
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].id == m_recTrackId.load()) ti = int(i);
        if (ti < 0) return;
        Region r;
        r.start = m_recStartBeat;
        if (audio) {
            auto data = std::make_shared<AudioData>();
            data->name = m_tracks[size_t(ti)].name + " take";
            data->pcm = std::move(take);
            computePeaks(*data);
            r.name = data->name;
            r.source = r.audio = data;
            r.frames = r.srcFrames = data->frames();
        } else {
            r.midi = true;
            r.name = "Recorded MIDI";
            double maxEnd = m_recStartBeat;
            for (const Note& n : notes) maxEnd = std::max(maxEnd, n.start + n.length);
            double endBar = tm->nextBarLine(maxEnd);
            if (endBar <= m_recStartBeat + 1e-6) endBar = tm->barStart(tm->barAt(m_recStartBeat) + 1);
            r.lengthBeats = endBar - m_recStartBeat;
            for (Note n : notes) { n.start = std::max(0.0, n.start - m_recStartBeat); r.notes.push_back(n); }
        }
        m_tracks[size_t(ti)].regions.push_back(std::move(r));
    }
    changed(false);
}

void Session::pushRecorded(const std::vector<float>& stereo) {
    std::lock_guard<std::mutex> l(m_recMutex);
    m_recBuf.insert(m_recBuf.end(), stereo.begin(), stereo.end());
}

// ------------------------------------------------------------------ rendering
void Session::flushDsp() {
    for (Track& t : m_tracks) {
        for (auto& fx : t.inserts) fx->reset();
        if (t.instrument) t.instrument->reset();
    }
    for (auto& a : m_aux) a->reset();
    m_limiter->reset();
}

void Session::addClicks(i64 tf, int frames, float* out) {
    const dsp::TempoMap& tm = *m_tm;
    tm.forEachClick(tm.frameToBeat(double(std::max<i64>(0, tf - kClickLen))), tm.frameToBeat(double(tf + frames)),
                    [&](double beat, bool accent) {
        const i64 st = i64(std::llround(tm.beatToFrame(beat)));
        const i64 a = std::max(tf, st), b = std::min<i64>(tf + frames, st + kClickLen);
        const double freq = accent ? 1600.0 : 1000.0;
        for (i64 f = a; f < b; ++f) {
            const double n = double(f - st);
            const float v = float(std::sin(2 * kPi * freq * n / kSampleRate) * std::exp(-n / 250.0) * 0.35);
            out[(f - tf) * 2] += v;
            out[(f - tf) * 2 + 1] += v;
        }
    });
}

// Mixes [pos, pos + frames) of the whole project into 'out'. m_mutex must be held and m_tm set.
// 'running' is false while the transport is stopped: only live notes and effect tails are rendered.
void Session::renderBlock(i64 pos, int frames, float* out, bool running) {
    const dsp::TempoMap& tm = *m_tm;
    const size_t n2 = size_t(frames) * 2;
    std::fill(out, out + n2, 0.f);
    for (auto& a : m_auxBuf) std::fill(a.begin(), a.begin() + std::ptrdiff_t(n2), 0.f);

    const double beat0 = tm.frameToBeat(double(pos)), beat1 = tm.frameToBeat(double(pos + frames));
    auto bf = [&](double beat) { return i64(std::llround(tm.beatToFrame(beat))); };
    const bool jump = running && m_expected.load() != pos;
    const bool anySolo = std::any_of(m_tracks.begin(), m_tracks.end(), [](const Track& t) { return t.solo; });
    float* tb = m_trackBuf.data();

    for (Track& t : m_tracks) {
        const bool onlyThis = m_only >= 0 && &t == &m_tracks[size_t(m_only)];
        if (m_only >= 0 ? !onlyThis : (t.mute || (anySolo && !t.solo))) {
            t.peakL = t.peakR = 0;
            if (t.instrument) t.instrument->allNotesOff();   // never leave voices hanging on a silent track
            continue;
        }
        std::fill(tb, tb + n2, 0.f);

        if (t.type == TrackType::Audio && running) {
            for (const Region& rg : t.regions) {
                if (rg.midi || !rg.audio || rg.mute) continue;
                const i64 st = bf(rg.start);
                const i64 a = std::max(pos, st), b = std::min<i64>(pos + frames, st + rg.frames);
                if (a >= b) continue;
                const float* s = rg.audio->pcm.data() + (rg.offset + a - st) * 2;
                const float lin = rg.gainDb == 0.f ? 1.f : dsp::dbToLinear(rg.gainDb);
                const bool fades = rg.fadeIn > 0 || rg.fadeOut > 0;
                for (i64 f = a; f < b; ++f, s += 2) {
                    const float g = fades ? lin * dsp::fadeGain(f - st, rg.frames, rg.fadeIn, rg.fadeOut) : lin;
                    tb[(f - pos) * 2] += s[0] * g;
                    tb[(f - pos) * 2 + 1] += s[1] * g;
                }
            }
        } else if (t.type == TrackType::Drums && running) {
            const i64 first = std::max<i64>(0, i64(std::floor(tm.frameToBeat(double(std::max<i64>(0, pos - m_drumMax))) * 4.0)) - 1);
            const i64 last = i64(std::floor(beat1 * 4.0));   // 16th-note steps
            for (i64 k = first; k <= last; ++k) {
                const size_t s = size_t(k % kSteps);
                const i64 st = bf(double(k) * 0.25);
                for (int r = 0; r < kDrumRows; ++r) {
                    if (!t.steps[size_t(r)][s]) continue;
                    const auto& snd = dsp::DrumKit::sound(kRowPiece[r]);
                    const i64 a = std::max(pos, st), b = std::min<i64>(pos + frames, st + i64(snd.size()));
                    for (i64 f = a; f < b; ++f) {
                        const float v = snd[size_t(f - st)];
                        tb[(f - pos) * 2] += v;
                        tb[(f - pos) * 2 + 1] += v;
                    }
                }
            }
        } else if ((t.type == TrackType::Instrument || t.type == TrackType::Drummer) && t.instrument) {
            if (!running) {
                t.instrument->render(tb, frames);   // live notes only
            } else {
                m_events.clear();
                if (jump) t.instrument->allNotesOff();
                for (const Region& rg : t.regions) {
                    if (!rg.midi || rg.mute) continue;
                    const double regEnd = rg.start + rg.lengthBeats;
                    for (const Note& n : rg.notes) {
                        if (n.start >= rg.lengthBeats) continue;
                        const i64 s = bf(rg.start + n.start);
                        i64 e = bf(std::min(rg.start + n.start + n.length, regEnd));
                        if (e <= s) e = s + 1;
                        if (s >= pos && s < pos + frames) m_events.push_back({int(s - pos), true, n.pitch, n.velocity});
                        if (e >= pos && e < pos + frames) m_events.push_back({int(e - pos), false, n.pitch, 0});
                        if (jump && s < pos && e > pos) m_events.push_back({0, true, n.pitch, n.velocity});   // note already sounding
                    }
                }
                std::stable_sort(m_events.begin(), m_events.end(), [](const Ev& a, const Ev& b) {
                    return a.off != b.off ? a.off < b.off : a.on < b.on;   // releases before attacks
                });
                int cur = 0;
                for (const Ev& ev : m_events) {
                    if (ev.off > cur) { t.instrument->render(tb + cur * 2, ev.off - cur); cur = ev.off; }
                    if (ev.on) t.instrument->noteOn(ev.pitch, ev.vel);
                    else t.instrument->noteOff(ev.pitch);
                }
                if (cur < frames) t.instrument->render(tb + cur * 2, frames - cur);
            }
        }

        for (auto& fx : t.inserts)
            if (!fx->bypass) fx->process(tb, frames);

        // Fader (with volume automation), pan law and aux sends.
        const bool automated = !t.volAuto.empty();
        const float g0 = autoValue(t.volAuto, beat0, t.volume), g1 = autoValue(t.volAuto, beat1, t.volume);
        const bool panAuto = !t.panAuto.empty(), sendAuto = !t.sendAAuto.empty() || !t.sendBAuto.empty();
        const float pn0 = autoValue(t.panAuto, beat0, t.pan), pn1 = autoValue(t.panAuto, beat1, t.pan);
        const float sa0 = autoValue(t.sendAAuto, beat0, t.sendA), sa1 = autoValue(t.sendAAuto, beat1, t.sendA);
        const float sb0 = autoValue(t.sendBAuto, beat0, t.sendB), sb1 = autoValue(t.sendBAuto, beat1, t.sendB);
        const float ang0 = (pn0 + 1.f) * 0.25f * float(kPi), ang1 = (pn1 + 1.f) * 0.25f * float(kPi);
        float pl = std::cos(ang0) * 1.41421f, pr = std::sin(ang0) * 1.41421f;
        (void)automated;
        float peakL = 0, peakR = 0;
        for (int i = 0; i < frames; ++i) {
            const float k = float(i) / float(frames);
            const float g = g0 + (g1 - g0) * k;
            if (panAuto) {   // pan law per sample while the pan is automated
                const float ang = ang0 + (ang1 - ang0) * k;
                pl = std::cos(ang) * 1.41421f;
                pr = std::sin(ang) * 1.41421f;
            }
            const float sendA = sendAuto ? sa0 + (sa1 - sa0) * k : t.sendA, sendB = sendAuto ? sb0 + (sb1 - sb0) * k : t.sendB;
            const float a = tb[2 * i] * g, b = tb[2 * i + 1] * g;
            if (sendA > 0.f) { m_auxBuf[0][size_t(2 * i)] += a * sendA; m_auxBuf[0][size_t(2 * i + 1)] += b * sendA; }
            if (sendB > 0.f) { m_auxBuf[1][size_t(2 * i)] += a * sendB; m_auxBuf[1][size_t(2 * i + 1)] += b * sendB; }
            const float l = a * pl, r = b * pr;
            out[2 * i] += l;
            out[2 * i + 1] += r;
            peakL = std::max(peakL, std::fabs(l));
            peakR = std::max(peakR, std::fabs(r));
        }
        t.peakL = peakL;
        t.peakR = peakR;
    }

    for (size_t bus = 0; bus < m_aux.size() && m_only < 0; ++bus) {   // sends are not part of a bounce in place
        if (!m_aux[bus]->bypass) m_aux[bus]->process(m_auxBuf[bus].data(), frames);
        for (size_t i = 0; i < n2; ++i) out[i] += m_auxBuf[bus][i];
    }
    if (running && m_metro && m_only < 0) addClicks(pos, frames, out);
    if (running) m_expected = pos + frames;
}

void Session::masterProcess(float* buf, int frames) {
    const float g = m_master;
    for (int i = 0; i < frames * 2; ++i) buf[i] *= g;
    if (!m_limiter->bypass) m_limiter->process(buf, frames);
    float ml = 0, mr = 0;
    for (int i = 0; i < frames; ++i) {
        ml = std::max(ml, std::fabs(buf[2 * i]));
        mr = std::max(mr, std::fabs(buf[2 * i + 1]));
    }
    m_masterL = ml;
    m_masterR = mr;
}

void Session::fillInt16(std::int16_t* out, int frames) {
    const auto tempo = tempoMap();   // keeps the map alive for this call
    std::lock_guard<std::mutex> lk(m_mutex);
    m_tm = tempo.get();
    const size_t need = size_t(frames) * 2;
    if (m_scratch.size() < need) {   // grows only; no allocation once warmed up
        m_scratch.resize(need);
        m_trackBuf.resize(need);
        for (auto& a : m_auxBuf) a.resize(need);
    }
    const int fl = m_flush.exchange(0);
    if (fl & 2) flushDsp();
    else if (fl & 1)
        for (Track& t : m_tracks)
            if (t.instrument) t.instrument->allNotesOff();

    const bool running = m_playing;
    if (!running && m_idle <= 0 && m_toneRemain <= 0 && !m_audition) {   // idle: no work and no CPU use
        std::fill(out, out + need, std::int16_t(0));
        m_masterL = m_masterR = 0;
        for (Track& t : m_tracks) t.peakL = t.peakR = 0;
        return;
    }

    const i64 la = i64(std::llround(tempo->beatToFrame(m_loopA))), lb = i64(std::llround(tempo->beatToFrame(m_loopB)));
    const bool looping = m_loopOn && lb > la;
    i64 pos = m_pos;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        float* dst = m_scratch.data() + size_t(done) * 2;
        if (running && m_countRemain > 0) {   // count-in: clicks only, the playhead stays put
            n = int(std::min<i64>(n, m_countRemain));
            std::fill(dst, dst + size_t(n) * 2, 0.f);
            const i64 click = std::max<i64>(1, m_countClick), el = m_countTotal - m_countRemain;
            for (int f = 0; f < n; ++f) {
                const i64 t = el + f, k = t / click, off = t - k * click;
                if (off >= kClickLen) continue;
                const float v = float(std::sin(2 * kPi * (k % m_countPerBar == 0 ? 1600.0 : 1000.0) * double(off) / kSampleRate) *
                                      std::exp(-double(off) / 250.0) * 0.35);
                dst[2 * f] += v;
                dst[2 * f + 1] += v;
            }
            m_countRemain -= n;
            if (m_countRemain == 0 && m_recording && m_recAudio)
                if (m_hooks.startInput) m_hooks.startInput();
            done += n;
            continue;
        }
        if (running && looping) {
            if (pos >= lb) pos = la;
            n = int(std::min<i64>(n, lb - pos));
        }
        renderBlock(pos, n, dst, running);
        if (running) pos += n;
        done += n;
    }
    if (running) m_pos = pos;
    else m_idle -= frames;

    masterProcess(m_scratch.data(), frames);

    if (m_toneRemain > 0) {   // device test tone
        for (int i = 0; i < frames && m_toneRemain > 0; ++i, --m_toneRemain) {
            const i64 n = kToneFrames - m_toneRemain;
            const float env = float(std::min<i64>(std::min<i64>(n, m_toneRemain), 2000)) / 2000.f;
            const float v = 0.25f * env * float(std::sin(2 * kPi * 440.0 * double(n) / kSampleRate));
            m_scratch[size_t(2 * i)] += v;
            m_scratch[size_t(2 * i + 1)] += v;
        }
    }
    if (m_audition) {   // loop browser preview
        for (int i = 0; i < frames; ++i) {
            if (m_auditionPos >= m_audition->frames()) { m_audition.reset(); break; }
            m_scratch[size_t(2 * i)] += m_audition->pcm[size_t(2 * m_auditionPos)] * 0.8f;
            m_scratch[size_t(2 * i + 1)] += m_audition->pcm[size_t(2 * m_auditionPos + 1)] * 0.8f;
            ++m_auditionPos;
        }
    }
    for (size_t i = 0; i < need; ++i) out[i] = std::int16_t(std::clamp(m_scratch[i], -1.f, 1.f) * 32767.f);
}


// ------------------------------------------------------------------ construction
Session::Session() {
    m_tempo = std::make_shared<const dsp::TempoMap>();
    for (int p = 0; p < dsp::kDrumPieceCount; ++p) m_drumMax = std::max<i64>(m_drumMax, i64(dsp::DrumKit::sound(p).size()));
    auto reverb = dsp::createEffect("reverb");
    reverb->find("Mix")->set(1.f);
    reverb->find("Size")->set(0.8f);
    auto echo = dsp::createEffect("echo");
    echo->find("Mix")->set(1.f);
    m_aux = {reverb, echo};
    m_limiter = dsp::createEffect("limiter");
    newProject();
}

Session::~Session() = default;

Text Session::tr(const char* key) const {
    if (m_hooks.translate) return Text(m_hooks.translate(key));
    if (m_table) return Text(m_table->translate(key));
    return Text(key);
}

std::shared_ptr<dsp::Effect> Session::makeEffect(const std::string& id) const {
    if (startsWith(id, "vst3:") || startsWith(id, "native:")) return m_hooks.makeEffect ? m_hooks.makeEffect(id) : nullptr;
    return dsp::createEffect(id);
}

std::shared_ptr<dsp::Instrument> Session::makeInstrument(const std::string& id) const {
    if (id == "synth") { auto s = std::make_shared<dsp::Synth>(); s->setPreset(0); return s; }
    if (id == "drumkit") return std::make_shared<dsp::DrumKit>();
    if (startsWith(id, "vst3:")) return m_hooks.makeInstrument ? m_hooks.makeInstrument(id) : nullptr;
    return nullptr;
}

void Session::decodeAsync(const std::string& path, DecodeDone done) {
    if (m_hooks.decode) { m_hooks.decode(path, std::move(done)); return; }
    // Without a platform decoder only WAV files can be read; the result arrives immediately.
    auto data = std::make_shared<AudioData>();
    data->name = baseName(path);
    data->path = path;
    std::string err;
    if (!readWavFile(path, data->pcm, &err) || data->frames() == 0) {
        done(nullptr, tr("Could not decode %1").arg(data->name).str());
        return;
    }
    computePeaks(*data);
    done(data, {});
}

void Session::playTestTone() {
    m_toneRemain = kToneFrames;
    m_idle = i64(kSampleRate);
}

void Session::audition(const std::string& path) {
    decodeAsync(path, [this](std::shared_ptr<AudioData> data, std::string err) {
        if (!data) { message(err); return; }
        std::lock_guard<std::mutex> l(m_mutex);
        m_audition = data;
        m_auditionPos = 0;
    });
}

void Session::stopAudition() {
    std::lock_guard<std::mutex> l(m_mutex);
    m_audition.reset();
}

void Session::ensureScratch(size_t need) {
    if (m_scratch.size() < need) {   // grows only; no allocation once warmed up
        m_scratch.resize(need);
        m_trackBuf.resize(need);
        for (auto& a : m_auxBuf) a.resize(need);
    }
}

std::vector<float> Session::renderOffline(i64 startFrame, i64 frames) {
    const auto tempo = tempoMap();
    std::lock_guard<std::mutex> lk(m_mutex);
    m_tm = tempo.get();
    constexpr int kBlock = 4096;
    ensureScratch(size_t(kBlock) * 2);
    flushDsp();
    m_expected = -1;
    std::vector<float> out;
    out.reserve(size_t(frames) * 2);
    for (i64 p = startFrame; p < startFrame + frames; p += kBlock) {
        const int n = int(std::min<i64>(kBlock, startFrame + frames - p));
        renderBlock(p, n, m_scratch.data(), true);
        masterProcess(m_scratch.data(), n);
        out.insert(out.end(), m_scratch.begin(), m_scratch.begin() + std::ptrdiff_t(n) * 2);
    }
    flushDsp();
    m_expected = -1;
    return out;
}


}  // namespace core
