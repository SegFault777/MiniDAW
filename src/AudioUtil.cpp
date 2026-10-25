// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioUtil.h"

#include <algorithm>

namespace {
template <class T, class F>
void convert(const T* p, int frames, int ch, F toFloat, std::vector<float>& out) {
    out.resize(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        out[size_t(2 * i)] = toFloat(p[i * ch]);
        out[size_t(2 * i + 1)] = toFloat(p[i * ch + (ch > 1 ? 1 : 0)]);  // mono is duplicated
    }
}
}  // namespace

bool toStereoFloat(const void* data, int frames, const QAudioFormat& fmt, std::vector<float>& out) {
    const int ch = std::max(1, fmt.channelCount());
    switch (fmt.sampleFormat()) {
    case QAudioFormat::UInt8:
        convert(static_cast<const quint8*>(data), frames, ch, [](quint8 v) { return (int(v) - 128) / 128.f; }, out);
        return true;
    case QAudioFormat::Int16:
        convert(static_cast<const qint16*>(data), frames, ch, [](qint16 v) { return v / 32768.f; }, out);
        return true;
    case QAudioFormat::Int32:
        convert(static_cast<const qint32*>(data), frames, ch, [](qint32 v) { return float(v / 2147483648.0); }, out);
        return true;
    case QAudioFormat::Float:
        convert(static_cast<const float*>(data), frames, ch, [](float v) { return v; }, out);
        return true;
    default:
        return false;
    }
}

void computePeaks(AudioData& d) {
    d.peaks.assign(size_t((d.frames() + kPeakStride - 1) / kPeakStride), 0.f);
    for (qint64 i = 0; i < d.frames(); ++i) {
        float& pk = d.peaks[size_t(i / kPeakStride)];
        pk = std::max({pk, std::fabs(d.pcm[size_t(2 * i)]), std::fabs(d.pcm[size_t(2 * i + 1)])});
    }
}

void writeWavHeader(QFile& f, quint32 dataBytes) {
    auto w32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };  // Windows is little-endian
    auto w16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    w32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    w32(16); w16(1); w16(2); w32(kSampleRate); w32(kSampleRate * 4); w16(4); w16(16);
    f.write("data", 4);
    w32(dataBytes);
}

bool writeWavFile(const QString& path, const std::vector<float>& pcm, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    writeWavHeader(f, quint32(pcm.size() * 2));
    std::vector<qint16> buf(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) buf[i] = qint16(std::clamp(pcm[i], -1.f, 1.f) * 32767.f);
    f.write(reinterpret_cast<const char*>(buf.data()), qint64(buf.size() * 2));
    return true;
}
